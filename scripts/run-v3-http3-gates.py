#!/usr/bin/env python3
"""Fail-closed real-network HTTP/3 smoke gate. Client provisioning is explicit."""
import argparse
import collections
import hashlib
import json
import os
from pathlib import Path
import queue
import re
import subprocess
import threading
import struct
import time
import sys

UPLOAD = b'first\x00second\xffthird\x00'
PINS = {'aioquic': '1.3.0', 'quic-go': 'v0.55.0'}
sys.path.insert(0, str(Path(__file__).parent / 'lib'))
import v3_release_artifact as release_identity

ROOT = Path(__file__).resolve().parents[1]


class GateFailure(RuntimeError):
    pass


def need(condition, message):
    if not condition:
        raise GateFailure(message)


def readiness(line):
    match = re.fullmatch(r'READY ([0-9]+)\n', line)
    need(match and 0 < int(match[1]) <= 65535, 'invalid fixture readiness')
    return int(match[1])


def validate_receipt(receipt, client, matrix="smoke"):
    if matrix == "task174":
        need(isinstance(receipt, dict) and receipt.get("matrix") == matrix, "wrong semantic matrix")
        extra = {"streaming", "slow_producer", "slow_consumer", "goaway"}
        cases = receipt.get("cases")
        need(isinstance(cases, list) and all(isinstance(c, dict) and isinstance(c.get("case"), str) for c in cases), "malformed semantic cases")
        need(collections.Counter(c.get("case") for c in cases if c.get("case") in extra) == collections.Counter(extra), "missing or duplicated semantic scenario")
        validate_receipt({**receipt, "cases": [c for c in cases if c.get("case") not in extra]}, client)
        validate_semantic_cases({c["case"]: c for c in cases}, client)
        return
    need(isinstance(receipt, dict) and receipt.get('client') == client and receipt.get('version') == PINS[client], 'wrong client identity')
    cases = receipt.get('cases')
    names = ['handshake', 'get', 'post', 'concurrency', 'cancellation', 'tls_failure', 'missing']
    need(isinstance(cases, list) and len(cases) == len(names), 'incomplete scenario receipt')
    need(all(isinstance(case, dict) for case in cases), 'malformed scenario receipt')
    need(collections.Counter(case.get('case') for case in cases) == collections.Counter(names), 'missing or duplicated scenario')
    c = {case['case']: case for case in cases}
    h = c['handshake']
    need(h.get('alpn') == 'h3' and h.get('quic_version') == 1 and h.get('tls_version') == 'TLSv1.3' and h.get('verified') is True and h.get('early_data') is False, 'unverified or wrong handshake')
    for name, body in [('get', b'http3 fixture'), ('post', UPLOAD)]:
        need(c[name].get('status') == 200 and c[name].get('body_hex') == body.hex(), name + ' body/status mismatch')
    parallel = c['concurrency']
    need(parallel.get('health_status') == 204 and parallel.get('hold_status') == 200 and parallel.get('health_before_release') is True, 'request streams did not overlap')
    need(isinstance(parallel.get('connection'), int) and parallel['connection'] > 0, 'missing connection witness')
    ids = [c['get'].get('stream'), c['post'].get('stream'), parallel.get('held_stream'), parallel.get('health_stream'), c['cancellation'].get('stream')]
    need(all(isinstance(i, int) and i >= 0 and i % 4 == 0 for i in ids) and len(set(ids)) == len(ids), 'invalid or reused request stream IDs')
    cancel = c['cancellation']
    need(cancel.get('type') == ('StreamReset' if client == 'aioquic' else '*quic.StreamError') and cancel.get('code') == 268 and cancel.get('sibling_status') == 200 and cancel.get('server_cancelled') is True, 'wrong cancellation outcome')
    failure = c['tls_failure']
    need(failure.get('type') == ('ConnectionTerminated' if client == 'aioquic' else '*tls.CertificateVerificationError') and failure.get('code') in (298, 304) and failure.get('response') is False, 'TLS failure was not certificate verification')
    need(c['missing'].get('status') == 404, 'application result mismatch')


def validate_semantic_cases(cases, client):
    connection = cases['concurrency']['connection']
    need(cases['cancellation'].get('sibling_connection') == connection, 'cancellation sibling used another connection')
    ids = [cases['cancellation'].get('sibling_stream'), cases['get']['stream'], cases['post']['stream'], cases['concurrency']['held_stream'], cases['concurrency']['health_stream'], cases['cancellation']['stream']]
    for name in ['streaming', 'slow_producer']:
        c = cases[name]
        need(c.get('connection') == connection and c.get('before_fin') is True and c.get('prefix_bytes') == 6, 'upload prefix not observed before FIN')
        need(c.get('status') == 200 and c.get('body_hex') == UPLOAD.hex(), 'streamed binary body mismatch')
        need(c.get('trailers') == [['x-reply', 'done']] and c.get('fin_count') == 1, 'missing separated response trailers or terminal FIN')
        if client == 'aioquic':
            application_fields(c.get('headers'))
            need([f[1] for f in c['headers'] if f[0].lower() == 'x-request-trailer'] == ['yes'], 'request trailers were not observed after EOF')
        ids.append(c.get('stream'))
    for name in ['slow_producer', 'slow_consumer']:
        c = cases[name]
        need(c.get('sibling_connection') == connection and c.get('connection') == connection and c.get('sibling_status') == 204, 'slow peer blocked sibling or used another connection')
        ids.append(c.get('sibling_stream'))
    c = cases['slow_consumer']
    ids.append(c.get('stream'))
    need(c.get('withheld') is True and c.get('resumed') is True and c.get('credit') == 4096 and c.get('offset') == 4096, 'missing withheld stream credit witness')
    need(type(c.get('started')) is int and type(c.get('completed')) is int and c['started'] == c['completed'] + 1 and c['completed'] >= 0, 'missing pending transport writer')
    need(c.get('status') == 200 and c.get('body_bytes') == 524288 and c.get('body_sha256') == hashlib.sha256(bytes(range(256)) * 2048).hexdigest(), 'slow-consumer body mismatch')
    need(c.get('trailers') == [['x-reply', 'done']] and c.get('fin_count') == 1, 'slow-consumer trailers/FIN mismatch')
    need(all(type(i) is int and i >= 0 and i % 4 == 0 for i in ids) and len(set(ids)) == len(ids), 'invalid or reused semantic stream IDs')
    c = cases['goaway']
    accepted, late = c.get('accepted_stream'), c.get('late_stream')
    need(type(accepted) is int and accepted >= 0 and accepted % 4 == 0 and type(late) is int and late >= accepted + 4 and late % 4 == 0, 'invalid drain stream IDs')
    need(type(c.get('connection')) is int and c['connection'] > 0 and c['connection'] != connection, 'drain needs a fresh connection')
    need(c.get('ids') == [(1 << 62) - 4, accepted + 4], 'missing or wrong received GOAWAY stages')
    need(c.get('rejection') == 267 and c.get('accepted_status') == 200 and c.get('close_code') == 256 and c.get('drain_complete') is True, 'drain did not gracefully complete accepted work and refuse later work')

def application_fields(headers):
    need(isinstance(headers, list) and len(headers) <= 64, 'malformed response fields')
    result = collections.defaultdict(list)
    for field in headers:
        need(isinstance(field, list) and len(field) == 2 and all(isinstance(value,str) for value in field), 'malformed response field')
        name,value = field
        name = name.lower()
        if name.startswith('x-') and name != 'x-request-trailer':
            result[name].append(value)
    return dict(result)


def validate_parity(rows):
    need(isinstance(rows,list) and len(rows)==2 and all(isinstance(row,dict) and isinstance(row.get('protocol'),str) for row in rows), 'incomplete H1/H2 parity execution')
    need(collections.Counter(row.get('protocol') for row in rows)==collections.Counter(['h1','h2']), 'duplicated or missing parity protocol')
    expected = {'x-method':['POST'], 'x-target':['/items/../semantic?x=1'], 'x-route':['/semantic'], 'x-host':['localhost'], 'x-repeat':['a','b']}
    for row in rows:
        need(row.get('status')==200 and row.get('body_hex')==UPLOAD.hex() and row.get('missing_status')==404, 'H1/H2 body/status mismatch')
        need(application_fields(row.get('headers'))==expected and row.get('trailers')==[['x-reply','done']], 'H1/H2 semantic fields/trailers mismatch')
    return expected


def run_parity(binary, directory):
    directory.mkdir(mode=0o700)
    messages = queue.Queue(maxsize=32)
    process = Process([str(binary)],directory,'parity',messages)
    rows=[]
    try:
        deadline=time.monotonic()+15
        while True:
            _,line=next_message(messages,deadline)
            if line is None:break
            need(len(rows)<2,'unexpected parity results')
            rows.append(json.loads(line))
        process.process.wait(timeout=3)
        need(process.process.returncode==0,'native parity fixture failed')
        validate_parity(rows)
        return rows
    finally:
        process.stop()
        (directory/'exits.json').write_text(json.dumps({'command':process.command,'exit_status':process.process.returncode,'reader_errors':process.errors})+'\n')
        need(not process.errors and not any(thread.is_alive() for thread in process.threads),'parity diagnostic capture failed')


class GoawayPending(GateFailure):
    """A valid received-stage prefix needs more actual capture evidence."""


def decode_goaway(text, cutoff=None):
    ids=[]
    for line in text.splitlines():
        parts=line.split('\t')
        need(len(parts)==2,'malformed decoder row')
        kinds,payloads=parts[0].split(','),parts[1].split(',')
        need(len(kinds)==len(payloads),'ambiguous decoder frame alignment')
        for kind,payload in zip(kinds,payloads):
            try:frame_type=int(kind,0)
            except ValueError as error:raise GateFailure('malformed decoder frame type') from error
            if frame_type!=7:continue
            try:raw=bytes.fromhex(payload.replace(':',''))
            except ValueError as error:raise GateFailure('malformed GOAWAY payload') from error
            need(raw and len(raw)==1<<(raw[0]>>6),'malformed GOAWAY integer')
            value=int.from_bytes(bytes([raw[0]&63])+raw[1:],'big')
            if not ids or ids[-1]!=value:ids.append(value)
    need(len(ids)<=2 and (not ids or ids[0]==(1<<62)-4),'wrong received GOAWAY stages')
    if len(ids)==2:
        need(ids[1]<=ids[0] and ids[1]%4==0 and (cutoff is None or ids[1]==cutoff),'wrong received GOAWAY cutoff')
    else:
        raise GoawayPending('missing received GOAWAY stages')
    return ids


def goaway_witness(tshark, directory, port, deadline=None, cutoff=4):
    deadline=min(time.monotonic()+10,deadline) if deadline is not None else time.monotonic()+10
    snapshot=directory/'goaway.pcap'
    command=[str(tshark),'-r',str(snapshot),'-o','tls.keylog_file:'+str(directory/'tls.keys'),'-d',f'udp.port=={port},quic','-Y',f'udp.srcport=={port} && http3.frame_type==7','-T','fields','-e','http3.frame_type','-e','http3.frame_payload']
    attempts=0
    while True:
        remaining=deadline-time.monotonic()
        need(remaining>0,'GOAWAY witness deadline exceeded: missing received stages')
        snapshot.write_bytes((directory/'packets.pcap').read_bytes())
        remaining=deadline-time.monotonic()
        need(remaining>0,'GOAWAY witness deadline exceeded: missing received stages')
        result=subprocess.run(command,capture_output=True,text=True,timeout=remaining)
        attempts+=1
        (directory/'goaway-decoded.tsv').write_text(result.stdout)
        (directory/'goaway-decoder.json').write_text(json.dumps({'command':command,'exit_status':result.returncode,'stderr':result.stderr,'capture_sha256':hashlib.sha256(snapshot.read_bytes()).hexdigest(),'attempts':attempts},indent=2)+'\n')
        need(result.returncode==0,'GOAWAY decoder failed')
        try:ids=decode_goaway(result.stdout,cutoff)
        except GoawayPending:
            remaining=deadline-time.monotonic()
            need(remaining>0,'GOAWAY witness deadline exceeded: missing received stages')
            time.sleep(min(0.05,remaining))
            continue
        need(time.monotonic()<deadline,'GOAWAY witness deadline exceeded')
        return {'ids':ids}


class Process:
    def __init__(self, command, directory, name, messages):
        self.command = command
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, bufsize=1)
        self.threads = []
        self.errors = []
        for channel in ['stdout', 'stderr']:
            thread = threading.Thread(target=self.read, args=(getattr(self.process, channel), directory / f'{name}.{channel}', channel, messages), daemon=True)
            thread.start()
            self.threads.append(thread)

    def read(self, stream, path, channel, messages):
        total = 0
        try:
            with path.open('w') as output:
                for line in stream:
                    total += len(line)
                    need(total <= 1048576 and len(line) <= 65536, 'process diagnostic bound exceeded')
                    output.write(line); output.flush()
                    if channel == 'stdout':
                        messages.put((self, line), timeout=1)
        except Exception as error:
            self.errors.append(str(error))
        finally:
            if channel == 'stdout':
                try: messages.put((self,None),timeout=1)
                except queue.Full:self.errors.append('stdout queue remained full')

    def write(self, value):
        need(self.process.poll() is None, 'process exited before command')
        self.process.stdin.write(value + '\n'); self.process.stdin.flush()

    def stop(self, graceful=None):
        if self.process.poll() is None and graceful:
            try: self.write(graceful)
            except (BrokenPipeError, GateFailure): pass
        try: self.process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.process.terminate()
            try: self.process.wait(timeout=2)
            except subprocess.TimeoutExpired: self.process.kill(); self.process.wait(timeout=2)
        for thread in self.threads: thread.join(timeout=2)
        for stream in [self.process.stdin, self.process.stdout, self.process.stderr]: stream.close()


def capture_counts(path, port):
    data=path.read_bytes()
    need(len(data)>=24, 'capture header missing')
    need(struct.unpack_from('<IHHIIII',data)==(0xa1b2c3d4,2,4,0,0,65535,101),'unsupported pcap header')
    at=24;counts={'received_count':0,'sent_count':0,'received_bytes':0,'sent_bytes':0}
    while at<len(data):
        need(len(data)-at>=16,'truncated pcap record')
        sec,micro,length,original=struct.unpack_from('<IIII',data,at);at+=16
        need(sec>0 and micro<1000000 and length==original and length>=28 and length<=65535 and at+length<=len(data),'invalid pcap packet bounds')
        packet=data[at:at+length];at+=length
        need(packet[0]==0x45 and packet[9]==17 and packet[12:20]==bytes([127,0,0,1,127,0,0,1]),'capture is not loopback IPv4 UDP')
        source,destination,udp_length=struct.unpack_from('!HHH',packet,20)
        need(struct.unpack_from('!H',packet,2)[0]==length and udp_length==length-20,'pcap IP/UDP length mismatch')
        need(source==port or destination==port,'pcap endpoint mismatch')
        direction='sent' if source==port else 'received'
        counts[direction+'_count']+=1;counts[direction+'_bytes']+=udp_length-8
    need(counts['sent_count']>0 and counts['received_count']>0,'empty one-way capture')
    return counts


def validate_artifacts(directory,receipt,client):
    lines=(directory/'fixture.stdout').read_text().splitlines()[1:]
    events=[json.loads(line) for line in lines]
    traffic=[event for event in events if event.get('event')=='traffic']
    need(len(traffic)==1,'missing fixture traffic accounting')
    traffic=traffic[0]
    counts=capture_counts(directory/'packets.pcap',traffic['port'])
    need(all(traffic.get(key)==value for key,value in counts.items()),'pcap traffic accounting mismatch')
    cases={case['case']:case for case in receipt['cases']}
    connection=cases['concurrency']['connection']
    for name,event_name,stream in [('get','get',cases['get']['stream']),('post','post',cases['post']['stream']),('concurrency','health',cases['concurrency']['health_stream'])]:
        matching=[e for e in events if e.get('event')==event_name and e.get('stream')==stream]
        need(len(matching)==1 and matching[0]['connection']==connection,name+' used a separate connection')
    keys=directory/'tls.keys'
    need(keys.is_file() and keys.stat().st_size>0 and keys.stat().st_mode & 0o777==0o600,'missing or insecure TLS key diagnostics')
    qlogs=list(directory.glob('*.qlog'))+list(directory.glob('*.sqlog'))
    need(len(qlogs)>=2 and all(p.stat().st_size>0 for p in qlogs),'missing success/failure qlogs')
    if receipt.get('matrix')=='task174':semantic_artifacts(events,receipt,qlogs,directory)
    (directory/'artifacts.json').write_text(json.dumps({'traffic':counts,'qlogs':[p.name for p in qlogs],'tls_keys':keys.name},indent=2)+'\n')

def flow_witness(events, stream, sibling):
    exhausted=[]; healthy=[]; grants=[]; finished=[]
    for index,event in enumerate(events):
        for frame in event.get('data',{}).get('frames',[]):
            if event.get('name')=='transport:packet_received' and frame.get('frame_type')=='stream':
                end=frame.get('offset',0)+frame.get('length',0)
                if frame.get('stream_id')==stream and end==4096:exhausted.append(index)
                if frame.get('stream_id')==sibling and frame.get('fin') is True:healthy.append(index)
                if frame.get('stream_id')==stream and frame.get('fin') is True and end>524288:finished.append(index)
            if event.get('name')=='transport:packet_sent' and frame.get('frame_type')=='max_stream_data' and frame.get('stream_id')==stream and frame.get('maximum',0)>4096:
                grants.append(index)
    need(exhausted and healthy and grants and finished and min(exhausted)<min(healthy)<min(grants)<min(finished),'missing native exhausted-credit/sibling/grant/FIN ordering')
    return {'exhausted_event':min(exhausted),'sibling_fin_event':min(healthy),'grant_event':min(grants),'response_fin_event':min(finished)}


def drain_ack_witness(events, case):
    need(case.get('fin_acknowledged') is True and case.get('local_hold_stream') == 8,
         'missing native drain synchronization')
    need(case.get('accepted_body_hex') == '', 'accepted drain response payload mismatch')
    fins = set()
    acknowledged = None
    closed = False
    for event in events:
        name = event.get('name')
        data = event.get('data', {})
        for frame in data.get('frames', []):
            kind = frame.get('frame_type')
            need(frame.get('stream_id') != case['local_hold_stream'], 'local hold appeared on wire')
            if name == 'transport:packet_received':
                if kind == 'stream' and frame.get('stream_id') == case['accepted_stream'] and frame.get('fin'):
                    fins.add(data['header']['packet_number'])
                if kind == 'connection_close':
                    need(frame.get('error_space') == 'application' and frame.get('raw_error_code') == 256,
                         'wrong native drain close')
                    need(acknowledged is not None, 'server close preceded accepted FIN ACK')
                    closed = True
            elif name == 'transport:packet_sent':
                need(kind != 'connection_close' or closed, 'client closed before server drain')
                if kind == 'ack' and not closed:
                    for bounds in frame.get('acked_ranges', []):
                        for number in fins:
                            if bounds[0] <= number <= bounds[-1]: acknowledged = number
    need(closed and acknowledged is not None, 'missing accepted FIN ACK/server close')
    return {'fin_packet': acknowledged, 'local_hold_stream': case['local_hold_stream'], 'server_close_code': 256}


def semantic_artifacts(events, receipt, qlogs, directory):
    cases={c['case']:c for c in receipt['cases']}
    connection=cases['concurrency']['connection']
    sequence=[e.get('sequence') for e in events if e.get('event')!='traffic']
    need(sequence==list(range(1,len(sequence)+1)),'missing or nonmonotonic fixture sequence')
    cancelled=[e for e in events if e.get('event')=='cancelled' and e.get('stream')==cases['cancellation']['stream'] and e.get('connection')==connection]
    need(len(cancelled)==1,'server cancellation was not exactly once')
    siblings=[e for e in events if e.get('event')=='get' and e.get('stream')==cases['cancellation']['sibling_stream']]
    need(len(siblings)==1 and siblings[0].get('connection')==connection and siblings[0]['sequence']>cancelled[0]['sequence'],'missing cancellation sibling on same connection')
    for name in ['streaming','slow_producer','slow_consumer']:
        case=cases[name]
        matches=[e for e in events if e.get('connection')==connection and e.get('stream')==case['stream']]
        need(sum(e['event']=='entered' for e in matches)==1 and sum(e['event']=='finished' for e in matches)==1,'incomplete semantic handler lifecycle')
        final=next(e for e in matches if e['event']=='finished')
        need(final['started']>=2 and final['completed']==final['started'],'response did not complete multiple awaited writes')
        if name!='slow_consumer':need(final['read_bytes']==len(UPLOAD),'server did not consume exact incremental request bytes')
    c=cases['slow_consumer']
    snapshots=[e for e in events if e.get('event')=='snapshot' and e.get('connection')==connection and e.get('stream')==c['stream']]
    need(len(snapshots)==1 and snapshots[0].get('started')==c['started'] and snapshots[0].get('completed')==c['completed'] and snapshots[0].get('finished')==0,'false pending-writer snapshot')
    d=cases['goaway']
    need(not any(e.get('connection')==d['connection'] and e.get('stream')==d['late_stream'] and e.get('event') in ('get','entered') for e in events),'late drain request entered a handler')
    need(sum(e.get('event')=='drained' and e.get('connection')==d['connection'] for e in events)==1,'missing completed drain ticket')
    witnesses=[]
    drain_witnesses=[]
    if receipt['client']=='quic-go':
        need(not any(e.get('connection')==d['connection'] and e.get('stream')==d.get('local_hold_stream') for e in events), 'local hold admitted by server')
    for path in qlogs:
        need(path.stat().st_size<=16*1024*1024,'qlog diagnostic bound')
        if path.suffix=='.qlog':native=json.loads(path.read_text())['traces'][0]['events']
        else:native=[json.loads(line.lstrip('\x1e')) for line in path.read_text().splitlines() if line.lstrip('\x1e')]
        need(len(native)<=100000,'qlog event bound')
        if receipt['client']=='quic-go' and any(e.get('name')=='transport:connection_closed' and e.get('data',{}).get('owner')=='remote' and e.get('data',{}).get('application_code')==256 for e in native):
            drain_witnesses.append({'qlog':path.name, **drain_ack_witness(native,d)})
        if any(f.get('stream_id')==c['stream'] and e.get('name')=='transport:packet_received' for e in native for f in e.get('data',{}).get('frames',[])):
            witnesses.append({'qlog':path.name,**flow_witness(native,c['stream'],c['sibling_stream'])})
    if receipt['client']=='quic-go':
        need(len(drain_witnesses)==1,'missing or ambiguous native drain ACK witness')
        (directory/'drain-ack-witness.json').write_text(json.dumps(drain_witnesses[0],indent=2)+'\n')
    need(len(witnesses)==1,'missing or ambiguous native flow-control witness')
    (directory/'flow-witness.json').write_text(json.dumps(witnesses[0],indent=2)+'\n')


def certificates(python, directory):
    code = '''
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID
from datetime import datetime, timedelta, timezone
from pathlib import Path
import os, sys
p=Path(sys.argv[1]);now=datetime.now(timezone.utc)
def key():return rsa.generate_private_key(public_exponent=65537,key_size=2048)
def cert(name,k,issuer,issuer_key,ca):
 n=x509.Name([x509.NameAttribute(NameOID.COMMON_NAME,name)])
 b=x509.CertificateBuilder().subject_name(n).issuer_name(issuer).public_key(k.public_key()).serial_number(x509.random_serial_number()).not_valid_before(now-timedelta(minutes=1)).not_valid_after(now+timedelta(days=1)).add_extension(x509.BasicConstraints(ca=ca,path_length=0 if ca else None),critical=True)
 if not ca:b=b.add_extension(x509.SubjectAlternativeName([x509.DNSName('localhost')]),critical=False)
 return b.sign(issuer_key,hashes.SHA256())
k=key();name=x509.Name([x509.NameAttribute(NameOID.COMMON_NAME,'TASK-162 ephemeral CA')]);ca=cert('TASK-162 ephemeral CA',k,name,k,True)
leaf_key=key();leaf=cert('localhost',leaf_key,ca.subject,k,False)
w=key();wn=x509.Name([x509.NameAttribute(NameOID.COMMON_NAME,'wrong CA')]);wrong=cert('wrong CA',w,wn,w,True)
for file,value in [('ca.pem',ca.public_bytes(serialization.Encoding.PEM)),('cert.pem',leaf.public_bytes(serialization.Encoding.PEM)+ca.public_bytes(serialization.Encoding.PEM)),('wrong-ca.pem',wrong.public_bytes(serialization.Encoding.PEM)),('key.pem',leaf_key.private_bytes(serialization.Encoding.PEM,serialization.PrivateFormat.PKCS8,serialization.NoEncryption()))]:
 fd=os.open(p/file,os.O_WRONLY|os.O_CREAT|os.O_TRUNC,0o600)
 with os.fdopen(fd,'wb') as out:out.write(value)
'''
    subprocess.run([python, '-c', code, str(directory)], check=True, capture_output=True, text=True, timeout=20)


def next_message(messages, deadline):
    remaining = deadline - time.monotonic()
    need(remaining > 0, 'case deadline exceeded')
    try: return messages.get(timeout=remaining)
    except queue.Empty as error: raise GateFailure('case deadline exceeded') from error


def run_client(name, fixture_path, client_command, credentials, directory, matrix="smoke", tshark=None):
    semantic = matrix == "task174"
    directory.mkdir(mode=0o700)
    messages = queue.Queue(maxsize=512)
    fixture = None; client = None
    exit_status = {}
    events = []
    try:
        fixture = Process([str(fixture_path), str(credentials / 'cert.pem'), str(credentials / 'key.pem'), str(directory / 'packets.pcap')], directory, 'fixture', messages)
        process,line = next_message(messages,time.monotonic()+15)
        need(process is fixture and line is not None, 'fixture startup failed')
        port = readiness(line)
        command = client_command + ['--port',str(port),'--ca',str(credentials/'ca.pem'),'--wrong-ca',str(credentials/'wrong-ca.pem'),'--log-dir',str(directory)]
        client = Process(command,directory,'client',messages)
        deadline = time.monotonic()+15
        waiting = None; receipt = None; active = set(); connection_id = None; drain_deadline = None; accounting = None
        while receipt is None:
            process,line = next_message(messages,deadline)
            if line is None and process is fixture:
                fixture.process.wait(timeout=3)
                for thread in fixture.threads: thread.join(timeout=2)
                diagnostic = (directory/'fixture.stderr').read_text().strip()
                prefix = 'fixture exited nonzero' if fixture.process.returncode else 'fixture exited'
                raise GateFailure(prefix + ' before all receipts' + (': ' + diagnostic if diagnostic else ''))
            need(line is not None, 'process exited before all receipts')
            try: item=json.loads(line)
            except json.JSONDecodeError as error: raise GateFailure('malformed JSON process receipt') from error
            if process is fixture:
                if item.get('event') == 'traffic':
                    need(accounting is None, 'duplicate fixture traffic accounting')
                    keys = ('received_count', 'sent_count', 'received_bytes', 'sent_bytes')
                    need(set(item) == {'event', 'port', *keys} and type(item.get('port')) is int
                         and item['port'] == port
                         and all(type(item.get(key)) is int and item[key] >= 0 for key in keys),
                         'malformed fixture traffic accounting')
                    accounting = item
                    continue  # Teardown accounting never acknowledges semantic work.
                need(accounting is None, 'fixture lifecycle event after final accounting')
                allowed = ('held','released','cancelled','get','post','health')
                if semantic:
                    allowed += ('entered','read','write_started','write_completed','finished','snapshot','drain_started','drained')
                need(item.get('event') in allowed, 'unknown fixture event')
                events.append(item)
                need(len(events)<=(1024 if semantic else 32),'fixture event bound')
            else:
                need(process is client,'unknown process')
                if 'receipt' in item:
                    need(accounting is None, 'fixture accounting before semantic receipt')
                    receipt=item['receipt'];break
                command=item.get('command')
                need(waiting is None,'overlapping runner commands')
                if command=='reset':
                    fixture.write('reset');client.write('{}');deadline=time.monotonic()+15
                elif semantic and command == 'goaway_witness':
                    need(drain_deadline is not None,'GOAWAY witness requested before drain')
                    client.write(json.dumps(goaway_witness(tshark,directory,port,min(deadline,drain_deadline),item['stream']+4)))
                    deadline=time.monotonic()+15
                elif semantic and command in ('snapshot','drain'):
                    waiting = {**item, 'event': 'snapshot' if command == 'snapshot' else 'drain_started'}
                    if command=='drain':drain_deadline=time.monotonic()+5
                    fixture.write(f"{command} {item['connection']} {item['stream']}")
                elif command in ('wait','release'):
                    waiting={**item,'event':item.get('event') if command=='wait' else 'released'}
                    if command=='release':fixture.write(f"release {item['connection']} {item['stream']}" if item.get('connection') else 'release')
                else: raise GateFailure('unknown client control command')
            if waiting:
                matching=[e for e in events if e['event']==waiting['event'] and e['stream']==waiting['stream'] and (waiting.get('connection') is None or e['connection']==waiting['connection']) and e.get('read_bytes',0)>=waiting.get('min_bytes',0)]
                if matching:
                    witness=matching[-1]
                    if connection_id is None:connection_id=witness['connection']
                    need(semantic or witness['connection']==connection_id,'request cases used separate connections')
                    key=(witness['connection'],witness['stream'])
                    if witness['event']=='held':active.add(key)
                    elif witness['event'] in ('released','cancelled'):
                        need(key in active,'terminal event without active handler');active.remove(key)
                    client.write(json.dumps(witness));waiting=None;deadline=time.monotonic()+15
        need(waiting is None and not active,'incomplete handler lifecycle')
        (directory/'unvalidated-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
        validate_receipt(receipt,name,matrix)
        need(next(c for c in receipt['cases'] if c['case']=='concurrency')['connection']==connection_id,'connection witness mismatch')
        client.process.wait(timeout=10)
        need(client.process.returncode==0,'client exited nonzero')
        (directory/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
        return receipt
    finally:
        primary = sys.exc_info()[1]
        if client:client.stop()
        if fixture:fixture.stop('quit')
        for process,key in [(fixture,'fixture'),(client,'client')]:
            if process:
                exit_status[key]=process.process.returncode
                exit_status[key+'_command']=process.command
                if process.errors:exit_status[key+'_reader_errors']=process.errors
        (directory/'exits.json').write_text(json.dumps(exit_status,indent=2)+'\n')
        cleanup=[]
        if fixture and fixture.process.returncode !=0:cleanup.append('fixture exited nonzero')
        if any(p.errors or any(t.is_alive() for t in p.threads) for p in [fixture,client] if p):cleanup.append('diagnostic capture failed')
        if cleanup:
            exit_status['cleanup_errors']=cleanup
            (directory/'exits.json').write_text(json.dumps(exit_status,indent=2)+'\n')
            if primary is None:raise GateFailure('; '.join(cleanup))
        if primary is None and client and (directory/'receipt.json').is_file():validate_artifacts(directory,receipt,name)


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--tshark', type=Path)
    parser.add_argument('--matrix', choices=['smoke','task174'], default='smoke')
    parser.add_argument('--build-dir',type=Path,required=True)
    parser.add_argument('--log-dir',type=Path,required=True)
    parser.add_argument('--python',required=True)
    parser.add_argument('--go-client',type=Path,required=True)
    args=parser.parse_args()
    logs_owned=False
    manifest={'source_head':release_identity.revision(ROOT),'pins':PINS,'status':'failed','matrix':args.matrix,'command':sys.argv,'capture_kind':'application-boundary datagrams with synthesized IPv4/UDP envelopes','nonlocal_checks':'BSD, Windows and other platforms deferred to CI/v3 PR per repository policy'}
    try:
        fixture=(args.build_dir/'test/http3_udp_fixture').resolve()
        need(fixture.is_file() and os.access(fixture,os.X_OK),'fixture binary missing')
        need(args.go_client.is_file() and os.access(args.go_client,os.X_OK),'quic-go client missing')
        identity=subprocess.run([args.python,'-c','import aioquic,json,sys;print(json.dumps({"client":"aioquic","version":aioquic.__version__,"python":sys.version,"executable":sys.executable}))'],capture_output=True,text=True,timeout=10,check=True)
        aio=json.loads(identity.stdout);need(aio['version']==PINS['aioquic'],'wrong aioquic version')
        go=json.loads(subprocess.check_output([str(args.go_client.resolve()),'--identity'],text=True,timeout=10));need(go.get('client')=='quic-go' and go.get('version')==PINS['quic-go'],'wrong Go client identity')
        need(not args.log_dir.exists(),'log directory already exists; retain prior evidence')
        args.log_dir.mkdir(parents=True,mode=0o700);logs_owned=True;directory=args.log_dir.resolve()
        manifest.update({'tools':[aio,go],'fixture':str(fixture),'go_client':str(args.go_client.resolve()),'python':args.python})
        cred=directory/'credentials';cred.mkdir(mode=0o700);certificates(args.python,cred)
        manifest['matrices']={}
        if args.matrix == 'task174':
            need(args.tshark and args.tshark.is_file(),'received-wire decoder missing')
            manifest['parity']=run_parity((args.build_dir/'test/http3_semantic_parity_fixture').resolve(),directory/'parity')
        for name,command in [('aioquic',[args.python,str(ROOT/'test/integ/http3_client_aioquic.py')]),('quic-go',[str(args.go_client.resolve())])]:
            manifest['matrices'][name]=run_client(name,fixture,command + (['--matrix','task174'] if args.matrix == 'task174' else []),cred,directory/name,args.matrix,args.tshark.resolve() if args.tshark else None)
        if args.matrix == 'task174':
            expected=validate_parity(manifest['parity'])
            for receipt in manifest['matrices'].values():
                for case in receipt['cases']:
                    if case['case'] in ('streaming','slow_producer'):
                        need(application_fields(case.get('headers'))==expected,'H3 did not match executed H1/H2 semantics')
        manifest['status']='passed'
        print('PASS: aioquic and quic-go real HTTP/3 matrices')
        return 0
    except (GateFailure,OSError,subprocess.SubprocessError,ValueError) as error:
        manifest['failure']=str(error);print('FAIL: '+str(error));return 1
    finally:
        if logs_owned:
            sources = [
                'src/detail/quic_repacketize.cpp','src/detail/quic_send_schedule.cpp','src/httpserver/detail/quic_recovery.hpp',
                'src/detail/http3_response_stream.cpp','src/detail/http3_body_stream.cpp','src/detail/http3_drain.cpp','src/detail/http3_control_drain.cpp',
                'test/support/http3_network_owner.cpp','test/support/http3_network_owner.hpp','test/support/http3_network_lifecycle.cpp',
                'test/support/http3_semantic_routes.hpp','test/support/http3_semantic_h1.hpp','test/integ/http3_udp_fixture.cpp','test/integ/http3_semantic_parity_fixture.cpp',
                'test/integ/http3_client_aioquic.py','test/integ/http3_semantic_aioquic.py','test/integ/http3-client-requirements.txt',
                'test/integ/http3-client-go/main.go','test/integ/http3-client-go/semantic.go','test/integ/http3-client-go/semantic_test.go',
                'test/integ/http3-client-go/go.mod','test/integ/http3-client-go/go.sum','test/integ/http3_gate_runner_test.py',
                'test/unit/http3_route_parity_fixture.hpp','test/unit/http2_request_fixture.hpp','test/unit/quic_owner_peer.hpp',
                'test/unit/http3_streaming_test.cpp','test/unit/http3_network_owner_test.cpp','test/unit/http3_semantic_h1_test.cpp','test/Makefile.am','Makefile.am','.gitignore','docs/task-174-http3-semantic-matrix.md',
                'scripts/run-v3-http3-gates.py']
            manifest['source_files_sha256']={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in sources if (ROOT/name).is_file()}
            manifest['fixture_sha256']=hashlib.sha256(fixture.read_bytes()).hexdigest() if fixture.is_file() else None
            manifest['go_client_sha256']=hashlib.sha256(args.go_client.read_bytes()).hexdigest() if args.go_client.is_file() else None
            parity=args.build_dir/'test/http3_semantic_parity_fixture'
            if args.matrix=='task174' and parity.is_file():manifest['parity_fixture_sha256']=hashlib.sha256(parity.read_bytes()).hexdigest()
            patch=release_identity.working_diff(ROOT)
            for name in ([] if os.environ.get('V3_RELEASE_IDENTITY') else subprocess.check_output(['git','ls-files','--others','--exclude-standard'],cwd=ROOT,text=True).splitlines()):
                result=subprocess.run(['git','diff','--no-index','--binary','--','/dev/null',name],cwd=ROOT,capture_output=True)
                need(result.returncode in (0,1),'working diff capture failed')
                patch+=result.stdout
            (args.log_dir/'working.diff').write_bytes(patch)
            manifest['working_diff_sha256']=hashlib.sha256(patch).hexdigest()
            (args.log_dir/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')


if __name__=='__main__':raise SystemExit(main())

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

UPLOAD = b'first\x00second\xffthird\x00'
PINS = {'aioquic': '1.3.0', 'quic-go': 'v0.55.0'}
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


def validate_receipt(receipt, client):
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
    (directory/'artifacts.json').write_text(json.dumps({'traffic':counts,'qlogs':[p.name for p in qlogs],'tls_keys':keys.name},indent=2)+'\n')


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


def run_client(name, fixture_path, client_command, credentials, directory):
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
        waiting = None; receipt = None; active = set(); connection_id = None
        while receipt is None:
            process,line = next_message(messages,deadline)
            need(line is not None, 'process exited before all receipts')
            try: item=json.loads(line)
            except json.JSONDecodeError as error: raise GateFailure('malformed JSON process receipt') from error
            if process is fixture:
                need(item.get('event') in ('held','released','cancelled','get','post','health'), 'unknown fixture event')
                events.append(item)
                need(len(events)<=32,'fixture event bound')
            else:
                need(process is client,'unknown process')
                if 'receipt' in item:
                    receipt=item['receipt'];break
                command=item.get('command')
                need(waiting is None,'overlapping runner commands')
                if command=='reset':
                    fixture.write('reset');client.write('{}');deadline=time.monotonic()+15
                elif command in ('wait','release'):
                    waiting={**item,'event':item.get('event') if command=='wait' else 'released'}
                    if command=='release':fixture.write('release')
                else: raise GateFailure('unknown client control command')
            if waiting:
                matching=[e for e in events if e['event']==waiting['event'] and e['stream']==waiting['stream']]
                if matching:
                    witness=matching[-1]
                    if connection_id is None:connection_id=witness['connection']
                    need(witness['connection']==connection_id,'request cases used separate connections')
                    key=(witness['connection'],witness['stream'])
                    if witness['event']=='held':active.add(key)
                    else:need(key in active,'terminal event without active handler');active.remove(key)
                    client.write(json.dumps(witness));waiting=None;deadline=time.monotonic()+15
        need(waiting is None and not active,'incomplete handler lifecycle')
        validate_receipt(receipt,name)
        need(next(c for c in receipt['cases'] if c['case']=='concurrency')['connection']==connection_id,'connection witness mismatch')
        client.process.wait(timeout=10)
        need(client.process.returncode==0,'client exited nonzero')
        (directory/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
        return receipt
    finally:
        if client:client.stop()
        if fixture:fixture.stop('quit')
        for process,key in [(fixture,'fixture'),(client,'client')]:
            if process:
                exit_status[key]=process.process.returncode
                exit_status[key+'_command']=process.command
                if process.errors:exit_status[key+'_reader_errors']=process.errors
        (directory/'exits.json').write_text(json.dumps(exit_status,indent=2)+'\n')
        if fixture and fixture.process.returncode !=0:raise GateFailure('fixture exited nonzero')
        need(not any(p.errors or any(t.is_alive() for t in p.threads) for p in [fixture,client] if p),'diagnostic capture failed')
        if client and (directory/'receipt.json').is_file():validate_artifacts(directory,receipt,name)


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--build-dir',type=Path,required=True)
    parser.add_argument('--log-dir',type=Path,required=True)
    parser.add_argument('--python',required=True)
    parser.add_argument('--go-client',type=Path,required=True)
    args=parser.parse_args()
    logs_owned=False
    manifest={'source_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'pins':PINS,'status':'failed'}
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
        for name,command in [('aioquic',[args.python,str(ROOT/'test/integ/http3_client_aioquic.py')]),('quic-go',[str(args.go_client.resolve())])]:
            manifest['matrices'][name]=run_client(name,fixture,command,cred,directory/name)
        manifest['status']='passed'
        print('PASS: aioquic and quic-go real HTTP/3 matrices')
        return 0
    except (GateFailure,OSError,subprocess.SubprocessError,ValueError) as error:
        manifest['failure']=str(error);print('FAIL: '+str(error));return 1
    finally:
        if logs_owned:
            manifest['source_files_sha256']={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [ROOT/'src/detail/quic_repacketize.cpp',ROOT/'src/httpserver/detail/quic_recovery.hpp',ROOT/'test/support/http3_network_owner.cpp',ROOT/'test/support/http3_network_owner.hpp',ROOT/'test/integ/http3_udp_fixture.cpp',ROOT/'test/integ/http3_client_aioquic.py',ROOT/'test/integ/http3-client-go/main.go',Path(__file__).resolve()]}
            manifest['fixture_sha256']=hashlib.sha256(fixture.read_bytes()).hexdigest() if fixture.is_file() else None
            manifest['working_diff_sha256']=hashlib.sha256(subprocess.check_output(['git','diff'],cwd=ROOT)).hexdigest()
            (args.log_dir/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')


if __name__=='__main__':raise SystemExit(main())

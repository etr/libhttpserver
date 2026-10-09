#!/usr/bin/env python3
"""Fail-closed independent WebSocket-over-H3 gate; provisioning is explicit."""
import argparse
import collections
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('h3_gate_support', ROOT/'scripts/run-v3-http3-gates.py')
support = importlib.util.module_from_spec(spec)
spec.loader.exec_module(support)
GateFailure = support.GateFailure
need = support.need
Process = support.Process
next_message = support.next_message
CASES = ['handshake','text','binary','fragmentation','segmentation','ping','close','invalid_mask','reset','backpressure','tls_failure']
WS_EVENTS = {'ws_accepted','ws_refused','ws_received','ws_sent','ws_blocked','ws_resumed','ws_closed','ws_retired','ws_snapshot','ws_flushing','ws_flushed'}
ALLOWED_EVENTS = WS_EVENTS | {'held','released','health'}
BINARY = b'\0\xff\x80binary\0'
PING = b'probe\0\xff'
FLOOD_SHA = hashlib.sha256(b''.join(bytes([i])*8192 for i in range(64))).hexdigest()


def stream_id(value):
    return type(value) is int and 0 <= value < (1 << 62) and value % 4 == 0


def upgraded(case):
    fields = case.get('headers')
    need(isinstance(fields,list) and len(fields) <= 64, 'missing upgrade fields')
    parsed = {}
    for field in fields:
        need(isinstance(field,list) and len(field)==2 and all(type(x) is str for x in field), 'malformed upgrade fields')
        name,value=field
        need(name not in parsed,'duplicated upgrade field');parsed[name]=value
    need(parsed.get(':status')=='200' and parsed.get('sec-websocket-protocol')=='harness.v1','wrong Extended CONNECT status/subprotocol')
    need(not {'upgrade','connection','sec-websocket-extensions','sec-websocket-accept'} & parsed.keys(),'unexpected extension/HTTP1 upgrade')


def closed(case, reason, clean):
    state=case.get('server')
    need(isinstance(state,dict) and state.get('connection')==case['connection'] and state.get('stream')==case['stream'], 'missing close identity')
    need(state.get('event')=='ws_retired' and state.get('retired') is True and state.get('close_count')==1 and state.get('reason')==reason and state.get('clean') is clean,'missing or duplicated close outcome')
    need(state.get('waiting') is False,'retired handler still waiting')
    if clean:
        need(state.get('code')==1000 and case.get('close_hex')=='03e8' and case.get('close_code')==1000 and case.get('terminal')=='FIN','wrong clean close/code/FIN')
    else:
        need(state.get('code') is None,'abnormal close had peer close code')


def validate_receipt(receipt):
    need(isinstance(receipt,dict) and receipt.get('client')=='aioquic' and receipt.get('version')=='1.3.0' and receipt.get('matrix')=='websocket','wrong independent client identity')
    rows=receipt.get('cases')
    need(isinstance(rows,list) and len(rows)==len(CASES) and all(isinstance(c,dict) and type(c.get('case')) is str for c in rows),'incomplete WebSocket receipt')
    need(collections.Counter(c['case'] for c in rows)==collections.Counter(CASES),'missing/duplicate/unexpected case')
    c={r['case']:r for r in rows}
    h=c['handshake']
    need(isinstance(h.get('settings'),dict) and h['settings'].get('8')==1, 'missing actual enabling SETTINGS')
    need(h.get('alpn')=='h3' and h.get('quic_version')==1 and h.get('tls_version')=='TLSv1.3' and h.get('verified') is True and h.get('early_data') is False,'unverified transport handshake')
    for name in CASES[:-1]:
        case=c[name];need(stream_id(case.get('stream')) and type(case.get('connection')) is int and case['connection']>0,'invalid WebSocket identity');upgraded(case)
    for name in ['text','binary','fragmentation','segmentation','ping','close']:
        need((c[name]['connection'],c[name]['stream'])==(h['connection'],h['stream']),'echo cases used another tunnel')
    for name,opcode,payload in [('text',1,b'hello HTTP/3'),('binary',2,BINARY)]:
        need(c[name].get('opcode')==opcode and c[name].get('payload_hex')==payload.hex() and c[name].get('masked') is False,'wrong echo payload/opcode/masking')
    need(c['fragmentation'].get('payload_hex')=='fragment \u20ac done'.encode().hex() and c['fragmentation'].get('pong_hex')==PING.hex() and c['fragmentation'].get('masked') is False,'wrong fragmented echo/Pong')
    need(c['segmentation'].get('split_hex')==BINARY.hex() and c['segmentation'].get('coalesced_hex')==[b'coalesced'.hex(),BINARY.hex()],'segmentation/coalescing mismatch')
    need(c['ping'].get('pong_hex')==PING.hex() and c['ping'].get('echo_hex')==b'after ping'.hex(),'wrong Ping/Pong or continued echo')
    closed(c['close'],'ok',True)
    closed(c['invalid_mask'],'protocol_error',False)
    need(c['invalid_mask'].get('type')=='StreamReset' and c['invalid_mask'].get('code')==268,'unmasked input was not refused')
    closed(c['reset'],'cancelled',False)
    need(c['reset'].get('terminal')=='StreamReset' and c['reset'].get('code')==268,'wrong reset outcome')
    closed(c['backpressure'],'ok',True)
    identifiers=collections.defaultdict(list)
    for name in ['handshake','invalid_mask','reset','backpressure']:
        identifiers[c[name]['connection']].append(c[name]['stream'])
    for name in ['invalid_mask','reset','backpressure']:
        case=c[name]
        need(case.get('sibling_connection')==case['connection'] and case.get('sibling_status')==204 and stream_id(case.get('sibling_stream')),'sibling blocked or on another connection')
        identifiers[case['connection']].append(case['sibling_stream'])
    reset,bp=c['reset'],c['backpressure']
    need(reset['connection']==bp['connection'] and reset['connection']!=h['connection'] and c['invalid_mask']['connection']==h['connection'],'wrong scenario connections')
    for case in [reset,bp]:
        need(case.get('held_connection')==case['connection'] and stream_id(case.get('held_stream')) and case.get('held_pending') is True,'held HTTP sibling did not span scenario')
    need(reset['held_stream']==bp['held_stream'],'reset/backpressure used different held requests')
    identifiers[bp['connection']].append(bp['held_stream'])
    need(all(len(ids)==len(set(ids)) for ids in identifiers.values()),'invalid or reused bidirectional stream IDs')
    snapshot=bp.get('snapshot')
    need(isinstance(snapshot,dict) and snapshot.get('event')=='ws_snapshot' and snapshot.get('connection')==bp['connection'] and snapshot.get('stream')==bp['stream'],'missing selected-stream snapshot')
    need(snapshot.get('waiting') is True and snapshot.get('close_count')==0 and snapshot.get('retired') is False and type(snapshot.get('blocked')) is int and snapshot['blocked']>0 and type(snapshot.get('accepted')) is int and 0<snapshot['accepted']<64,'no actual pending writable wait')
    final=bp['server']
    need(final.get('accepted')==64 and type(final.get('resumed')) is int and final['resumed']>0 and final['accepted']>snapshot['accepted'],'flood never resumed')
    need(bp.get('credit')==4096 and bp.get('offset')==4096 and bp.get('messages')==64 and bp.get('body_bytes')==524288 and bp.get('body_sha256')==FLOOD_SHA and bp.get('hold_status')==200,'wrong exhausted-credit/ordered flood/held result')
    failure=c['tls_failure']
    need(failure.get('type')=='ConnectionTerminated' and failure.get('code') in (298,304) and failure.get('response') is False,'missing wrong-CA certificate verification failure')


def validate_events(events, receipt):
    sequence=[e.get('sequence') for e in events]
    need(sequence==list(range(1,len(events)+1)),'missing/nonmonotonic fixture events')
    need(all(e.get('event') in ALLOWED_EVENTS for e in events),'unexpected fixture event')
    c={r['case']:r for r in receipt['cases']}
    def one(kind, conn, stream):
        matches=[e for e in events if e.get('event')==kind and e.get('connection')==conn and e.get('stream')==stream]
        need(len(matches)==1,'missing/duplicate '+kind+' observation');return matches[0]
    for name in ['close','invalid_mask','reset','backpressure']:
        case=c[name];conn,stream=case['connection'],case['stream']
        admitted=one('ws_accepted',conn,stream);closing=one('ws_closed',conn,stream);retired=one('ws_retired',conn,stream)
        need(admitted.get('subprotocol')=='harness.v1' and admitted['sequence']<closing['sequence']<retired['sequence'],'incomplete WebSocket handler lifecycle')
        need(case['server']==retired,'forged server close receipt')
        need(all(closing.get(k)==retired.get(k) for k in ['close_count','reason','code','clean']),'close callback changed on retirement')
        if name!='close':
            health=one('health',conn,case['sibling_stream'])
            if name!='backpressure':need(health['sequence']>retired['sequence'],'health did not follow reset/refusal')
    echo=c['close']['server']
    need(echo.get('received')==7 and echo.get('received_bytes')==78 and echo.get('accepted')==7,'server did not observe exact echoed message kinds/bytes')
    reset,bp=c['reset'],c['backpressure'];conn=bp['connection']
    held=one('held',conn,bp['held_stream']);release=one('released',conn,bp['held_stream'])
    snap=one('ws_snapshot',conn,bp['stream']);health=one('health',conn,bp['sibling_stream'])
    need(bp['snapshot']==snap and snap.get('waiting') is True,'forged pending writable snapshot')
    need(held['sequence']<one('ws_accepted',conn,reset['stream'])['sequence']<reset['server']['sequence']<snap['sequence']<health['sequence']<bp['server']['sequence']<release['sequence'],'held request released before reset/backpressure completed')
    resumed=[e for e in events if e['event']=='ws_resumed' and e['connection']==conn and e['stream']==bp['stream'] and e['sequence']>health['sequence']]
    need(resumed and any(e.get('resumed',0)>0 for e in resumed),'missing actual resumed send progress')
    blocked=[e for e in events if e['event']=='ws_blocked' and e['connection']==conn and e['stream']==bp['stream'] and e['sequence']<snap['sequence'] and e.get('waiting') is True]
    need(blocked,'missing actual backpressured send disposition')
    expected={(c[n]['connection'],c[n]['stream']) for n in ['close','invalid_mask','reset','backpressure']}
    need({(e['connection'],e['stream']) for e in events if e['event'] in WS_EVENTS}==expected,'unexpected WebSocket observations')


def flow_witness(events, stream, sibling):
    return support.flow_witness(events,stream,sibling)


def validate_artifacts(directory, receipt):
    raw=(directory/'fixture.stdout').read_text().splitlines()
    need(raw and raw[0].startswith('READY '),'missing fixture readiness artifact')
    events=[json.loads(line) for line in raw[1:]]
    traffic=[e for e in events if e.get('event')=='traffic']
    need(len(traffic)==1,'missing/duplicate capture traffic accounting')
    traffic=traffic[0];counts=support.capture_counts(directory/'packets.pcap',traffic['port'])
    need(all(traffic.get(k)==v for k,v in counts.items()),'capture traffic accounting mismatch')
    validate_events([e for e in events if e.get('event')!='traffic'],receipt)
    keys=directory/'tls.keys'
    need(keys.is_file() and keys.stat().st_size>0 and keys.stat().st_mode & 0o777==0o600,'missing/insecure TLS key diagnostics')
    paths=list(directory.glob('*.qlog'))
    need(len(paths)==3 and all(p.stat().st_size>0 for p in paths),'missing success/failure qlogs')
    traces={}
    for path in paths:
        data=json.loads(path.read_text());need(isinstance(data.get('traces'),list) and len(data['traces'])==1,'malformed native qlog')
        trace=data['traces'][0];odcid=trace.get('common_fields',{}).get('ODCID')
        need(odcid and odcid not in traces,'missing/duplicate qlog connection identity');traces[odcid]=trace['events']
    c={r['case']:r for r in receipt['cases']}
    def native(case):
        need(case.get('qlog_id') in traces,'receipt not tied to native connection qlog');return traces[case['qlog_id']]
    handshake=native(c['handshake'])
    settings=[(i,e) for i,e in enumerate(handshake) if e.get('name')=='http:settings_received']
    request=[i for i,e in enumerate(handshake) if e.get('name')=='http:frame_created' and e.get('data',{}).get('stream_id')==c['handshake']['stream'] and e.get('data',{}).get('frame',{}).get('frame_type')=='headers']
    need(len(settings)==1 and settings[0][1].get('data',{}).get('settings')==c['handshake']['settings'] and request and settings[0][0]<min(request),'SETTINGS not received before Extended CONNECT')
    for name in ['handshake','invalid_mask','reset','backpressure']:
        case=c[name];rows=native(case)
        parsed=[e['data']['frame'].get('headers') for e in rows if e.get('name')=='http:frame_parsed' and e.get('data',{}).get('stream_id')==case['stream'] and e.get('data',{}).get('frame',{}).get('frame_type')=='headers']
        need(len(parsed)==1 and [[f['name'],f['value']] for f in parsed[0]]==case['headers'],'upgrade headers missing from native qlog')
        created=[e['data']['frame'].get('headers') for e in rows if e.get('name')=='http:frame_created' and e.get('data',{}).get('stream_id')==case['stream'] and e.get('data',{}).get('frame',{}).get('frame_type')=='headers']
        expected={':method':'CONNECT',':protocol':'websocket',':scheme':'https',':authority':'localhost',':path':'/ws/flood' if name=='backpressure' else '/ws/echo','sec-websocket-version':'13','sec-websocket-protocol':'other, harness.v1','sec-websocket-extensions':'permessage-deflate'}
        need(len(created)==1 and len(created[0])==len(expected) and {f['name']:f['value'] for f in created[0]}==expected,'missing native RFC 9220 request headers')
    need(c['handshake']['qlog_id']==c['invalid_mask']['qlog_id'] and c['reset']['qlog_id']==c['backpressure']['qlog_id'] and c['reset']['qlog_id']!=c['handshake']['qlog_id'],'wrong native scenario connections')
    def packet_frame(case, direction, kind, **required):
        return any(e.get('name')=='transport:packet_'+direction and f.get('frame_type')==kind and f.get('stream_id')==case['stream'] and all(f.get(k)==v for k,v in required.items()) for e in native(case) for f in e.get('data',{}).get('frames',[]))
    need(packet_frame(c['invalid_mask'],'received','reset_stream',error_code=268),'missing native invalid-mask termination')
    for direction,kind in [('sent','stop_sending'),('sent','reset_stream'),('received','reset_stream')]:
        need(packet_frame(c['reset'],direction,kind,error_code=268),'missing native STOP_SENDING/RESET_STREAM cancellation')
    need(packet_frame(c['close'],'received','stream',fin=True),'missing native clean-close FIN')
    unused=set(traces)-{c['handshake']['qlog_id'],c['reset']['qlog_id']}
    need(len(unused)==1,'wrong TLS failure qlog identity')
    failure_rows=traces[unused.pop()]
    need(any(e.get('name')=='transport:packet_sent' and f.get('frame_type')=='connection_close' and f.get('error_code')==c['tls_failure']['code'] for e in failure_rows for f in e.get('data',{}).get('frames',[])) and not any(e.get('name')=='http:frame_parsed' for e in failure_rows),'missing native certificate verification alert')
    witness=flow_witness(native(c['backpressure']),c['backpressure']['stream'],c['backpressure']['sibling_stream'])
    (directory/'artifacts.json').write_text(json.dumps({'traffic':counts,'qlogs':[p.name for p in paths],'tls_keys':keys.name,'flow_witness':witness},indent=2)+'\n')


def validate_command(item):
    need(isinstance(item,dict),'malformed client command')
    command=item.get('command')
    need(command in ('wait','release','ws_snapshot') and stream_id(item.get('stream')),'malformed client command/stream')
    conn=item.get('connection')
    need(conn is None or type(conn) is int and conn>0,'malformed command connection')
    if command=='wait':
        need(item.get('event') in ('ws_accepted','ws_retired','held','health'),'unknown wait event')
    else:
        need(conn is not None,'targeted command requires exact connection')
    return command


def run_client(fixture_path, command, credentials, directory):
    directory.mkdir(mode=0o700)
    messages=queue.Queue(maxsize=512)
    fixture=client=None
    receipt=None;events=[];commands=[];exits={};cleanup=[]
    primary=None
    try:
        fixture=Process([str(fixture_path),str(credentials/'cert.pem'),str(credentials/'key.pem'),str(directory/'packets.pcap')],directory,'fixture',messages)
        process,line=next_message(messages,time.monotonic()+10)
        need(process is fixture and line is not None,'fixture exited before readiness')
        port=support.readiness(line)
        client=Process(command+['--port',str(port),'--ca',str(credentials/'ca.pem'),'--wrong-ca',str(credentials/'wrong-ca.pem'),'--log-dir',str(directory)],directory,'client',messages)
        waiting=None;start=time.monotonic();due=start+15;active=set()
        while receipt is None:
            process,line=next_message(messages,min(due,start+40))
            need(line is not None,'process exited before complete receipt')
            item=json.loads(line)
            need(isinstance(item,dict),'malformed process JSON')
            if process is fixture:
                need(item.get('event') in ALLOWED_EVENTS,'unexpected fixture event')
                need(type(item.get('sequence')) is int and item['sequence']==len(events)+1 and type(item.get('connection')) is int and item['connection']>0 and stream_id(item.get('stream')),'malformed fixture identity/sequence')
                events.append(item);need(len(events)<=1024,'fixture event bound')
                key=(item['connection'],item['stream'])
                if item['event']=='held':need(key not in active,'duplicate held handler');active.add(key)
                if item['event']=='released':need(key in active,'release without held handler');active.remove(key)
            else:
                need(process is client,'unknown process source')
                if 'receipt' in item:
                    receipt=item['receipt'];break
                need(waiting is None,'overlapping commands')
                kind=validate_command(item);commands.append(item);need(len(commands)<=64,'command bound')
                waiting={**item,'event':item.get('event') if kind=='wait' else ('released' if kind=='release' else 'ws_snapshot')}
                if kind!='wait':fixture.write(f"{kind} {item['connection']} {item['stream']}")
            if waiting:
                matches=[e for e in events if e['event']==waiting['event'] and e['stream']==waiting['stream'] and (waiting.get('connection') is None or e['connection']==waiting['connection'])]
                need(len(matches)<=1,'ambiguous command identity')
                if matches:
                    client.write(json.dumps(matches[0]));waiting=None;due=time.monotonic()+15
        need(waiting is None and not active,'unfinished control/held handler lifecycle')
        (directory/'unvalidated-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
        validate_receipt(receipt)
        client.process.wait(timeout=5);need(client.process.returncode==0,'client exited nonzero')
    except Exception as error:
        primary=error
        raise
    finally:
        for process,name,graceful in [(client,'client',None),(fixture,'fixture','quit')]:
            if process:
                try:process.stop(graceful)
                except Exception as error:cleanup.append(str(error))
                exits[name]=process.process.returncode;exits[name+'_command']=process.command
                if process.process.returncode!=0:cleanup.append(name+' exited nonzero')
                if process.errors or any(t.is_alive() for t in process.threads):cleanup.append(name+' diagnostic capture failed')
        exits['cleanup_errors']=cleanup
        (directory/'exits.json').write_text(json.dumps(exits,indent=2)+'\n')
        (directory/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        if primary is None:need(not cleanup,'; '.join(cleanup))
    validate_artifacts(directory,receipt)
    (directory/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    return receipt


def record_sources(directory, manifest, fixture):
    sources=['src/detail/http3_request_engine.cpp','src/httpserver/detail/http3_request_state.hpp','test/unit/http3_websocket_lifecycle_test.cpp','scripts/run-v3-http3-websocket-gates.py','scripts/run-v3-http3-gates.py','test/integ/http3_udp_fixture.cpp','test/integ/http3_client_aioquic.py','test/integ/http3_semantic_aioquic.py','test/integ/http3-client-requirements.txt','test/support/http3_network_owner.hpp','test/support/http3_network_owner.cpp','test/support/http3_network_lifecycle.cpp','test/support/http3_websocket_routes.hpp','test/unit/http3_websocket_harness_test.cpp','test/Makefile.am','Makefile.am','.gitignore','docs/task-175-http3-websocket-harness.md']
    sources.extend(str(p.relative_to(ROOT)) for p in (ROOT/'test/integ').glob('http3_websocket*.py'))
    manifest['source_files_sha256']={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in sources if (ROOT/name).is_file()}
    manifest['fixture_sha256']=hashlib.sha256(fixture.read_bytes()).hexdigest() if fixture.is_file() else None
    patch=subprocess.check_output(['git','diff','--binary','HEAD'],cwd=ROOT)
    for name in subprocess.check_output(['git','ls-files','--others','--exclude-standard'],cwd=ROOT,text=True).splitlines():
        result=subprocess.run(['git','diff','--no-index','--binary','--','/dev/null',name],cwd=ROOT,capture_output=True)
        need(result.returncode in (0,1),'diff capture failed');patch+=result.stdout
    (directory/'working.diff').write_bytes(patch)
    manifest['working_diff_sha256']=hashlib.sha256(patch).hexdigest()
    (directory/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--build-dir',type=Path,required=True)
    parser.add_argument('--log-dir',type=Path,required=True)
    parser.add_argument('--python',required=True)
    args=parser.parse_args()
    owned=False;fixture=(args.build_dir/'test/http3_udp_fixture').resolve()
    manifest={'status':'failed','matrix':'websocket','command':sys.argv,'source_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'nonlocal_checks':'BSD, Windows and other nonlocal checks deferred to CI/v3 PR','capture_kind':'actual application-boundary datagrams with synthesized IPv4/UDP envelopes'}
    try:
        need(not args.log_dir.exists(),'evidence directory already exists; preserve prior evidence')
        need(fixture.is_file() and os.access(fixture,os.X_OK),'fixture binary missing')
        identity=subprocess.run([args.python,'-c','import aioquic,json,sys;print(json.dumps({"version":aioquic.__version__,"python":sys.version,"executable":sys.executable}))'],capture_output=True,text=True,timeout=10,check=True)
        tool=json.loads(identity.stdout);need(tool.get('version')=='1.3.0','wrong aioquic identity')
        args.log_dir.mkdir(mode=0o700,parents=True);owned=True;directory=args.log_dir.resolve()
        manifest.update(tool=tool,fixture=str(fixture))
        credentials=directory/'credentials';credentials.mkdir(mode=0o700);support.certificates(args.python,credentials)
        manifest['receipt']=run_client(fixture,[args.python,str(ROOT/'test/integ/http3_websocket_aioquic.py')],credentials,directory/'aioquic')
        manifest['status']='passed';print('PASS: independent RFC 9220 WebSocket-over-H3 matrix');return 0
    except (GateFailure,OSError,subprocess.SubprocessError,ValueError,KeyError,TypeError) as error:
        manifest['failure']=str(error);print('FAIL: '+str(error));return 1
    finally:
        if owned:record_sources(args.log_dir,manifest,fixture)


if __name__=='__main__':raise SystemExit(main())

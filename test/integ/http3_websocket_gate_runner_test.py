"""Gate rejection contracts; synthetic witnesses never establish interoperability."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import queue
import subprocess
import struct
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]


def good():
    headers = [[':status', '200'], ['sec-websocket-protocol', 'harness.v1']]
    identity = {'qlog_id':'a', 'stream': 0, 'connection': 1, 'headers': headers}
    def server(connection, stream, **kw):
        return {'event': 'ws_retired', 'connection': connection, 'stream': stream, 'close_count': 1, 'reason': 'ok', 'clean': True, 'code': 1000, 'retired': True, 'accepted': 7, 'blocked': 0, 'resumed': 0, 'waiting': False, 'received': 7, 'received_bytes': 78, 'subprotocol': 'harness.v1', **kw}
    close = {'close_hex': '03e8', 'close_code': 1000, 'terminal': 'FIN', 'server': server(1,0)}
    reset_id = {'qlog_id':'b', 'stream': 4, 'connection': 2, 'headers': headers}
    flood_id = {'qlog_id':'b', 'stream': 12, 'connection': 2, 'headers': headers}
    held = {'held_stream': 0, 'held_connection': 2, 'held_pending': True}
    cases = [
        {'case': 'handshake', **identity, 'settings': {'8':1}, 'alpn':'h3', 'quic_version':1, 'tls_version':'TLSv1.3','verified':True,'early_data':False},
        {'case':'text', **identity, 'opcode':1, 'payload_hex':b'hello HTTP/3'.hex(), 'masked':False},
        {'case':'binary', **identity, 'opcode':2, 'payload_hex':b'\0\xff\x80binary\0'.hex(), 'masked':False},
        {'case':'fragmentation', **identity, 'payload_hex':'fragment \u20ac done'.encode().hex(), 'pong_hex':b'probe\0\xff'.hex(), 'masked':False},
        {'case':'segmentation', **identity, 'split_hex':b'\0\xff\x80binary\0'.hex(), 'coalesced_hex':[b'coalesced'.hex(), b'\0\xff\x80binary\0'.hex()]},
        {'case':'ping', **identity, 'pong_hex':b'probe\0\xff'.hex(), 'echo_hex':b'after ping'.hex()},
        {'case':'close', **identity, **close},
        {'case':'invalid_mask', 'qlog_id':'a', 'connection':1, 'stream':4, 'headers':headers, 'type':'StreamReset','code':268,'server':server(1,4,reason='protocol_error',clean=False,code=None,accepted=0,received=0,received_bytes=0),'sibling_stream':8,'sibling_connection':1,'sibling_status':204},
        {'case':'reset', **reset_id, **held, 'code':268,'terminal':'StreamReset','server':server(2,4,reason='cancelled',clean=False,code=None,accepted=0,received=0,received_bytes=0),'sibling_stream':8,'sibling_connection':2,'sibling_status':204},
        {'case':'backpressure', **flood_id, **held, 'credit':4096,'offset':4096,'snapshot':{'event':'ws_snapshot','connection':2,'stream':12,'accepted':2,'blocked':2,'waiting':True,'close_count':0,'retired':False},'sibling_stream':16,'sibling_connection':2,'sibling_status':204,'hold_status':200,'messages':64,'body_bytes':524288,'body_sha256':hashlib.sha256(b''.join(bytes([i])*8192 for i in range(64))).hexdigest(), **close, 'server':server(2,12,accepted=64,blocked=63,resumed=63,received=0,received_bytes=0)},
        {'case':'tls_failure','type':'ConnectionTerminated','code':298,'response':False},
    ]
    return {'client':'aioquic','version':'1.3.0','matrix':'websocket','cases':cases}


def events_for(receipt):
    c = {r['case']:r for r in receipt['cases']}
    events = []
    def event(kind, conn, stream, **kw):
        events.append({'event':kind,'connection':conn,'stream':stream,**kw})
    for name in ['close','invalid_mask']:
        r=c[name];event('ws_accepted',1,r['stream'],subprotocol='harness.v1')
        event('ws_closed',1,r['stream'], **{k:r['server'][k] for k in ['close_count','reason','clean','code']})
        events.append(copy.deepcopy(r['server']))
    event('health',1,8)
    event('held',2,0);event('ws_accepted',2,4,subprotocol='harness.v1')
    event('ws_closed',2,4,close_count=1,reason='cancelled',clean=False,code=None);events.append(copy.deepcopy(c['reset']['server']))
    event('health',2,8);event('ws_accepted',2,12,subprotocol='harness.v1')
    event('ws_blocked',2,12,waiting=True,accepted=2,blocked=2)
    events.append(copy.deepcopy(c['backpressure']['snapshot']));event('health',2,16)
    event('ws_resumed',2,12,resumed=3,accepted=3,waiting=False)
    event('ws_closed',2,12,close_count=1,reason='ok',clean=True,code=1000);events.append(copy.deepcopy(c['backpressure']['server']))
    event('released',2,0)
    for n,e in enumerate(events,1): e['sequence']=n
    for r in receipt['cases']:
        for field in ['server','snapshot']:
            if field in r:
                matches=[e for e in events if e['event']==r[field]['event'] and e['connection']==r['connection'] and e['stream']==r['stream']]
                r[field]=copy.deepcopy(matches[0])
    return events


def flow():
    def packet(name, stream, **kw):
        return {'name':'transport:packet_'+name,'data':{'frames':[{'frame_type':'stream','stream_id':stream,**kw}]}}
    return [packet('received',12,offset=3000,length=1096),packet('received',16,offset=0,length=4,fin=True),{'name':'transport:packet_sent','data':{'frames':[{'frame_type':'max_stream_data','stream_id':12,'maximum':8192}]}},packet('received',12,offset=4096,length=1000),packet('received',12,offset=530000,length=4,fin=True)]


class GateTests(unittest.TestCase):
    def setUp(self):
        path=ROOT/'scripts/run-v3-http3-websocket-gates.py'
        self.assertTrue(path.is_file(), 'WebSocket gate runner missing')
        spec=importlib.util.spec_from_file_location('ws_gate',path)
        self.g=importlib.util.module_from_spec(spec);spec.loader.exec_module(self.g)

    def test_good_receipt_and_correlated_events(self):
        r=good();events=events_for(r)
        self.g.validate_receipt(r);self.g.validate_events(events,r)
        self.g.flow_witness(flow(),12,16)

    def test_forged_receipts_rejected(self):
        mutations=[('handshake','settings',{'8':0}),('handshake','verified',False),('text','payload_hex','00'),('binary','masked',True),('fragmentation','pong_hex','00'),('ping','echo_hex','00'),('segmentation','split_hex','00'),('close','terminal','RESET'),('invalid_mask','type','FIN'),('reset','held_pending',False),('reset','sibling_connection',3),('backpressure','credit',8192),('backpressure','body_bytes',1),('backpressure','sibling_stream',12)]
        for case,key,value in mutations:
            r=good();next(c for c in r['cases'] if c['case']==case)[key]=value
            with self.subTest(case=case,key=key),self.assertRaises(self.g.GateFailure):self.g.validate_receipt(r)
        for case,field,key,value in [('invalid_mask','server','reason','ok'),('reset','server','close_count',2),('backpressure','snapshot','waiting',False),('backpressure','server','resumed',0)]:
            r=good();next(c for c in r['cases'] if c['case']==case)[field][key]=value
            with self.subTest(key=key),self.assertRaises(self.g.GateFailure):self.g.validate_receipt(r)
        for mutate in [lambda r:r['cases'].pop(),lambda r:r['cases'].append(r['cases'][0]),lambda r:r.update(version='0')]:
            r=good();mutate(r)
            with self.assertRaises(self.g.GateFailure):self.g.validate_receipt(r)

    def test_server_evidence_cannot_be_forged_or_released_early(self):
        for mutation in ['duplicate_close','wrong_connection','early_release','no_wait','wrong_final']:
            r=good();events=events_for(r)
            if mutation=='duplicate_close':events.append(copy.deepcopy(next(e for e in events if e['event']=='ws_closed')))
            if mutation=='wrong_connection':next(e for e in events if e['event']=='health' and e['connection']==2)['connection']=3
            if mutation=='early_release':events.insert(7,events.pop())
            if mutation=='no_wait':next(e for e in events if e['event']=='ws_snapshot')['waiting']=False
            if mutation=='wrong_final':next(e for e in events if e['event']=='ws_retired')['received_bytes']=0
            for n,e in enumerate(events,1):e['sequence']=n
            with self.subTest(mutation=mutation),self.assertRaises(self.g.GateFailure):self.g.validate_events(events,r)

    def test_credit_proof_rejects_missing_premature_wrong_stream_grants(self):
        for mutation in ['missing','early','wrong_stream','no_resume','no_fin']:
            rows=flow()
            if mutation=='missing':rows.pop(2)
            if mutation=='early':rows.insert(0,rows.pop(2))
            if mutation=='wrong_stream':rows[2]['data']['frames'][0]['stream_id']=0
            if mutation=='no_resume':rows.pop(3);rows[-1]['data']['frames'][0]['offset']=0
            if mutation=='no_fin':rows.pop()
            with self.subTest(mutation=mutation),self.assertRaises(self.g.GateFailure):self.g.flow_witness(rows,12,16)

    def test_process_failure_retirement_and_primary_error_preserved(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp);fixture=p/'fixture'
            fixture.write_text('#!/bin/sh\nprintf "READY invalid\\n"\nexit 3\n');fixture.chmod(0o700)
            with self.assertRaisesRegex(self.g.GateFailure,'invalid fixture readiness'):
                self.g.run_client(fixture,[],p,p/'evidence')
            exits=json.loads((p/'evidence/exits.json').read_text());self.assertEqual(exits['fixture'],3)

    def fake_processes(self, directory, *, client_exit=0, fixture_exit=0, malformed=False):
        r=good();events=events_for(r)
        fixture=directory/'fixture'
        fixture.write_text('#!'+sys.executable+'\nimport sys\nprint("READY 443",flush=True)\n'+
                           ''.join('print('+repr(json.dumps(e))+',flush=True)\n' for e in events)+
                           'sys.stdin.readline()\nsys.exit('+str(fixture_exit)+')\n')
        fixture.chmod(0o700)
        client=directory/'client.py'
        first={'command':'wait','event':'released','stream':0,'connection':2} if malformed else {'command':'wait','event':'health','stream':16,'connection':2}
        client.write_text('import sys\nprint('+repr(json.dumps(first))+',flush=True)\nsys.stdin.readline()\nprint('+repr(json.dumps({'receipt':r}))+',flush=True)\nsys.exit('+str(client_exit)+')\n')
        return fixture,[sys.executable,str(client)],r

    def test_valid_processes_retire_before_success(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp);fixture,client,r=self.fake_processes(p)
            with patch.object(self.g,'validate_artifacts') as artifacts:
                result=self.g.run_client(fixture,client,p,p/'evidence')
            self.assertEqual(result,r)
            artifacts.assert_called_once()
            exits=json.loads((p/'evidence/exits.json').read_text())
            self.assertEqual((exits['client'],exits['fixture']),(0,0))

    def test_nonzero_client_and_fixture_cannot_pass(self):
        for who in ['client','fixture']:
            with self.subTest(who=who),tempfile.TemporaryDirectory() as temp:
                p=Path(temp);fixture,client,_=self.fake_processes(p,**{who+'_exit':3})
                with patch.object(self.g,'validate_artifacts'),self.assertRaisesRegex(self.g.GateFailure,who+' exited nonzero'):
                    self.g.run_client(fixture,client,p,p/'evidence')
                self.assertFalse((p/'evidence/receipt.json').exists())

    def test_malformed_client_command_survives_bad_fixture_teardown(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp);fixture,client,_=self.fake_processes(p,fixture_exit=3,malformed=True)
            with self.assertRaisesRegex(self.g.GateFailure,'unknown wait event'):
                self.g.run_client(fixture,client,p,p/'evidence')
            self.assertIn('fixture exited nonzero',json.loads((p/'evidence/exits.json').read_text())['cleanup_errors'])

    def test_deadline_and_malformed_commands(self):
        with self.assertRaises(self.g.GateFailure):self.g.next_message(queue.Queue(),time.monotonic()-1)
        for c in [{},{'command':'wait','stream':True,'event':'held'},{'command':'release','stream':0},{'command':'ws_snapshot','stream':4,'connection':0},{'command':'wait','event':'invented','stream':0}]:
            with self.subTest(c=c),self.assertRaises(self.g.GateFailure):self.g.validate_command(c)

    def test_missing_artifacts_fail_closed(self):
        r=good();events=events_for(r)
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp)
            with self.assertRaises((self.g.GateFailure,OSError)):self.g.validate_artifacts(p,r)
            (p/'fixture.stdout').write_text('READY 443\n'+'\n'.join(json.dumps(e) for e in events)+'\n')
            with self.assertRaises(self.g.GateFailure):self.g.validate_artifacts(p,r)

    def artifact_fixture(self, directory):
        r=good();events=events_for(r)
        traffic={'event':'traffic','port':443,'sent_count':1,'received_count':1,'sent_bytes':1,'received_bytes':1}
        (directory/'fixture.stdout').write_text('READY 443\n'+'\n'.join(json.dumps(e) for e in events+[traffic])+'\n')
        wire=struct.pack('<IHHIIII',0xa1b2c3d4,2,4,0,0,65535,101)
        for source,dest in [(443,6000),(6000,443)]:
            packet=bytearray(29);packet[0]=0x45;packet[9]=17
            packet[12:20]=bytes([127,0,0,1,127,0,0,1])
            struct.pack_into('!H',packet,2,29);struct.pack_into('!HHH',packet,20,source,dest,9)
            wire+=struct.pack('<IIII',1,0,29,29)+packet
        (directory/'packets.pcap').write_bytes(wire)
        (directory/'tls.keys').write_text('CLIENT_HANDSHAKE_TRAFFIC_SECRET test\n');(directory/'tls.keys').chmod(0o600)
        def head(kind,stream):
            return {'name':'http:frame_'+kind,'data':{'stream_id':stream,'frame':{'frame_type':'headers','headers':[{'name':':status','value':'200'},{'name':'sec-websocket-protocol','value':'harness.v1'}]}}}
        def request(stream,path):
            row=head('created',stream)
            row['data']['frame']['headers']=[{'name':k,'value':v} for k,v in [(':method','CONNECT'),(':protocol','websocket'),(':scheme','https'),(':authority','localhost'),(':path',path),('sec-websocket-version','13'),('sec-websocket-protocol','other, harness.v1'),('sec-websocket-extensions','permessage-deflate')]]
            return row
        def packet(direction,kind,stream=4,**kw):
            return {'name':'transport:packet_'+direction,'data':{'frames':[{'frame_type':kind,'stream_id':stream,**kw}]}}
        reset=[packet('sent','stop_sending',error_code=268),packet('sent','reset_stream',error_code=268),packet('received','reset_stream',error_code=268)]
        rows={'a':[{'name':'http:settings_received','data':{'settings':{'8':1}}},request(0,'/ws/echo'),head('parsed',0),request(4,'/ws/echo'),head('parsed',4),packet('received','reset_stream',error_code=268),packet('received','stream',0,offset=40,length=4,fin=True)],'b':[request(4,'/ws/echo'),head('parsed',4),*reset,request(12,'/ws/flood'),head('parsed',12),*flow()],'c':[packet('sent','connection_close',error_code=298)]}
        for key,items in rows.items():
            (directory/(key+'.qlog')).write_text(json.dumps({'traces':[{'common_fields':{'ODCID':key},'events':items}]}))
        return r

    def test_complete_artifacts_require_actual_capture_qlogs_keys_and_headers(self):
        for mutation in [None,'capture','qlog','keys','insecure_keys','false_settings','false_headers','wrong_native_connection','missing_reset','wrong_request_protocol','missing_failure_alert']:
            with self.subTest(mutation=mutation),tempfile.TemporaryDirectory() as temp:
                p=Path(temp);r=self.artifact_fixture(p)
                if mutation in ('capture','qlog','keys'):
                    (p/{'capture':'packets.pcap','qlog':'b.qlog','keys':'tls.keys'}[mutation]).unlink()
                if mutation=='insecure_keys':(p/'tls.keys').chmod(0o644)
                if mutation in ('false_settings','false_headers'):
                    q=json.loads((p/'a.qlog').read_text())
                    if mutation=='false_settings':q['traces'][0]['events'][0]['data']['settings']={'8':0}
                    else:q['traces'][0]['events'][2]['data']['frame']['headers'][0]['value']='101'
                    (p/'a.qlog').write_text(json.dumps(q))
                if mutation=='wrong_native_connection':next(c for c in r['cases'] if c['case']=='backpressure')['qlog_id']='a'
                if mutation in ('missing_reset','wrong_request_protocol','missing_failure_alert'):
                    key={'missing_reset':'b','wrong_request_protocol':'a','missing_failure_alert':'c'}[mutation]
                    q=json.loads((p/(key+'.qlog')).read_text());rows=q['traces'][0]['events']
                    if mutation=='missing_reset':rows[:]=[e for e in rows if not any(f.get('frame_type') in ('stop_sending','reset_stream') for f in e.get('data',{}).get('frames',[]))]
                    if mutation=='missing_failure_alert':rows.clear()
                    if mutation=='wrong_request_protocol':
                        next(f for f in rows[1]['data']['frame']['headers'] if f['name']==':protocol')['value']='http'
                    (p/(key+'.qlog')).write_text(json.dumps(q))
                if mutation is None:self.g.validate_artifacts(p,r)
                else:
                    with self.assertRaises((self.g.GateFailure,OSError)):self.g.validate_artifacts(p,r)

    def test_existing_evidence_preserved(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp);manifest=p/'manifest.json';manifest.write_text('preserve')
            result=subprocess.run([sys.executable,str(ROOT/'scripts/run-v3-http3-websocket-gates.py'),'--build-dir',str(p/'missing'),'--log-dir',str(p),'--python',sys.executable],capture_output=True,text=True,timeout=5)
            self.assertEqual(result.returncode,1);self.assertEqual(manifest.read_text(),'preserve')


if __name__=='__main__':unittest.main()

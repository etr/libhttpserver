#!/usr/bin/env python3
"""Fail-closed receipt tests: these do not replace the independent network gate."""
import copy
import inspect
import hashlib
import importlib.util
from pathlib import Path
import unittest
import struct
import queue
import time
import tempfile
import sys
import subprocess
from unittest.mock import patch
ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('gate', ROOT / 'scripts/run-v3-http3-gates.py')
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)


class Receipts(unittest.TestCase):
    def good(self):
        return {'client': 'aioquic', 'version': '1.3.0', 'cases': [
            {'case': 'handshake', 'alpn': 'h3', 'quic_version': 1, 'tls_version': 'TLSv1.3', 'verified': True, 'early_data': False},
            {'case': 'get', 'status': 200, 'body_hex': b'http3 fixture'.hex(), 'stream': 0},
            {'case': 'post', 'status': 200, 'body_hex': gate.UPLOAD.hex(), 'stream': 4},
            {'case': 'concurrency', 'held_stream': 8, 'health_stream': 12, 'health_status': 204, 'hold_status': 200, 'health_before_release': True, 'connection': 1},
            {'case': 'cancellation', 'stream': 16, 'sibling_stream': 20, 'sibling_connection': 1, 'type': 'StreamReset', 'code': 268, 'sibling_status': 200, 'server_cancelled': True},
            {'case': 'tls_failure', 'type': 'ConnectionTerminated', 'code': 298, 'response': False},
            {'case': 'missing', 'status': 404} ]}

    def test_complete_receipt_is_accepted(self):
        gate.validate_receipt(self.good(), 'aioquic')

    def test_missing_duplicate_wrong_body_and_protocol_are_rejected(self):
        original = self.good()
        variants = [{}, {**original, 'cases': []}, {**original, 'client': 'quic-go'}, {**original, 'version': '0.0'}]
        for field,value in [('alpn','h2'), ('quic_version',2), ('verified',False), ('early_data',True)]:
            r=copy.deepcopy(original);r['cases'][0][field]=value;variants.append(r)
        for index,field,value in [(1,'body_hex','00'),(2,'body_hex',''),(3,'health_before_release',False),(3,'held_stream',12),(4,'server_cancelled',False),(5,'response',True),(6,'status',200)]:
            r=copy.deepcopy(original);r['cases'][index][field]=value;variants.append(r)
        variants.append({**original,'cases': original['cases']+[original['cases'][0]]})
        for receipt in variants:
            with self.subTest(receipt=receipt), self.assertRaises(gate.GateFailure):
                gate.validate_receipt(receipt,'aioquic')

    def test_readiness_is_exact_and_port_bounded(self):
        self.assertEqual(gate.readiness('READY 40000\n'),40000)
        for line in ['', 'ready 44\n', 'READY 0\n', 'READY 65536\n', 'READY 44 trailing\n']:
            with self.assertRaises(gate.GateFailure): gate.readiness(line)

    def test_capture_rejects_truncated_header_record_and_non_udp(self):
        header = struct.pack('<IHHIIII', 0xa1b2c3d4, 2, 4, 0, 0, 65535, 101)
        for data in [b'', header, header + b'bad', header + struct.pack('<IIII', 1, 0, 40, 40) + bytes(40)]:
            with tempfile.TemporaryDirectory() as directory:
                path=Path(directory)/'packets.pcap';path.write_bytes(data)
                with self.assertRaises(gate.GateFailure):gate.capture_counts(path, 443)

    def test_deadline_and_process_retirement(self):
        with self.assertRaises(gate.GateFailure):gate.next_message(queue.Queue(),time.monotonic()-1)
        with tempfile.TemporaryDirectory() as directory:
            messages=queue.Queue()
            process=gate.Process([sys.executable,'-c','import sys; print("READY invalid",flush=True);sys.stdin.readline()'],Path(directory),'probe',messages)
            try:
                _,line=gate.next_message(messages,time.monotonic()+3)
                with self.assertRaises(gate.GateFailure):gate.readiness(line)
            finally:process.stop('quit')
            self.assertIsNotNone(process.process.poll())
            self.assertFalse(any(thread.is_alive() for thread in process.threads))

    def test_primary_failure_survives_fixture_teardown_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            path=Path(temporary);fixture=path/'fixture'
            fixture.write_text('#!/bin/sh\nprintf "READY invalid\\n"\nexit 3\n');fixture.chmod(0o700)
            with self.assertRaisesRegex(gate.GateFailure,'invalid fixture readiness'):
                gate.run_client('aioquic',fixture,[],path,path/'evidence')
            self.assertTrue((path/'evidence/exits.json').is_file())

    def test_failed_preflight_preserves_existing_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory);manifest=path/'manifest.json';manifest.write_text('retained evidence')
            result=subprocess.run([sys.executable,str(ROOT/'scripts/run-v3-http3-gates.py'),'--build-dir',str(path/'missing'),'--log-dir',str(path),'--python',sys.executable,'--go-client',str(path/'missing-client')],capture_output=True,text=True,timeout=5)
            self.assertEqual(result.returncode,1)
            self.assertEqual(manifest.read_text(),'retained evidence')


class SemanticReceipts(unittest.TestCase):
    def semantic(self):
        r = Receipts().good()
        r['matrix'] = 'task174'
        r['cases'] += [
            {'case': 'streaming', 'stream': 24, 'connection': 1, 'prefix_bytes': 6, 'before_fin': True, 'headers': [['x-request-trailer','yes']], 'status': 200, 'body_hex': gate.UPLOAD.hex(), 'trailers': [['x-reply','done']], 'fin_count': 1},
            {'case': 'slow_producer', 'stream': 28, 'connection': 1, 'prefix_bytes': 6, 'before_fin': True, 'sibling_stream': 32, 'sibling_connection': 1, 'sibling_status': 204, 'headers': [['x-request-trailer','yes']], 'status': 200, 'body_hex': gate.UPLOAD.hex(), 'trailers': [['x-reply','done']], 'fin_count': 1},
            {'case': 'slow_consumer', 'stream': 36, 'connection': 1, 'sibling_stream': 40, 'sibling_connection': 1, 'sibling_status': 204, 'credit': 4096, 'offset': 4096, 'withheld': True, 'started': 100, 'completed': 99, 'resumed': True, 'status': 200, 'body_bytes': 524288, 'body_sha256': hashlib.sha256(bytes(range(256))*2048).hexdigest(), 'trailers': [['x-reply','done']], 'fin_count': 1},
            {'case': 'goaway', 'connection': 2, 'accepted_stream': 0, 'late_stream': 4, 'ids': [(1<<62)-4,4], 'rejection': 267, 'accepted_status': 200, 'close_code': 256, 'drain_complete': True},
        ]
        return r

    def test_semantic_mode_rejects_a_smoke_receipt(self):
        self.assertIn('matrix', inspect.signature(gate.validate_receipt).parameters)
        with self.assertRaises(gate.GateFailure):
            gate.validate_receipt(Receipts().good(), 'aioquic', 'task174')

    def test_semantic_missing_stages_are_rejected(self):
        self.assertIn('matrix', inspect.signature(gate.validate_receipt).parameters)
        original=self.semantic()
        for case in ['streaming','slow_producer','slow_consumer','goaway']:
            r=copy.deepcopy(original);r['cases']=[c for c in r['cases'] if c['case']!=case]
            with self.subTest(case=case), self.assertRaises(gate.GateFailure):
                gate.validate_receipt(r,'aioquic','task174')

    def test_valid_semantic_receipt_is_accepted(self):
        gate.validate_receipt(self.semantic(),'aioquic','task174')

    def test_false_streaming_backpressure_and_goaway_claims_fail_closed(self):
        variants = {
            'cancellation': [('sibling_connection',2),('sibling_stream',16)],
            'streaming': [('before_fin',False),('prefix_bytes',0),('body_hex','00'),('trailers',[]),('fin_count',2),('stream',0),('headers',[])],
            'slow_producer': [('sibling_connection',2),('before_fin',False),('sibling_stream',28),('sibling_status',500)],
            'slow_consumer': [('withheld',False),('resumed',False),('started',99),('offset',4000),('body_sha256','wrong'),('body_bytes',0)],
            'goaway': [('ids',[4]),('ids',[4,(1<<62)-4]),('ids',[(1<<62)-4,8]),('rejection',268),('close_code',0),('drain_complete',False),('accepted_status',500)]}
        for name,changes in variants.items():
            for field,value in changes:
                r=self.semantic();next(c for c in r['cases'] if c['case']==name)[field]=value
                with self.subTest(case=name,field=field),self.assertRaises(gate.GateFailure):
                    gate.validate_receipt(r,'aioquic','task174')

class ParityContract(unittest.TestCase):
    def observation(self, protocol):
        return {'protocol':protocol,'status':200,'body_hex':gate.UPLOAD.hex(),'headers':[['x-method','POST'],['x-target','/items/../semantic?x=1'],['x-route','/semantic'],['x-host','localhost'],['x-repeat','a'],['x-repeat','b']],'trailers':[['x-reply','done']],'missing_status':404}

    def test_actual_h1_h2_results_are_required(self):
        self.assertTrue(hasattr(gate,'validate_parity'), 'semantic parity validation is missing')
        rows=[self.observation('h1'),self.observation('h2')]
        gate.validate_parity(rows)
        for value in [[], rows[:1], rows+rows[:1], [{**rows[0],'protocol':[]},rows[1]], [{**rows[0],'status':500},rows[1]], [{**rows[0],'headers':list(reversed(rows[0]['headers']))},rows[1]]]:
            with self.assertRaises(gate.GateFailure):gate.validate_parity(value)

    def test_received_goaway_payload_is_decoded_and_not_inferred_from_server_logs(self):
        self.assertTrue(hasattr(gate,'decode_goaway'), 'received-wire GOAWAY decoder is missing')
        self.assertEqual(gate.decode_goaway('0x7\tfffffffffffffffc\n0x7\t04\n'),[(1<<62)-4,4])
        for value in ['', '0x7\tff\n', '0x7\t0004\n','0x7\t04\n0x7\t08\n']:
            with self.assertRaises(gate.GateFailure):gate.decode_goaway(value)

class FlowWitness(unittest.TestCase):
    def test_credit_exhaustion_sibling_and_resume_need_native_frame_order(self):
        self.assertTrue(hasattr(gate,'flow_witness'), 'native flow-control validation is missing')
        events=[
            {'name':'transport:packet_received','data':{'frames':[{'frame_type':'stream','stream_id':36,'offset':0,'length':4096}]}},
            {'name':'transport:packet_received','data':{'frames':[{'frame_type':'stream','stream_id':40,'offset':0,'length':3,'fin':True}]}},
            {'name':'transport:packet_sent','data':{'frames':[{'frame_type':'max_stream_data','stream_id':36,'maximum':8192}]}},
            {'name':'transport:packet_received','data':{'frames':[{'frame_type':'stream','stream_id':36,'offset':4096,'length':524288,'fin':True}]}}]
        gate.flow_witness(events,36,40)
        for variant in [events[:2],events[1:], [events[0],events[2],events[1],events[3]],events[:-1]]:
            with self.assertRaises(gate.GateFailure):gate.flow_witness(variant,36,40)

class GoawayWitness(unittest.TestCase):
    first = '0x7\tfffffffffffffffc\n'
    both = first + '0x7\t04\n'

    def decoder(self, directory):
        """External decoder stand-in: decode the snapshot, then grow live capture."""
        executable = directory/'decoder'
        executable.write_text('#!' + sys.executable + '\n'
            'import pathlib, shutil, sys\n'
            'snapshot = pathlib.Path(sys.argv[sys.argv.index("-r") + 1])\n'
            'print(snapshot.read_text(), end="")\n'
            'next_capture = snapshot.parent/"next-capture"\n'
            'if next_capture.exists():\n'
            '    shutil.copyfile(next_capture, snapshot.parent/"packets.pcap")\n'
            '    next_capture.unlink()\n'
            'status = snapshot.parent/"decoder-status"\n'
            'sys.exit(int(status.read_text()) if status.exists() else 0)\n')
        executable.chmod(0o700)
        return executable

    def test_growing_capture_waits_for_both_actual_received_stages(self):
        for initial in ['', self.first]:
            with self.subTest(initial=initial), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                decoder = self.decoder(directory)
                (directory/'packets.pcap').write_text(initial)
                (directory/'next-capture').write_text(self.both)

                result = gate.goaway_witness(decoder, directory, 443)

                self.assertEqual(result, {'ids': [(1<<62)-4, 4]})
                self.assertEqual((directory/'goaway-decoded.tsv').read_text(), self.both)
                self.assertEqual((directory/'goaway.pcap').read_text(), self.both)

    def test_absent_second_stage_fails_at_fixed_deadline(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            decoder = self.decoder(directory)
            (directory/'packets.pcap').write_text(self.first)
            now = [0.0]
            def advance(seconds): now[0] += seconds

            with patch.object(gate.time, 'monotonic', lambda: now[0]), patch.object(gate.time, 'sleep', advance):
                with self.assertRaisesRegex(gate.GateFailure, 'GOAWAY witness deadline exceeded'):
                    gate.goaway_witness(decoder, directory, 443, deadline=0.1)

            self.assertEqual(now[0], 0.1)
            self.assertEqual((directory/'goaway-decoded.tsv').read_text(), self.first)

    def test_invalid_wire_evidence_fails_without_refreshing_snapshot(self):
        for text in ['bad-row\n', 'bad-kind\t04\n', '0x7\tff\n',
                     '0x7\t04\n', self.first+'0x7\t08\n',
                     self.both+'0x7\t08\n']:
            with self.subTest(text=text), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                decoder = self.decoder(directory)
                (directory/'packets.pcap').write_text(text)
                (directory/'next-capture').write_text(self.both)

                with self.assertRaises(gate.GateFailure):
                    gate.goaway_witness(decoder, directory, 443)

                self.assertEqual((directory/'goaway.pcap').read_text(), text)

    def test_decoder_failure_is_not_incomplete_wire_evidence(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            decoder = self.decoder(directory)
            (directory/'packets.pcap').write_text(self.first)
            (directory/'next-capture').write_text(self.both)
            (directory/'decoder-status').write_text('2')

            with self.assertRaisesRegex(gate.GateFailure, 'GOAWAY decoder failed'):
                gate.goaway_witness(decoder, directory, 443)

            self.assertEqual((directory/'goaway.pcap').read_text(), self.first)

    def test_decoder_timeout_is_clipped_to_remaining_witness_budget(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory/'packets.pcap').write_text(self.first)
            expired = subprocess.TimeoutExpired(['decoder'], 1)

            with patch.object(gate.time, 'monotonic', return_value=0), patch.object(gate.subprocess, 'run', side_effect=expired) as decoder:
                with self.assertRaises(subprocess.TimeoutExpired):
                    gate.goaway_witness(Path('decoder'), directory, 443, deadline=1)

            self.assertEqual(decoder.call_args.kwargs['timeout'], 1)

    def test_complete_evidence_after_deadline_cannot_acknowledge_client(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory/'packets.pcap').write_text(self.both)
            now = [0.0]
            def late_decode(command, **kwargs):
                now[0] = 1.01
                return subprocess.CompletedProcess(command, 0, self.both, '')

            with patch.object(gate.time, 'monotonic', lambda: now[0]), patch.object(gate.subprocess, 'run', late_decode):
                with self.assertRaisesRegex(gate.GateFailure, 'GOAWAY witness deadline exceeded'):
                    gate.goaway_witness(Path('decoder'), directory, 443, deadline=1)

    def test_missing_capture_fails_before_decoder_invocation(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            decoder = self.decoder(directory)

            with self.assertRaises(FileNotFoundError):
                gate.goaway_witness(decoder, directory, 443)

            self.assertFalse((directory/'goaway-decoded.tsv').exists())

if __name__ == '__main__': unittest.main()

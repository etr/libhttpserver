#!/usr/bin/env python3
"""Fail-closed receipt tests: these do not replace the independent network gate."""
import copy
import importlib.util
from pathlib import Path
import unittest
import struct
import queue
import time
import tempfile
import sys
import subprocess
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
            {'case': 'cancellation', 'stream': 16, 'type': 'StreamReset', 'code': 268, 'sibling_status': 200, 'server_cancelled': True},
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

    def test_failed_preflight_preserves_existing_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory);manifest=path/'manifest.json';manifest.write_text('retained evidence')
            result=subprocess.run([sys.executable,str(ROOT/'scripts/run-v3-http3-gates.py'),'--build-dir',str(path/'missing'),'--log-dir',str(path),'--python',sys.executable,'--go-client',str(path/'missing-client')],capture_output=True,text=True,timeout=5)
            self.assertEqual(result.returncode,1)
            self.assertEqual(manifest.read_text(),'retained evidence')


if __name__ == '__main__': unittest.main()

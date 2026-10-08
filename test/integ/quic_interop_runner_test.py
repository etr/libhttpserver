#!/usr/bin/env python3
"""Task-local runner and endpoint contracts; process fakes cover orchestration only."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import struct
import socket
import time
import unittest
from unittest import mock
ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("interop", ROOT / "scripts/run-v3-quic-interop.py")
interop = importlib.util.module_from_spec(spec)
spec.loader.exec_module(interop)

class RunnerTests(unittest.TestCase):
    def receipt(self):
        return {"servers": ["libhttpserver"], "clients": ["quic-go"], "results": [[{"name": name, "result": "succeeded"} for name in ("handshake", "transfer")]]}

    def test_failed_missing_duplicate_and_unsupported_cells_fail_closed(self):
        self.assertTrue(interop.validate_results(self.receipt()))
        for result in ("failed", "unsupported", None):
            receipt = self.receipt()
            receipt["results"][0][1]["result"] = result
            self.assertFalse(interop.validate_results(receipt))
        for cells in ([], self.receipt()["results"][0][:1], self.receipt()["results"][0] * 2):
            self.assertFalse(interop.validate_results({"results": [cells]}))

    def test_wrong_matrix_identity_is_rejected(self):
        receipt = self.receipt()
        receipt["servers"] = ["external-server"]
        self.assertFalse(interop.validate_results(receipt))

    def test_unsupported_role_and_case_return_127(self):
        for role, case in (("client", "handshake"), ("server", "http3"), ("server", "retry")):
            process = subprocess.run([ROOT / "test/interop/quic/run_endpoint.sh"], env={**os.environ, "ROLE": role, "TESTCASE": case}, capture_output=True)
            self.assertEqual(process.returncode, 127)

    def test_missing_test_mode_fails_before_secret_or_readiness(self):
        with tempfile.TemporaryDirectory() as temp:
            keys = Path(temp) / "keys.log"
            env = {**os.environ, "ROLE": "server", "TESTCASE": "handshake", "SSLKEYLOGFILE": str(keys), "LIBHTTPSERVER_INTEROP_TEST_MODE": "0"}
            process = subprocess.run([ROOT / "test/interop/quic/run_endpoint.sh"], env=env, capture_output=True)
            self.assertEqual(process.returncode, 1)
            self.assertNotIn(b"READY", process.stdout)
            self.assertFalse(keys.exists())

    def test_empty_malformed_and_truncated_captures_are_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "packets.pcap"
            header = struct.pack("<IHHIIII", 0xa1b2c3d4, 2, 4, 0, 0, 65535, 101)
            for data in (b"", b"bad", header, header + b"incomplete", header + struct.pack("<IIII", 0, 0, 30, 30) + b"short"):
                path.write_bytes(data)
                with self.assertRaises(ValueError):
                    interop.capture_records(path)
            path.write_bytes(header + struct.pack("<IIII", 0, 0, 28, 28) + b"x" * 28)
            self.assertEqual(interop.capture_records(path), 1)

    def test_kernel_pcapng_records_are_parsed_and_truncation_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "kernel.pcap"
            section = struct.pack("<IIIHHqI", 0x0a0d0d0a, 28, 0x1a2b3c4d, 1, 0, -1, 28)
            interface = struct.pack("<IIHHII", 1, 20, 1, 0, 65535, 20)
            packet = struct.pack("<IIIIIII", 6, 60, 0, 0, 0, 28, 28) + bytes(28) + struct.pack("<I", 60)
            path.write_bytes(section + interface + packet)
            self.assertEqual(interop.capture_records(path), 1)
            path.write_bytes(section + interface + packet[:-1])
            with self.assertRaises(ValueError):
                interop.capture_records(path)

    def test_optional_compatibility_is_scoped_to_runner_network_options(self):
        compose = "com.docker.network.bridge.enable_ip_masquerade: 'false'\n"
        self.assertNotIn("nat-unprotected", interop.compose_config(compose))
        compatible = interop.compose_config(compose, True)
        self.assertIn("com.docker.network.bridge.gateway_mode_ipv4: 'nat-unprotected'", compatible)
        self.assertIn("com.docker.network.bridge.gateway_mode_ipv6: 'nat-unprotected'", compatible)

    def test_missing_artifacts_never_pass(self):
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaises((ValueError, FileNotFoundError)):
                interop.validate_artifacts(temp)

    def test_existing_evidence_is_preserved_before_missing_tools(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "keep"
            path.write_text("original evidence")
            with self.assertRaises(ValueError):
                interop.preflight("/missing/runner", temp)
            self.assertEqual(path.read_text(), "original evidence")
            with self.assertRaises(ValueError):
                interop.preflight("/missing/runner", Path(temp) / "fresh")

    def test_run_deadline_reaps_the_real_process(self):
        with tempfile.TemporaryDirectory() as temp:
            started = time.monotonic()
            with self.assertRaises(RuntimeError):
                interop.run_bounded(["python3", "-c", "import time; time.sleep(60)"], temp, Path(temp) / "stdout", Path(temp) / "stderr", 0.1)
            self.assertLess(time.monotonic() - started, 12)

    def test_driver_preserves_failed_upstream_receipt(self):
        with tempfile.TemporaryDirectory() as temp:
            runner = Path(temp) / "runner"
            runner.mkdir()
            (runner / "run.py").write_text("import json,sys\nfrom pathlib import Path\nPath(sys.argv[sys.argv.index('-j')+1]).write_text(json.dumps({'results':[[]]}))\n")
            (runner / "implementations_quic.json").write_text("{}")
            (runner / "docker-compose.yml").write_text("image: martenseemann/quic-network-simulator\n")
            output = Path(temp) / "evidence"
            def identity(command, **kwargs):
                return '[{"Id":"sha256:test-fixture"}]' if command[0] == "docker" else "source-head"
            with mock.patch.object(interop, "preflight"), mock.patch.object(interop.subprocess, "check_output", side_effect=identity), mock.patch.object(interop.subprocess, "run"):
                with self.assertRaises(ValueError):
                    interop.main(["--runner-dir", str(runner), "--output", str(output), "--server-image", "test-endpoint"])
            self.assertEqual(json.loads((output / "results.json").read_text()), {"results": [[]]})
            manifest = json.loads((output / "manifest.json").read_text())
            self.assertFalse(manifest["passed"])
            self.assertTrue((output / "stdout.log").exists())

class NativeEndpointTests(unittest.TestCase):
    def endpoint(self):
        path = os.environ.get("QUIC_INTEROP_ENDPOINT")
        if not path:
            self.skipTest("set QUIC_INTEROP_ENDPOINT to the built test-only executable")
        return Path(path)

    def test_disabled_diagnostics_and_missing_inputs_fail_before_ready(self):
        endpoint = self.endpoint()
        with tempfile.TemporaryDirectory() as temp:
            keys = Path(temp) / "keys.log"
            for mode in ("0", "1"):
                env = {**os.environ, "LIBHTTPSERVER_INTEROP_TEST_MODE": mode, "LIBHTTPSERVER_INTEROP_CERT": "/missing/cert", "LIBHTTPSERVER_INTEROP_LOGS": temp, "SSLKEYLOGFILE": str(keys)}
                process = subprocess.run([endpoint], env=env, capture_output=True, timeout=5)
                self.assertEqual(process.returncode, 1)
                self.assertNotIn(b"READY", process.stdout)
                self.assertFalse(keys.exists())
                self.assertFalse((Path(temp) / "packets.pcap").exists())

    def test_simulator_wait_probe_receives_version_negotiation(self):
        endpoint = self.endpoint()
        with tempfile.TemporaryDirectory() as temp:
            env = {**os.environ, "LIBHTTPSERVER_INTEROP_TEST_MODE": "1", "LIBHTTPSERVER_INTEROP_CERT": str(ROOT / "test/data/tls_credentials/a.pem"), "LIBHTTPSERVER_INTEROP_KEY": str(ROOT / "test/data/tls_credentials/a-key.pem"), "LIBHTTPSERVER_INTEROP_WWW": temp, "LIBHTTPSERVER_INTEROP_LOGS": temp, "LIBHTTPSERVER_INTEROP_PORT": "0", "LIBHTTPSERVER_INTEROP_BIND": "127.0.0.1"}
            process = subprocess.Popen([endpoint], env=env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                port = int(process.stdout.readline().split()[1])
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
                    udp.settimeout(0.5)
                    udp.sendto(b"\xc0WAIT\x00\x00" + bytes(1200), ("127.0.0.1", port))
                    reply, _ = udp.recvfrom(1200)
                    self.assertEqual(reply[1:5], bytes(4))
                    self.assertEqual(reply[-4:], b"\x00\x00\x00\x01")
            finally:
                process.terminate()
                process.communicate(timeout=5)

    def test_runner_ec_parameter_prefix_is_accepted(self):
        endpoint = self.endpoint()
        with tempfile.TemporaryDirectory() as temp:
            key, cert = Path(temp) / "priv.key", Path(temp) / "cert.pem"
            subprocess.run(["openssl", "ecparam", "-name", "prime256v1", "-genkey", "-out", str(key)], check=True, capture_output=True)
            subprocess.run(["openssl", "req", "-x509", "-new", "-key", str(key), "-subj", "/CN=server", "-out", str(cert)], check=True, capture_output=True)
            env = {**os.environ, "LIBHTTPSERVER_INTEROP_TEST_MODE": "1", "LIBHTTPSERVER_INTEROP_CERT": str(cert), "LIBHTTPSERVER_INTEROP_KEY": str(key), "LIBHTTPSERVER_INTEROP_WWW": temp, "LIBHTTPSERVER_INTEROP_LOGS": temp, "LIBHTTPSERVER_INTEROP_PORT": "0", "LIBHTTPSERVER_INTEROP_BIND": "127.0.0.1"}
            process = subprocess.Popen([endpoint], env=env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                self.assertTrue(process.stdout.readline().startswith(b"READY "))
            finally:
                process.terminate()
                process.communicate(timeout=5)

    def test_stdin_closed_server_lives_until_signal(self):
        endpoint = self.endpoint()
        with tempfile.TemporaryDirectory() as temp:
            env = {**os.environ, "LIBHTTPSERVER_INTEROP_TEST_MODE": "1", "LIBHTTPSERVER_INTEROP_CERT": str(ROOT / "test/data/tls_credentials/a.pem"), "LIBHTTPSERVER_INTEROP_KEY": str(ROOT / "test/data/tls_credentials/a-key.pem"), "LIBHTTPSERVER_INTEROP_WWW": temp, "LIBHTTPSERVER_INTEROP_LOGS": temp, "LIBHTTPSERVER_INTEROP_PORT": "0", "LIBHTTPSERVER_INTEROP_BIND": "127.0.0.1"}
            process = subprocess.Popen([endpoint], env=env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                self.assertTrue(process.stdout.readline().startswith(b"READY "))
                time.sleep(0.1)
                self.assertIsNone(process.poll())
                process.terminate()
                _, stderr = process.communicate(timeout=5)
                self.assertEqual(process.returncode, 0, stderr)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.communicate()

if __name__ == "__main__":
    unittest.main()

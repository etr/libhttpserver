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

    def test_client_starts_only_after_actual_simulator_control_listener(self):
        source = "services:\n  sim:\n    hostname: sim\n  server:\n    depends_on:\n      - sim\n  client:\n    depends_on:\n      - sim\n"
        configured = interop.compose_config(source)
        self.assertIn("condition: service_healthy", configured.split("  client:")[1])
        self.assertIn("/proc/net/tcp", configured.split("  server:")[0])
        self.assertIn("interval: 100ms", configured)
        self.assertIn("retries: 100", configured)
        self.assertEqual(configured.split("  server:")[1].split("  client:")[0], source.split("  server:")[1].split("  client:")[0])

    def test_clock_compatibility_uses_explicit_immutable_image_only_for_simulator(self):
        source = "services:\n  sim:\n    image: martenseemann/quic-network-simulator\n  client:\n    image: original-client\n"
        patched = "sha256:" + "1" * 64
        actual = interop.compose_config(source, simulator_image=patched)
        self.assertIn("image: " + patched, actual.split("  client:")[0])
        self.assertEqual(actual.split("  client:")[1], source.split("  client:")[1])
        self.assertIn(interop.SIMULATOR, interop.compose_config(source))

    def test_clock_receipt_rejects_wrong_pin_and_changed_artifact(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "receipt.json"
            receipt = {"status": "passed", "upstream_image": "unmatched", "clock": "steady_clock", "artifacts": []}
            path.write_text(json.dumps(receipt))
            with self.assertRaises(ValueError):
                interop.simulator_clock_configuration(path)
            receipt["upstream_image"] = interop.SIMULATOR
            artifact = Path(temp) / "patched-core.so"
            artifact.write_bytes(b"changed")
            receipt["artifacts"] = [{"path": str(artifact), "sha256": "0" * 64}]
            path.write_text(json.dumps(receipt))
            with self.assertRaises(ValueError):
                interop.simulator_clock_configuration(path)

    def test_retained_clock_receipt_revalidates_selected_path_after_worktree_cleanup(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "retained-receipt.json"
            core = Path(temp) / "core.so"
            core.write_bytes(b"retained simulator core")
            checksum = interop.hashlib.sha256(core.read_bytes()).hexdigest()
            receipt = {"status": "passed", "upstream_image": interop.SIMULATOR,
                       "clock": "steady_clock", "receipt_path": "/removed/task196/receipt.json",
                       "image_id": "sha256:" + "1" * 64, "core_binary_sha256": checksum,
                       "artifacts": [{"path": str(core), "sha256": checksum}]}
            path.write_text(json.dumps(receipt))
            with mock.patch.object(interop.subprocess, "check_output",
                                   return_value=checksum + "  /ns3/out/lib/libns3.46.1-core.so\n"):
                selected = interop.simulator_clock_configuration(path)
                self.assertEqual(selected["receipt_path"], str(path.resolve()))
                self.assertEqual(interop.simulator_clock_configuration(selected["receipt_path"]), selected)
                self.assertEqual(json.loads(path.read_text()), receipt)
                core.write_bytes(b"modified simulator core")
                with self.assertRaisesRegex(ValueError, "artifact changed"):
                    interop.simulator_clock_configuration(selected["receipt_path"])

    def test_simulator_selects_realtime_before_scenario_initialization(self):
        compose = ("services:\n  sim:\n    environment:\n"
                   "      - SCENARIO=$SCENARIO\n  client:\n    environment:\n"
                   "      - ROLE=client\n  server:\n    environment:\n      - ROLE=server\n")
        setting = "NS_GLOBAL_VALUE=SimulatorImplementationType=ns3::RealtimeSimulatorImpl"
        for compatible in (False, True):
            configured = interop.compose_config(compose, compatible)
            self.assertEqual(configured.count(setting), 1)
            self.assertIn(setting, configured.split("  client:")[0])
            self.assertIn("      - SCENARIO=$SCENARIO\n", configured)
            self.assertEqual(configured.split("  client:")[1], compose.split("  client:")[1])

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
            (runner / "testcases_quic.py").write_text("        unresponded = [c for c in challenges if c not in responses]\n")
            (runner / "docker-compose.yml").write_text("image: martenseemann/quic-network-simulator\n")
            output = Path(temp) / "evidence"
            def identity(command, **kwargs):
                return '[{"Id":"sha256:test-fixture"}]' if command[0] == "docker" else "source-head"
            with mock.patch.object(interop, "preflight"), mock.patch.object(interop.subprocess, "check_output", side_effect=identity), mock.patch.object(interop.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, b"", b"")):
                with self.assertRaises(ValueError):
                    interop.main(["--runner-dir", str(runner), "--output", str(output), "--server-image", "test-endpoint"])
            self.assertEqual(json.loads((output / "results.json").read_text()), {"results": [[]]})
            manifest = json.loads((output / "manifest.json").read_text())
            self.assertFalse(manifest["passed"])
            self.assertTrue((output / "stdout.log").exists())

    def test_task173_copied_observation_imports_without_source_checkout(self):
        with tempfile.TemporaryDirectory() as temp:
            runner = Path(temp) / "runner"
            runner.mkdir()
            (runner / "run.py").write_text("import json,sys\nfrom pathlib import Path\n"
                "sys.path.insert(0, str(Path.cwd()))\n"
                "from task196_observation import validate_terminal_challenges\n"
                "assert callable(validate_terminal_challenges)\n"
                "Path(sys.argv[sys.argv.index('-j')+1]).write_text(json.dumps({'results':[[]]}))\n"
                "print('observation import ready')\n")
            (runner / "implementations_quic.json").write_text("{}")
            (runner / "testcases_quic.py").write_text("        unresponded = [c for c in challenges if c not in responses]\n")
            (runner / "docker-compose.yml").write_text("image: martenseemann/quic-network-simulator\n")
            output = Path(temp) / "evidence"
            def identity(command, **kwargs):
                return '[{"Id":"sha256:test-fixture"}]' if command[0] == "docker" else "source-head"
            original = interop.run_bounded
            def isolated(command, *args):
                return original([command[0], '-I', *command[1:]], *args)
            with mock.patch.object(interop, "preflight"), mock.patch.object(interop.subprocess, "check_output", side_effect=identity), mock.patch.object(interop.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, b"", b"")), mock.patch.object(interop, "run_bounded", side_effect=isolated):
                with self.assertRaises(ValueError):
                    interop.main(["--runner-dir", str(runner), "--output", str(output),
                                  "--server-image", "test-endpoint", "--matrix", "task173"])
            self.assertIn('observation import ready', (output / "stdout.log").read_text(),
                          (output / "stderr.log").read_text())

class Task173MatrixTests(unittest.TestCase):
    cases = ("handshake", "transfer", "transferloss", "rebind-port", "amplificationlimit", "keyupdate")

    def matrix(self):
        self.assertTrue(callable(getattr(interop, "load_matrix", None)), "TASK-173 matrix selection is missing")
        return interop.load_matrix("task173")

    def receipt(self):
        # Upstream exports rows in clients order, then servers order.
        return {"servers": ["libhttpserver"], "clients": ["picoquic", "quic-go"],
                "results": [[{"name": case, "result": "succeeded"} for case in self.cases] for _ in range(2)]}

    def test_complete_two_peer_receipt_is_accepted_in_upstream_order(self):
        self.assertTrue(interop.validate_results(self.receipt(), self.matrix()))

    def test_each_required_cell_must_succeed_for_each_peer(self):
        matrix = self.matrix()
        for row in range(2):
            for column in range(len(self.cases)):
                for result in ("failed", "unsupported", None):
                    receipt = self.receipt()
                    receipt["results"][row][column]["result"] = result
                    self.assertFalse(interop.validate_results(receipt, matrix))
                receipt = self.receipt()
                del receipt["results"][row][column]
                self.assertFalse(interop.validate_results(receipt, matrix))
                receipt = self.receipt()
                receipt["results"][row][column] = receipt["results"][row][(column + 1) % len(self.cases)]
                self.assertFalse(interop.validate_results(receipt, matrix))

    def test_wrong_peer_server_and_malformed_receipts_fail_closed(self):
        matrix = self.matrix()
        for key, values in (("clients", ["quic-go"]), ("clients", ["quic-go", "quic-go"]),
                            ("clients", ["quic-go", "ngtcp2"]), ("servers", ["external-server"])):
            receipt = self.receipt()
            receipt[key] = values
            self.assertFalse(interop.validate_results(receipt, matrix))
        for document in (None, [], {}, {"clients": None}, {**self.receipt(), "results": None},
                         {**self.receipt(), "results": [None, []]}):
            self.assertFalse(interop.validate_results(document, matrix))

    def test_two_aliases_of_one_stack_cannot_satisfy_independence(self):
        matrix = self.matrix()
        matrix["peers"]["picoquic"]["stack"] = "quic-go"
        self.assertFalse(interop.validate_results(self.receipt(), matrix))

    def test_unknown_matrix_name_is_rejected(self):
        with self.assertRaises(ValueError):
            interop.load_matrix("unrecognized")

    def test_task173_pins_independent_stacks_and_all_six_categories(self):
        matrix = self.matrix()
        self.assertEqual(set(matrix["categories"]), {"handshake", "loss", "rebinding", "amplification", "key_update", "closure"})
        self.assertEqual(set(matrix["cases"]), set(self.cases))
        self.assertEqual(len({peer["stack"] for peer in matrix["peers"].values()}), 2)
        for peer in matrix["peers"].values():
            self.assertRegex(peer["image"], r"@sha256:[0-9a-f]{64}$")

    def artifact_tree(self, root):
        capture = struct.pack("<IHHIIII", 0xa1b2c3d4, 2, 4, 0, 0, 65535, 101) + struct.pack("<IIII", 0, 0, 28, 28) + bytes(28)
        for peer in ("picoquic", "quic-go"):
            for case in self.cases:
                cell = Path(root) / ("libhttpserver_" + peer) / case
                (cell / "server").mkdir(parents=True)
                (cell / "sim").mkdir()
                key = cell / "server/keys.log"
                key.write_text("".join(label + " " + "aa" * 32 + " " + "bb" * 32 + "\n" for label in ("CLIENT_TRAFFIC_SECRET_0", "SERVER_TRAFFIC_SECRET_0")))
                key.chmod(0o600)
                (cell / "server/packets.pcap").write_bytes(capture)
                for name in ("trace_node_left.pcap", "trace_node_right.pcap"):
                    (cell / "sim" / name).write_bytes(capture)
                (cell / "output.txt").write_text("READY 443\n")

    def test_artifacts_are_required_for_every_peer_and_case(self):
        matrix = self.matrix()
        with tempfile.TemporaryDirectory() as temp:
            self.artifact_tree(temp)
            self.assertEqual(len(interop.validate_artifacts(temp, matrix)), 36)
            for peer in matrix["peers"]:
                for case in matrix["cases"]:
                    path = Path(temp) / ("libhttpserver_" + peer) / case / "sim/trace_node_right.pcap"
                    original = path.read_bytes()
                    path.unlink()
                    with self.assertRaises((ValueError, FileNotFoundError)):
                        interop.validate_artifacts(temp, matrix)
                    path.write_bytes(original)

    def test_empty_secrets_and_unsafe_permissions_fail(self):
        matrix = self.matrix()
        with tempfile.TemporaryDirectory() as temp:
            self.artifact_tree(temp)
            key = Path(temp) / "libhttpserver_picoquic/handshake/server/keys.log"
            original = key.read_text()
            key.chmod(0o644)
            with self.assertRaises(ValueError):
                interop.validate_artifacts(temp, matrix)
            key.chmod(0o600)
            key.write_text(original.replace("bb" * 32, ""))
            with self.assertRaises(ValueError):
                interop.validate_artifacts(temp, matrix)

    def test_authenticated_normal_close_rejects_errors_and_post_close_streams(self):
        self.assertTrue(callable(getattr(interop, "validate_closure_rows", None)), "closure witness validation missing")
        normal = "1\t193.167.0.100\t0x1d\t\t0\n"
        self.assertEqual(interop.validate_closure_rows(normal)["frame"], 1)
        for rows in ("", "1\t193.167.0.100\t0x1d\t\t42\n",
                     "1\t193.167.100.100\t0x1d\t\t0\n",
                     normal + "2\t193.167.100.100\t0x0a\t\t\n", "malformed"):
            with self.assertRaises(ValueError):
                interop.validate_closure_rows(rows)

    def test_terminal_challenge_requires_authenticated_matching_close_before_arrival(self):
        self.assertTrue(callable(getattr(interop, "validate_terminal_challenge_rows", None)), "terminal path lifetime oracle missing")
        close = "23000\t1\t7644\t193.167.0.100\t0x1d\t\t\t\t0\n"
        challenge = "23001\t1\t25170\t193.167.100.100\t0x1a\tb58f27168672de8a\t\t\t\n"
        result = interop.validate_terminal_challenge_rows(close + challenge, ["b5:8f:27:16:86:72:de:8a"])
        self.assertEqual(result["b58f27168672de8a"], {"connection": 1, "close_frame": 23000, "close_packet_number": 7644, "challenge_frame": 23001})
        live = challenge.replace("23001", "22999") + close
        for rows in (challenge, live, close.replace("\t1\t7644", "\t2\t7644") + challenge,
                     close.replace("\t7644\t", "\t\t") + challenge,
                     close.replace("\t0\n", "\t42\n") + challenge,
                     close.replace("193.167.0.100", "193.167.100.100") + challenge,
                     close + challenge.replace("b58f27168672de8a", "1122334455667788")):
            with self.assertRaises(ValueError):
                interop.validate_terminal_challenge_rows(rows, ["b58f27168672de8a"])

    def test_peer_normal_application_close_code_is_explicit(self):
        import inspect
        self.assertIn("application_error", inspect.signature(interop.validate_closure_rows).parameters)
        rows = "1\t193.167.0.100\t0x1d\t\t256\n"
        self.assertEqual(interop.validate_closure_rows(rows, application_error=256)["error_code"], 256)
        with self.assertRaises(ValueError):
            interop.validate_closure_rows("1\t193.167.0.100\t0x1c\t256\t\n", application_error=256)

    def run_fake_driver(self, temp, outcome="failed", cleanup_error=False):
        # Process fakes prove orchestration only, never interoperability acceptance.
        runner = Path(temp) / "runner"
        runner.mkdir()
        for name, data in (("run.py", ""), ("implementations_quic.json", "{}"),
                           ("docker-compose.yml", "image: martenseemann/quic-network-simulator\n")):
            (runner / name).write_text(data)
        (runner / "testcases_quic.py").write_text("        unresponded = [c for c in challenges if c not in responses]\n")
        output = Path(temp) / "evidence"
        def identity(command, **kwargs):
            return '[{"Id":"sha256:fixture"}]' if command[:3] == ["docker", "image", "inspect"] else "fixture-version"
        def run(command, cwd, stdout, stderr, timeout):
            Path(stdout).write_text("orchestration fixture")
            Path(stderr).write_text("")
            if outcome == "timeout":
                raise RuntimeError("upstream runner deadline exceeded")
            receipt = self.receipt()
            if outcome != "succeeded":
                receipt["results"][1][0]["result"] = "failed"
            (output / "results.json").write_text(json.dumps(receipt))
            return 0
        cleanup = mock.Mock(side_effect=RuntimeError("cleanup failed")) if cleanup_error else mock.Mock(return_value=subprocess.CompletedProcess([], 0, b"", b""))
        with mock.patch.object(interop, "preflight"), mock.patch.object(interop.subprocess, "check_output", side_effect=identity), \
             mock.patch.object(interop, "run_bounded", side_effect=run), mock.patch.object(interop.subprocess, "run", cleanup), \
             mock.patch.object(interop, "validate_artifacts", return_value={}), \
             mock.patch.object(interop, "validate_closure_artifacts", return_value={}) as closure:
            args = ["--runner-dir", str(runner), "--output", str(output), "--server-image", "fixture", "--matrix", "task173"]
            if outcome == "succeeded":
                self.assertEqual(interop.main(args), 0)
                closure.assert_called_once()
                cleanup_env = cleanup.call_args.kwargs.get("env", {})
                for name in ("SERVER", "CLIENT", "SERVER_WWW", "CLIENT_WWW", "SERVER_DOWNLOADS", "CLIENT_DOWNLOADS", "CERTS"):
                    self.assertTrue(cleanup_env.get(name), "cleanup requires Compose interpolation input " + name)
            else:
                with self.assertRaisesRegex((ValueError, RuntimeError), "deadline" if outcome == "timeout" else "required upstream"):
                    interop.main(args)
        return output

    def test_cli_generates_exact_matrix_registry_and_command(self):
        self.assertTrue(callable(getattr(interop, "validate_closure_artifacts", None)), "matrix execution missing")
        with tempfile.TemporaryDirectory() as temp:
            output = self.run_fake_driver(temp, "succeeded")
            manifest = json.loads((output / "manifest.json").read_text())
            registry = json.loads((output / "runner/implementations_quic.json").read_text())
            self.assertEqual(set(registry), {"libhttpserver", "quic-go", "picoquic"})
            command = manifest["command"]
            self.assertEqual(command[command.index("-c") + 1], "quic-go,picoquic")
            self.assertEqual(set(command[command.index("-t") + 1].split(",")), set(self.cases))
            self.assertTrue(manifest["passed"])
            self.assertIn("src/detail/quic_connection_identity.cpp", manifest["provenance"]["source_sha256"])
            self.assertIn("src/detail/quic_send_schedule.cpp", manifest["provenance"]["source_sha256"])
            self.assertEqual(set(manifest["matrix"]["categories"]), set(self.matrix()["categories"]))

    def test_cleanup_failure_preserves_primary_error_and_manifest(self):
        self.assertTrue(callable(getattr(interop, "validate_closure_artifacts", None)), "matrix execution missing")
        with tempfile.TemporaryDirectory() as temp:
            output = self.run_fake_driver(temp, cleanup_error=True)
            manifest = json.loads((output / "manifest.json").read_text())
            self.assertFalse(manifest["passed"])
            self.assertIn("required upstream", manifest["failure"])
            self.assertIn("cleanup failed", manifest["cleanup_failure"])
            self.assertIn("results.json", manifest["artifacts"])

    def test_timeout_retains_logs_provenance_and_primary_failure(self):
        self.assertTrue(callable(getattr(interop, "validate_closure_artifacts", None)), "matrix execution missing")
        with tempfile.TemporaryDirectory() as temp:
            output = self.run_fake_driver(temp, "timeout", cleanup_error=True)
            manifest = json.loads((output / "manifest.json").read_text())
            self.assertIn("deadline", manifest["failure"])
            self.assertIn("stdout.log", manifest["artifacts"])
            self.assertIn("provenance", manifest)

    def test_all_selected_server_case_names_dispatch_without_parameters(self):
        # Verified upstream testname(SERVER) maps the six selected cases to these two names.
        with tempfile.TemporaryDirectory() as temp:
            endpoint = Path(temp) / "stub"
            endpoint.write_text("#!/bin/sh\nexit 42\n")
            endpoint.chmod(0o755)
            for case in ("handshake", "transfer"):
                env = {**os.environ, "ROLE": "server", "TESTCASE": case,
                       "LIBHTTPSERVER_INTEROP_LOCAL_DIRECT": "1", "LIBHTTPSERVER_INTEROP_TEST_MODE": "1",
                       "LIBHTTPSERVER_INTEROP_ENDPOINT": str(endpoint)}
                self.assertEqual(subprocess.run([ROOT / "test/interop/quic/run_endpoint.sh"], env=env).returncode, 42)
                env["SERVER_PARAMS"] = "unrecognized"
                process = subprocess.run([ROOT / "test/interop/quic/run_endpoint.sh"], env=env, capture_output=True)
                self.assertEqual(process.returncode, 1)
                self.assertIn(b"SERVER_PARAMS unsupported", process.stderr)

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

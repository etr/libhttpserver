#!/usr/bin/env python3
"""Bounded driver for a pinned, explicitly provisioned upstream QUIC runner."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import struct
import subprocess
import sys

RUNNER_COMMIT = "740c05a10b61d65e8abd3ad38d60898004d335d9"
CLIENT = "martenseemann/quic-go-interop@sha256:6743eea41cb8d525686eb8f2437e3ac45835740ec87c62728f645cd89e85ab88"
PROVIDER_RELEASE = "3.5.9"
PROVIDER_ARCHIVE_SHA256 = "603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a"
SIMULATOR = "martenseemann/quic-network-simulator@sha256:c23d82a55caffe681b1bdae65d4d30d23e1283141a414a7f02ee56cf15f9c6b9"
SIMULATOR_GLOBAL_VALUES = "SimulatorImplementationType=ns3::RealtimeSimulatorImpl"

def load_matrix(name="task163"):
    if name == "task173":
        path = Path(__file__).resolve().parents[1] / "test/interop/quic/task173-matrix.json"
        return json.loads(path.read_text())
    if name != "task163":
        raise ValueError("unknown interoperability matrix")
    return {"server": "libhttpserver", "peers": {"quic-go": {"image": CLIENT, "url": "https://github.com/quic-go/quic-go", "stack": "quic-go"}},
            "cases": ["handshake", "transfer"]}


def validate_results(document, matrix=None):
    matrix = matrix or load_matrix()
    try:
        if len({peer["stack"] for peer in matrix["peers"].values()}) != len(matrix["peers"]):
            return False
        peers = document["clients"]
        if document["servers"] != [matrix["server"]] or not isinstance(peers, list):
            return False
        if len(peers) != len(matrix["peers"]) or set(peers) != set(matrix["peers"]):
            return False
        rows = document["results"]
        if not isinstance(rows, list) or len(rows) != len(peers):
            return False
        for cells in rows:
            if not isinstance(cells, list) or len(cells) != len(matrix["cases"]):
                return False
            if {cell["name"] for cell in cells} != set(matrix["cases"]):
                return False
            if any(cell["result"] != "succeeded" for cell in cells):
                return False
        return True
    except (KeyError, TypeError, AttributeError):
        return False


def capture_records(path):
    data = Path(path).read_bytes()
    if not 24 <= len(data) <= 64 * 1024 * 1024:
        raise ValueError("capture size")
    if data[:4] == b"\x0a\x0d\x0d\x0a":
        return pcapng_records(data)
    endian = {b"\xd4\xc3\xb2\xa1": "<", b"\xa1\xb2\xc3\xd4": ">", b"\x4d\x3c\xb2\xa1": "<"}.get(data[:4])
    if endian is None or struct.unpack_from(endian + "HH", data, 4) != (2, 4):
        raise ValueError("capture format")
    snaplen, link = struct.unpack_from(endian + "II", data, 16)
    if link not in (1, 101, 113):
        raise ValueError("capture envelope")
    offset, records = 24, 0
    while offset < len(data):
        if offset + 16 > len(data):
            raise ValueError("truncated capture record")
        captured, original = struct.unpack_from(endian + "II", data, offset + 8)
        offset += 16
        if not captured or captured > min(original, snaplen) or offset + captured > len(data):
            raise ValueError("invalid capture record")
        offset += captured
        records += 1
    if not records:
        raise ValueError("empty capture")
    return records


def pcapng_records(data):
    endian = {b"\x4d\x3c\x2b\x1a": "<", b"\x1a\x2b\x3c\x4d": ">"}.get(data[8:12])
    if endian is None:
        raise ValueError("pcapng byte order")
    offset, records, interfaces = 0, 0, []
    while offset < len(data):
        if offset + 12 > len(data):
            raise ValueError("truncated pcapng")
        kind, length = struct.unpack_from(endian + "II", data, offset)
        if length < 12 or length % 4 or offset + length > len(data) or struct.unpack_from(endian + "I", data, offset + length - 4)[0] != length:
            raise ValueError("pcapng block length")
        if kind == 1:
            if length < 20:
                raise ValueError("pcapng interface")
            link = struct.unpack_from(endian + "H", data, offset + 8)[0]
            snaplen = struct.unpack_from(endian + "I", data, offset + 12)[0]
            if link not in (1, 101, 113):
                raise ValueError("pcapng link type")
            interfaces.append(snaplen)
        if kind == 6:
            if length < 32:
                raise ValueError("pcapng packet")
            interface = struct.unpack_from(endian + "I", data, offset + 8)[0]
            captured, original = struct.unpack_from(endian + "II", data, offset + 20)
            if interface >= len(interfaces) or not captured or captured > min(original, interfaces[interface]) or 32 + ((captured + 3) // 4) * 4 > length:
                raise ValueError("pcapng packet size")
            records += 1
        offset += length
    if not records:
        raise ValueError("empty pcapng")
    return records


def validate_artifacts(logs, matrix=None):
    matrix = matrix or load_matrix()
    inventory = {}
    for peer, case in ((peer, case) for peer in matrix["peers"] for case in matrix["cases"]):
        root = Path(logs) / (matrix["server"] + "_" + peer) / case
        key = root / "server/keys.log"
        lines = key.read_text().splitlines()
        labels = set()
        for line in lines:
            fields = line.split()
            if len(fields) != 3 or len(fields[1]) != 64 or len(fields[2]) not in (64, 96):
                raise ValueError("malformed NSS key material")
            bytes.fromhex(fields[1])
            bytes.fromhex(fields[2])
            labels.add(fields[0])
        if not {"SERVER_TRAFFIC_SECRET_0", "CLIENT_TRAFFIC_SECRET_0"} <= labels or key.stat().st_mode & 0o077:
            raise ValueError("NSS key material missing or permissions unsafe")
        captures = [root / "sim" / name for name in ("trace_node_left.pcap", "trace_node_right.pcap")]
        for path in captures + [root / "server/packets.pcap"]:
            inventory[str(path)] = capture_records(path)
        terminal = root / "sim/terminal-challenge-lifetimes.json"
        if terminal.exists():
            proof = json.loads(terminal.read_text())
            actual = validate_terminal_challenges(root / "sim/trace_node_left.pcap", key, proof["unanswered"], root / "sim/trace_node_right.pcap")
            if actual != proof:
                raise ValueError("terminal challenge proof changed")
        output = (root / "output.txt").read_text()
        if "interop endpoint failure:" in output or "READY 443" not in output:
            raise ValueError("endpoint failure or missing readiness")
    return inventory


def validate_closure_rows(text, application_error=0):
    close = None
    for line in text.splitlines():
        fields = line.split("\t")
        if len(fields) != 5:
            raise ValueError("malformed closure witness")
        number, source, kinds, transport, application = fields
        frames = {int(value, 0) for value in kinds.split(",") if value}
        if source == "193.167.0.100" and frames & {0x1c, 0x1d}:
            transport_codes = [int(value, 0) for value in transport.split(",") if value]
            application_codes = [int(value, 0) for value in application.split(",") if value]
            if (0x1c in frames and not transport_codes) or (0x1d in frames and not application_codes):
                raise ValueError("peer closure error code missing")
            if any(transport_codes) or any(code != application_error for code in application_codes):
                raise ValueError("peer closure has an unexpected error code")
            code = application_codes[0] if application_codes else 0
            close = close or {"frame": int(number), "source": source, "error_code": code}
        if close and source == "193.167.100.100" and frames & set(range(8, 16)):
            raise ValueError("server STREAM after authenticated peer closure")
    if close is None:
        raise ValueError("authenticated zero-error peer CONNECTION_CLOSE missing")
    return close


def validate_closure_artifacts(logs, matrix):
    witnesses = {}
    for peer in matrix["peers"]:
        root = Path(logs) / (matrix["server"] + "_" + peer) / matrix["categories"]["closure"]
        command = ["tshark", "-r", str(root / "sim/trace_node_right.pcap"),
                   "-o", "tls.keylog_file:" + str(root / "server/keys.log"),
                   "-d", "udp.port==443,quic", "--disable-protocol", "http3",
                   "-Y", "quic.header_form == 0 && quic.frame_type", "-T", "fields",
                   "-e", "frame.number", "-e", "ip.src", "-e", "quic.frame_type",
                   "-e", "quic.cc.error_code", "-e", "quic.cc.error_code.app"]
        rows = subprocess.check_output(command, text=True, timeout=120)
        (root / "closure-witness.tsv").write_text(rows)
        witnesses[peer] = {"command": command, **validate_closure_rows(rows, matrix["peers"][peer].get("closure_application_error", 0))}
    return witnesses


def validate_terminal_challenge_rows(text, unanswered):
    """Only authenticated client closure before challenge arrival retires it."""
    pending = {value.replace(":", "").lower() for value in unanswered}
    if not pending or any(len(value) != 16 for value in pending):
        raise ValueError("invalid unanswered path challenge")
    closes, before_close, retired = {}, set(), {}
    previous = 0
    for line in text.splitlines():
        fields = line.split("\t")
        if len(fields) != 9:
            raise ValueError("malformed path lifetime witness")
        frame, connection, packet, source, kinds, challenges, responses, transport, application = fields
        number = int(frame)
        if number <= previous or not connection.isdecimal() or not packet.isdecimal():
            raise ValueError("unverifiable authenticated connection/packet ordering")
        previous = number
        connection = int(connection)
        frames = {int(value, 0) for value in kinds.split(",") if value}
        if source == "193.167.0.100" and frames & {0x1c, 0x1d}:
            codes = [int(value, 0) for value in (transport + "," + application).split(",") if value]
            if not codes or any(codes) or (0x1c in frames and not transport) or (0x1d in frames and not application):
                raise ValueError("terminal path witness requires zero-error close")
            closes.setdefault(connection, (number, int(packet)))
        for value in challenges.split(",") if challenges else ():
            value = value.replace(":", "").lower()
            if value not in pending:
                continue
            if source != "193.167.100.100" or 0x1a not in frames:
                raise ValueError("unverifiable challenge source/frame")
            if connection not in closes:
                before_close.add(value)
            else:
                retired[value] = {"connection": connection, "close_frame": closes[connection][0], "close_packet_number": closes[connection][1], "challenge_frame": number}
        if any(value.replace(":", "").lower() in pending for value in responses.split(",") if value):
            raise ValueError("upstream unanswered set contradicts capture response")
    if before_close or set(retired) != pending:
        raise ValueError("live, missing or unmatched unanswered path challenge")
    return retired


def path_lifetime_rows(capture, keylog):
    fields = ["frame.number", "quic.connection.number", "quic.packet_number", "ip.src", "quic.frame_type",
              "quic.path_challenge.data", "quic.path_response.data", "quic.cc.error_code", "quic.cc.error_code.app"]
    command = ["tshark", "-r", str(capture), "-o", "tls.keylog_file:" + str(keylog),
               "-d", "udp.port==443,quic", "--disable-protocol", "http3",
               "-Y", "quic.header_form == 0 && quic.frame_type && !icmp && !icmpv6", "-T", "fields"]
    for name in fields:
        command += ["-e", name]
    return subprocess.check_output(command, text=True, timeout=120)


def validate_terminal_challenges(capture, keylog, unanswered, server_capture):
    retired = validate_terminal_challenge_rows(path_lifetime_rows(capture, keylog), unanswered)
    server_rows = path_lifetime_rows(server_capture, keylog)
    closure_rows = "\n".join("\t".join([f[0], f[3], f[4], f[7], f[8]]) for f in (line.split("\t") for line in server_rows.splitlines()))
    close = validate_closure_rows(closure_rows)
    rows = [line.split("\t") for line in server_rows.splitlines()]
    connections = {row[1] for row in rows}
    server_closes = {int(row[2]) for row in rows if row[3] == "193.167.0.100" and
                     {int(value, 0) for value in row[4].split(",") if value} & {0x1c, 0x1d}}
    if len(connections) != 1 or server_closes != {proof["close_packet_number"] for proof in retired.values()}:
        raise ValueError("terminal client/server close connection or packet mismatch")
    return {"unanswered": unanswered, "retired": retired, "server_close": close}


def runner_observation_patch(text):
    original = "        unresponded = [c for c in challenges if c not in responses]\n"
    if text.count(original) != 1:
        raise ValueError("pinned rebinding oracle changed")
    amendment = original + """        if unresponded:
            from task196_observation import validate_terminal_challenges
            import json
            from pathlib import Path
            try:
                proof = validate_terminal_challenges(
                    self._client_trace()._filename, self._keylog_file(), unresponded,
                    self._server_trace()._filename)
                Path(self._sim_log_dir.name, "terminal-challenge-lifetimes.json").write_text(json.dumps(proof, sort_keys=True))
                unresponded = [c for c in unresponded if c.replace(":", "").lower() not in proof["retired"]]
            except (ValueError, OSError, RuntimeError) as error:
                logging.info("Unanswered live/unverifiable PATH_CHALLENGE: %s", error)
"""
    return text.replace(original, amendment)


def run_bounded(command, cwd, stdout, stderr, timeout):
    with Path(stdout).open("wb") as out, Path(stderr).open("wb") as err:
        process = subprocess.Popen(command, cwd=cwd, stdout=out, stderr=err, start_new_session=True)
        try:
            return process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=10)
            raise RuntimeError("upstream runner deadline exceeded")


def preflight(runner, output):
    if Path(output).exists():
        raise ValueError("existing evidence directory preserved")
    for name in ("run.py", "implementations_quic.json", "docker-compose.yml"):
        if not (Path(runner) / name).is_file():
            raise ValueError("upstream runner missing " + name)
    revision = subprocess.check_output(["git", "-C", str(runner), "rev-parse", "HEAD"], text=True).strip()
    if revision != RUNNER_COMMIT:
        raise ValueError("runner revision must be " + RUNNER_COMMIT)
    if subprocess.check_output(["git", "-C", str(runner), "status", "--porcelain"], text=True).strip():
        raise ValueError("upstream runner checkout must be clean")
    for command in (["docker", "info"], ["docker", "compose", "version"], ["tshark", "--version"]):
        subprocess.run(command, check=True, capture_output=True, timeout=30)


def simulator_clock_configuration(path):
    document = json.loads(Path(path).read_text())
    if (document.get("status") != "passed" or document.get("upstream_image") != SIMULATOR
            or document.get("clock") != "steady_clock" or not document.get("artifacts")):
        raise ValueError("invalid pinned simulator clock compatibility receipt")
    for record in document["artifacts"]:
        if hashlib.sha256(Path(record["path"]).read_bytes()).hexdigest() != record["sha256"]:
            raise ValueError("simulator clock compatibility artifact changed")
    image = document["image_id"]
    if not image.startswith("sha256:") or len(image) != 71:
        raise ValueError("simulator compatibility image must be immutable")
    core = subprocess.check_output(["docker", "run", "--rm", "--network", "none", "--entrypoint", "sha256sum",
                                    image, "/ns3/out/lib/libns3.46.1-core.so"], text=True, timeout=30)
    if core.split()[0] != document["core_binary_sha256"]:
        raise ValueError("simulator clock compatibility binary changed")
    return document


def compose_config(text, direct_routing_compat=False, simulator_image=SIMULATOR):
    text = text.replace("image: martenseemann/quic-network-simulator\n", "image: " + simulator_image + "\n")
    # Select real time during ns-3 global initialization, before a scenario can
    # instantiate the default discrete-event simulator and finish immediately.
    text = text.replace("      - SCENARIO=$SCENARIO\n", "      - SCENARIO=$SCENARIO\n      - NS_GLOBAL_VALUE=" + SIMULATOR_GLOBAL_VALUES + "\n")
    if direct_routing_compat:
        text = text.replace("com.docker.network.bridge.enable_ip_masquerade: 'false'", "com.docker.network.bridge.enable_ip_masquerade: 'false'\n      com.docker.network.bridge.gateway_mode_ipv4: 'nat-unprotected'\n      com.docker.network.bridge.gateway_mode_ipv6: 'nat-unprotected'")
    if "    hostname: sim\n" in text:
        text = text.replace("    hostname: sim\n", "    hostname: sim\n    healthcheck:\n      test: [CMD-SHELL, \"grep -Eq '00000000:E1E8 +00000000:0000 +0A' /proc/net/tcp\"]\n      interval: 100ms\n      timeout: 1s\n      retries: 100\n")
        before, client = text.split("  client:\n", 1)
        client = client.replace("    depends_on:\n      - sim\n", "    depends_on:\n      sim:\n        condition: service_healthy\n", 1)
        text = before + "  client:\n" + client
    return text


def provenance(image):
    root = Path(__file__).resolve().parents[1]
    sources = ["src/detail/quic_send_schedule.cpp", "src/detail/quic_connection_identity.cpp", "test/unit/quic_owner_peer.hpp", "test/unit/quic_interop_tls_test.cpp", "src/detail/tls_session.cpp", "src/detail/quic_tls_session.cpp", "test/Makefile.am", "test/support/http3_network_owner.cpp", "test/support/http3_network_lifecycle.cpp", "test/interop/quic/task173-matrix.json", "test/support/http3_network_owner.hpp", "test/support/quic_interop_files.cpp", "test/support/quic_interop_files.hpp", "test/integ/quic_interop_endpoint.cpp", "test/interop/quic/run_endpoint.sh", "test/interop/quic/Dockerfile", "test/interop/quic/Dockerfile.native", "scripts/run-v3-quic-interop.py"]
    commands = {"docker": ["docker", "version"], "compose": ["docker", "compose", "version"], "tshark": ["tshark", "--version"], "python_packages": [sys.executable, "-m", "pip", "freeze"], "endpoint_sha256": ["docker", "run", "--rm", "--network", "none", "--entrypoint", "sha256sum", image, "/quic_interop_endpoint"]}
    return {"provider_release": PROVIDER_RELEASE, "provider_archive_sha256": PROVIDER_ARCHIVE_SHA256, "source_sha256": {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in sources}, "tools": {name: subprocess.check_output(command, text=True, timeout=30).strip() for name, command in commands.items()}}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner-dir", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--server-image", required=True)
    parser.add_argument("--docker-direct-routing-compat", action="store_true", help="Docker 28+ compatibility on the two task-owned runner networks only")
    parser.add_argument("--simulator-clock-receipt", help="Explicit pinned-simulator monotonic pacing compatibility evidence")
    parser.add_argument("--matrix", choices=("task163", "task173"), default="task163")
    parser.add_argument("--timeout", type=int, default=1200)
    args = parser.parse_args(argv)
    matrix = load_matrix(args.matrix)
    output = Path(args.output).resolve()
    preflight(args.runner_dir, output)
    clock = simulator_clock_configuration(args.simulator_clock_receipt) if args.simulator_clock_receipt else None
    simulator_image = clock["image_id"] if clock else SIMULATOR
    images = {image: json.loads(subprocess.check_output(["docker", "image", "inspect", image], text=True))[0]["Id"] for image in [args.server_image, SIMULATOR] + ([simulator_image] if clock else []) + [peer["image"] for peer in matrix["peers"].values()]}
    inputs = provenance(images[args.server_image])
    output.mkdir(mode=0o700, parents=True)
    runner = output / "runner"
    shutil.copytree(args.runner_dir, runner, ignore=shutil.ignore_patterns(".git", "__pycache__"))
    registry = {matrix["server"]: {"image": images[args.server_image], "url": "https://github.com/etr/libhttpserver", "role": "server"}}
    registry.update({name: {"image": peer["image"], "url": peer["url"], "role": "client"} for name, peer in matrix["peers"].items()})
    (runner / "implementations_quic.json").write_text(json.dumps(registry, indent=2))
    if args.matrix == "task173":
        observation = runner / "testcases_quic.py"
        shutil.copyfile(observation, runner / "testcases_quic.original.py")
        observation.write_text(runner_observation_patch(observation.read_text()))
        shutil.copyfile(__file__, runner / "task196_observation.py")
    compose = runner / "docker-compose.yml"
    compose.write_text(compose_config(compose.read_text(), args.docker_direct_routing_compat, simulator_image))
    command = [sys.executable, "run.py", "-s", "libhttpserver", "-c", ",".join(matrix["peers"]), "-t", ",".join(matrix["cases"]), "-n", "libhttpserver", "-l", str(output / "runner-logs"), "-j", str(output / "results.json")]
    manifest = {"matrix": matrix, "runner_commit": RUNNER_COMMIT, "images": images, "docker_direct_routing_compat": args.docker_direct_routing_compat, "simulator_global_values": SIMULATOR_GLOBAL_VALUES, "simulator_clock_compatibility": clock, "command": command, "source_head": subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(), "passed": False, "provenance": inputs,
                "runner_observation": {"module_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                    "patched_testcases_sha256": hashlib.sha256((runner / "testcases_quic.py").read_bytes()).hexdigest(),
                    "original_testcases_sha256": hashlib.sha256((runner / "testcases_quic.original.py").read_bytes()).hexdigest()} if args.matrix == "task173" else None}
    try:
        code = run_bounded(command, runner, output / "stdout.log", output / "stderr.log", args.timeout)
        manifest["returncode"] = code
        if code or not validate_results(json.loads((output / "results.json").read_text()), matrix):
            raise ValueError("required upstream matrix cells did not succeed")
        manifest["capture_records"] = validate_artifacts(output / "runner-logs", matrix)
        if args.matrix == "task173":
            manifest["closure_witnesses"] = validate_closure_artifacts(output / "runner-logs", matrix)
        manifest["passed"] = True
    except Exception as error:
        manifest["failure"] = str(error)
        raise
    finally:
        try:
            cleanup_env = {**os.environ, "SERVER": images[args.server_image], "CLIENT": next(iter(matrix["peers"].values()))["image"]}
            cleanup_env.update({name: str(output) for name in ("SERVER_WWW", "CLIENT_WWW", "SERVER_DOWNLOADS", "CLIENT_DOWNLOADS", "SERVER_LOGS", "CLIENT_LOGS", "CERTS")})
            cleanup = subprocess.run(["docker", "compose", "--env-file", "empty.env", "down", "--timeout", "5"], cwd=runner, env=cleanup_env, capture_output=True, timeout=30)
            manifest["cleanup_returncode"] = cleanup.returncode
            if cleanup.returncode:
                manifest["cleanup_failure"] = str(cleanup.stderr)
        except (RuntimeError, OSError, subprocess.SubprocessError) as error:
            manifest["cleanup_failure"] = str(error)
        manifest["artifacts"] = {str(path.relative_to(output)): {"bytes": path.stat().st_size, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()} for path in output.rglob("*") if path.is_file() and "runner" not in path.relative_to(output).parts}
        (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, RuntimeError, OSError, subprocess.SubprocessError) as error:
        print("QUIC interop failed: " + str(error), file=sys.stderr)
        raise SystemExit(1)

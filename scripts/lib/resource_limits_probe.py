#!/usr/bin/env python3
"""TASK-180 Linux probes of the existing bounded TLS and UDP owners.

The client runs independently of the measured server. No sanitizer RSS claims.
Pinned client imports are lazy so receipt tests remain dependency free.
"""
import argparse
import asyncio
import importlib.util
from importlib.metadata import version
import json
import os
from pathlib import Path
import queue
import select
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'test/integ'))


def validate_window(sample):
    assert sample['wall_seconds'] >= .99, sample
    assert sample['cpu_seconds'] / sample['wall_seconds'] <= .2, sample
    assert sample['rss_after'] <= sample['rss_before'] + 1048576, sample


def measure(pid):
    # comm may contain spaces or parentheses; fields after its final ')' are stable.
    fields = Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()
    cpu = (int(fields[11]) + int(fields[12])) / os.sysconf('SC_CLK_TCK')
    # statm/status RSS counters are asynchronous; rollup walks the resident maps.
    rows = [line.split() for line in Path(f'/proc/{pid}/smaps_rollup').read_text().splitlines()
            if line.split() and line.split()[0] == 'Rss:']
    if (len(rows) != 1 or len(rows[0]) != 3 or rows[0][2] != 'kB'
            or not rows[0][1].isascii() or not rows[0][1].isdigit()):
        raise ValueError(f'invalid precise RSS in /proc/{pid}/smaps_rollup')
    rss = int(rows[0][1]) * 1024
    return cpu, rss


def window(pid, before, after, start, end):
    result = dict(cpu_seconds=after[0] - before[0], wall_seconds=end - start,
                  rss_before=before[1], rss_after=after[1], server_pid=pid)
    validate_window(result)
    return result


def h2_probe(fixture):
    from http2_client_h2 import Client
    client = Client(str(fixture), ['--resource-probe'])
    result = {'engine': 'h2', 'cycles': []}
    try:
        client.connect()
        def budgets():
            client.command('BUDGETS')
            assert select.select([client.proc.stdout], [], [], 3)[0], 'budget timeout'
            return json.loads(client.proc.stdout.readline())
        client.health(); client.barrier()
        baseline = budgets()
        for cycle in range(3):
            stream = client.request('/large')
            client.wait(lambda: len(client.data.get(stream, b'')) == 32)
            client.barrier(); client.health(); client.barrier()
            saturated = budgets()
            assert saturated['response_queue_bytes'] > baseline['response_queue_bytes'], saturated
            before = measure(client.proc.pid); start = time.monotonic()
            time.sleep(1)
            sample = window(client.proc.pid, before, measure(client.proc.pid), start, time.monotonic())
            client.barrier()
            assert len(client.data[stream]) == 32 and stream not in client.ended
            pressure = []
            for count in (64, 512):
                before = measure(client.proc.pid); start = time.monotonic()
                acknowledged = len(client.pings)
                # Stay below the production control rate. Every pressure frame
                # reaches the real stalled connection and receives its ACK.
                for i in range(count):
                    tag = b'pres' + (cycle * 1024 + count + i).to_bytes(4, 'big')
                    client.h2.ping(tag); client.flush()
                    client.wait(lambda: tag in client.pings)
                    if (i + 1) % 64 == 0: time.sleep(.35)
                after = measure(client.proc.pid)
                assert len(client.pings) == acknowledged + count
                assert after[1] <= sample['rss_before'] + 1048576, after
                assert len(client.data[stream]) == 32
                pressure.append(dict(attempted_control_frames=count, acknowledged_control_frames=count,
                                     wire_bytes=count * 17, accepted_response_bytes=32,
                                     cpu_seconds=after[0] - before[0], wall_seconds=time.monotonic() - start, rss=after[1]))
            client.h2.reset_stream(stream, error_code=8); client.flush()
            client.health(); client.barrier()
            retired = budgets()
            assert retired['response_queue_bytes'] == baseline['response_queue_bytes'], (baseline, retired)
            assert retired['streams'] == baseline['streams'], (baseline, retired)
            result['cycles'].append(dict(idle=sample, saturated=saturated, retired=retired, pressure=pressure))
            client.data.pop(stream, None); client.events.clear()
        uploads = []
        payload = b'U' * 16384
        for cycle in range(3):
            stream = client.request('/upload-hold', 'POST', False, [('content-length', '1048576')])
            client.barrier()
            count = min(len(payload), client.h2.local_flow_control_window(stream))
            assert count == 16384
            client.h2.send_data(stream, payload); client.flush(); client.barrier()
            saturated = budgets()
            assert saturated['body_buffer_bytes'] > baseline['body_buffer_bytes']
            before = measure(client.proc.pid); start = time.monotonic(); time.sleep(1)
            sample = window(client.proc.pid, before, measure(client.proc.pid), start, time.monotonic())
            assert client.h2.local_flow_control_window(stream) == 0
            client.h2.reset_stream(stream, error_code=8); client.flush(); client.health(); client.barrier()
            retired = budgets()
            assert retired['body_buffer_bytes'] == baseline['body_buffer_bytes'], (baseline, retired)
            uploads.append(dict(idle=sample, accepted_bytes=count, saturated=saturated, retired=retired))
        result['uploads'] = uploads
        # Ordered resumption after cancellation/refill witnesses.
        stream = client.request('/large'); client.wait(lambda: len(client.data.get(stream, b'')) == 32)
        client.h2.increment_flow_control_window(131072, stream_id=stream); client.flush()
        client.wait(lambda: stream in client.ended)
        assert client.data[stream] == b'L' * 131072
        result.update(baseline=baseline, resumed_bytes=len(client.data[stream]), control='PING ACK and sibling 204')
    finally:
        client.close()
    return result


def gate_module():
    spec = importlib.util.spec_from_file_location('existing_h3_gate', ROOT / 'scripts/run-v3-http3-gates.py')
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module


async def h3_probe(fixture, directory):
    from aioquic.asyncio import connect
    from aioquic.quic.configuration import QuicConfiguration
    from http3_semantic_aioquic import SemanticClient, done
    from http3_client_aioquic import StreamFailure
    gate = gate_module(); credentials = directory / 'credentials'; credentials.mkdir()
    gate.certificates(sys.executable, credentials)
    messages = queue.Queue(maxsize=1024)
    owner = gate.Process([str(fixture), str(credentials / 'cert.pem'), str(credentials / 'key.pem'),
                          str(directory / 'packets.pcap'), '--resource-probe'], directory, 'h3-server', messages)
    async def event(kind):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            try:
                _, line = messages.get_nowait()
            except queue.Empty:
                await asyncio.sleep(.001); continue
            assert line is not None, 'owner exited'
            if line.startswith('READY'):
                if kind == 'READY': return gate.readiness(line)
                continue
            item = json.loads(line)
            if item['event'] == kind: return item
        raise AssertionError('owner event timeout: ' + kind)
    async def budgets():
        owner.write('budgets 1 0'); return await event('budgets')
    result = {'engine': 'h3', 'cycles': []}
    try:
        port = await event('READY')
        config = QuicConfiguration(is_client=True, alpn_protocols=['h3'], supported_versions=[1],
                                   server_name='localhost', max_stream_data=4096, max_data=16777216)
        config.load_verify_locations(str(credentials / 'ca.pem'))
        async with connect('127.0.0.1', port, configuration=config, create_protocol=SemanticClient) as client:
            await done(client.request('GET', '/health')); await asyncio.sleep(.2)
            baseline = await budgets()
            for cycle in range(3):
                state = client.start('GET', '/large', finish=True); stream = state['stream']; client.frozen.add(stream)
                deadline = time.monotonic() + 5
                while client._quic._streams[stream].receiver.highest_offset != client._quic._streams[stream].max_stream_data_local:
                    assert time.monotonic() < deadline, 'credit not saturated'
                    await asyncio.sleep(.001)
                assert not state['done'].done()
                health = await done(client.request('GET', '/health')); assert dict(health['headers'])[b':status'] == b'204'
                await asyncio.sleep(.2); saturated = await budgets()
                assert saturated['response_queue_bytes'] > baseline['response_queue_bytes'], saturated
                before = measure(owner.process.pid); start = time.monotonic()
                await asyncio.sleep(1)
                sample = window(owner.process.pid, before, measure(owner.process.pid), start, time.monotonic())
                assert not state['done'].done()
                pressure = []
                for count in (64, 512):
                    before = measure(owner.process.pid); start = time.monotonic()
                    for i in range(count):
                        client._quic.send_ping(cycle * 1024 + count + i); client.transmit()
                        if (i + 1) % 64 == 0: await asyncio.sleep(.05)
                    await asyncio.sleep(.2)
                    await asyncio.wait_for(client.ping(), 5)
                    after = measure(owner.process.pid)
                    assert after[1] <= sample['rss_before'] + 1048576, after
                    assert not state['done'].done()
                    pressure.append(dict(attempted_control_frames=count, accepted_response_bytes=len(state['body']),
                                         cpu_seconds=after[0] - before[0], wall_seconds=time.monotonic() - start, rss=after[1]))
                client._quic.stop_stream(stream, 268); client._quic.reset_stream(stream, 268); client.transmit()
                try: await done(state)
                except StreamFailure: pass
                # The next cycle supplies readmission; preserve the bounded
                # owner's initial sixteen bidirectional transport streams.
                await asyncio.sleep(.2)
                retired = await budgets()
                assert retired['response_queue_bytes'] == baseline['response_queue_bytes'], (baseline, retired)
                assert retired['streams'] == baseline['streams'], (baseline, retired)
                result['cycles'].append(dict(idle=sample, saturated=saturated, retired=retired, pressure=pressure))
                client.frozen.discard(stream)
            uploads = []
            for cycle in range(3):
                state = client.start('POST', '/upload-hold', 1048576)
                await event('upload_held')
                stream = state['stream']
                client.h3.send_data(stream, b'U' * 131072, end_stream=False); client.transmit()
                await asyncio.sleep(.2)
                saturated = await budgets()
                assert saturated['body_buffer_bytes'] > baseline['body_buffer_bytes'], saturated
                before = measure(owner.process.pid); start = time.monotonic(); await asyncio.sleep(1)
                sample = window(owner.process.pid, before, measure(owner.process.pid), start, time.monotonic())
                transport = client._quic._streams[stream]
                assert transport.sender.highest_offset == transport.max_stream_data_remote
                assert transport.sender._buffer_stop > transport.sender.highest_offset
                assert not state['done'].done()
                client._quic.reset_stream(stream, 268); client._quic.stop_stream(stream, 268); client.transmit()
                try: await done(state)
                except StreamFailure: pass
                await done(client.request('GET', '/health')); await asyncio.sleep(.2)
                retired = await budgets()
                assert retired['body_buffer_bytes'] == baseline['body_buffer_bytes'], (baseline, retired)
                uploads.append(dict(idle=sample, supplied_bytes=131072, wire_offset=transport.sender.highest_offset,
                                    peer_credit=transport.max_stream_data_remote, saturated=saturated, retired=retired))
            result['uploads'] = uploads
            state = client.start('GET', '/large', finish=True)
            complete = await done(state)
            expected = bytes(i % 256 for i in range(524288))
            assert complete['body'] == expected
            result.update(baseline=baseline, resumed_bytes=len(complete['body']), control='QUIC ACK/PING and sibling 204')
    finally:
        owner.stop('quit')
        assert owner.process.returncode == 0 and not owner.errors, owner.errors
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--h2-fixture', type=Path, required=True)
    parser.add_argument('--h3-fixture', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    def expired(*_):
        raise TimeoutError('probe deadline')
    signal.signal(signal.SIGALRM, expired)
    signal.alarm(90)
    args = parser.parse_args(); args.output.mkdir(parents=True, exist_ok=False)
    pins = {'h2': version('h2'), 'aioquic': version('aioquic')}
    assert pins == {'h2': '4.4.1', 'aioquic': '1.3.0'}, pins
    result = {'client_versions': pins, 'rss_slack': 1048576, 'idle_core_fraction': .2, 'normal_allocator_only': True,
              'h2': h2_probe(args.h2_fixture.resolve()),
              'h3': asyncio.run(h3_probe(args.h3_fixture.resolve(), args.output))}
    (args.output / 'receipt.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))

if __name__ == '__main__':
    main()

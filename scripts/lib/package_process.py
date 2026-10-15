"""Bounded native Windows/POSIX subprocess pipes with failure receipts."""
import json
import os
from pathlib import Path
import queue
import subprocess
import threading
import time


class Consumer:
    def __init__(self, command, cwd, env, receipt, timeout=15, output_limit=65536):
        self.command, self.receipt = command, Path(receipt)
        self.timeout, self.limit = timeout, output_limit
        self.lines, self.errors = queue.Queue(maxsize=256), queue.Queue()
        self.buffers = {'stdout': bytearray(), 'stderr': bytearray()}
        self.process = subprocess.Popen(command, cwd=cwd, env=env, stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.readers = [threading.Thread(target=self.drain, args=(name,), daemon=True)
                        for name in self.buffers]
        for reader in self.readers:
            reader.start()

    def drain(self, name):
        pipe, pending = getattr(self.process, name), bytearray()
        try:
            while chunk := os.read(pipe.fileno(), 4096):
                buffer = self.buffers[name]
                room = self.limit - len(buffer)
                buffer.extend(chunk[:room])
                if len(chunk) > room:
                    self.errors.put(RuntimeError('consumer output budget exceeded: ' + name))
                    return
                if name == 'stdout':
                    pending.extend(chunk)
                    while b'\n' in pending:
                        line, _, remaining = pending.partition(b'\n')
                        pending = bytearray(remaining)
                        self.lines.put_nowait(line.decode(errors='replace').strip())
            if name == 'stdout':
                self.lines.put_nowait(None)
        except (OSError, queue.Full) as error:
            self.errors.put(RuntimeError('consumer pipe failed: ' + str(error)))
        finally:
            pipe.close()

    def check_errors(self):
        if not self.errors.empty():
            raise self.errors.get()

    def line(self):
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            self.check_errors()
            try:
                line = self.lines.get(timeout=min(.05, max(.001, deadline - time.monotonic())))
            except queue.Empty:
                continue
            if line is None:
                raise RuntimeError('consumer startup exited without readiness')
            return line
        raise RuntimeError('consumer startup deadline exceeded')

    def port(self):
        line = self.line()
        if not line.isdecimal() or not 0 < int(line) <= 65535:
            raise RuntimeError('malformed consumer readiness: ' + line[:200])
        return int(line)

    def stop(self):
        self.process.stdin.write(b'stop\n')
        self.process.stdin.close()
        deadline = time.monotonic() + self.timeout
        while self.process.poll() is None and time.monotonic() < deadline:
            self.check_errors()
            time.sleep(.01)
        if self.process.poll() is None:
            raise RuntimeError('consumer shutdown deadline exceeded')
        for reader in self.readers:
            reader.join(timeout=1)
        self.check_errors()
        if self.process.returncode:
            raise RuntimeError('consumer shutdown failed: ' + str(self.process.returncode))

    def __enter__(self):
        return self

    def __exit__(self, kind, error, traceback):
        if self.process.poll() is None:
            self.process.kill()
        self.process.wait(timeout=5)
        if not self.process.stdin.closed:
            self.process.stdin.close()
        for reader in self.readers:
            reader.join(timeout=1)
        record = dict(command=self.command, exit=self.process.returncode,
                      **{name: bytes(value).decode(errors='replace') for name, value in self.buffers.items()})
        if error is not None:
            record['error'] = str(error)
        self.receipt.write_text(json.dumps(record, indent=2) + '\n')

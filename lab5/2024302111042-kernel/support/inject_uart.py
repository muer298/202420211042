#!/usr/bin/env python3
"""Course UART script interface adapted to the actual lab2 shell and diskless boot.

SEND text / SENDRAW text / WAIT milliseconds / REPEAT count text / EXPECT regex
The original course script is preserved in ../../materials/inject_uart.original.py.
EXPECT consumes matches so old output cannot make subsequent tests pass.
"""
import argparse
import os
from pathlib import Path
import re
import selectors
import subprocess
import time


class Session:
    def __init__(self, tree, log, cpus=1):
        self.logpath = Path(log).resolve()
        self.logpath.parent.mkdir(parents=True, exist_ok=True)
        self.irqpath = self.logpath.with_suffix('.int.log')
        self.log = self.logpath.open('wb')
        self.data = b''
        self.cursor = 0
        self.proc = subprocess.Popen([
            os.environ.get('QEMU', 'qemu-system-riscv64'), '-machine', 'virt',
            '-bios', 'none', '-kernel', 'kernel/kernel', '-m', '128M',
            '-smp', str(cpus), '-nographic', '-monitor', 'none',
            '-d', 'int', '-D', str(self.irqpath)
        ], cwd=tree, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, bufsize=0)
        self.sel = selectors.DefaultSelector()
        self.sel.register(self.proc.stdout, selectors.EVENT_READ)

    def pump(self, seconds=0.05):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            for key, _ in self.sel.select(max(0, deadline-time.monotonic())):
                chunk = os.read(key.fd, 65536)
                if not chunk:
                    raise AssertionError('QEMU exited: ' + self.data[-1000:].decode(errors='replace'))
                self.data += chunk
                self.log.write(chunk)
                self.log.flush()

    def send(self, data):
        if isinstance(data, str):
            data = data.encode()
        self.proc.stdin.write(data)
        self.proc.stdin.flush()

    def expect(self, pattern, timeout=10):
        regex = re.compile(pattern.encode(), re.M)
        deadline = time.monotonic() + timeout
        while True:
            match = regex.search(self.data, self.cursor)
            if match:
                self.cursor = match.end()
                return match
            if time.monotonic() >= deadline:
                raise AssertionError(f'EXPECT {pattern!r} timed out; tail={self.data[-1000:]!r}')
            self.pump(min(0.05, max(0, deadline-time.monotonic())))

    def close(self):
        if self.proc.poll() is None:
            self.proc.terminate()
        self.proc.communicate(timeout=3)
        self.sel.close()
        self.log.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--tree', required=True)
    ap.add_argument('--script', required=True)
    ap.add_argument('--log', default='inject_uart.log')
    args = ap.parse_args()
    session = Session(args.tree, args.log)
    try:
        session.expect(r'sh> ')
        print('boot ok')
        for line in Path(args.script).read_text().splitlines():
            if not line.strip() or line.startswith('#'):
                continue
            parts = line.split(None, 1)
            op, arg = parts[0].upper(), parts[1] if len(parts)>1 else ''
            if op == 'SEND':
                session.send(arg + '\n')
            elif op == 'SENDRAW':
                session.send(arg)
            elif op == 'WAIT':
                session.pump(int(arg)/1000)
            elif op == 'REPEAT':
                n, text = arg.split(None, 1)
                for _ in range(int(n)):
                    session.send(text+'\n')
                    session.pump(0.02)
            elif op == 'EXPECT':
                session.expect(arg)
                print(f'EXPECT {arg!r}: PASS')
            else:
                raise ValueError(f'Unknown script operation: {op}')
        print('EXPECT failures: 0')
    finally:
        session.close()


if __name__ == '__main__':
    main()

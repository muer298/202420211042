#!/usr/bin/env python3
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'support'))
from inject_uart import Session


class Integration(unittest.TestCase):
    def setUp(self):
        self.session = Session(ROOT, ROOT / 'test-results' / (self._testMethodName+'.log'))
        self.addCleanup(self.finish)
        self.session.expect(r'sh> ')
        banner = (ROOT / 'expect_banner.txt').read_bytes()
        self.assertEqual(self.session.data[:len(banner)], banner)

    def finish(self):
        self.session.close()
        self.assertNotIn(b'PANIC', self.session.data)
        self.assertNotIn(b'SELFTEST FAIL', self.session.data)
        # ecalls (cause 8) are expected synchronous exceptions in lab2.
        log = self.session.irqpath.read_text()
        faults = [line for line in log.splitlines() if 'async:0' in line and 'cause:0000000000000008' not in line]
        self.assertEqual(faults, [])

    def command(self, text, expected):
        self.session.send(text+'\n')
        self.session.expect(expected)
        self.session.expect(r'sh> ')

    def test_syscalls_and_preemption(self):
        self.command('hi', r'hi: user program running, pid=2')
        self.command('badecall', r'TEST-1 PASS: unknown syscalls all return -1')
        self.command('selftest', r'SELFTEST PASS: pointers fork wait exec timer preemption')

    def test_empty_read_line_commit(self):
        self.session.send('readprobe\n')
        self.session.expect(r'READPROBE READY capacity=96')
        self.session.send('abc')
        self.session.pump(0.2)
        self.assertNotIn(b'READPROBE n=', self.session.data)
        self.session.send('\n')
        self.session.expect(r'READPROBE n=4')
        self.session.expect(r'sh> ')

    def test_capacity_overflow_and_wrap(self):
        for count in [95, 180, 30, 70, 95, 2]:
            self.session.send('readprobe\n')
            self.session.expect(r'READPROBE READY capacity=96')
            self.session.send('x'*count+'\n')
            expected=min(count,95)+1
            self.session.expect(rf'READPROBE n={expected}\r?\n')
            self.session.expect('x'*min(count,95)+r'\r?\n')
            self.session.expect(r'sh> ')

    def test_editing_and_exec_failure(self):
        self.session.send(b'hX\x7fi\r')
        self.session.expect(r'hi: user program running')
        self.session.expect(r'sh> ')
        self.command('missing', r'exec missing\r?\n failed')
        self.command('hi', r'hi: user program running')

    def test_spin_uart_and_ctrl_c(self):
        self.session.send('spin\n')
        self.session.expect(r'spin 0')
        self.session.send('XYZ')
        self.session.expect('XYZ')
        self.session.send(b'\x03')
        self.session.expect(r'\^C')
        self.session.expect(r'sh> ')
        self.command('hi', r'hi: user program running')

    def test_reap_reuse(self):
        for _ in range(70):
            self.command('hi', r'hi: user program running')


class Packaging(unittest.TestCase):
    def test_gifts_unchanged(self):
        gifts = ROOT.parent / 'gifts'
        for group in ['kernel', 'user']:
            for path in (gifts/group).iterdir():
                self.assertEqual(hashlib.sha256(path.read_bytes()).digest(),
                                 hashlib.sha256((ROOT/group/path.name).read_bytes()).digest(), path.name)
        for path in (gifts/'tests').glob('*.c'):
            self.assertEqual(path.read_bytes(), (ROOT/'user'/path.name).read_bytes())

    def test_flat_image_entry_and_size(self):
        prefix = os.environ.get('TOOLPREFIX')
        if prefix is None:
            prefix = 'riscv64-unknown-elf-' if shutil.which('riscv64-unknown-elf-gcc') else 'riscv64-elf-'
        for path in (ROOT/'user-flat').glob('*.elf'):
            out = subprocess.check_output([prefix+'readelf','-h',str(path)], text=True)
            self.assertRegex(out, r'Entry point address:\s+0x0\b', path.name)
            out = subprocess.check_output([prefix+'nm','-n',str(path)], text=True)
            end = re.search(r'^([0-9a-f]+) [A-Za-z] end$',out,re.M)
            self.assertIsNotNone(end)
            self.assertLessEqual(int(end[1],16), 64*1024-4096)

    def test_course_injector(self):
        for name in ['storm', 'storm-overflow']:
            subprocess.run([sys.executable,str(ROOT/'support/inject_uart.py'),
                            '--tree',str(ROOT),'--script',str(ROOT/'tests'/f'{name}.txt'),
                            '--log',str(ROOT/'test-results'/f'{name}.log')],check=True)

    def test_four_harts(self):
        session = Session(ROOT,ROOT/'test-results/four-harts.log',cpus=4)
        try:
            session.expect(r'sh> ')
            session.send('hi\n')
            session.expect(r'hi: user program running, pid=2')
            session.expect(r'sh> ')
            banner=(ROOT/'expect_banner.txt').read_bytes()
            self.assertEqual(session.data.count(banner),1)
            self.assertNotIn(b'PANIC',session.data)
        finally:
            session.close()

    def test_character_stream_variant(self):
        # Override only a test-build switch; immutable student parameters stay intact.
        with tempfile.TemporaryDirectory(prefix='lab2-stream-') as td:
            lab = Path(td)/'lab2'
            shutil.copytree(ROOT.parent/'gifts',lab/'gifts')
            tree = lab/ROOT.name
            shutil.copytree(ROOT,tree,ignore=shutil.ignore_patterns('test-results','*.o','user-flat','__pycache__'))
            subprocess.run(['make','clean'],cwd=tree,check=True,stdout=subprocess.DEVNULL)
            flags='-Wall -Werror -O -std=gnu99 -mcmodel=medany -ffreestanding -nostdlib -fno-common -ggdb -march=rv64gc -fno-stack-protector -fno-pie -fno-builtin -DCONSOLE_SEMANTICS=1'
            subprocess.run(['make',f'CFLAGS={flags}'],cwd=tree,check=True,stdout=subprocess.DEVNULL)
            session = Session(tree,ROOT/'test-results/stream.log')
            try:
                session.expect(r'sh> ')
                session.send('readprobe\n')
                session.expect(r'READPROBE READY capacity=96')
                session.send('q')
                session.expect(r'READPROBE n=1')
                session.expect(r'sh> ')
                session.send('hi\n')
                session.expect(r'hi: user program running')
                session.expect(r'sh> ')
                self.assertNotIn(b'PANIC',session.data)
            finally:
                session.close()


if __name__ == '__main__':
    unittest.main(verbosity=2)

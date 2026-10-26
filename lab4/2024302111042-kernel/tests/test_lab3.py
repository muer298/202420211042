#!/usr/bin/env python3
"""Lab3 integration, regression, ABI, and packaging checks."""
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
        log = ROOT / 'test-results' / (self._testMethodName + '.log')
        self.session = Session(ROOT, log)
        self.addCleanup(self.finish)
        self.session.expect(r'sh> ')
        banner = (ROOT / 'expect_banner.txt').read_bytes()
        self.assertEqual(self.session.data[:len(banner)], banner)
        self.assertIn(b'LAB3-SELFTEST PASS allocator order=1 rollback', self.session.data)
        self._check_boot_dump()

    def _check_boot_dump(self):
        data = self.session.data.decode(errors='replace')
        block = re.search(r'DUMP-PAGETABLE begin\r?\n(.*?)DUMP-PAGETABLE end',
                          data, re.S)
        self.assertIsNotNone(block)
        dump = block.group(1)
        self.assertRegex(dump, r'L2 va=0x0 pa=0x[0-9a-f]+ perm=----')
        self.assertRegex(dump, r'L1 va=0x0 pa=0x[0-9a-f]+ perm=----')
        self.assertRegex(dump, r'LEAF va=0x0 pa=0x[0-9a-f]+ perm=r-xu')
        self.assertRegex(dump, r'LEAF va=0x3fffffe000 .* perm=rw--')
        self.assertRegex(dump, r'LEAF va=0x3ffffff000 .* perm=r-x-')
        self.assertNotRegex(dump, r'LEAF va=0x0 .* perm=.w')

    def finish(self):
        self.session.close()
        self.assertNotIn(b'PANIC', self.session.data)
        self.assertNotIn(b' FAIL', self.session.data)
        # User ecalls and the two deliberate badaccess page faults are expected.
        allowed = {'0000000000000008', '000000000000000d',
                   '000000000000000f'}
        bad = []
        for line in self.session.irqpath.read_text().splitlines():
            if 'async:0' not in line:
                continue
            match = re.search(r'cause:([0-9a-f]+)', line)
            if match and match.group(1) not in allowed:
                bad.append(line)
        self.assertEqual(bad, [])

    def command(self, text, expected, timeout=10):
        self.session.send(text + '\n')
        self.session.expect(expected, timeout=timeout)
        self.session.expect(r'sh> ', timeout=timeout)

    def test_lab2_regression_and_vm(self):
        self.command('hi', r'hi: user program running, pid=2')
        self.command('badecall', r'TEST-1 PASS: unknown syscalls all return -1')
        self.command('selftest',
                     r'SELFTEST PASS: pointers fork wait exec timer preemption')
        self.command('vmtest', r'VMTEST PASS cross-page copy and sbrk')

    def test_pagetable_dump_syscall(self):
        self.session.send('pgtbl\n')
        self.session.expect(r'DUMP-PAGETABLE begin')
        self.session.expect(r'LEAF va=0x0 pa=0x[0-9a-f]+ perm=r-xu')
        self.session.expect(r'LEAF va=0x3fffffe000 .* perm=rw--')
        self.session.expect(r'LEAF va=0x3ffffff000 .* perm=r-x-')
        self.session.expect(r'DUMP-PAGETABLE end')
        self.session.expect(r'PGTBL PASS')
        self.session.expect(r'sh> ')

    def test_official_badaccess(self):
        self.command('badaccess',
                     r'TEST-3a PASS: wild write killed[\s\S]*'
                     r'TEST-3b PASS: beyond-sbrk access killed', timeout=20)
        self.command('hi', r'hi: user program running')

    def test_official_memcap(self):
        self.command('memcap',
                     r'TEST-4 PASS: OOM returned error after [1-9][0-9]* chunks; '
                     r'existing alloc intact; kernel alive', timeout=240)
        self.command('hi', r'hi: user program running')

    def test_console_and_reap_reuse(self):
        self.session.send('readprobe\n')
        self.session.expect(r'READPROBE READY capacity=96')
        self.session.send('abc')
        self.session.pump(0.2)
        self.assertNotIn(b'READPROBE n=', self.session.data)
        self.session.send('\n')
        self.session.expect(r'READPROBE n=4')
        self.session.expect(r'sh> ')
        for _ in range(70):
            self.command('hi', r'hi: user program running')


class Packaging(unittest.TestCase):
    def test_gifts_unchanged(self):
        gifts = ROOT.parent / 'gifts'
        for group in ['kernel', 'user']:
            for path in (gifts / group).iterdir():
                actual = ROOT / group / path.name
                self.assertTrue(actual.exists(), path.name)
                self.assertEqual(hashlib.sha256(path.read_bytes()).digest(),
                                 hashlib.sha256(actual.read_bytes()).digest(),
                                 path.name)
        for path in (gifts / 'tests').glob('*.c'):
            self.assertEqual(path.read_bytes(),
                             (ROOT / 'user' / path.name).read_bytes())

    def test_official_sources_unchanged(self):
        official = ROOT.parent / 'support' / 'tests'
        for name in ['badaccess.c', 'memcap.c']:
            self.assertEqual((official / name).read_bytes(),
                             (ROOT / 'user' / name).read_bytes(), name)

    def test_flat_image_layout(self):
        prefix = os.environ.get('TOOLPREFIX')
        if prefix is None:
            prefix = ('riscv64-unknown-elf-' if
                      shutil.which('riscv64-unknown-elf-gcc') else
                      'riscv64-elf-')
        for path in (ROOT / 'user-flat').glob('*.elf'):
            out = subprocess.check_output([prefix + 'readelf', '-h', str(path)],
                                          text=True)
            self.assertRegex(out, r'Entry point address:\s+0x0\b', path.name)
            syms = subprocess.check_output([prefix + 'nm', '-n', str(path)],
                                           text=True)
            data = re.search(r'^([0-9a-f]+) [A-Za-z] data_start$', syms, re.M)
            end = re.search(r'^([0-9a-f]+) [A-Za-z] end$', syms, re.M)
            self.assertIsNotNone(data, path.name)
            self.assertIsNotNone(end, path.name)
            self.assertEqual(int(data[1], 16) % 4096, 0, path.name)
            self.assertLessEqual(int(end[1], 16), 64 * 1024 - 4096, path.name)

    def test_course_uart_scripts(self):
        for name in ['storm', 'storm-overflow']:
            subprocess.run([
                sys.executable, str(ROOT / 'support' / 'inject_uart.py'),
                '--tree', str(ROOT), '--script', str(ROOT / 'tests' / f'{name}.txt'),
                '--log', str(ROOT / 'test-results' / f'{name}.log')
            ], check=True)

    def test_four_harts_single_boot(self):
        session = Session(ROOT, ROOT / 'test-results' / 'four-harts.log', cpus=4)
        try:
            session.expect(r'sh> ')
            session.send('hi\n')
            session.expect(r'hi: user program running, pid=2')
            session.expect(r'sh> ')
            banner = (ROOT / 'expect_banner.txt').read_bytes()
            self.assertEqual(session.data.count(banner), 1)
            self.assertNotIn(b'PANIC', session.data)
        finally:
            session.close()

    def test_character_stream_variant(self):
        with tempfile.TemporaryDirectory(prefix='lab3-stream-') as td:
            lab = Path(td) / 'lab3'
            shutil.copytree(ROOT.parent / 'gifts', lab / 'gifts')
            tree = lab / ROOT.name
            ignore = shutil.ignore_patterns('test-results', '*.o', 'user-flat',
                                             '__pycache__')
            shutil.copytree(ROOT, tree, ignore=ignore)
            subprocess.run(['make', 'clean'], cwd=tree, check=True,
                           stdout=subprocess.DEVNULL)
            flags = ('-Wall -Werror -O -std=gnu99 -mcmodel=medany '
                     '-ffreestanding -nostdlib -fno-common -ggdb -march=rv64gc '
                     '-fno-stack-protector -fno-pie -fno-builtin '
                     '-DCONSOLE_SEMANTICS=1')
            subprocess.run(['make', f'CFLAGS={flags}'], cwd=tree, check=True,
                           stdout=subprocess.DEVNULL)
            session = Session(tree, ROOT / 'test-results' / 'stream.log')
            try:
                session.expect(r'sh> ')
                session.send('readprobe\n')
                session.expect(r'READPROBE READY capacity=96')
                session.send('q')
                session.expect(r'READPROBE n=1')
                session.expect(r'sh> ')
                self.assertNotIn(b'PANIC', session.data)
            finally:
                session.close()


if __name__ == '__main__':
    unittest.main(verbosity=2)

#!/usr/bin/env python3
"""Lab5 COW fork, pmc, and exec argv integration checks."""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'support'))
from inject_uart import Session


class Lab5Integration(unittest.TestCase):
    def setUp(self):
        self.session = Session(ROOT, ROOT / 'test-results' / (self._testMethodName + '.log'))
        self.addCleanup(self.session.close)
        self.session.expect(r'LAB5-SELFTEST PASS refcnt alloc/share/release balance', timeout=30)
        self.session.expect(r'sh> ', timeout=30)

    def run_program(self, command, pass_line, timeout=60):
        self.session.send(command + '\n')
        self.session.expect(pass_line, timeout=timeout)
        self.session.expect(r'sh> ', timeout=timeout)

    def test_cow_and_exec_acceptance(self):
        self.run_program('zerocopy', r'T5-1 PASS: fork-immediately-exec ~zero copy')
        output = self.session.data.decode(errors='replace')
        self.assertRegex(output, r'T5-1 copies: \d+ -> \d+ \(delta [0-2]\)')

        self.run_program('cowpair', r'T5-2 PASS: pair-write exact 2 copies, data isolated')
        self.assertIn('T5-2 copies delta = 2 (expect 2)',
                      self.session.data.decode(errors='replace'))

        self.run_program('echo hi', r'T5-3 PASS: argc=2 argv\[0\]=echo aligned')
        self.assertRegex(self.session.data.decode(errors='replace'), r'\nhi\r?\n')

        self.run_program('cowtest', r'COWTEST PASS all requested cases', timeout=120)
        output = self.session.data.decode(errors='replace')
        self.assertIn('reached LAB5_COW_BUDGET=194', output)
        self.assertIn('COWTEST PASS readonly write isolated', output)
        self.assertIn('COWTEST PASS invalid pointers and exec argv', output)


if __name__ == '__main__':
    unittest.main(verbosity=2)

#!/usr/bin/env python3
"""Lab4 scheduling and syscall integration checks."""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'support'))
from inject_uart import Session


class Lab4Integration(unittest.TestCase):
    def setUp(self):
        self.session = Session(ROOT, ROOT / 'test-results' / (self._testMethodName + '.log'))
        self.addCleanup(self.session.close)
        self.session.expect(r'sh> ', timeout=30)

    def run_program(self, name, pass_line, timeout=60):
        self.session.send(name + '\n')
        self.session.expect(pass_line, timeout=timeout)
        self.session.expect(r'sh> ', timeout=30)

    def test_fork_exit_wait_and_wakeup(self):
        self.run_program('bench', r'PASS: bench')
        output = self.session.data.decode(errors='replace')
        self.assertRegex(output, r'RESULT avg_fork_wait = \d+')
        self.assertRegex(output, r'RESULT concurrent_children = 8')

    def test_mlfq_timer_and_observation_syscalls(self):
        self.run_program('mlfq_test', r'PASS: mlfq timer accounting and process lifecycle')
        output = self.session.data.decode(errors='replace')
        self.assertRegex(output, r'RESULT mlfq_observations = [1-9]\d*')
        self.assertIn('RESULT mlfq_quantum_mask = 15', output)


if __name__ == '__main__':
    unittest.main(verbosity=2)

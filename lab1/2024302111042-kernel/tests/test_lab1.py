#!/usr/bin/env python3
"""V2 tests: rebuild in a temporary copy and run actual RISC-V code in QEMU.

Requires Python 3, make, RISC-V GCC/binutils and QEMU.
The student's course_sid.h is read but never modified.
"""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
QEMU = os.environ.get("QEMU", "qemu-system-riscv64")


def protocol1(data):
    return b"".join(bytes((byte, ord("."))) for byte in data)


def run_kernel(image, directory, cpus=1, reset=False, expected_size=0):
    serial = directory / "serial.bin"
    interrupts = directory / "interrupts.log"
    proc = subprocess.Popen([
        QEMU, "-machine", "virt", "-bios", "none", "-kernel", str(image),
        "-nographic", "-serial", "file:" + str(serial), "-monitor", "stdio",
        "-smp", str(cpus), "-d", "int", "-D", str(interrupts),
    ], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def await_serial(size):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if proc.poll() is not None:
                raise AssertionError("QEMU exited before completing serial output")
            if serial.exists() and serial.stat().st_size >= size:
                time.sleep(0.2)  # Catch unexpected output after the expected bytes.
                return
            time.sleep(0.02)
        raise AssertionError("Timed out waiting for kernel output")

    try:
        await_serial(expected_size)
        if reset:
            proc.stdin.write(b"system_reset\n")
            proc.stdin.flush()
            await_serial(expected_size * 2)
        monitor, stderr = proc.communicate(b"info registers\nquit\n", timeout=5)
        if proc.returncode:
            raise AssertionError(stderr.decode(errors="replace"))
        return serial.read_bytes(), interrupts.read_text(), monitor.decode(errors="replace")
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.communicate()


class Lab1Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="lab1-v2-tests-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        cls.source = cls.directory / "source"
        shutil.copytree(ROOT, cls.source, ignore=shutil.ignore_patterns("*.o", "__pycache__"))
        header = (ROOT / "kernel/course_sid.h").read_text()
        cls.sid = int(re.search(r"#define COURSE_SID (\d+)", header)[1])
        cls.stack_kb = int(re.search(r"#define LAB1_STACK_KB (\d+)", header)[1])
        cls.protocol = int(re.search(r"#define LAB1_BANNER_PROTOCOL (\d+)", header)[1])
        cls.banner = protocol1(f"OSLAB1 sid={cls.sid} mod97={cls.sid % 97:#x}\n".encode())
        subprocess.run(["make", "-B", "kernel/kernel"], cwd=cls.source, check=True)
        cls.banner_image = cls.directory / "banner.elf"
        shutil.copyfile(cls.source / "kernel/kernel", cls.banner_image)
        shutil.copyfile(ROOT / "tests/printf_main.c", cls.source / "kernel/main.c")
        subprocess.run(["make", "-B", "kernel/kernel"], cwd=cls.source, check=True)
        cls.test_image = cls.source / "kernel/kernel"

    def check_run(self, image, expected, cpus=1, reset=False):
        with tempfile.TemporaryDirectory(dir=self.directory) as directory:
            actual, traps, registers = run_kernel(image, Path(directory), cpus, reset, len(expected))
        self.assertEqual(actual, expected * (2 if reset else 1))
        self.assertEqual(traps.strip(), "", "Unexpected QEMU exception/interrupt")
        self.assertRegex(registers, r"priv\s+S\b")
        self.assertRegex(registers, r"satp\s+0+\b")
        self.assertRegex(registers, r"scause\s+0+\b")
        self.assertEqual(actual[1::2], b"." * (len(actual) // 2))

    def test_expected_banner_and_course_checker(self):
        self.assertEqual(self.protocol, 1, "This suite targets the assigned protocol 1")
        self.assertEqual(self.sid % 3, self.protocol)
        self.assertEqual((ROOT / "expect_banner.txt").read_bytes(), self.banner)
        subprocess.run(["python3", "check_expect.py", str(self.sid)], cwd=ROOT, check=True)

    def test_single_core_banner(self):
        self.check_run(self.banner_image, self.banner)

    def test_four_core_banner(self):
        self.check_run(self.banner_image, self.banner, cpus=4)

    def test_reset_banner(self):
        self.check_run(self.banner_image, self.banner, reset=True)

    def test_printf_and_protocol_boundaries(self):
        plain = (
            "d:0,-123,2147483647,-2147483648\n"
            "x:0x0,0x48,0xffffffff\n"
            "s:[],[Hello RISC-V OS!]\n"
            "percent:%\n"
            "long:9223372036854775807,-9223372036854775808,"
            "18446744073709551615,0xffffffffffffffff\n"
            ".\n\n" + "abcdefghijklmnopqrstuvwxyz0123456789" * 128 + "\n"
        ).encode()
        self.check_run(self.test_image, protocol1(plain))

    def test_entry_and_stack(self):
        prefix = os.environ.get("TOOLPREFIX")
        if prefix is None:
            prefix = "riscv64-unknown-elf-" if shutil.which("riscv64-unknown-elf-gcc") else "riscv64-elf-"
        symbols = subprocess.check_output([prefix + "nm", "-S", str(self.banner_image)], text=True)
        stack = re.search(r"^([0-9a-f]+) ([0-9a-f]+) B boot_stack$", symbols, re.M)
        self.assertIsNotNone(stack)
        self.assertEqual(int(stack[1], 16) % 16, 0)
        self.assertEqual(int(stack[2], 16), self.stack_kb * 1024)
        elf = subprocess.check_output([prefix + "readelf", "-h", str(self.banner_image)], text=True)
        self.assertRegex(elf, r"Entry point address:\s+0x80000000")
        entry = subprocess.check_output([prefix + "objdump", "-d", str(self.banner_image)], text=True)
        entry = entry.split("<_entry>:", 1)[1].split("<park>:", 1)[0]
        self.assertRegex(entry, r"csrw\s+mie,zero")


if __name__ == "__main__":
    unittest.main(verbosity=2)

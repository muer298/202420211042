#!/usr/bin/env python3
"""Generate the embedded-program table with lab3 segment metadata."""
import argparse
from pathlib import Path
import subprocess

#用 nm 读取 main、data_start、end，检查入口为 0、data_start 按页对齐，然后生成包含这些元数据和 .bin 的 _uprog_table。Makefile 在 [`第 38–39 行` (line 38)](/Users/kugua/Desktop/操作系统实践/lab3/2024302111042-kernel/Makefile:38) 调用它生成
#所以运行时内核不需要从 .bin 猜哪段是代码或数据：构建时已经把边界随程序一起交给内核。
def symbols(nm, elf):
    found = {}
    output = subprocess.check_output([nm, "-n", str(elf)], text=True)
    for line in output.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] in {"main", "data_start", "end"}:
            found[parts[2]] = int(parts[0], 16)
    missing = {"main", "data_start", "end"} - found.keys()
    if missing:
        raise SystemExit(f"{elf}: missing symbols {sorted(missing)}")
    if found["main"] != 0:
        raise SystemExit(f"{elf}: main/entry must be zero, got {found['main']:#x}")
    if found["data_start"] % 4096:
        raise SystemExit(f"{elf}: data_start is not page aligned")
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nm", required=True)
    ap.add_argument("--output", required=True)
    ap.add_argument("programs", nargs="+")
    args = ap.parse_args()
    lines = [".section .rodata", ".global _uprog_table", "_uprog_table:"]
    for name in args.programs:
        meta = symbols(args.nm, Path("user-flat") / f"{name}.elf")
        lines += [
            f" .quad _uprog_{name}_start",
            f" .quad _uprog_{name}_end",
            f" .quad {meta['data_start']}",
            f" .quad {meta['end']}",
            f' .asciz "{name}"',
            " .balign 8",
            f".global _uprog_{name}_start",
            f"_uprog_{name}_start:",
            f' .incbin "user-flat/{name}.bin"',
            f".global _uprog_{name}_end",
            f"_uprog_{name}_end:",
            " .balign 8",
        ]
    lines += [" .quad 0", " .quad 0", ""]
    Path(args.output).write_text("\n".join(lines))


if __name__ == "__main__":
    main()

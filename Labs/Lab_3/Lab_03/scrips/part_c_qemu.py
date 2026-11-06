#!/usr/bin/env python3
from pathlib import Path

import pexpect


ROOT = Path("/home/elaina/embedded_lab3")
LOG = ROOT / "logs/part_c/qemu_arm_strace.log"

if LOG.exists():
    raise SystemExit(f"Refusing to overwrite {LOG}")

command = [
    "qemu-system-arm",
    "-M",
    "vexpress-a9",
    "-cpu",
    "cortex-a9",
    "-m",
    "512M",
    "-smp",
    "2",
    "-nographic",
    "-serial",
    "stdio",
    "-monitor",
    "none",
    "-kernel",
    "/home/elaina/embedded_lab1/output/zImage",
    "-dtb",
    "/home/elaina/embedded_lab1/output/vexpress-v2p-ca9.dtb",
    "-initrd",
    str(ROOT / "output/initramfs_lab3_part_c.cpio.gz"),
    "-append",
    "console=ttyAMA0,115200 rdinit=/sbin/init mem=512M",
    "-net",
    "none",
]

with LOG.open("x", encoding="utf-8") as logfile:
    qemu = pexpect.spawn(
        command[0],
        command[1:],
        encoding="utf-8",
        timeout=75,
        logfile=logfile,
    )
    try:
        qemu.expect_exact("Please press Enter to activate this console.")
        qemu.sendline("")
        qemu.expect_exact("/ # ")
        qemu.sendline("set -e")
        qemu.expect_exact("/ # ")
        qemu.sendline(
            "/usr/bin/strace -o /tmp/strace_arm_full.txt "
            "/usr/bin/perf_target_arm --iterations 2 --search-runs 20 "
            "--io-lines 1000; "
            "echo '--- guest strace full trace ---'; "
            "cat /tmp/strace_arm_full.txt; "
            "echo '--- guest strace summary ---'; "
            "/usr/bin/strace -c /usr/bin/perf_target_arm --iterations 2 "
            "--search-runs 20 --io-lines 1000; "
            "echo '--- guest file evidence ---'; "
            "wc -l /tmp/perf_test.txt; "
            "sha256sum /usr/bin/perf_target_arm /usr/bin/strace"
        )
        qemu.expect_exact("/ # ")
        qemu.sendline("poweroff -f")
        qemu.expect(pexpect.EOF, timeout=15)
    finally:
        qemu.close(force=True)

print(f"QEMU guest strace transcript: {LOG}")

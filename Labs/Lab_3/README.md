# LAB-03 — Kernel Debugging and Performance Analysis

**Course:** Embedded Linux Systems  
**Target platform:** ARMv7-A / Cortex-A9 (QEMU `vexpress-a9`)  
**Topics:** Remote GDB · Valgrind · strace · perf · Real-Time Scheduling

## 1. Overview

LAB-03 introduces debugging and performance analysis tools for embedded Linux. The lab covers remote debugging of an ARM application, detecting and fixing memory leaks, tracing system calls, profiling application performance, and measuring scheduling latency on a Linux host.

## 2. Learning Objectives

- Debug an ARM application remotely using GDB and `gdbserver`.
- Identify memory leaks using Valgrind and verify a corrected implementation.
- Analyze system calls and CPU profiling data with `strace` and `perf`.
- Measure scheduling latency with `cyclictest` under normal and stressed conditions.
- Understand the limitations of emulation and the role of PREEMPT_RT.

## 3. Repository Structure

```text
Lab_3/
├── README.md
├── apps/
│   ├── buggy_app.c
│   ├── leaky_app.c
│   ├── leaky_app_fixed.c
│   └── perf_target.c
├── scripts/
│   ├── part_a_debug.gdb
│   ├── part_b_host.sh
│   ├── part_b_proc_guest.sh
│   ├── part_c_host.sh
│   ├── part_c_qemu.py
│   ├── part_d_measure.sh
│   ├── part_d_distance_test.sh
│   └── part_d_plot.py
└── logs/
    ├── part_a/results.txt
    ├── part_b/results.txt
    ├── part_c/results.txt
    └── part_d/
        ├── results.txt
        └── latency_histogram.png
```

> This is the recommended GitHub subset of the lab workspace. Large root filesystems, downloaded tools, generated binaries, and intermediate build outputs are not included.

## 4. Development Environment

| Component | Environment |
| --- | --- |
| ARM target | ARMv7-A / Cortex-A9 |
| Emulator | QEMU `vexpress-a9` |
| Cross compiler | `arm-linux-gnueabihf-gcc` |
| Debugging | GDB / `gdbserver` |
| Memory analysis | Valgrind |
| Tracing and profiling | `strace`, `perf` |
| Latency measurement | `cyclictest`, `stress-ng` |
| Part D measurement host | x86_64 Linux (not QEMU) |

## 5. Lab Implementation

### Part A — Remote Debugging with GDB

Cross-compile `buggy_app.c` for ARM with debugging symbols, run the application under `gdbserver` in QEMU, and connect from host GDB. Use breakpoints and variable inspection to investigate the application behavior.

**Result:** ARM build, QEMU boot, `gdbserver` startup, and remote GDB breakpoints were verified. See `logs/part_a/results.txt`.

### Part B — Memory Leak Detection with Valgrind

Analyze `leaky_app.c` using Valgrind Memcheck, identify allocations that are not freed, and compare the result with `leaky_app_fixed.c`. Observe process memory information through `/proc` in the guest.

**Result:** The original application reported **16,900 bytes definitely lost in 250 blocks**. The corrected version reported **0 bytes in use at exit and 0 Memcheck errors**. See `logs/part_b/results.txt`.

### Part C — System Call Tracing and Performance Profiling

Use `perf_target.c` as the test workload. Inspect system-call behavior using `strace` and collect profiling information using `perf`. The lab includes both host measurements and a separate ARM QEMU workload; results from the two environments are distinguished.

**Result:** The workload executed successfully on host and ARM QEMU with matching outputs for the scaled comparison run. See `logs/part_c/results.txt`.

### Part D — Real-Time Scheduling and Latency

Use `cyclictest` to measure thread wake-up latency, compare baseline and stressed runs, and visualize the latency distribution. Review Linux scheduling behavior and PREEMPT_RT concepts.

**Result:** Host-side latency measurements were collected using `cyclictest` and `stress-ng`. The tested host kernel was **not PREEMPT_RT**. See `logs/part_d/results.txt` and `logs/part_d/latency_histogram.png`.

## 6. Results and Notes

- Remote ARM debugging was exercised inside QEMU using `gdbserver` and host GDB.
- Valgrind verified the memory-leak fix on the host.
- Profiling and tracing results depend on the execution environment and workload.
- Real-time latency measurements were performed on the **Linux host**, not the emulated ARM guest; they do not establish hard real-time guarantees.
- The full lab workspace contains additional test logs and runtime artifacts that are intentionally excluded from this compact GitHub version.

## 7. References

- Linux `gdbserver`, `strace`, `perf`, and `cyclictest` documentation.
- Valgrind Memcheck documentation.
- Embedded Linux Systems — LAB-03 assignment materials.

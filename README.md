# Embedded Linux Systems

A collection of laboratory exercises and assignments for the **Embedded Linux Systems** course. The project explores building an embedded Linux environment, developing kernel drivers, debugging applications, and integrating a multi-threaded monitoring system on an ARM target.

## Development Environment

- **Target architecture:** ARMv7-A (Cortex-A9)
- **Emulation:** QEMU `vexpress-a9`
- **Operating system:** Embedded Linux
- **Core tools:** Linux Kernel, U-Boot, BusyBox, ARM cross-compiler
- **Programming:** C, Shell scripting

## Laboratory Exercises

### LAB-01 — Linux Kernel Configuration and Boot

- Configure and build the Linux kernel for ARM.
- Build U-Boot and prepare the Device Tree.
- Create a minimal root filesystem using BusyBox and initramfs.
- Boot and explore embedded Linux using QEMU.

### LAB-02 — Device Driver and File System Management

- Develop a Linux character device driver with `open()`, `read()`, `write()`, and `release()`.
- Transfer data between kernel space and user space.
- Expose driver information through procfs and sysfs.
- Explore MTD, JFFS2, and BusyBox initialization.

### LAB-03 — Kernel Debugging and Performance Analysis

- Debug ARM applications remotely using GDB and gdbserver.
- Detect memory problems using Valgrind.
- Trace system calls and profile performance with strace and perf.
- Measure scheduling latency with cyclictest and study PREEMPT_RT.

## Assignments

### Assignment 01 — How to Write a Device Driver

- Extend a character device driver with an `ioctl` interface.
- Implement a counter device using the Linux `miscdevice` framework.
- Apply synchronization and atomic operations.
- Analyze and compare Linux device driver architectures.

### Assignment 02 — Sensor Monitoring System

- Develop a simulated sensor kernel driver.
- Build a multi-threaded monitoring application with POSIX threads.
- Use shared buffers, mutexes, condition variables, and message queues.
- Process sensor readings, generate alerts, and record logs.
- Debug, profile, and evaluate real-time behavior.

## Learning Path

```text
LAB-01: Linux Boot and Root Filesystem
                  |
                  v
LAB-02: Kernel Modules and Device Drivers
                  |
                  v
LAB-03: Debugging and Performance Analysis
                  |
                  v
Assignment 01: Advanced Device Drivers
                  |
                  v
Assignment 02: Sensor Monitoring System
```

## About

This repository is intended for educational use and documents the main topics covered by the Embedded Linux Systems labs and assignments.

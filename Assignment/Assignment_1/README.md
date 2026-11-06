# Assignment 01 — Linux Character Device Drivers

**Course:** Embedded Linux Systems  
**Target platform:** ARMv7 / QEMU VExpress-A9, Linux 5.15  
**Topics:** Character devices · IOCTL · Misc devices · procfs · Kernel synchronization

## 1. Overview

Assignment 01 focuses on implementing and testing Linux kernel character device drivers. It includes an IOCTL-based character driver and a misc-device counter driver, both tested in an ARM QEMU environment.

## 2. Objectives

- Implement kernel module initialization, cleanup, and character-device operations.
- Exchange data between kernel space and user space through `read()`, `write()`, and `ioctl()`.
- Protect shared driver state using appropriate synchronization.
- Register a misc device and expose runtime statistics through `/proc`.
- Verify driver functionality and module cleanup in QEMU.

## 3. Repository Structure

```text
Assignment_1/
├── README.md
├── task1_ioctl/
│   ├── asgn1_driver.c
│   ├── asgn1_ioctl.h
│   ├── Makefile
│   └── test_asgn1.sh
├── task2_misc/
│   ├── counter_driver.c
│   ├── Makefile
│   └── test_counter.sh
├── logs/
│   ├── test_asgn1_output.txt
│   └── test_counter_output.txt
└── report/
    └── SE204445_Asgn01_BaoCao.pdf
```

> This structure contains the selected source files, test scripts, representative test logs, and report. Generated kernel build files and submission archives are excluded.

## 4. Implementation

### Task 1 — Character Device Driver with IOCTL

Implemented `asgn1_driver.c`, a character device driver supporting buffered reads and writes, mutex-protected shared state, and four IOCTL operations:

- `ASGN1_RESET_BUFFER` — clear the stored data.
- `ASGN1_GET_STATS` — retrieve open, read, and write statistics.
- `ASGN1_SET_MODE` — change the driver's data-writing mode.
- `ASGN1_GET_VERSION` — retrieve the driver version.

The shared IOCTL definitions are provided in `asgn1_ioctl.h`.

**Result:** The retained ARM QEMU test log reports `ASGN1_QEMU_RESULT=PASS`, including device operations, supported IOCTL commands, handling of an unsupported command, and module cleanup. See `logs/test_asgn1_output.txt`.

### Task 2 — Misc Device Counter

Implemented `counter_driver.c` using the Linux misc-device framework. The driver exposes `/dev/counter` for reading and updating a non-negative counter and `/proc/counter_info` for inspecting the current value and read/write totals. Atomic variables maintain the shared counters.

**Result:** The retained ARM QEMU test log reports that Task 2 tests passed and shows successful registration and removal of `/dev/counter`. See `logs/test_counter_output.txt`.

## 5. Build and Testing

Both tasks provide their own `Makefile` and test script. The Makefiles use the ARM cross-compiler prefix `arm-linux-gnueabihf-` and expect a configured Linux 5.15 kernel build tree.

```bash
# Build Task 1 (from the task1_ioctl directory)
make

# Build Task 2 (from the task2_misc directory)
make
```

> Update `KDIR` in each Makefile to match your local Linux kernel source/build directory before compiling. The test scripts depend on the QEMU environment used for the assignment.

## 6. Results

| Task | Main functionality | Recorded result |
| --- | --- | --- |
| Task 1 | Character driver, buffered I/O, IOCTL, statistics | PASS in ARM QEMU |
| Task 2 | Misc-device counter and `/proc` statistics | PASS in ARM QEMU |

Detailed explanations and evidence are provided in `report/SE204445_Asgn01_BaoCao.pdf` and the selected files under `logs/`.

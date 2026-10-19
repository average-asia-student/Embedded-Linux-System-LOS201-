# LAB-02: Linux Character Device Driver & File System Interfaces

**Course:** Embedded Linux Systems  
**Target platform:** ARMv7-A / Cortex-A9 (QEMU `vexpress-a9`)  
**Topics:** Linux Kernel Modules · Character Devices · procfs · sysfs · Cross-Compilation

## 1. Overview

LAB-02 focuses on Linux kernel device drivers and file system interfaces. The assignment also covers MTD/JFFS2 flash file systems and BusyBox initialization. **This repository currently contains the character device driver implementation and its Makefile; MTD/JFFS2 and BusyBox integration artifacts are not included.**

The driver registers a character device named `lab2`, provides basic read/write operations through `/dev/lab2`, and exposes internal state through procfs and sysfs.

## 2. Learning Objectives

- Understand the kernel module lifecycle (`module_init` and `module_exit`).
- Implement `open`, `read`, `write`, and `release` through `file_operations`.
- Transfer data between user space and kernel space with `copy_to_user()` and `copy_from_user()`.
- Expose driver statistics through `/proc` and device attributes through `/sys`.
- Cross-compile an out-of-tree kernel module for ARMv7 using KBuild.

## 3. Repository Structure

```text
Lab_2/
├── README.md
└── Lab_02/
    └── driver/
        ├── lab2_driver.c    # Character device driver + procfs/sysfs
        └── Makefile         # ARM cross-compilation via kernel KBuild
```

> The uploaded archive does not include a compiled `.ko` file, test scripts, QEMU boot artifacts, screenshots, or runtime logs.

## 4. Development Environment

| Component | Configuration |
| --- | --- |
| Target architecture | ARMv7-A (Cortex-A9) |
| Emulator used in assignment | QEMU `vexpress-a9` |
| Kernel source expected by Makefile | `~/embedded_lab1/kernel/linux-5.15` |
| Cross-compiler prefix | `arm-linux-gnueabihf-` |
| Kernel build system | KBuild / Make |
| Module source | `Lab_02/driver/lab2_driver.c` |

**Prerequisite:** The kernel source/build tree from LAB-01 must be available and configured for the target kernel. A module must be built against the kernel configuration and version used at runtime.

## 5. Driver Implementation

### 5.1 Character Device

| Property | Value |
| --- | --- |
| Device name | `lab2` |
| Device node | `/dev/lab2` |
| Class name | `lab2_class` |
| Major number | `240` (fixed in source) |
| Minor number | `0` |
| Internal buffer | `1024` bytes |
| Maximum accepted write | `1023` bytes (longer writes are truncated) |

The module registers its character device with `register_chrdev()`, creates a device class and device object, and unregisters them during module removal.

The `file_operations` structure implements:

- **`open()`** — increments `open_count`.
- **`read()`** — copies buffered data to user space, respecting the file offset and returning EOF when appropriate.
- **`write()`** — copies data from user space into the internal buffer, with a size limit.
- **`release()`** — logs device closure.

A mutex protects the open counter update and the write operation. This implementation is a learning example, not a fully concurrency-hardened production driver.

### 5.2 procfs

The driver creates the read-only entry:

```text
/proc/lab2_info
```

It reports the driver name, major number, buffer capacity, current data length, number of opens, and last written data (when present).

### 5.3 sysfs

The driver exposes three **read-only** attributes under:

```text
/sys/class/lab2_class/lab2/
├── buffer_len
├── open_count
└── last_data
```

These attributes report the current data length, device open count, and most recent buffered text.

## 6. Build Instructions

Install the ARM cross-compiler and required kernel build dependencies on the Linux host, and ensure the LAB-01 Linux 5.15 kernel tree is prepared.

From the repository root:

```bash
cd Lab_02/driver
make
```

The Makefile uses:

```makefile
KDIR := $(HOME)/embedded_lab1/kernel/linux-5.15
ARCH := arm
CROSS_COMPILE := arm-linux-gnueabihf-
```

If your kernel source is in a different directory, update `KDIR` in `Makefile` before building.

Expected build artifact **if compilation succeeds**:

```text
Lab_02/driver/lab2_driver.ko
```

Inspect the module with:

```bash
file lab2_driver.ko
modinfo lab2_driver.ko
modinfo lab2_driver.ko | grep vermagic
```

The module architecture and `vermagic` must match the target kernel. To remove generated build files:

```bash
make clean
```

## 7. Loading and Testing on the Target

The following commands are **suggested verification steps**, not recorded test results from this repository. Run them as root **inside a compatible ARM Linux target**, not directly on an x86 host.

Copy the compiled `.ko` file into the target root filesystem, then load it from its actual location. For example, if placed in `/lib/modules/5.15.0/`:

```sh
insmod /lib/modules/5.15.0/lab2_driver.ko
lsmod | grep lab2_driver
```

Ensure the device node exists. If automatic device-node creation is unavailable:

```sh
mknod /dev/lab2 c 240 0
```

Test writing and reading:

```sh
echo 'Hello from LAB-02' > /dev/lab2
cat /dev/lab2
```

Inspect procfs and sysfs:

```sh
cat /proc/lab2_info
cat /sys/class/lab2_class/lab2/buffer_len
cat /sys/class/lab2_class/lab2/open_count
cat /sys/class/lab2_class/lab2/last_data
```

Inspect kernel messages and unload the module:

```sh
dmesg | grep lab2_driver
rmmod lab2_driver
```

> `procfs` and `sysfs` must be mounted for their entries to be accessible. Device registration may fail if major number `240` is already in use. The provided source uses the Linux 5.15-era `class_create(THIS_MODULE, ...)` API and may require changes for newer kernels.

## 8. Implementation Status

| Assignment area | Repository evidence | Status |
| --- | --- | --- |
| Part A: Character device driver | `lab2_driver.c`, `Makefile` | Source included |
| Part B: procfs and sysfs | Implemented in `lab2_driver.c` | Source included |
| Part C: MTD / nandsim / JFFS2 | No related files in archive | Not included |
| Part D: BusyBox init integration | No init scripts or updated rootfs in archive | Not included |
| Runtime verification | No compiled module, logs or screenshots | Not verified |

## 9. Notes and Limitations

- The module stores only the latest written data in a fixed-size memory buffer.
- `open_count` tracks successful calls to the driver's `open()` callback; there are no read/write counters in this version.
- The sysfs attributes are read-only; there is no sysfs `store()` implementation.
- The driver uses a fixed major number rather than dynamically allocating one.
- The source has not been independently compiled or executed as part of this README preparation.

## 10. References

- [Linux Kernel Documentation](https://www.kernel.org/doc/html/latest/)
- [Linux Kernel Driver API](https://www.kernel.org/doc/html/latest/driver-api/)
- [Linux procfs Documentation](https://www.kernel.org/doc/html/latest/filesystems/proc.html)
- [Linux sysfs Documentation](https://www.kernel.org/doc/html/latest/filesystems/sysfs.html)
- [QEMU ARM System Emulator](https://www.qemu.org/docs/master/system/target-arm.html)

---

*Educational project for the Embedded Linux Systems course — LAB-02.*

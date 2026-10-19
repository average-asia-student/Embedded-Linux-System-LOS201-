# LAB-01 — Embedded Linux Kernel Configuration and Boot

## Overview

This lab explores the boot process of an embedded Linux system on an **ARMv7 Cortex-A9** target emulated with **QEMU `vexpress-a9`**. It covers cross-compiling a Linux kernel, preparing U-Boot, building a minimal BusyBox root filesystem, and packaging an initramfs.

The repository contains the kernel image, device tree, U-Boot executable, initramfs archive, build configurations, and selected root filesystem files.

## Objectives

- Configure and cross-compile a Linux kernel for ARMv7.
- Build U-Boot for the Versatile Express Cortex-A9 platform.
- Prepare a minimal root filesystem using BusyBox.
- Package the root filesystem as an initramfs.
- Boot and inspect the system using QEMU.

## Development Environment

| Component | Lab specification |
| --- | --- |
| Host OS | Ubuntu 20.04 / 22.04 LTS (64-bit) |
| Target architecture | ARMv7-A, Cortex-A9 |
| Emulator | QEMU `vexpress-a9` |
| Cross compiler | `arm-linux-gnueabihf-` |
| Linux kernel source | 5.15 LTS |
| U-Boot source | 2022.04 |
| BusyBox source | 1.35.0 |
| Console | `ttyAMA0`, 115200 baud |

> These versions are specified by the lab handout. The archive does not include a host environment log or complete build history to independently confirm every installed version.

## Repository Structure

```text
Lab01/
├── configs/
│   ├── kernel.config
│   └── busybox.config
├── output/
│   ├── zImage
│   ├── vexpress-v2p-ca9.dtb
│   ├── initramfs.cpio.gz
│   └── u-boot
└── rootfs/
    └── initramfs/
        ├── bin/
        │   └── busybox
        ├── dev/
        ├── etc/
        │   ├── hostname
        │   ├── inittab
        │   ├── passwd
        │   └── init.d/
        │       └── rcS
        ├── init
        ├── lib/
        ├── proc/
        ├── sbin/
        ├── sys/
        ├── tmp/
        └── usr/
```

**Key files:** `zImage` is the compressed ARM kernel; the `.dtb` describes the emulated hardware; `initramfs.cpio.gz` provides the initial userspace; `u-boot` is a separate bootloader executable. The `configs/` directory stores kernel and BusyBox configurations.

## Boot Sequence

```text
QEMU vexpress-a9
    |
    +-- zImage (Linux kernel)
    +-- vexpress-v2p-ca9.dtb (device tree)
    +-- initramfs.cpio.gz (initial filesystem)
    |
    v
Linux kernel initialization
    |
    v
BusyBox init (/sbin/init)
    |
    v
/etc/inittab -> /etc/init.d/rcS
    |
    v
BusyBox shell
```

The direct-QEMU method below **does not execute U-Boot**. U-Boot is included as a separately built artifact; a complete U-Boot-to-Linux boot has not been established from the submitted files alone.

## Implementation

### Part A — Linux Kernel

The kernel is configured for the ARM Versatile Express platform and cross-compiled to produce `zImage` and `vexpress-v2p-ca9.dtb`.

```bash
export ARCH=arm
export CROSS_COMPILE=arm-linux-gnueabihf-
cd ~/embedded_lab1/kernel/linux-5.15
make vexpress_defconfig
make menuconfig
make -j$(nproc) zImage dtbs
```

The generated artifacts are included in `Lab01/output/`. The saved kernel configuration is in `Lab01/configs/kernel.config`.

### Part B — U-Boot

The submitted `output/u-boot` file is a U-Boot build artifact. The lab's build procedure uses:

```bash
export ARCH=arm
export CROSS_COMPILE=arm-linux-gnueabihf-
cd ~/embedded_lab1/uboot/u-boot-2022.04
make vexpress_ca9x4_defconfig
make -j$(nproc)
```

To inspect the bootloader console separately:

```bash
qemu-system-arm \
  -M vexpress-a9 \
  -cpu cortex-a9 \
  -m 512M \
  -nographic \
  -kernel Lab01/output/u-boot
```

The bootloader shell can be used to explore commands such as `version`, `bdinfo`, and `printenv`. This is **not** the same as verifying a full kernel boot through U-Boot.

### Part C — BusyBox Root Filesystem

The repository includes a BusyBox binary and these startup files:

- `/init`: mounts `proc` and `sysfs`, then executes `/sbin/init`.
- `/etc/inittab`: starts `/etc/init.d/rcS` and provides an interactive shell.
- `/etc/init.d/rcS`: mounts `proc`, `sysfs`, and `tmpfs`, displays a boot message, and sets the hostname.
- `/etc/hostname`: sets the hostname to `embedded-lab`.

The packaged root filesystem is `Lab01/output/initramfs.cpio.gz`.

## Run with QEMU

Install the required tools on an Ubuntu host:

```bash
sudo apt update
sudo apt install -y qemu-system-arm cpio gzip file
```

From the **parent directory containing `Lab01/`**, run:

```bash
qemu-system-arm \
  -M vexpress-a9 \
  -cpu cortex-a9 \
  -m 512M \
  -smp 2 \
  -nographic \
  -kernel Lab01/output/zImage \
  -dtb Lab01/output/vexpress-v2p-ca9.dtb \
  -initrd Lab01/output/initramfs.cpio.gz \
  -append 'console=ttyAMA0,115200 rdinit=/sbin/init mem=512M'
```

This command uses the prebuilt files committed in this lab. If the BusyBox `askfirst` prompt appears, press **Enter** to access the shell. To exit QEMU in `-nographic` mode, press **Ctrl+A**, then **X**.

### Expected Output

The startup script contains the following message:

```text
Boot completed! Welcome to Embedded Linux Lab 1
```

This message is present in the submitted `rcS` file; it is an **expected** console output, not independent proof of a successful boot.

## Verification

Run the following commands **inside the QEMU guest** after reaching the shell:

```sh
uname -a
cat /proc/cpuinfo
cat /proc/meminfo | head -5
hostname
mount
ps
cat /sys/devices/system/cpu/possible
```

Expected checks: ARMv7 architecture, Cortex-A9 emulation, hostname `embedded-lab`, mounted pseudo-filesystems, and the configured CPU count. Actual command output and screenshots should be recorded separately as evidence.

## Notes and Limitations

- The archive includes prebuilt binaries and configuration files, but not the complete Linux, U-Boot, or BusyBox source trees.
- A U-Boot executable is present, but a complete boot **through U-Boot** has not been verified from the archive.
- No runtime screenshots or QEMU console logs were included in the submitted `Lab01/` folder, so boot success is not claimed solely from file presence.
- The root filesystem directory is a selected file tree; the packaged initramfs is the intended image for the QEMU command above.

## References

- [Linux Kernel Documentation](https://www.kernel.org/doc/html/latest/)
- [U-Boot Documentation](https://u-boot.readthedocs.io/)
- [BusyBox](https://busybox.net/)
- [QEMU ARM System Emulator](https://www.qemu.org/docs/master/system/arm/)

---

*Embedded Linux Systems — LAB-01: Kernel Configuration and Embedded System Boot.*

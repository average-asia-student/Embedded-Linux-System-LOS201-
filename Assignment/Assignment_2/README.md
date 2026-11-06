# Assignment 02 – Embedded Linux Sensor Monitoring System

## Overview

This assignment implements a simulated Sensor Monitoring System (SMS) on an ARM-based Embedded Linux platform. The system integrates a Linux character device driver with a multithreaded userspace application to acquire, process, monitor, and log sensor data.

The project is developed and tested using Linux 5.15.0 on ARMv7 with QEMU VExpress-A9.

## Objectives

- Develop a Linux character device driver for simulated temperature and humidity sensors.
- Implement communication between kernel space and userspace using `read()`, `ioctl()`, and `/proc`.
- Build a multithreaded application using POSIX threads.
- Apply mutexes, condition variables, circular buffers, and POSIX message queues.
- Analyze memory usage, synchronization, and application performance.
- Evaluate real-time scheduling and sensor sampling jitter.

## Project Structure

```text
Assignment_2/
├── driver/
│   ├── sms_sensor_driver.c
│   ├── sms_sensor.h
│   └── Makefile
├── app/
│   ├── sms_app.c
│   ├── sms_app.h
│   ├── sensor_thread.c
│   ├── processor_thread.c
│   ├── alert_thread.c
│   ├── logger_thread.c
│   ├── circular_buffer.c
│   └── tests/
├── logs/
├── report/
│   └── SE204445_TongTranDang_Asgn02_BaoCao.pdf
└── README.md
```

*The structure above represents the recommended GitHub layout. Additional headers, build scripts, and test files should be retained where required.*

## Implementation

### Task 1 – Sensor Driver and Multithreaded Application

- Implemented a character device at `/dev/sms_sensor`.
- Provided sensor statistics through `/proc/sms_stats`.
- Supported sampling interval configuration using `ioctl()`.
- Developed four POSIX worker threads:
  - **Sensor Thread:** Reads sensor data and stores samples in a circular buffer.
  - **Processor Thread:** Calculates rolling averages and detects abnormal readings.
  - **Alert Thread:** Handles temperature, humidity, and spike alerts.
  - **Logger Thread:** Records sensor readings and alerts.
- Implemented synchronization using mutexes, condition variables, and POSIX message queues.

### Task 2 – Memory Management and Driver Integration

- Analyzed application memory usage using Valgrind and Linux `/proc` interfaces.
- Tested sampling interval configuration and application error handling.
- Evaluated driver read counts and application processing statistics.

### Task 3 – Debugging and Performance Profiling

- Used GDB to inspect thread states and circular-buffer operations.
- Used `strace` to analyze system calls.
- Used `perf` to examine application performance.
- Conducted host-side debugging and profiling with a simulated sensor interface.

### Task 4 – Real-Time Scheduling and Jitter Analysis

- Implemented `SCHED_FIFO` and `SCHED_OTHER` scheduling modes.
- Used `CLOCK_MONOTONIC` and absolute-time sleeping for periodic sampling.
- Measured Sensor Thread wake-up jitter under ARM QEMU.
- Compared average, maximum, and P99 jitter between scheduling policies.

## Testing and Results

| Test | Result |
|---|---|
| ARM QEMU integration | 119 samples processed in 60 seconds |
| Circular buffer overflow | 0 |
| Valgrind memory errors | 0 |
| Valgrind memory leaks | 0 bytes |
| GDB debugging | Thread states captured; race-debug evidence partial |
| strace and perf | Completed on host x86_64 with sensor mock |
| SCHED_FIFO average jitter | 5.861 ms |
| SCHED_OTHER average jitter | 5.730 ms |
| Scheduling comparison | 1,000 jitter samples per policy |

**Known limitations:**
- A shutdown-boundary test reproduced the possibility of losing an in-flight sample. The production fix has not been implemented.
- Task 2 driver integration and part of Task 3 debugging remain partially verified.
- QEMU timing results do not represent guaranteed real-time performance on physical ARM hardware.

## Development Environment

- **Target:** ARMv7 – QEMU VExpress-A9
- **Kernel:** Linux 5.15.0
- **Languages:** C, Shell
- **Tools:** GCC, Make, GDB, Valgrind, strace, perf, QEMU
- **Libraries/APIs:** POSIX Threads, POSIX Message Queues, Linux Kernel APIs

## References

- *Embedded Linux Primer* – Christopher Hallinan
- *Linux Device Drivers* – Jonathan Corbet, Alessandro Rubini, Greg Kroah-Hartman
- *The Linux Programming Interface* – Michael Kerrisk
- Linux Kernel Documentation and Linux man-pages

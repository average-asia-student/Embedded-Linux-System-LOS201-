#!/bin/sh

if [ "${1:-}" = "--guest" ]; then
    if [ ! -c /dev/asgn1 ]; then
        mknod /dev/asgn1 c 241 0
    fi

    if /asgn1_ioctl_test; then
        result=0
    else
        result=1
    fi

    if ! rmmod asgn1_driver; then
        echo "FAIL: failed to unload asgn1_driver"
        result=1
    fi

    echo "----- dmesg after driver cleanup -----"
    dmesg
    if [ "$result" -eq 0 ]; then
        echo "ASGN1_QEMU_RESULT=PASS"
    else
        echo "ASGN1_QEMU_RESULT=FAIL"
    fi
    poweroff -f
    exit "$result"
fi

set -eu

TASK_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_DIR=$(CDPATH= cd -- "$TASK_DIR/.." && pwd)
INITRAMFS=/home/elaina/embedded_lab2/output/initramfs.cpio.gz
DTB=/home/elaina/embedded_lab2/output/vexpress-v2p-ca9.dtb
KERNEL=/home/elaina/embedded_lab2/output/zImage
LOG_DIR="$PROJECT_DIR/logs"
LOG_FILE="$LOG_DIR/test_asgn1_output.txt"
TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/asgn1-test.XXXXXX")

cleanup()
{
    rm -f -- "$TMP_DIR/asgn1_driver.ko" \
        "$TMP_DIR/asgn1_ioctl_test.c" "$TMP_DIR/asgn1_ioctl_test" \
        "$TMP_DIR/init" "$TMP_DIR/test_asgn1.sh" \
        "$TMP_DIR/overlay.cpio" "$TMP_DIR/initrd"
    rmdir -- "$TMP_DIR"
}
trap cleanup EXIT

if [ ! -f "$TASK_DIR/asgn1_driver.ko" ]; then
    echo "Build the module first with: make -C \"$TASK_DIR\"" >&2
    exit 1
fi
for asset in "$INITRAMFS" "$DTB" "$KERNEL"; do
    if [ ! -f "$asset" ]; then
        echo "Required existing LAB-02 asset is missing: $asset" >&2
        exit 1
    fi
done

cat > "$TMP_DIR/asgn1_ioctl_test.c" <<'EOF'
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "asgn1_ioctl.h"

static int check_data(int fd, const char *expected)
{
    char buffer[128];
    size_t expected_size = strlen(expected);
    ssize_t bytes_read;

    if (lseek(fd, 0, SEEK_SET) < 0)
        return -1;
    bytes_read = read(fd, buffer, sizeof(buffer));
    if (bytes_read != (ssize_t)expected_size ||
        memcmp(buffer, expected, expected_size) != 0) {
        fprintf(stderr, "FAIL: read data did not match expected value\n");
        return -1;
    }
    buffer[bytes_read] = '\0';
    printf("Read verified: [%s]\n", buffer);
    return 0;
}

static void print_stats(const struct asgn1_stats *stats)
{
    printf("open_count=%d\n", stats->open_count);
    printf("write_count=%d\n", stats->write_count);
    printf("read_count=%d\n", stats->read_count);
    printf("buffer_len=%d\n", stats->buffer_len);
    printf("last_write_size=%d\n", stats->last_write_size);
}

int main(void)
{
    const char echo_data[] = "echo-mode";
    const char kernel_data[] = "kernel-mode";
    const char kernel_result[] = "[KERNEL]kernel-mode";
    struct asgn1_stats stats;
    char version[ASGN1_VERSION_SIZE];
    int fd;
    int mode;
    char byte;

    fd = open("/dev/asgn1", O_RDWR);
    if (fd < 0) {
        perror("open /dev/asgn1");
        return 1;
    }
    if (ioctl(fd, ASGN1_RESET_BUFFER) < 0) {
        perror("ASGN1_RESET_BUFFER");
        return 1;
    }
    if (ioctl(fd, ASGN1_GET_VERSION, version) < 0) {
        perror("ASGN1_GET_VERSION");
        return 1;
    }
    if (strcmp(version, "asgn1_driver v1.0") != 0) {
        fprintf(stderr, "FAIL: unexpected version: %s\n", version);
        return 1;
    }
    printf("Version: %s\n", version);

    mode = 1;
    if (ioctl(fd, ASGN1_SET_MODE, &mode) < 0) {
        perror("ASGN1_SET_MODE echo");
        return 1;
    }
    if (write(fd, echo_data, sizeof(echo_data) - 1) !=
        (ssize_t)(sizeof(echo_data) - 1)) {
        perror("write echo-mode data");
        return 1;
    }
    if (check_data(fd, echo_data) < 0)
        return 1;
    if (ioctl(fd, ASGN1_GET_STATS, &stats) < 0) {
        perror("ASGN1_GET_STATS");
        return 1;
    }
    print_stats(&stats);
    if (stats.open_count != 1 || stats.write_count != 1 ||
        stats.read_count != 1 || stats.buffer_len != sizeof(echo_data) - 1 ||
        stats.last_write_size != sizeof(echo_data) - 1) {
        fprintf(stderr, "FAIL: echo-mode stats did not match\n");
        return 1;
    }
    printf("PASS: GET_STATS and echo mode\n");

    mode = 0;
    if (ioctl(fd, ASGN1_SET_MODE, &mode) < 0) {
        perror("ASGN1_SET_MODE kernel");
        return 1;
    }
    if (write(fd, kernel_data, sizeof(kernel_data) - 1) !=
        (ssize_t)(sizeof(kernel_data) - 1)) {
        perror("write kernel-mode data");
        return 1;
    }
    if (check_data(fd, kernel_result) < 0)
        return 1;
    printf("PASS: kernel mode prefix\n");

    if (ioctl(fd, ASGN1_RESET_BUFFER) < 0) {
        perror("ASGN1_RESET_BUFFER final");
        return 1;
    }
    if (lseek(fd, 0, SEEK_SET) < 0 || read(fd, &byte, 1) != 0) {
        fprintf(stderr, "FAIL: buffer was not empty after reset\n");
        return 1;
    }
    if (ioctl(fd, ASGN1_GET_STATS, &stats) < 0) {
        perror("ASGN1_GET_STATS after reset");
        return 1;
    }
    if (stats.write_count != 0 || stats.read_count != 0 ||
        stats.buffer_len != 0 || stats.last_write_size != 0) {
        fprintf(stderr, "FAIL: reset did not clear buffer counters\n");
        return 1;
    }
    printf("PASS: reset clears buffer and counters\n");

    errno = 0;
    if (ioctl(fd, _IO(ASGN1_IOC_MAGIC, 99)) != -1 || errno != ENOTTY) {
        fprintf(stderr, "FAIL: unsupported ioctl did not return ENOTTY\n");
        return 1;
    }
    printf("PASS: unsupported ioctl returns ENOTTY\n");

    if (close(fd) < 0) {
        perror("close");
        return 1;
    }
    puts("ASGN1_TEST_RESULT=PASS");
    return 0;
}
EOF

cat > "$TMP_DIR/init" <<'EOF'
#!/bin/sh
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
if ! insmod /asgn1_driver.ko; then
    echo "ASGN1_QEMU_RESULT=FAIL (module load)"
    poweroff -f
fi
/bin/sh /test_asgn1.sh --guest
EOF

chmod 755 "$TMP_DIR/init"
cp "$TASK_DIR/test_asgn1.sh" "$TMP_DIR/test_asgn1.sh"
cp "$TASK_DIR/asgn1_driver.ko" "$TMP_DIR/asgn1_driver.ko"

CROSS_COMPILE=${CROSS_COMPILE:-arm-linux-gnueabihf-}
"${CROSS_COMPILE}gcc" -static -Wall -Wextra -Werror \
    -I"$TASK_DIR" "$TMP_DIR/asgn1_ioctl_test.c" \
    -o "$TMP_DIR/asgn1_ioctl_test"

python3 - "$TMP_DIR" "$TMP_DIR/overlay.cpio" <<'PY'
import os
import sys

root, archive_path = sys.argv[1:]
entries = [
    ("init", 0o100755),
    ("test_asgn1.sh", 0o100755),
    ("asgn1_driver.ko", 0o100644),
    ("asgn1_ioctl_test", 0o100755),
]

def emit_entry(output, name, mode, data, inode):
    encoded_name = name.encode() + b"\0"
    fields = (
        inode, mode, 0, 0, 1, 0, len(data), 0, 0, 0, 0,
        len(encoded_name), 0,
    )
    output.write(b"070701" + b"".join(f"{value:08x}".encode() for value in fields))
    output.write(encoded_name)
    output.write(b"\0" * ((-(110 + len(encoded_name))) % 4))
    output.write(data)
    output.write(b"\0" * ((-len(data)) % 4))

with open(archive_path, "wb") as output:
    for inode, (name, mode) in enumerate(entries, 1):
        with open(os.path.join(root, name), "rb") as source:
            emit_entry(output, name, mode, source.read(), inode)
    emit_entry(output, "TRAILER!!!", 0, b"", len(entries) + 1)
PY

cat "$INITRAMFS" "$TMP_DIR/overlay.cpio" > "$TMP_DIR/initrd"
mkdir -p "$LOG_DIR"
echo "Running Assignment 01 Task 1 tests in QEMU..." | tee "$LOG_FILE"
if timeout 120s qemu-system-arm \
    -M vexpress-a9 -cpu cortex-a9 -m 256M \
    -kernel "$KERNEL" -dtb "$DTB" -initrd "$TMP_DIR/initrd" \
    -append "console=ttyAMA0,115200 rdinit=/init" \
    -nographic -no-reboot >> "$LOG_FILE" 2>&1; then
    qemu_status=0
else
    qemu_status=$?
fi
cat "$LOG_FILE"

if [ "$qemu_status" -eq 0 ] &&
   grep -q '^ASGN1_QEMU_RESULT=PASS' "$LOG_FILE"; then
    echo "Task 1 QEMU tests passed. Full output: $LOG_FILE" | tee -a "$LOG_FILE"
else
    echo "Task 1 QEMU tests failed (QEMU exit status: $qemu_status). See: $LOG_FILE" |
        tee -a "$LOG_FILE" >&2
    exit 1
fi

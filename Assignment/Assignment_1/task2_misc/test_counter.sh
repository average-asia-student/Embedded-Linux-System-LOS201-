#!/bin/sh

if [ "${1:-}" = "--guest" ]; then
    fail()
    {
        echo "FAIL: $*"
        result=1
    }

    result=0
    if [ ! -c /dev/counter ]; then
        fail "/dev/counter is missing"
    fi
    if ! grep -q '^ *10 misc$' /proc/devices; then
        fail "misc major 10 is not registered"
    fi
    if ! grep -q '^counter_driver ' /proc/modules; then
        fail "counter_driver is not listed in /proc/modules"
    fi
    ls -l /dev/counter
    if command -v lsmod >/dev/null 2>&1; then
        lsmod
    fi

    echo "----- COUNTER_INFO_INITIAL_BEGIN -----"
    cat /proc/counter_info
    echo "----- COUNTER_INFO_INITIAL_END -----"

    value=$(cat /dev/counter) || fail "initial read failed"
    [ "$value" = "0" ] || fail "first read returned [$value], expected [0]"
    value=$(cat /dev/counter) || fail "second read failed"
    [ "$value" = "1" ] || fail "second read returned [$value], expected [1]"
    echo "PASS: read returns current value and advances once per cat"

    echo '100' > /dev/counter || fail "write of 100 failed"
    echo "----- COUNTER_INFO_AFTER_WRITE_BEGIN -----"
    cat /proc/counter_info
    echo "----- COUNTER_INFO_AFTER_WRITE_END -----"
    value=$(cat /dev/counter) || fail "read after setting 100 failed"
    [ "$value" = "100" ] ||
        fail "read after setting 100 returned [$value]"
    echo "PASS: write sets counter to 100"

    echo '0' > /dev/counter || fail "write reset to 0 failed"
    value=$(cat /dev/counter) || fail "read after reset failed"
    [ "$value" = "0" ] || fail "read after reset returned [$value]"
    echo "PASS: write of 0 resets counter"

    if echo '-1' > /dev/counter 2>/dev/null; then
        fail "negative value was accepted"
    else
        echo "PASS: negative value rejected"
    fi
    if echo 'not-a-number' > /dev/counter 2>/dev/null; then
        fail "non-numeric value was accepted"
    else
        echo "PASS: non-numeric value rejected"
    fi

    echo "----- COUNTER_INFO_FINAL_BEGIN -----"
    cat /proc/counter_info
    echo "----- COUNTER_INFO_FINAL_END -----"

    grep -q '^counter=1$' /proc/counter_info ||
        fail "proc counter value is incorrect"
    grep -q '^read_count=4$' /proc/counter_info ||
        fail "proc read count is incorrect"
    grep -q '^write_count=2$' /proc/counter_info ||
        fail "proc write count is incorrect"
    if [ "$result" -eq 0 ]; then
        echo "PASS: /proc/counter_info values"
    fi

    if ! rmmod counter_driver; then
        fail "counter_driver unload failed"
    fi
    if [ -e /dev/counter ] || [ -e /proc/counter_info ]; then
        fail "device or proc entry remains after unload"
    else
        echo "PASS: module cleanup removed device and proc entry"
    fi

    if [ "$result" -eq 0 ]; then
        echo "COUNTER_TEST_RESULT=PASS"
    else
        echo "COUNTER_TEST_RESULT=FAIL"
    fi
    echo "----- dmesg after driver cleanup -----"
    dmesg
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
LOG_FILE="$LOG_DIR/test_counter_output.txt"
TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/counter-test.XXXXXX")

cleanup()
{
    rm -f -- "$TMP_DIR/counter_driver.ko" "$TMP_DIR/init" \
        "$TMP_DIR/test_counter.sh" "$TMP_DIR/overlay.cpio" "$TMP_DIR/initrd"
    rmdir -- "$TMP_DIR"
}
trap cleanup EXIT

if [ ! -f "$TASK_DIR/counter_driver.ko" ]; then
    echo "Build the module first with: make -C \"$TASK_DIR\"" >&2
    exit 1
fi
for asset in "$INITRAMFS" "$DTB" "$KERNEL"; do
    if [ ! -f "$asset" ]; then
        echo "Required existing LAB-02 asset is missing: $asset" >&2
        exit 1
    fi
done

cat > "$TMP_DIR/init" <<'EOF'
#!/bin/sh
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
if ! insmod /counter_driver.ko; then
    echo "COUNTER_QEMU_RESULT=FAIL (module load)"
    poweroff -f
fi
/bin/sh /test_counter.sh --guest
EOF
chmod 755 "$TMP_DIR/init"
cp "$TASK_DIR/test_counter.sh" "$TMP_DIR/test_counter.sh"
cp "$TASK_DIR/counter_driver.ko" "$TMP_DIR/counter_driver.ko"

python3 - "$TMP_DIR" "$TMP_DIR/overlay.cpio" <<'PY'
import os
import sys

root, archive_path = sys.argv[1:]
entries = [
    ("init", 0o100755),
    ("test_counter.sh", 0o100755),
    ("counter_driver.ko", 0o100644),
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
echo "Running Assignment 01 Task 2 tests in QEMU..." > "$LOG_FILE"
if timeout 120s qemu-system-arm \
    -M vexpress-a9 -cpu cortex-a9 -m 256M \
    -kernel "$KERNEL" -dtb "$DTB" -initrd "$TMP_DIR/initrd" \
    -append "console=ttyAMA0,115200 rdinit=/init" \
    -nographic -no-reboot >> "$LOG_FILE" 2>&1; then
    qemu_status=0
else
    qemu_status=$?
fi

python3 - "$LOG_FILE" "$LOG_DIR" <<'PY'
from pathlib import Path
import sys

log_path = Path(sys.argv[1])
log_dir = Path(sys.argv[2])
lines = log_path.read_text(errors="replace").splitlines()
for label, filename in (
    ("INITIAL", "counter_info_initial.txt"),
    ("AFTER_WRITE", "counter_info_after_write.txt"),
    ("FINAL", "counter_info_final.txt"),
):
    start = f"----- COUNTER_INFO_{label}_BEGIN -----"
    end = f"----- COUNTER_INFO_{label}_END -----"
    try:
        first = lines.index(start)
        last = lines.index(end, first + 1)
    except ValueError:
        continue
    (log_dir / filename).write_text("\n".join(lines[first + 1:last]) + "\n")
PY

if [ "$qemu_status" -eq 0 ] &&
   grep -q '^COUNTER_TEST_RESULT=PASS' "$LOG_FILE"; then
    echo "Task 2 QEMU tests passed." >> "$LOG_FILE"
    cat "$LOG_FILE"
else
    echo "COUNTER_QEMU_RESULT=FAIL (QEMU exit status: $qemu_status)" |
        tee -a "$LOG_FILE" >&2
    cat "$LOG_FILE"
    exit 1
fi

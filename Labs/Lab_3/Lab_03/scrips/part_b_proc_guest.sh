#!/bin/sh

sample_process() {
    name=$1
    app=$2

    "$app" --hold-seconds 12 </etc/passwd &
    pid=$!
    echo "=== ${name}: pid=${pid} ==="
    sleep 1
    if [ ! -r "/proc/${pid}/status" ]; then
        echo "Process exited before /proc sampling"
        wait "$pid" || return 1
        return 1
    fi
    grep -E '^Vm(Size|RSS|Data|Stk):' "/proc/${pid}/status"
    echo "--- first 20 memory map entries ---"
    sed -n '1,20p' "/proc/${pid}/maps"
    wait "$pid"
}

sample_process leaky /usr/bin/leaky_app_arm
sample_process fixed /usr/bin/leaky_app_fixed_arm

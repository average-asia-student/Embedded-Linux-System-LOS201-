#!/bin/bash
set -u

root=/home/elaina/embedded_lab3
cd "$root"

outputs=(
    logs/part_d/cyclictest_standard.txt
    logs/part_d/hist_standard.txt
    logs/part_d/cyclictest_stress.txt
    logs/part_d/hist_stress.txt
    logs/part_d/stress_ng.txt
    logs/part_d/measurement_status.txt
)
for output in "${outputs[@]}"; do
    if [[ -e "$output" ]]; then
        echo "Refusing to overwrite existing output: $output" >&2
        exit 2
    fi
done

sudo -v || exit $?

{
    date --iso-8601=seconds
    uname -a
    printf 'perf_event_paranoid='
    cat /proc/sys/kernel/perf_event_paranoid
    printf 'sched_rt_runtime_us='
    cat /proc/sys/kernel/sched_rt_runtime_us
} >logs/part_d/measurement_environment.txt

set +e
sudo cyclictest --mlockall --threads=4 --priority=80 --interval=1000 \
    --distance=500 --loops=10000 --histogram=200 \
    --histfile="$root/logs/part_d/hist_standard.txt" --quiet \
    >logs/part_d/cyclictest_standard.txt 2>&1
baseline_status=$?

stress-ng --cpu 4 --io 2 --timeout 30s --metrics-brief \
    >logs/part_d/stress_ng.txt 2>&1 &
stress_pid=$!
sleep 1
sudo cyclictest --mlockall --threads=4 --priority=80 --interval=1000 \
    --distance=500 --loops=30000 --histogram=200 \
    --histfile="$root/logs/part_d/hist_stress.txt" --quiet \
    >logs/part_d/cyclictest_stress.txt 2>&1
stress_cyclictest_status=$?
wait "$stress_pid"
stress_status=$?
set -e

{
    printf 'baseline_cyclictest_exit=%s\n' "$baseline_status"
    printf 'stress_cyclictest_exit=%s\n' "$stress_cyclictest_status"
    printf 'stress_ng_exit=%s\n' "$stress_status"
} >logs/part_d/measurement_status.txt

cat logs/part_d/measurement_status.txt
if ((baseline_status != 0 || stress_cyclictest_status != 0 || stress_status != 0)); then
    exit 1
fi

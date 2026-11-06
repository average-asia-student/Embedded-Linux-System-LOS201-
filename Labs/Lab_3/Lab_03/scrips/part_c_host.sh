#!/bin/sh
set -eu

root=/home/elaina/embedded_lab3
cd "$root"

if [ -e logs/part_c/host_run.txt ] ||
   [ -e logs/part_c/host_time.txt ] ||
[ -e logs/part_c/host_scaled_run.txt ] ||
[ -e logs/part_c/host_scaled_time.txt ] ||
[ -e logs/part_c/host_output_check.txt ] ||
[ -e logs/part_c/strace_full.txt ] ||
   [ -e logs/part_c/strace_summary.txt ] ||
   [ -e logs/part_c/strace_filtered.txt ] ||
   [ -e logs/part_c/perf_record_attempt.txt ] ||
   [ -e logs/part_c/perf_stat_attempt.txt ]; then
    echo "Part C output already exists; refusing to overwrite it." >&2
    exit 2
fi

gcc -g -O1 -Wall -Wextra -fno-omit-frame-pointer \
    -o apps/perf_target_host apps/perf_target.c -lm \
    2>logs/part_c/build_host_retry.log

/usr/bin/time -v ./apps/perf_target_host \
    >logs/part_c/host_run.txt 2>logs/part_c/host_time.txt

/usr/bin/time -v ./apps/perf_target_host \
    --iterations 2 --search-runs 20 --io-lines 1000 \
    >logs/part_c/host_scaled_run.txt 2>logs/part_c/host_scaled_time.txt
wc -l /tmp/perf_test.txt >logs/part_c/host_output_check.txt

strace -o logs/part_c/strace_full.txt \
    ./apps/perf_target_host --iterations 2 --search-runs 20 --io-lines 1000 \
    >logs/part_c/strace_full_stdout.txt 2>&1

strace -c -o logs/part_c/strace_summary.txt \
    ./apps/perf_target_host --iterations 2 --search-runs 20 --io-lines 1000 \
    >logs/part_c/strace_summary_stdout.txt 2>&1

strace -e trace=file,write,read -o logs/part_c/strace_filtered.txt \
    ./apps/perf_target_host --iterations 1 --search-runs 5 --io-lines 20 \
    >logs/part_c/strace_filtered_stdout.txt 2>&1

set +e
timeout 10s perf record -F 49 -g -o logs/part_c/perf.data -- \
    ./apps/perf_target_host --iterations 1 --search-runs 10 --io-lines 100 \
    >logs/part_c/perf_record_stdout.txt 2>logs/part_c/perf_record_attempt.txt
record_status=$?
timeout 10s perf stat -d -- \
    ./apps/perf_target_host --iterations 1 --search-runs 10 --io-lines 100 \
    >logs/part_c/perf_stat_stdout.txt 2>logs/part_c/perf_stat_attempt.txt
stat_status=$?
set -e

printf 'perf record exit: %s\nperf stat exit: %s\n' \
    "$record_status" "$stat_status" >logs/part_c/perf_attempt_status.txt

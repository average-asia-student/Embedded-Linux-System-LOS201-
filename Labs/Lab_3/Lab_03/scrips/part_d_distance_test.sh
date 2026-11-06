#!/bin/bash
set -eu

root=/home/elaina/embedded_lab3
cd "$root"

output=logs/part_d/cyclictest_distance500.txt
if [[ -e "$output" ]]; then
    echo "Refusing to overwrite existing output: $output" >&2
    exit 2
fi

set +e
sudo cyclictest --mlockall --threads=4 --priority=80 --interval=1000 \
    --distance=500 --loops=10000 --quiet >"$output" 2>&1
status=$?
set -e
printf 'exit_status=%s\n' "$status" >>"$output"
exit "$status"

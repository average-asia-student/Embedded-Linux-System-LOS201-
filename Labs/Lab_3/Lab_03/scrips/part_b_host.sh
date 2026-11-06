#!/bin/sh
set -eu

VALGRIND=${VALGRIND:-valgrind}

gcc -g -O0 -Wall -Wextra -o apps/leaky_app apps/leaky_app.c
gcc -g -O0 -Wall -Wextra -o apps/leaky_app_fixed apps/leaky_app_fixed.c

file apps/leaky_app apps/leaky_app_fixed > logs/part_b/host_binaries.txt 2>&1
./apps/leaky_app > logs/part_b/leaky_app_stdout.txt 2>&1
./apps/leaky_app_fixed > logs/part_b/leaky_app_fixed_stdout.txt 2>&1

"$VALGRIND" --tool=memcheck --leak-check=full --show-leak-kinds=all \
    --track-origins=yes --error-exitcode=99 \
    --log-file=logs/part_b/valgrind_report.txt ./apps/leaky_app \
    > logs/part_b/valgrind_leaky_stdout.txt 2>&1 || status=$?
status=${status:-0}
if [ "$status" -ne 99 ]; then
    printf 'Expected Valgrind to report leaks (exit 99), got %s\n' "$status" >&2
    exit 1
fi

"$VALGRIND" --tool=memcheck --leak-check=full --show-leak-kinds=all \
    --track-origins=yes --error-exitcode=99 \
    --log-file=logs/part_b/valgrind_fixed.txt ./apps/leaky_app_fixed \
    > logs/part_b/valgrind_fixed_stdout.txt 2>&1

grep -q 'ERROR SUMMARY: 0 errors from 0 contexts' logs/part_b/valgrind_fixed.txt
grep -q 'All heap blocks were freed -- no leaks are possible' logs/part_b/valgrind_fixed.txt

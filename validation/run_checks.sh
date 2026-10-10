#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
python3 ../tools/host_check_v6.py
python3 ../tools/check_v6.py
for task in host_checks_v6 metadata_checks_v6; do
  g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer "$task.cpp" -o "$task"
  ASAN_OPTIONS=detect_leaks=0 "./$task"
done
for diagnostics in 0 1; do
  g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -DDAW_SERIAL_DIAGNOSTICS=$diagnostics serial_checks_v6.cpp -o serial_checks_v6
  ASAN_OPTIONS=detect_leaks=0 ./serial_checks_v6
done

#!/usr/bin/env bash
# vesper acceptance: the full check suite every batch must pass.
# Any failing step stops the script. Never report acceptance as passed
# with a check disabled, an exit code ignored, or a failing case skipped.
# TSan joins at D4 (see CLAUDE.md); until then this is the whole suite.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/../.."
check_jobs="${VESPER_CHECK_JOBS:-4}"
# The visor borrow points at the live tree by default. While the visor side
# is mid-refactor, export VISOR_ROOT to a stable materialized pin (a
# git-archive checkout) so this suite always runs against a whole tree.
visor_root="${VISOR_ROOT:-/home/charliechen/visor}"

echo "== 1/4 rel: configure, build, test =="
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ \
    -DVISOR_ROOT="$visor_root"
cmake --build build --parallel "$check_jobs"
ctest --test-dir build --output-on-failure

echo "== 2/4 asan/ubsan: configure, build, test =="
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ \
    -DVESPER_SANITIZE=ON -DVISOR_ROOT="$visor_root"
cmake --build build-asan --parallel "$check_jobs"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    ctest --test-dir build-asan --output-on-failure

echo "== 3/4 clang-format =="
cmake --build build --target vesper.check-format

echo "== 4/4 clang-tidy =="
cmake --build build --target vesper.check-tidy

echo "acceptance: all green"

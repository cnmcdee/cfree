#!/usr/bin/env bash
# CMake tests for FDesigner: generated CMakeLists.txt contents, real
# configure+build of generated and hand-written CMake projects (GTK under
# Xvfb, console run), settings persistence, and failure reporting.
# Usage: tests/cmake_tests.sh  (run from the repo root; builds fdesigner
# first so the self-test's CMake phase is exercised too.)
set -u
cd "$(dirname "$0")/.."

pass=0; fail=0
note() { if [ "$1" = 0 ]; then pass=$((pass+1)); else fail=$((fail+1)); fi; }

echo "== building fdesigner =="
make > /tmp/fdesigner_cmake_make.log 2>&1
note $?
if [ $fail -ne 0 ]; then echo "FAIL make — see /tmp/fdesigner_cmake_make.log"; exit 1; fi
echo "PASS make"

echo "== building cmake probe =="
g++ -O2 -Wall -Wextra -std=c++17 -o /tmp/cmake_probe tests/cmake_probe.cpp \
    src/generator.cpp src/cmake_build.cpp -I src \
    $(pkg-config --cflags --libs gtk+-3.0) > /tmp/cmake_probe_build.log 2>&1
note $?
if [ $fail -ne 0 ]; then echo "FAIL probe build — see /tmp/cmake_probe_build.log"; exit 1; fi
echo "PASS probe build"

echo "== probe (generate + cmake configure/build + run + settings + failures) =="
/tmp/cmake_probe /tmp/fdesigner_cmake_tests
rc=$?
note $rc

echo "== designer self-test (includes CMake Phase 9) =="
FDESIGNER_SELFTEST=1 timeout 120 xvfb-run -a ./fdesigner > /tmp/fdesigner_cmake_selftest.log 2>&1
rc=$?
if [ $rc -eq 0 ] && grep -q "SELFTEST PASS" /tmp/fdesigner_cmake_selftest.log; then
    echo "PASS selftest"; pass=$((pass+1))
else
    echo "FAIL selftest (rc=$rc) — see /tmp/fdesigner_cmake_selftest.log"; fail=$((fail+1))
fi

echo "== cmake tests: $pass passed, $fail failed =="
[ $fail -eq 0 ]

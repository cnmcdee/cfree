#!/usr/bin/env bash
# Lazarus-widget tests: designer self-test (Phase 7 places, edits, drags,
# deletes and generates every new widget), then a generator-API project
# holding every new widget: per-widget code markers, a -Wall -Wextra
# compile of the generated app, and a brief run under Xvfb.
set -u
cd "$(dirname "$0")/.."

pass=0; fail=0; failed=""
ok()   { echo "PASS $1"; pass=$((pass+1)); }
bad()  { echo "FAIL $1"; fail=$((fail+1)); failed="$failed $1"; }

echo "== building fdesigner =="
if make > /tmp/widget_make.log 2>&1; then ok make; else
    bad make; cat /tmp/widget_make.log; exit 1
fi

echo "== designer self-test (includes Phase 7 Lazarus widgets) =="
if FDESIGNER_SELFTEST=1 timeout 60 xvfb-run -a ./fdesigner \
        > /tmp/widget_selftest.log 2>&1; then
    if grep -q "SELFTEST PASS" /tmp/widget_selftest.log; then ok selftest;
    else bad "selftest(no PASS line)"; tail -20 /tmp/widget_selftest.log; fi
else
    bad selftest; tail -20 /tmp/widget_selftest.log
fi
grep -E "GENERATED7" /tmp/widget_selftest.log || true

echo "== widget generator test (per-widget markers) =="
if g++ -O2 -Wall -Wextra -std=c++17 -o /tmp/widget_tests \
        tests/widget_tests.cpp src/generator.cpp src/cmake_build.cpp src/project_io.cpp; then
    rm -rf /tmp/fdesigner_widgets_gen
    if /tmp/widget_tests /tmp/fdesigner_widgets_gen; then
        ok widget-markers
    else
        bad widget-markers
    fi
else
    bad widget-tests-build
fi

echo "== compile generated project (all new widgets) =="
if (cd /tmp/fdesigner_widgets_gen && \
        make CXXFLAGS="-O2 -Wall -Wextra -std=c++17" >build.log 2>&1); then
    if grep -q "warning" /tmp/fdesigner_widgets_gen/build.log; then
        bad "generated-compile(warnings)"
        grep "warning" /tmp/fdesigner_widgets_gen/build.log | head
    else ok generated-compile; fi
else
    bad generated-compile; cat /tmp/fdesigner_widgets_gen/build.log
fi

echo "== run generated project briefly =="
timeout 3 xvfb-run -a /tmp/fdesigner_widgets_gen/app \
    > /tmp/fdesigner_widgets_gen/run.log 2>&1
rc=$?
if [ $rc -ne 124 ]; then
    bad "generated-run(exit $rc)"; cat /tmp/fdesigner_widgets_gen/run.log
else
    badlog=$(grep -E "CRITICAL|WARNING \*\*" \
        /tmp/fdesigner_widgets_gen/run.log | grep -v "Could not load a pixbuf" || true)
    if [ -n "$badlog" ]; then
        bad "generated-run(runtime warnings)"; echo "$badlog"
    else ok generated-run; fi
fi

echo "== widget_tests: $pass passed, $fail failed ==$failed"
[ $fail -eq 0 ]

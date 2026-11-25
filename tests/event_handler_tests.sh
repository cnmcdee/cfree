#!/usr/bin/env bash
# Event handler tests: designer self-test (Phase 8 connects every signal),
# generator-API checks for all 36 widget types (per-widget markers), a
# -Wall -Wextra compile of the generated app, and a runtime harness that
# triggers every signal through the GTK API under Xvfb and checks the
# handler ran (EVENT trace lines). Draw handlers are verified via queued
# exposes; ColorBox "color-set" and TreeView "row-activated" have no
# programmatic GTK trigger, so the harness emits them by name.
set -u
cd "$(dirname "$0")/.."

pass=0; fail=0; failed=""
ok()   { echo "PASS $1"; pass=$((pass+1)); }
bad()  { echo "FAIL $1"; fail=$((fail+1)); failed="$failed $1"; }

echo "== building fdesigner =="
if make > /tmp/events_make.log 2>&1; then ok make; else
    bad make; cat /tmp/events_make.log; exit 1
fi

echo "== designer self-test (Phase 8: event handlers) =="
if FDESIGNER_SELFTEST=1 timeout 60 xvfb-run -a ./fdesigner \
        > /tmp/events_selftest.log 2>&1; then
    if grep -q "SELFTEST PASS" /tmp/events_selftest.log; then ok selftest;
    else bad "selftest(no PASS line)"; tail -20 /tmp/events_selftest.log; fi
else
    bad selftest; tail -20 /tmp/events_selftest.log
fi
grep -E "GENERATED8" /tmp/events_selftest.log || true

echo "== event generator test (all 36 widget types) =="
if g++ -O2 -Wall -Wextra -std=c++17 -o /tmp/event_handler_tests \
        tests/event_handler_tests.cpp src/generator.cpp src/cmake_build.cpp src/project_io.cpp; then
    rm -rf /tmp/fdesigner_events_gen
    if /tmp/event_handler_tests /tmp/fdesigner_events_gen; then
        ok event-markers
    else
        bad event-markers
    fi
else
    bad event-tests-build
fi

echo "== compile generated project (all widgets + handlers) =="
if (cd /tmp/fdesigner_events_gen && \
        make CXXFLAGS="-O2 -Wall -Wextra -std=c++17" >build.log 2>&1); then
    if grep -q "warning" /tmp/fdesigner_events_gen/build.log; then
        bad "generated-compile(warnings)"
        grep "warning" /tmp/fdesigner_events_gen/build.log | head
    else ok generated-compile; fi
else
    bad generated-compile; cat /tmp/fdesigner_events_gen/build.log
fi

echo "== run generated project briefly =="
timeout 3 xvfb-run -a /tmp/fdesigner_events_gen/app \
    > /tmp/fdesigner_events_gen/run.log 2>&1
rc=$?
if [ $rc -ne 124 ]; then
    bad "generated-run(exit $rc)"; cat /tmp/fdesigner_events_gen/run.log
else
    badlog=$(grep -E "CRITICAL|WARNING \*\*" \
        /tmp/fdesigner_events_gen/run.log | grep -v "Could not load a pixbuf" || true)
    if [ -n "$badlog" ]; then
        bad "generated-run(runtime warnings)"; echo "$badlog"
    else ok generated-run; fi
fi

echo "== runtime harness: trigger every signal, check handlers ran =="
GEN_SRCS=$(cd /tmp/fdesigner_events_gen && ls *.cpp | grep -v '^main.cpp$')
# shellcheck disable=SC2086
if g++ -O2 -Wall -Wextra -std=c++17 -DFDESIGNER_EVENT_TRACE \
        -I/tmp/fdesigner_events_gen -o /tmp/event_handler_runtime \
        tests/event_handler_runtime.cpp \
        $(for s in $GEN_SRCS; do echo "/tmp/fdesigner_events_gen/$s"; done) \
        $(pkg-config --cflags --libs gtk+-3.0) \
        > /tmp/event_runtime_build.log 2>&1; then
    if grep -q "warning" /tmp/event_runtime_build.log; then
        echo "note: harness build warnings (not fatal):"
        grep "warning" /tmp/event_runtime_build.log | head
    fi
    rm -f /tmp/event_trace.log
    if timeout 30 xvfb-run -a /tmp/event_handler_runtime /tmp/event_trace.log \
            > /tmp/event_runtime.log 2> /tmp/event_trace.log; then
        ok event-runtime
        cat /tmp/event_runtime.log
    else
        bad event-runtime; cat /tmp/event_runtime.log
    fi
else
    bad event-runtime-build; cat /tmp/event_runtime_build.log
fi

echo "== event_handler_tests: $pass passed, $fail failed ==$failed"
[ $fail -eq 0 ]

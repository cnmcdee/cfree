#!/bin/sh
# FDesigner color themes + code-view gutter/folding tests.
#   tests/theme_tests.sh
# Builds + runs the theme harness (tests/theme_tests.cpp, needs Xvfb),
# runs the fdesigner self-test (which has its own theme/folding block),
# and smoke tests the real Code View window and Color Theme dialog
# (FDESIGNER_OPEN_CODEVIEW / FDESIGNER_OPEN_THEME) with xdotool if
# available. Prints a PASS/FAIL summary; exits non-zero on any failure.
# Run from the repo root.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
FAIL=0
say() { printf '%s\n' "$*"; }

say "== building theme harness =="
g++ -O2 -Wall -Wextra -std=c++17 -I src -o /tmp/theme_tests tests/theme_tests.cpp \
    src/theme.cpp src/codeview.cpp src/debugger.cpp src/debugger_ui.cpp \
    $(pkg-config --cflags --libs gtk+-3.0) \
    2>/tmp/theme_tests_build.log || {
    say "FAIL theme harness build"; cat /tmp/theme_tests_build.log; exit 1
}
if grep -q "warning" /tmp/theme_tests_build.log; then
    say "FAIL theme harness build has warnings"; cat /tmp/theme_tests_build.log
    FAIL=1
fi
say "== theme harness (under Xvfb) =="
rm -rf /tmp/fdesigner_theme_home
xvfb-run -a /tmp/theme_tests || FAIL=1

say "== building fdesigner =="
make >/tmp/theme_make.log 2>&1 || {
    say "FAIL fdesigner build"; cat /tmp/theme_make.log; FAIL=1
}
if grep -q "warning" /tmp/theme_make.log; then
    say "FAIL fdesigner build has warnings"
    grep "warning" /tmp/theme_make.log
    FAIL=1
fi
if [ "$FAIL" = "0" ]; then
    say "== fdesigner self-test (includes theme/folding block) =="
    FDESIGNER_SELFTEST=1 xvfb-run -a ./fdesigner || FAIL=1
fi

say "== View menu wiring =="
grep -q 'id="mi_view_code"' ui/designer.ui &&
    grep -q 'id="mi_view_theme"' ui/designer.ui &&
    grep -q 'mi_view_code' src/main.cpp &&
    grep -q 'mi_view_theme' src/main.cpp &&
    say "PASS menu-items-wired" ||
    { say "FAIL menu-items-wired"; FAIL=1; }

if ! command -v xdotool >/dev/null 2>&1; then
    say "SKIP GUI smoke tests (xdotool not installed)"
else
    say "== Code View window smoke test =="
    xvfb-run -a sh -c '
        FDESIGNER_OPEN_CODEVIEW=1 ./fdesigner >/tmp/theme_codeview.log 2>&1 &
        APP=$!
        FOUND=""
        i=0
        while [ $i -lt 50 ]; do
            FOUND=$(xdotool search --name "FDesigner — Code" 2>/dev/null | head -1)
            [ -n "$FOUND" ] && break
            sleep 0.2; i=$((i+1))
        done
        if [ -n "$FOUND" ]; then echo "PASS codeview-opened (window $FOUND)"; else echo "FAIL codeview-did-not-open"; fi
        kill $APP 2>/dev/null
        wait $APP 2>/dev/null
        [ -n "$FOUND" ]
    ' || FAIL=1
    say "== Color Theme dialog smoke test =="
    xvfb-run -a sh -c '
        FDESIGNER_OPEN_THEME=1 ./fdesigner >/tmp/theme_dlg.log 2>&1 &
        APP=$!
        FOUND=""
        i=0
        while [ $i -lt 50 ]; do
            FOUND=$(xdotool search --name "^Color Theme$" 2>/dev/null | head -1)
            [ -n "$FOUND" ] && break
            sleep 0.2; i=$((i+1))
        done
        if [ -n "$FOUND" ]; then
            echo "PASS theme-dialog-opened (window $FOUND)"
            xdotool windowactivate "$FOUND" key Escape
            sleep 0.5
        else
            echo "FAIL theme-dialog-did-not-open"
            xdotool search --name ".*" getwindowname %@ 2>/dev/null | head -5
        fi
        kill $APP 2>/dev/null
        wait $APP 2>/dev/null
        [ -n "$FOUND" ]
    ' || FAIL=1
fi

if [ "$FAIL" = "0" ]; then say "THEME TESTS: ALL PASS"; else say "THEME TESTS: FAILURES (see above)"; fi
exit "$FAIL"

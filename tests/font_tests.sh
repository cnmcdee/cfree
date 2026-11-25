#!/bin/sh
# FDesigner font settings tests.
#   tests/font_tests.sh
# Builds + runs the preferences harness (tests/font_tests.cpp, needs Xvfb),
# runs the fdesigner self-test (which has its own font block), and smoke
# tests the real Preferences dialog (FDESIGNER_OPEN_PREFERENCES=1) with
# xdotool if available. Prints a PASS/FAIL summary; exits non-zero on
# any failure. Run from the repo root.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
FAIL=0
say() { printf '%s\n' "$*"; }

say "== building font harness =="
g++ -O2 -Wall -Wextra -std=c++17 -I src -o /tmp/font_tests tests/font_tests.cpp \
    src/preferences.cpp $(pkg-config --cflags --libs gtk+-3.0) \
    2>/tmp/font_tests_build.log || {
    say "FAIL font harness build"; cat /tmp/font_tests_build.log; exit 1
}
say "== font harness (under Xvfb) =="
xvfb-run -a /tmp/font_tests || FAIL=1

say "== building fdesigner =="
make >/tmp/font_make.log 2>&1 || {
    say "FAIL fdesigner build"; cat /tmp/font_make.log; FAIL=1
}
if [ "$FAIL" = "0" ]; then
    say "== fdesigner self-test (includes font block) =="
    FDESIGNER_SELFTEST=1 xvfb-run -a ./fdesigner || FAIL=1
fi

say "== Preferences dialog smoke test =="
if ! command -v xdotool >/dev/null 2>&1; then
    say "SKIP dialog smoke test (xdotool not installed)"
else
    # Launch under a fresh Xvfb; the env hook opens the dialog at startup.
    # The dialog is modal, so the app stays up until we close it with Esc.
    xvfb-run -a sh -c '
        FDESIGNER_OPEN_PREFERENCES=1 ./fdesigner >/tmp/font_dlg.log 2>&1 &
        APP=$!
        FOUND=""
        i=0
        while [ $i -lt 50 ]; do
            FOUND=$(xdotool search --name "^Preferences$" 2>/dev/null | head -1)
            [ -n "$FOUND" ] && break
            sleep 0.2; i=$((i+1))
        done
        if [ -n "$FOUND" ]; then
            echo "PASS dialog-opened (window $FOUND)"
            xdotool windowactivate "$FOUND" key Escape
            sleep 0.5
        else
            echo "FAIL dialog-did-not-open"
            xdotool search --name ".*" getwindowname %@ 2>/dev/null | head -5
        fi
        kill $APP 2>/dev/null
        wait $APP 2>/dev/null
        [ -n "$FOUND" ]
    ' || FAIL=1
    # The Edit -> Preferences menu item must exist in the .ui and be wired.
    grep -q 'id="mi_preferences"' ui/designer.ui &&
        grep -q 'mi_preferences' src/main.cpp &&
        say "PASS menu-item-wired" ||
        { say "FAIL menu-item-wired"; FAIL=1; }
    # Apply path: open the dialog with a scratch conf path, press Alt+A
    # (the _Apply mnemonic), and check the file was written.
    say "== Preferences dialog Apply test =="
    rm -f /tmp/font_gui_apply.conf
    xvfb-run -a sh -c '
        FDESIGNER_SETTINGS_CONF=/tmp/font_gui_apply.conf FDESIGNER_OPEN_PREFERENCES=1 ./fdesigner >/tmp/font_apply.log 2>&1 &
        APP=$!
        FOUND=""
        i=0
        while [ $i -lt 50 ]; do
            FOUND=$(xdotool search --name "^Preferences$" 2>/dev/null | head -1)
            [ -n "$FOUND" ] && break
            sleep 0.2; i=$((i+1))
        done
        [ -n "$FOUND" ] && xdotool windowfocus "$FOUND" key alt+a
        sleep 1
        kill $APP 2>/dev/null
        wait $APP 2>/dev/null
    '
    grep -q '^font=Sans 10$' /tmp/font_gui_apply.conf &&
        grep -q '^code_font=Monospace 11$' /tmp/font_gui_apply.conf &&
        say "PASS dialog-apply-saves" ||
        { say "FAIL dialog-apply-saves"; cat /tmp/font_gui_apply.conf 2>/dev/null; FAIL=1; }
fi

if [ "$FAIL" = "0" ]; then say "FONT TESTS: ALL PASS"; else say "FONT TESTS: FAILURES (see above)"; fi
exit "$FAIL"

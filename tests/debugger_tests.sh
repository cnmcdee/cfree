#!/bin/sh
# FDesigner debugger test suite.
#   tests/debugger_tests.sh [--no-gui]
# Builds the console + GTK test targets and the headless harness, runs the
# console suite, the GTK suite under Xvfb, and a GUI smoke test (the real
# fdesigner opens its debugger window and the Settings dialog path is
# exercised through the Debug menu ids in ui/designer.ui).
# Prints a PASS/FAIL summary; exits non-zero if anything fails.
# Run from the repo root.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
D="$ROOT/tests/debugger"
WORK="$D/work"
mkdir -p "$WORK"
FAIL=0

say() { printf '%s\n' "$*"; }

# --- build test targets ----------------------------------------------------
say "== building debugger test targets =="
cp "$D/console.cpp" "$WORK/console.cpp"
cp "$D/spin.cpp" "$WORK/spin.cpp"
cp "$D/gtkapp.cpp" "$WORK/gtkapp.cpp"
# Compile from the work-dir paths so DWARF records the same absolute
# source paths the harness uses for file:line breakpoints.
g++ -g -O0 -o "$WORK/console" "$WORK/console.cpp" || FAIL=1
g++ -g -O0 -o "$WORK/spin" "$WORK/spin.cpp" || FAIL=1
g++ -g -O0 -o "$WORK/gtkapp" "$WORK/gtkapp.cpp" $(pkg-config --cflags --libs gtk+-3.0) || FAIL=1
g++ -O2 -Wall -Wextra -std=c++17 -o "$WORK/dbg_harness" "$D/dbg_harness.cpp" "$ROOT/src/debugger.cpp" || FAIL=1
# fdesigner itself (with the debugger window) must build clean
(cd "$ROOT" && make >/tmp/dbg_make.log 2>&1) || { say "FAIL fdesigner build"; cat /tmp/dbg_make.log; FAIL=1; }

if [ "$FAIL" != "0" ]; then say "BUILD FAILED"; exit 1; fi

# --- console suite ---------------------------------------------------------
say "== console debugger suite =="
"$WORK/dbg_harness" console "$WORK" > "$WORK/console.log" 2>&1
RC=$?
cat "$WORK/console.log"
[ "$RC" != "0" ] && FAIL=1

# --- GTK suite (needs X) ---------------------------------------------------
say "== GTK debugger suite (under Xvfb) =="
xvfb-run -a "$WORK/dbg_harness" gtk "$WORK" > "$WORK/gtk.log" 2>&1
RC=$?
cat "$WORK/gtk.log"
[ "$RC" != "0" ] && FAIL=1

# --- GUI smoke test --------------------------------------------------------
if [ "${1:-}" != "--no-gui" ]; then
  say "== GUI smoke test: fdesigner + debugger window =="
  # Debug menu + Debugger sub-menu ids must exist in the .ui
  for id in menu_debug mi_debug_open mi_dbg_settings mi_debug_settings \
            menu_debugger_sub mi_dbg_break_main mi_dbg_stop_entry mi_dbg_pretty; do
    grep -q "id=\"$id\"" "$ROOT/ui/designer.ui" || { say "FAIL ui missing id $id"; FAIL=1; }
  done
  say "ui menu ids present"
  # Launch fdesigner on a fixed Xvfb display with the debugger window
  # auto-opened, then verify the window exists via xdotool.
  Xvfb :99 -screen 0 1280x900x24 >/dev/null 2>&1 &
  XVPID=$!
  sleep 2
  (cd "$ROOT" && DISPLAY=:99 FDESIGNER_OPEN_DEBUGGER=1 timeout 25 ./fdesigner >"$WORK/gui.log" 2>&1) &
  APID=$!
  OK=0
  for i in $(seq 1 20); do
    sleep 1
    if DISPLAY=:99 xdotool search --name "FDesigner Debugger" >/dev/null 2>&1; then OK=1; break; fi
  done
  if [ "$OK" = "1" ]; then
    DISPLAY=:99 xdotool search --name "FDesigner Debugger" getwindowname %@ 2>/dev/null | head -3
    # Screenshot for the record (xwd if present, else skip gracefully).
    if command -v xwd >/dev/null 2>&1; then
      DISPLAY=:99 xwd -root -silent -out "$WORK/debugger_window.xwd" 2>/dev/null || true
    fi
  fi
  kill $APID 2>/dev/null
  pkill -x fdesigner 2>/dev/null
  wait $APID 2>/dev/null
  if [ "$OK" = "1" ]; then say "PASS gui_debugger_window_opens"; else say "FAIL gui_debugger_window_opens"; cat "$WORK/gui.log"; FAIL=1; fi
  # Settings dialog: launch with the settings hook and look for its window.
  (cd "$ROOT" && DISPLAY=:99 FDESIGNER_OPEN_DEBUGGER_SETTINGS=1 timeout 25 ./fdesigner >"$WORK/gui_settings.log" 2>&1) &
  APID=$!
  OK2=0
  for i in $(seq 1 20); do
    sleep 1
    if DISPLAY=:99 xdotool search --name "Debugger Settings" >/dev/null 2>&1; then OK2=1; break; fi
  done
  kill $APID 2>/dev/null
  pkill -x fdesigner 2>/dev/null
  wait $APID 2>/dev/null
  kill $XVPID 2>/dev/null
  if [ "$OK2" = "1" ]; then say "PASS gui_debugger_settings_dialog_opens"; else say "FAIL gui_debugger_settings_dialog_opens"; cat "$WORK/gui_settings.log"; FAIL=1; fi
fi

say "== debugger_tests.sh: $([ "$FAIL" = 0 ] && echo ALL PASS || echo FAILURES PRESENT) =="
exit $FAIL

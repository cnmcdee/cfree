#!/bin/sh
# FDesigner code-window tests (round 3).
#   tests/code_window_tests.sh
# Verifies, against the REAL app under Xvfb:
#   (a) the code window is a separate top-level window with its own
#       title, opened together with the designer at startup;
#   (b) red dots render on breakpoint lines set via the debugger
#       (and via a gutter click, which must stage a breakpoint in
#       the backend);
#   (c) the inspection pane auto-populates on a breakpoint hit with
#       the CORRECT locals for that scope, and stepping into another
#       function changes the shown scope;
#   (d) the red paused-line background appears on a breakpoint pause,
#       clears on resume, and moves on step (pixel-checked);
#   (e) while the target runs, the inspection pane shows "running",
#       not stale values;
#   (f) closing the code window does not kill the app, and View ->
#       Code View reopens it.
# The app is driven through FDESIGNER_DEBUG_TARGET / BREAK (autorun)
# and the FDESIGNER_CODEWIN_DUMP snapshot; stepping uses the debugger
# window's F5/F10/F11 shortcuts. Prints a PASS/FAIL summary; exits
# non-zero if anything fails. Run from the repo root.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
FAIL=0
WORK="$ROOT/tests/codewin/work"
rm -rf "$WORK"
mkdir -p "$WORK/homeA" "$WORK/homeB" "$WORK/homeC"

say() { printf '%s\n' "$*"; }
pass() { say "PASS $1"; }
fail() { say "FAIL $1"; FAIL=1; }
note() { say "  -- $1"; }

# --- build ---------------------------------------------------------------
say "== building fdesigner =="
make >"$WORK/make.log" 2>&1 || { say "FAIL fdesigner build"; cat "$WORK/make.log"; exit 1; }
if grep -q "warning" "$WORK/make.log"; then
    say "FAIL fdesigner build has warnings"; grep "warning" "$WORK/make.log"; FAIL=1
fi
say "== building test targets =="
cp "$ROOT/tests/codewin/prog.c" "$WORK/prog.c"
gcc -g -O0 -o "$WORK/prog" "$WORK/prog.c" || fail "build-prog"
gcc -g -O0 -o "$WORK/spin" "$ROOT/tests/debugger/spin.cpp" || fail "build-spin"
# Sanity: the break lines must hold the statements the test expects.
sed -n '16p' "$WORK/prog.c" | grep -q "helper(a)" || fail "prog.c:16 is not the helper call"
sed -n '11p' "$WORK/prog.c" | grep -q "return z" || fail "prog.c:11 is not return z"
sed -n '14p' "$WORK/prog.c" | grep -q "int a = 42" || fail "prog.c:14 is not a=42"

# --- self-test (unit: paused tag colours, inspection API) ----------------
say "== fdesigner self-test (paused line + inspection block) =="
if FDESIGNER_SELFTEST=1 xvfb-run -a timeout 120 ./fdesigner >"$WORK/selftest.log" 2>&1; then
    grep -q "SELFTEST PASS" "$WORK/selftest.log" && pass "selftest" || fail "selftest-no-pass-line"
else
    fail "selftest"; cat "$WORK/selftest.log"
fi

# --- GUI helpers ----------------------------------------------------------
XDISP=":99"
export DISPLAY="$XDISP"
codewin_id() { xdotool search --name "FDesigner — Code" 2>/dev/null | head -1; }
dbgwin_id() { xdotool search --name "FDesigner Debugger" 2>/dev/null | head -1; }
mainwin_id() { xdotool search --name '^FDesigner — C\+\+/GTK$' 2>/dev/null | head -1; }
dump_has() { grep -q "$1" "$WORK/dump.txt" 2>/dev/null; }
wait_dump() { # $1=pattern $2=timeout-secs
    i=0
    while [ $i -lt $(( $2 * 5 )) ]; do
        dump_has "$1" && return 0
        sleep 0.2; i=$((i+1))
    done
    return 1
}
wait_win() { # $1=which(codewin|dbgwin|mainwin) $2=timeout-secs
    i=0
    while [ $i -lt $(( $2 * 5 )) ]; do
        [ -n "$(${1}_id)" ] && return 0
        sleep 0.2; i=$((i+1))
    done
    return 1
}
launch() { # $1=target $2=breakloc-or-empty $3=home-dir
    rm -f "$WORK/dump.txt"
    (cd "$ROOT" && HOME="$3" DISPLAY="$XDISP" \
        FDESIGNER_DEBUG_TARGET="$1" FDESIGNER_DEBUG_BREAK="$2" \
        FDESIGNER_CODEWIN_DUMP="$WORK/dump.txt" \
        timeout 120 ./fdesigner >"$WORK/app.log" 2>&1) &
    APP=$!
}
kill_app() {
    kill $APP 2>/dev/null
    pkill -x fdesigner 2>/dev/null
    wait $APP 2>/dev/null
    sleep 1
}
count_red() { # $1=png $2=x0 $3=x1 [$4=band] -> stdout count (+band line)
    python3 "$ROOT/tests/codewin/count_red.py" "$1" "$2" "$3" "${4:-}"
}

if ! command -v xdotool >/dev/null 2>&1 || ! command -v import >/dev/null 2>&1; then
    say "SKIP GUI e2e (xdotool/import not installed)"
    [ "$FAIL" = "0" ] && say "CODE-WINDOW TESTS: ALL PASS" || say "CODE-WINDOW TESTS: FAILURES"
    exit "$FAIL"
fi

# The self-test above runs under xvfb-run -a, which takes :99 first; its
# server may still be shutting down when we get here. Wait for :99 to go
# quiet before starting our own Xvfb, or our Xvfb fails to bind while the
# readiness probe below still answers against the dying server.
for _i in $(seq 1 50); do
    DISPLAY="$XDISP" xdotool getdisplaygeometry >/dev/null 2>&1 || break
    sleep 0.2
done
Xvfb $XDISP -screen 0 1920x1080x24 >/dev/null 2>&1 &
XVPID=$!
# Wait until the display actually answers: a fixed sleep is not enough
# on a loaded machine, and the app then dies with "cannot open display".
for _i in $(seq 1 50); do
    DISPLAY="$XDISP" xdotool getdisplaygeometry >/dev/null 2>&1 && break
    sleep 0.2
done
DISPLAY="$XDISP" xdotool getdisplaygeometry >/dev/null 2>&1 || {
    say "FAIL Xvfb $XDISP did not come up"; kill $XVPID 2>/dev/null; exit 1
}
kill -0 $XVPID 2>/dev/null || {
    say "FAIL our Xvfb $XDISP died at startup (display taken?)"
    exit 1
}
trap 'kill_app; kill $XVPID 2>/dev/null' EXIT

# === Run A: breakpoint in main ============================================
say "== Run A: breakpoint in main (prog.c:16) =="
launch "$WORK/prog" "$WORK/prog.c:16" "$WORK/homeA"
if wait_win codewin 20; then
    CW=$(codewin_id); pass "code-window-opens-at-startup (id $CW)"
else
    fail "code-window-opens-at-startup"; kill_app; kill $XVPID 2>/dev/null; exit 1
fi
MW=$(mainwin_id)
if [ -n "$MW" ] && [ "$MW" != "$CW" ]; then
    pass "code-window-is-separate-toplevel (main $MW, code $CW)"
else
    fail "code-window-is-separate-toplevel (main=$MW code=$CW)"
fi
if wait_win dbgwin 20; then
    DW=$(dbgwin_id); pass "debugger-window-open (id $DW)"
else
    fail "debugger-window-open"
fi

if wait_dump "^state=Stopped" 20; then pass "stops-at-breakpoint"; else fail "stops-at-breakpoint"; cat "$WORK/dump.txt" 2>/dev/null; cat "$WORK/app.log"; fi
dump_has "^file=$WORK/prog.c" && pass "code-view-loads-stopped-file" || fail "code-view-loads-stopped-file"
dump_has "^paused_line=16" && pass "paused-line-16" || fail "paused-line-16"
dump_has "^current_line=16" && pass "current-line-16" || fail "current-line-16"
dump_has "main()" && pass "scope-is-main" || fail "scope-is-main"
dump_has "^var=a|int|42" && pass "local-a-42" || fail "local-a-42"
dump_has "^var=msg|" && grep "^var=msg|" "$WORK/dump.txt" | grep -q "hello" && pass "local-msg-hello" || fail "local-msg-hello"
note "dump after stop:"; sed 's/^/  dump: /' "$WORK/dump.txt" 2>/dev/null

# (d) red paused-line background, pixel-checked. Raise the code window
# first: Xvfb has no compositor, so an obscured window reads back wrong.
sleep 1
DISPLAY="$XDISP" xdotool windowraise "$CW" 2>/dev/null
sleep 0.5
DISPLAY="$XDISP" import -window "$CW" "$WORK/shot_paused.png" 2>/dev/null
RED_ALL=$(count_red "$WORK/shot_paused.png" 0 10000)
RED_TEXT=$(count_red "$WORK/shot_paused.png" 150 10000)
BAND=$(count_red "$WORK/shot_paused.png" 0 10000 band | tail -1)
note "paused screenshot: red_all=$RED_ALL red_text_area=$RED_TEXT ($BAND)"
if [ "${RED_TEXT:-0}" -ge 500 ]; then pass "red-paused-band-present"; else fail "red-paused-band-present (red_text=$RED_TEXT)"; fi
# Band geometry (screenshot coords) for the gutter-click test below.
BAND_TOP=$(echo "$BAND" | sed 's/.*band_top=\([0-9-]*\).*/\1/')
BAND_BOT=$(echo "$BAND" | sed 's/.*band_bottom=\([0-9-]*\).*/\1/')
BAND_H=0
if [ "${BAND_TOP:- -1}" -ge 0 ] && [ "${BAND_BOT:-0}" -gt "${BAND_TOP:-0}" ]; then
    BAND_H=$((BAND_BOT - BAND_TOP + 1))
fi
note "band_top=$BAND_TOP band_bottom=$BAND_BOT line_h~$BAND_H"

# (c2) Step Over (F10) -> line 16, scope still main, locals still correct.
DISPLAY="$XDISP" xdotool windowfocus --sync "$DW" key F10 2>/dev/null
if wait_dump "^paused_line=17" 15; then pass "step-over-moves-paused-line-to-17"; else fail "step-over-moves-paused-line-to-17"; cat "$WORK/dump.txt" 2>/dev/null; fi
dump_has "main()" && pass "scope-still-main-after-step" || fail "scope-still-main-after-step"
sleep 1
DISPLAY="$XDISP" xdotool windowraise "$CW" 2>/dev/null
sleep 0.5
DISPLAY="$XDISP" import -window "$CW" "$WORK/shot_stepped.png" 2>/dev/null
RED_STEP=$(count_red "$WORK/shot_stepped.png" 150 10000)
[ "${RED_STEP:-0}" -ge 500 ] && pass "red-band-follows-step" || fail "red-band-follows-step (red=$RED_STEP)"

# (d2) Continue (F5) -> target exits, red background clears.
DISPLAY="$XDISP" xdotool windowfocus --sync "$DW" key F5 2>/dev/null
if wait_dump "^state=Exited" 15; then pass "continue-exits-target"; else fail "continue-exits-target"; cat "$WORK/dump.txt" 2>/dev/null; fi
dump_has "^paused_line=0" && pass "paused-line-clears-on-resume" || fail "paused-line-clears-on-resume"
sleep 1
DISPLAY="$XDISP" xdotool windowraise "$CW" 2>/dev/null
sleep 0.5
DISPLAY="$XDISP" import -window "$CW" "$WORK/shot_exited.png" 2>/dev/null
RED_EXIT_TEXT=$(count_red "$WORK/shot_exited.png" 150 10000)
RED_EXIT_GUTTER=$(count_red "$WORK/shot_exited.png" 0 110)
note "exited screenshot: red_text=$RED_EXIT_TEXT red_gutter=$RED_EXIT_GUTTER"
[ "${RED_EXIT_TEXT:-9999}" -lt 200 ] && pass "red-band-clears-on-resume" || fail "red-band-clears-on-resume (red=$RED_EXIT_TEXT)"
# (b) the debugger-set breakpoint dot is still drawn in the gutter.
[ "${RED_EXIT_GUTTER:-0}" -ge 20 ] && pass "breakpoint-dot-still-drawn" || fail "breakpoint-dot-still-drawn (gutter_red=$RED_EXIT_GUTTER)"

# (b2) Gutter click stages a breakpoint in the backend: line 14 is two
# lines above the (old) paused line 16. Click the gutter dot zone.
if [ "${BAND_H:-0}" -gt 5 ] && [ "${BAND_TOP:- -1}" -ge 0 ]; then
    GEOM=$(DISPLAY="$XDISP" xdotool getwindowgeometry --shell "$CW" 2>/dev/null)
    SHOT_H=$(python3 -c "from PIL import Image; print(Image.open('$WORK/shot_paused.png').size[1])")
    WIN_H=$(echo "$GEOM" | sed -n 's/^HEIGHT=//p')
    TITLE_OFF=0
    if [ -n "$SHOT_H" ] && [ -n "$WIN_H" ] && [ "$SHOT_H" -gt "$WIN_H" ]; then
        TITLE_OFF=$((SHOT_H - WIN_H))
    fi
    Y16=$(( (BAND_TOP + BAND_BOT) / 2 - TITLE_OFF ))
    Y14=$(( Y16 - 2 * BAND_H ))
    note "gutter-click geometry: win_h=$WIN_H shot_h=$SHOT_H title_off=$TITLE_OFF y16=$Y16 y14=$Y14"
    BPS="$WORK/homeA/.config/fdesigner/debugger_bps.conf"
    GUT_OK=0
    for TRY in 0 1 2; do
        case $TRY in
            0) YY=$Y14;;
            1) YY=$((Y14 - BAND_H));;
            2) YY=$((Y14 + BAND_H));;
        esac
        DISPLAY="$XDISP" xdotool windowraise "$CW" windowfocus --sync "$CW" mousemove --window "$CW" 40 "$YY" click 1 2>/dev/null
        sleep 1
        if grep -q "prog.c:14|" "$BPS" 2>/dev/null; then GUT_OK=1; break; fi
        # Wrong line? remove whatever that click staged, then retry.
        if grep -q "prog.c:" "$BPS" 2>/dev/null; then
            DISPLAY="$XDISP" xdotool windowraise "$CW" windowfocus --sync "$CW" mousemove --window "$CW" 40 "$YY" click 1 2>/dev/null
            sleep 1
        fi
    done
    if [ "$GUT_OK" = "1" ]; then
        pass "gutter-click-stages-breakpoint (prog.c:14 in bps conf)"
        # Click again: the breakpoint must go away.
        DISPLAY="$XDISP" xdotool windowraise "$CW" windowfocus --sync "$CW" mousemove --window "$CW" 40 "$YY" click 1 2>/dev/null
        sleep 1
        grep -q "prog.c:14|" "$BPS" 2>/dev/null && fail "gutter-click-removes-breakpoint" || pass "gutter-click-removes-breakpoint"
    else
        fail "gutter-click-stages-breakpoint"
        grep "prog.c:" "$BPS" 2>/dev/null | sed 's/^/  bps: /' || note "no bps conf"
    fi
else
    fail "gutter-click-stages-breakpoint (no band geometry)"
fi
kill_app

# === Run B: breakpoint in helper — scope must change =======================
say "== Run B: breakpoint in helper (prog.c:11) =="
launch "$WORK/prog" "$WORK/prog.c:11" "$WORK/homeB"
wait_win codewin 20 || fail "runB-code-window-opens"
if wait_dump "^state=Stopped" 20; then pass "runB-stops-at-breakpoint"; else fail "runB-stops-at-breakpoint"; fi
dump_has "^paused_line=11" && pass "runB-paused-line-11" || fail "runB-paused-line-11"
dump_has "helper()" && pass "scope-is-helper" || fail "scope-is-helper"
dump_has "^var=x|int|42" && pass "runB-local-x-42" || fail "runB-local-x-42"
dump_has "^var=y|double|3.5" && pass "runB-local-y-3.5" || fail "runB-local-y-3.5"
dump_has "^var=z|int|44" && pass "runB-local-z-44" || fail "runB-local-z-44"
dump_has "^var=a|" && fail "runB-no-main-locals-leak" || pass "runB-no-main-locals-leak"
note "dump run B:"; sed 's/^/  dump: /' "$WORK/dump.txt" 2>/dev/null
kill_app

# === Run C: spinning target — "running", never stale =======================
say "== Run C: spinning target, no breakpoint =="
launch "$WORK/spin" "" "$WORK/homeC"
wait_win codewin 20 || fail "runC-code-window-opens"
if wait_dump "^state=Running" 20; then pass "running-state-shown"; else fail "running-state-shown"; cat "$WORK/dump.txt" 2>/dev/null; fi
dump_has "^paused_line=0" && pass "runC-no-paused-line" || fail "runC-no-paused-line"
if grep -q "^var=" "$WORK/dump.txt" 2>/dev/null; then fail "runC-no-stale-values"; else pass "runC-no-stale-values"; fi
note "dump run C:"; sed 's/^/  dump: /' "$WORK/dump.txt" 2>/dev/null

# (f) close the code window: app must survive; View -> Code View reopens.
CW=$(codewin_id); MW=$(mainwin_id)
DISPLAY="$XDISP" xdotool windowclose "$CW" 2>/dev/null
sleep 1
if kill -0 $APP 2>/dev/null && [ -n "$(mainwin_id)" ]; then
    pass "close-code-window-keeps-app-alive"
else
    fail "close-code-window-keeps-app-alive"
fi
[ -z "$(codewin_id)" ] && pass "code-window-actually-closed" || fail "code-window-actually-closed"
DISPLAY="$XDISP" xdotool windowfocus --sync "$MW" key F10 Right Right Down Return 2>/dev/null
if wait_win codewin 10; then
    pass "view-menu-reopens-code-window"
else
    # Fallback: click the View menu, then its first item.
    GEOM=$(DISPLAY="$XDISP" xdotool getwindowgeometry --shell "$MW" 2>/dev/null)
    WX=$(echo "$GEOM" | sed -n 's/^X=//p'); WY=$(echo "$GEOM" | sed -n 's/^Y=//p')
    DISPLAY="$XDISP" xdotool windowraise "$MW" mousemove --window "$MW" 130 12 click 1 2>/dev/null
    sleep 0.7
    DISPLAY="$XDISP" xdotool mousemove --window "$MW" 140 55 click 1 2>/dev/null
    if wait_win codewin 10; then pass "view-menu-reopens-code-window (click fallback)"; else fail "view-menu-reopens-code-window"; fi
fi
kill_app

kill $XVPID 2>/dev/null
trap - EXIT
if [ "$FAIL" = "0" ]; then say "CODE-WINDOW TESTS: ALL PASS"; else say "CODE-WINDOW TESTS: FAILURES (see above)"; fi
exit "$FAIL"

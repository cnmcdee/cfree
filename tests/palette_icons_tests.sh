#!/usr/bin/env bash
# Palette-icon tests: every palette button shows its 16x16 glyph left of
# the label, the palette still fits its 250x780 window with no scrolling,
# clicks still place widgets, and the rest of the app is unbroken.
set -u
cd "$(dirname "$0")/.."

pass=0; fail=0; failed=""
ok()   { echo "PASS $1"; pass=$((pass+1)); }
bad()  { echo "FAIL $1"; fail=$((fail+1)); failed="$failed $1"; }

# Re-run the body under Xvfb when no display is set.
if [ -z "${DISPLAY:-}" ]; then
    PALETTE_ICONS_TEST_IN_XVFB=1 xvfb-run -a -s "-screen 0 2560x1440x24" "$0"
    exit $?
fi

echo "== generating + verifying icons =="
if python3 tools/gen_palette_icons.py > /tmp/icons_gen.log 2>&1; then
    ok icons-generate; cat /tmp/icons_gen.log
else bad icons-generate; cat /tmp/icons_gen.log; fi
if python3 tools/verify_palette_icons.py > /tmp/icons_verify.log 2>&1; then
    ok icons-verify; cat /tmp/icons_verify.log
else bad icons-verify; cat /tmp/icons_verify.log; fi

echo "== building fdesigner =="
if make clean > /tmp/icons_make.log 2>&1 && make >> /tmp/icons_make.log 2>&1; then
    ok make
    if grep -qi "warning" /tmp/icons_make.log; then
        bad make-no-warnings; grep -i "warning" /tmp/icons_make.log
    else ok make-no-warnings; fi
else bad make; cat /tmp/icons_make.log; exit 1; fi

echo "== self-test (36/36 icons + no-scroll checks are inside) =="
if FDESIGNER_SELFTEST=1 ./fdesigner > /tmp/icons_selftest.log 2>&1; then
    grep -q "SELFTEST PASS" /tmp/icons_selftest.log && ok selftest || \
        { bad selftest; tail -3 /tmp/icons_selftest.log; }
else bad selftest; tail -3 /tmp/icons_selftest.log; fi

echo "== launching designer under Xvfb ($DISPLAY) =="
./fdesigner > /tmp/icons_app.log 2>&1 &
APP_PID=$!
trap 'kill $APP_PID 2>/dev/null || true' EXIT
sleep 3
if ! kill -0 $APP_PID 2>/dev/null; then
    bad "app-start"; cat /tmp/icons_app.log; exit 1
fi
ok app-start

MAIN=$(xdotool search --name "FDesigner — C\+\+/GTK" | head -1)
PAL=$(xdotool search --name "FDesigner — Widgets" | head -1)
CODE=$(xdotool search --name "FDesigner — Code" | head -1)
[ -n "$PAL" ] && ok palette-window || bad palette-window

# Palette still exactly 250x780.
if [ -n "$PAL" ]; then
    geom() { xdotool getwindowgeometry "$1" | sed -n \
        -e 's/ *Position: \([0-9]*\),\([0-9]*\).*/X=\1; Y=\2/p' \
        -e 's/ *Geometry: \([0-9]*\)x\([0-9]*\)/W=\1; H=\2/p'; }
    eval "$(geom "$PAL")"
    echo "palette ${W}x${H}@${X},${Y}"
    if [ "$W" -eq 250 ] && [ "$H" -eq 780 ]; then ok palette-250x780;
    else bad palette-250x780; fi
fi

# Screenshot the palette; count section-accent pixels (icons carry the
# accent, labels are dark ink) - proves glyphs are really painted.
if [ -n "$PAL" ]; then
    import -window "$PAL" /tmp/icons_palette.png
    ACCENT=$(python3 - "$PAL" << "PYEOF" 2>/dev/null
import sys
from PIL import Image
im = Image.open("/tmp/icons_palette.png").convert("RGB")
accents = [(37, 99, 235), (22, 163, 74), (8, 145, 178),
           (234, 88, 12), (147, 51, 234)]
n = 0
for r, g, b in im.getdata():
    for (ar, ag, ab) in accents:
        if abs(r - ar) <= 10 and abs(g - ag) <= 10 and abs(b - ab) <= 10:
            n += 1
            break
print(n)
PYEOF
)
    echo "accent pixels in palette shot: ${ACCENT:-?}"
    if [ "${ACCENT:-0}" -ge 300 ] 2>/dev/null; then ok palette-icons-painted;
    else bad palette-icons-painted; fi
fi

# Clicking a palette button still places a widget (canvas changes).
if [ -n "$PAL" ] && [ -n "$MAIN" ]; then
    eval "$(xdotool getwindowgeometry "$MAIN" | sed -n \
        -e 's/ *Position: \([0-9]*\),\([0-9]*\).*/MX=\1; MY=\2/p')"
    eval "$(xdotool getwindowgeometry "$PAL" | sed -n \
        -e 's/ *Position: \([0-9]*\),\([0-9]*\).*/PX=\1; PY=\2/p')"
    CX=$((MX + 10)); CY=$((MY + 30))
    import -window root -crop 500x220+${CX}+${CY} /tmp/icons_before.png
    xdotool mousemove $((PX + 62)) $((PY + 39)) click 1; sleep 0.7
    # Memo button (Lazarus - Basic, left column, first row of its section).
    xdotool mousemove $((PX + 62)) $((PY + 269)) click 1; sleep 0.7
    import -window root -crop 500x220+${CX}+${CY} /tmp/icons_after.png
    DIFF=$(compare -metric AE -fuzz 5% /tmp/icons_before.png \
        /tmp/icons_after.png /tmp/icons_diff.png 2>&1 || true)
    echo "canvas pixel diff: $DIFF"
    if [ "${DIFF:-0}" -gt 1000 ] 2>/dev/null; then ok palette-click-places;
    else bad palette-click-places; fi
fi

# Screenshots for the record.
if [ -n "$PAL" ]; then
    import -window "$PAL" screenshots/15_palette_icons.png
    ok shot-palette
fi
import -window root screenshots/16_three_windows_icons.png
ok shot-desktop

# No GTK warnings/criticals in the app log.
if grep -qE "Gtk-CRITICAL|Gdk-WARNING" /tmp/icons_app.log; then
    bad "app-log-warnings"; grep -E "Gtk-CRITICAL|Gdk-WARNING" /tmp/icons_app.log
else ok app-log-clean; fi

kill $APP_PID 2>/dev/null || true
trap - EXIT
echo "== palette_icons_tests: $pass passed, $fail failed ==$failed"
[ $fail -eq 0 ]

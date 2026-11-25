#!/usr/bin/env bash
# Palette-window tests: the widget palette is its own top-level window
# ("FDesigner — Widgets"), tight 2-column Lazarus-style grid, opens with
# the designer, closes (hide, app survives) and reopens from View menu.
# Uses a tiny Xlib helper to send a real WM_DELETE_WINDOW (xdotool
# windowclose destroys the X window outright under bare Xvfb, which is
# not what a window manager does).
set -u
cd "$(dirname "$0")/.."

pass=0; fail=0; failed=""
ok()   { echo "PASS $1"; pass=$((pass+1)); }
bad()  { echo "FAIL $1"; fail=$((fail+1)); failed="$failed $1"; }

# Re-run the body under Xvfb when no display is set.
if [ -z "${DISPLAY:-}" ]; then
    PALETTE_TEST_IN_XVFB=1 xvfb-run -a -s "-screen 0 2560x1440x24" "$0"
    exit $?
fi

echo "== building fdesigner =="
if make > /tmp/palette_make.log 2>&1; then ok make; else
    bad make; cat /tmp/palette_make.log; exit 1
fi

echo "== building Xlib close helper =="
cat > /tmp/palette_wmclose.c << "C_EOF"
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char** argv) {
    Display* d = XOpenDisplay(NULL);
    if (!d) return 2;
    Window w = (Window)strtoul(argv[1], NULL, 0);
    Atom trans = XInternAtom(d, "WM_TRANSIENT_FOR", False);
    Atom actual; int fmt; unsigned long n, extra; unsigned char* prop = NULL;
    XGetWindowProperty(d, w, trans, 0, 1, False, XA_WINDOW,
                       &actual, &fmt, &n, &extra, &prop);
    printf("TRANSIENT=%lu\n", n);
    if (prop) XFree(prop);
    Atom protos = XInternAtom(d, "WM_PROTOCOLS", False);
    Atom del = XInternAtom(d, "WM_DELETE_WINDOW", False);
    Atom* plist = NULL; int count = 0, has_del = 0;
    if (XGetWMProtocols(d, w, &plist, &count)) {
        for (int i = 0; i < count; i++) if (plist[i] == del) has_del = 1;
        XFree(plist);
    }
    printf("DELETE_PROTOCOL=%d\n", has_del);
    if (argc > 2 && !strcmp(argv[2], "close")) {
        XEvent ev; memset(&ev, 0, sizeof(ev));
        ev.xclient.type = ClientMessage;
        ev.xclient.window = w;
        ev.xclient.message_type = protos;
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = del;
        ev.xclient.data.l[1] = CurrentTime;
        XSendEvent(d, w, False, NoEventMask, &ev);
        XSync(d, False);
        printf("SENT_CLOSE\n");
    }
    XCloseDisplay(d);
    return 0;
}
C_EOF
if gcc -O2 -Wall -Wextra -o /tmp/palette_wmclose /tmp/palette_wmclose.c -lX11 \
        > /tmp/palette_wmclose_build.log 2>&1; then ok wmclose-helper-build;
else bad wmclose-helper-build; cat /tmp/palette_wmclose_build.log; exit 1; fi

echo "== launching designer under Xvfb ($DISPLAY) =="
./fdesigner > /tmp/palette_app.log 2>&1 &
APP_PID=$!
trap 'kill $APP_PID 2>/dev/null || true' EXIT
sleep 3
if ! kill -0 $APP_PID 2>/dev/null; then
    bad "app-start"; cat /tmp/palette_app.log; exit 1
fi
ok app-start

MAIN=$(xdotool search --name "FDesigner — C\+\+/GTK" | head -1)
PAL=$(xdotool search --name "FDesigner — Widgets" | head -1)
[ -n "$MAIN" ] && ok main-window || bad main-window
[ -n "$PAL" ] && ok palette-window || bad palette-window
if [ -n "$MAIN" ] && [ -n "$PAL" ] && [ "$MAIN" != "$PAL" ]; then
    ok distinct-windows
else bad distinct-windows; fi

# Palette must be a plain top-level window, not transient/modal.
if [ -n "$PAL" ]; then
    OUT=$(/tmp/palette_wmclose "$PAL")
    echo "$OUT" | grep -q "TRANSIENT=0" && ok not-transient || bad not-transient
    echo "$OUT" | grep -q "DELETE_PROTOCOL=1" && ok delete-protocol || \
        bad delete-protocol
fi

# Palette parked beside the designer (left or right, not overlapping it).
geom() { # $1 = window id, $2/$3 = name prefix for X/Y/W/H vars
    eval "$(xdotool getwindowgeometry "$1" | sed -n \
        -e 's/ *Position: \([0-9]*\),\([0-9]*\).*/'"$2"'X=\1; '"$2"'Y=\2/p' \
        -e 's/ *Geometry: \([0-9]*\)x\([0-9]*\)/'"$2"'W=\1; '"$2"'H=\2/p')"
}
if [ -n "$MAIN" ] && [ -n "$PAL" ]; then
    geom "$MAIN" M
    geom "$PAL" P
    echo "main ${MW}x${MH}@${MX},${MY}  palette ${PW}x${PH}@${PX},${PY}"
    if [ "$PX" -eq $((MX - PW - 12)) ] || [ "$PX" -eq $((MX + MW + 12)) ]; then
        ok palette-beside-main
    else bad palette-beside-main; fi
    if [ "$PW" -ge 200 ] && [ "$PW" -le 300 ]; then ok palette-width;
    else bad palette-width; fi
fi

# Clicking palette buttons places widgets on the canvas: screenshot the
# canvas corner before/after clicking Button, then Memo (Lazarus).
if [ -n "$PAL" ] && [ -n "$MAIN" ]; then
    CX=$((MX + 10)); CY=$((MY + 30))
    import -window root -crop 500x220+${CX}+${CY} /tmp/palette_before.png
    # First button (Button, left column, first grid row).
    xdotool mousemove $((PX + 62)) $((PY + 39)) click 1; sleep 0.7
    # Memo button (Lazarus — Basic, left column).
    xdotool mousemove $((PX + 62)) $((PY + 350)) click 1; sleep 0.7
    import -window root -crop 500x220+${CX}+${CY} /tmp/palette_after.png
    DIFF=$(compare -metric AE -fuzz 5% /tmp/palette_before.png \
        /tmp/palette_after.png /tmp/palette_diff.png 2>&1 || true)
    echo "canvas pixel diff: $DIFF"
    if [ "${DIFF:-0}" -gt 1000 ] 2>/dev/null; then ok palette-places-widgets;
    else bad palette-places-widgets; fi
fi

# Closing the palette (real WM close) hides it; the app stays alive.
if [ -n "$PAL" ]; then
    /tmp/palette_wmclose "$PAL" close > /tmp/palette_close.log 2>&1
    sleep 1
    if [ -z "$(xdotool search --onlyvisible --name "FDesigner — Widgets")" ]; then
        ok palette-hides-on-close
    else bad palette-hides-on-close; fi
    if kill -0 $APP_PID 2>/dev/null && \
       [ -n "$(xdotool search --onlyvisible --name "FDesigner — C\+\+/GTK")" ]; then
        ok app-alive-after-close
    else bad app-alive-after-close; fi
    # Reopen from View -> Widget Palette (menu coords from main geometry).
    xdotool mousemove $((MX + 110)) $((MY + 12)) click 1; sleep 0.8
    xdotool mousemove $((MX + 141)) $((MY + 89)) click 1; sleep 1
    if [ -n "$(xdotool search --onlyvisible --name "FDesigner — Widgets")" ]; then
        ok palette-reopens-from-view-menu
    else bad palette-reopens-from-view-menu; fi
    # Clicking the canvas still works after palette + design interplay.
    xdotool mousemove $((MX + 300)) $((MY + 400)) click 1; sleep 0.5
    kill -0 $APP_PID 2>/dev/null && ok canvas-click-after-reopen || \
        bad canvas-click-after-reopen
fi

# No GTK warnings/criticals from the palette windows in the app log.
if grep -qE "Gtk-CRITICAL|Gdk-WARNING" /tmp/palette_app.log; then
    bad "app-log-warnings"; grep -E "Gtk-CRITICAL|Gdk-WARNING" /tmp/palette_app.log
else ok app-log-clean; fi

kill $APP_PID 2>/dev/null || true
trap - EXIT
echo "== palette_window_tests: $pass passed, $fail failed ==$failed"
[ $fail -eq 0 ]

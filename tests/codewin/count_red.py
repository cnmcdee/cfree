#!/usr/bin/env python3
"""Count red-ish pixels in a screenshot (the paused-line band is
#c00000, breakpoint dots are the same red).

Usage: count_red.py <png> [x0 x1] [band]
  Prints the red pixel count in columns [x0, x1).
  With "band": also prints the top/bottom row of the densest run of
  rows that each contain >100 red pixels (the full-line paused band),
  as band_top=.. band_bottom=.. band_rows=..
"""
import sys

from PIL import Image

im = Image.open(sys.argv[1]).convert("RGB")
px = im.load()
w, h = im.size
x0 = int(sys.argv[2]) if len(sys.argv) > 2 else 0
x1 = int(sys.argv[3]) if len(sys.argv) > 3 else w
x1 = min(x1, w)

n = 0
band_rows = []
for y in range(h):
    row = 0
    for x in range(x0, x1):
        r, g, b = px[x, y]
        if r > 150 and g < 70 and b < 70:
            n += 1
            row += 1
    if row > 100:
        band_rows.append(y)
print(n)
if len(sys.argv) > 4 and sys.argv[4] == "band":
    if band_rows:
        print("band_top=%d band_bottom=%d band_rows=%d"
              % (band_rows[0], band_rows[-1], len(band_rows)))
    else:
        print("band_top=-1 band_bottom=-1 band_rows=0")

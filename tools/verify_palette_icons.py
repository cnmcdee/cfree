#!/usr/bin/env python3
"""Verify icons/<Key>.png: one 16x16 RGBA PNG per palette type key in
src/model.h (kTypes), non-blank, all distinct. Run from the repo root:
python3 tools/verify_palette_icons.py"""
import hashlib
import os
import sys
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

KEYS = ["Button", "Label", "Entry", "Checkbutton", "Radiobutton", "Frame",
        "Combobox", "Listbox", "Menubutton", "Canvas", "Scale", "Scrollbar",
        "Text", "Toplevel", "Spreadsheet", "StockChart", "Memo", "GroupBox",
        "Panel", "Image", "Shape", "Notebook", "ScrollBox", "Splitter",
        "StatusBar", "ToolBar", "MenuBar", "SpinEdit", "TrackBar",
        "ProgressBar", "DateTimePicker", "ColorBox", "ListView", "TreeView",
        "StringGrid", "Plot"]


def main():
    ok = True
    hashes = {}
    for key in KEYS:
        path = os.path.join(ROOT, "icons", key + ".png")
        if not os.path.isfile(path):
            print(f"MISSING {key}.png")
            ok = False
            continue
        im = Image.open(path)
        if im.size != (16, 16) or im.mode != "RGBA":
            print(f"BAD FORMAT {key}.png: {im.size} {im.mode}")
            ok = False
            continue
        vis = [p for p in im.getdata() if p[3] > 0]
        if len(vis) < 25 or len(set(vis)) < 2:
            print(f"BLANK/FLAT {key}.png: {len(vis)} visible px, "
                  f"{len(set(vis))} colours")
            ok = False
            continue
        h = hashlib.md5(im.tobytes()).hexdigest()
        if h in hashes:
            print(f"DUPLICATE {key}.png == {hashes[h]}.png")
            ok = False
        hashes[h] = key
    print(f"palette icons: {len(hashes)}/{len(KEYS)} distinct, "
          f"{'OK' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

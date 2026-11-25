#!/usr/bin/env python3
"""Generate one 16x16 PNG icon per FDesigner palette widget (icons/<Key>.png).

Purpose-drawn glyphs (PIL ImageDraw), NOT downscaled screenshots: at 16x16
a screenshot is mush, a 2-3 element glyph stays readable.

Style matches the CWidgets icons (tools/gen_icons.py in the CWidgets repo):
dark ink + mid grey + light fill, one accent colour per palette section.
The glyphs are drawn on white and the white is then turned transparent, so
each icon sits directly on the GTK button face with no white box around it.

Glyph functions below are copied verbatim from the CWidgets icon generator
(same style, same constants) except g_stockchart / g_scrollbox / g_listview,
which are new here because FDesigner has no CWidgets twin for them.

Usage: python3 tools/gen_palette_icons.py   (run from the repo root)
"""
import os, sys
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

INK = (30, 30, 30)
MID = (140, 140, 140)
LIGHT = (226, 228, 232)
WHITE = (255, 255, 255)
GREEN = (22, 163, 74)
RED = (220, 38, 38)

# Accent colour per palette section (same palette as CWidgets sections).
BLUE = (37, 99, 235)      # Widgets
LAZ_GREEN = (22, 163, 74)  # Lazarus - Basic
CYAN = (8, 145, 178)       # Containers
ORANGE = (234, 88, 12)     # Input
PURPLE = (147, 51, 234)    # Data / Display


def g_button(d, a):
    d.rounded_rectangle([1, 4, 14, 11], radius=2, outline=INK, fill=LIGHT)
    d.line([4, 7, 11, 7], fill=a, width=2)

def g_label(d, a):
    d.line([2, 4, 13, 4], fill=INK, width=2)
    d.line([2, 8, 10, 8], fill=a, width=2)
    d.line([2, 12, 13, 12], fill=MID, width=2)

def g_entry(d, a):
    d.rectangle([1, 4, 14, 11], outline=INK, fill=WHITE)
    d.line([4, 7, 4, 9], fill=a, width=2)
    d.line([6, 8, 12, 8], fill=MID)

def g_check(d, a):
    d.rectangle([1, 3, 8, 10], outline=INK, fill=WHITE)
    d.line([2, 6, 4, 8, 7, 4], fill=a, width=2)
    d.line([10, 5, 14, 5], fill=MID); d.line([10, 8, 14, 8], fill=MID)

def g_radio(d, a):
    d.ellipse([1, 2, 8, 9], outline=INK, fill=WHITE)
    d.ellipse([3, 4, 6, 7], fill=a)
    d.line([10, 4, 14, 4], fill=MID); d.line([10, 7, 14, 7], fill=MID)

def g_frame(d, a):
    d.rectangle([1, 3, 14, 14], outline=INK)
    d.line([1, 3, 4, 3], fill=INK); d.line([9, 3, 14, 3], fill=INK)
    d.line([5, 1, 8, 1], fill=a, width=2)

def g_combo(d, a):
    d.rectangle([1, 4, 14, 11], outline=INK, fill=WHITE)
    d.line([10, 4, 10, 11], fill=INK)
    d.polygon([(11, 6), (14, 6), (12, 9)], fill=a)

def g_list(d, a):
    d.rectangle([1, 1, 14, 14], outline=INK, fill=WHITE)
    for y in (3, 7, 11):
        d.rectangle([3, y, 5, y + 1], fill=a)
        d.line([7, y, 12, y], fill=MID)

def g_menu(d, a):
    d.rectangle([1, 1, 14, 14], outline=INK, fill=WHITE)
    d.line([3, 4, 12, 4], fill=INK); d.line([3, 7, 12, 7], fill=MID)
    d.line([3, 10, 12, 10], fill=MID); d.line([3, 13, 8, 13], fill=MID)

def g_pencil(d, a):
    d.line([3, 12, 11, 4], fill=a, width=2)
    d.polygon([(11, 4), (13, 2), (13, 5)], fill=INK)
    d.line([2, 13, 5, 13], fill=INK)

def g_slider(d, a):
    d.line([1, 8, 14, 8], fill=INK, width=2)
    d.rectangle([6, 4, 9, 11], outline=INK, fill=a)
    d.line([3, 12, 3, 14], fill=MID); d.line([12, 12, 12, 14], fill=MID)

def g_scrollbar(d, a):
    d.rectangle([9, 1, 14, 14], outline=INK, fill=WHITE)
    d.rectangle([10, 4, 13, 10], fill=a)
    d.rectangle([1, 1, 7, 14], outline=INK, fill=WHITE)
    d.line([3, 4, 5, 4], fill=MID); d.line([3, 7, 5, 7], fill=MID); d.line([3, 10, 5, 10], fill=MID)

def g_textview(d, a):
    d.rectangle([1, 1, 14, 14], outline=INK, fill=WHITE)
    d.line([3, 4, 12, 4], fill=INK); d.line([3, 7, 12, 7], fill=MID)
    d.line([3, 10, 9, 10], fill=MID); d.line([3, 12, 12, 12], fill=MID)

def g_window(d, a):
    d.rectangle([1, 1, 14, 14], outline=INK, fill=WHITE)
    d.rectangle([1, 1, 14, 4], fill=a)
    d.point((3, 2), fill=WHITE); d.point((5, 2), fill=WHITE)
    d.line([3, 8, 11, 8], fill=MID); d.line([3, 11, 8, 11], fill=MID)

def g_spreadsheet(d, a):
    d.rectangle([1, 1, 14, 14], outline=INK, fill=WHITE)
    d.rectangle([1, 1, 14, 4], fill=(22, 163, 74))
    for x in (5, 10):
        d.line([x, 1, x, 14], fill=INK)
    for y in (8, 11):
        d.line([1, y, 14, y], fill=INK)

def g_memo(d, a):
    g_textview(d, a)
    d.line([11, 1, 11, 4, 14, 4], fill=INK)

def g_checkgroup(d, a):
    d.rectangle([1, 3, 14, 14], outline=INK)
    d.line([1, 3, 4, 3], fill=INK); d.line([9, 3, 14, 3], fill=INK)
    d.rectangle([3, 6, 6, 9], outline=INK)
    d.line([3, 7, 5, 9, 6, 6], fill=a)
    d.rectangle([3, 11, 6, 14], outline=INK)
    d.line([8, 7, 12, 7], fill=MID); d.line([8, 12, 12, 12], fill=MID)

def g_box(d, a):
    d.rectangle([1, 1, 14, 14], outline=INK, fill=WHITE)
    d.line([5, 1, 5, 14], fill=a); d.line([10, 1, 10, 14], fill=a)

def g_image(d, a):
    d.rectangle([1, 1, 14, 14], outline=INK, fill=WHITE)
    d.ellipse([4, 3, 6, 5], fill=a)
    d.line([1, 12, 6, 7, 9, 10, 14, 5], fill=INK, width=1)

def g_shape(d, a):
    d.ellipse([2, 2, 9, 9], outline=INK)
    d.rectangle([7, 7, 14, 14], outline=a, fill=WHITE)

def g_tabs(d, a):
    d.rectangle([2, 1, 7, 4], outline=INK, fill=a)
    d.rectangle([8, 1, 13, 4], outline=INK, fill=LIGHT)
    d.rectangle([1, 4, 14, 14], outline=INK, fill=WHITE)
    d.line([3, 8, 11, 8], fill=MID); d.line([3, 11, 8, 11], fill=MID)

def g_splitter(d, a):
    d.rectangle([1, 1, 6, 14], outline=INK, fill=WHITE)
    d.rectangle([9, 1, 14, 14], outline=INK, fill=WHITE)
    d.line([7, 4, 8, 4], fill=a, width=2); d.line([7, 7, 8, 7], fill=a, width=2)
    d.line([7, 10, 8, 10], fill=a, width=2)

def g_statusbar(d, a):
    d.rectangle([1, 1, 14, 14], outline=INK, fill=WHITE)
    d.rectangle([1, 11, 14, 14], fill=LIGHT, outline=INK)
    d.line([11, 12, 13, 14], fill=a); d.line([12, 12, 14, 14], fill=a)

def g_toolbar(d, a):
    d.rectangle([1, 1, 14, 5], fill=LIGHT, outline=INK)
    d.rectangle([2, 2, 5, 4], fill=a)
    d.rectangle([7, 2, 9, 4], outline=INK); d.rectangle([11, 2, 13, 4], outline=INK)
    d.rectangle([1, 6, 14, 14], outline=INK, fill=WHITE)
    d.line([3, 9, 11, 9], fill=MID); d.line([3, 12, 8, 12], fill=MID)

def g_menubar(d, a):
    d.rectangle([1, 1, 14, 4], fill=a)
    d.rectangle([2, 2, 5, 3], fill=WHITE); d.rectangle([7, 2, 10, 3], fill=WHITE)
    d.rectangle([1, 5, 14, 14], outline=INK, fill=WHITE)
    d.line([3, 8, 11, 8], fill=MID); d.line([3, 11, 8, 11], fill=MID)

def g_spin(d, a):
    d.rectangle([1, 3, 14, 12], outline=INK, fill=WHITE)
    d.line([10, 3, 10, 12], fill=INK)
    d.polygon([(11, 6), (13, 6), (12, 4)], fill=a)
    d.polygon([(11, 9), (13, 9), (12, 11)], fill=a)
    d.line([3, 7, 8, 7], fill=MID)

def g_trackbar(d, a):
    d.line([1, 8, 14, 8], fill=INK, width=2)
    for x in (3, 7, 11, 13):
        d.line([x, 5, x, 11], fill=MID)
    d.rectangle([6, 4, 9, 11], outline=INK, fill=a)

def g_progress(d, a):
    d.rectangle([1, 5, 14, 10], outline=INK, fill=WHITE)
    d.rectangle([2, 6, 9, 9], fill=a)

def g_clock(d, a):
    d.ellipse([2, 2, 13, 13], outline=INK, width=2)
    d.line([7, 7, 7, 4], fill=a, width=2)
    d.line([7, 7, 10, 9], fill=INK, width=2)
    d.line([7, 1, 7, 2], fill=INK)

def g_color(d, a):
    d.rectangle([1, 1, 7, 7], fill=(220, 38, 38))
    d.rectangle([8, 1, 14, 7], fill=(234, 179, 8))
    d.rectangle([1, 8, 7, 14], fill=(37, 99, 235))
    d.rectangle([8, 8, 14, 14], fill=(22, 163, 74))
    d.rectangle([1, 1, 14, 14], outline=INK)

def g_tree(d, a):
    d.rectangle([1, 1, 14, 14], outline=INK, fill=WHITE)
    d.line([3, 3, 3, 12], fill=MID)
    for y in (3, 7, 11):
        d.line([3, y, 5, y], fill=MID)
        d.rectangle([6, y - 1, 8, y + 1], fill=a)
        d.line([10, y, 12, y], fill=MID)

def g_grid(d, a):
    d.rectangle([1, 1, 14, 14], outline=INK, fill=WHITE)
    d.rectangle([1, 1, 14, 4], fill=a)
    for x in (5, 10):
        d.line([x, 1, x, 14], fill=INK)
    for y in (8, 11):
        d.line([1, y, 14, y], fill=INK)

def g_graph(d, a):
    d.line([2, 2, 2, 13], fill=INK, width=2)
    d.line([2, 13, 14, 13], fill=INK, width=2)
    d.line([3, 10, 6, 7, 9, 9, 13, 3], fill=a, width=2)

def g_stockchart(d, a):
    # Candlestick chart: axes + three candles (up / down / up).
    d.line([2, 2, 2, 13], fill=INK, width=2)
    d.line([2, 13, 14, 13], fill=INK, width=2)
    d.line([4, 5, 4, 12], fill=INK)
    d.rectangle([3, 7, 5, 11], fill=GREEN, outline=INK)
    d.line([8, 3, 8, 10], fill=INK)
    d.rectangle([7, 4, 9, 8], fill=RED, outline=INK)
    d.line([12, 6, 12, 12], fill=INK)
    d.rectangle([11, 8, 13, 11], fill=a, outline=INK)

def g_scrollbox(d, a):
    # Container box with a scrollbar thumb on its right edge.
    d.rectangle([1, 1, 14, 14], outline=INK, fill=WHITE)
    d.line([5, 1, 5, 14], fill=MID)
    d.line([9, 1, 9, 14], fill=MID)
    d.rectangle([11, 3, 13, 9], fill=a)

def g_listview(d, a):
    # List with a header bar (rows under a coloured header).
    d.rectangle([1, 1, 14, 14], outline=INK, fill=WHITE)
    d.rectangle([1, 1, 14, 4], fill=a)
    for y in (7, 10, 13):
        d.rectangle([3, y, 5, y + 1], fill=INK)
        d.line([7, y, 12, y], fill=MID)



# type key -> (glyph function, section accent). Keys must match kTypes in
# src/model.h exactly; file names are icons/<key>.png.
MAPPING = {
    # Widgets
    "Button": ("button", BLUE), "Label": ("label", BLUE),
    "Entry": ("entry", BLUE), "Checkbutton": ("check", BLUE),
    "Radiobutton": ("radio", BLUE), "Frame": ("frame", BLUE),
    "Combobox": ("combo", BLUE), "Listbox": ("list", BLUE),
    "Menubutton": ("menu", BLUE), "Canvas": ("pencil", BLUE),
    "Scale": ("slider", BLUE), "Scrollbar": ("scrollbar", BLUE),
    "Text": ("textview", BLUE), "Toplevel": ("window", BLUE),
    "Spreadsheet": ("spreadsheet", BLUE), "StockChart": ("stockchart", BLUE),
    # Lazarus - Basic
    "Memo": ("memo", LAZ_GREEN), "GroupBox": ("checkgroup", LAZ_GREEN),
    "Panel": ("box", LAZ_GREEN), "Image": ("image", LAZ_GREEN),
    "Shape": ("shape", LAZ_GREEN),
    # Containers
    "Notebook": ("tabs", CYAN), "ScrollBox": ("scrollbox", CYAN),
    "Splitter": ("splitter", CYAN), "StatusBar": ("statusbar", CYAN),
    "ToolBar": ("toolbar", CYAN), "MenuBar": ("menubar", CYAN),
    # Input
    "SpinEdit": ("spin", ORANGE), "TrackBar": ("trackbar", ORANGE),
    "ProgressBar": ("progress", ORANGE), "DateTimePicker": ("clock", ORANGE),
    "ColorBox": ("color", ORANGE),
    # Data / Display
    "ListView": ("listview", PURPLE), "TreeView": ("tree", PURPLE),
    "StringGrid": ("grid", PURPLE), "Plot": ("graph", PURPLE),
}

GLYPHS = {
    "box": g_box,
    "button": g_button,
    "check": g_check,
    "checkgroup": g_checkgroup,
    "clock": g_clock,
    "color": g_color,
    "combo": g_combo,
    "entry": g_entry,
    "frame": g_frame,
    "graph": g_graph,
    "grid": g_grid,
    "image": g_image,
    "label": g_label,
    "list": g_list,
    "listview": g_listview,
    "memo": g_memo,
    "menu": g_menu,
    "menubar": g_menubar,
    "pencil": g_pencil,
    "progress": g_progress,
    "radio": g_radio,
    "scrollbar": g_scrollbar,
    "scrollbox": g_scrollbox,
    "shape": g_shape,
    "slider": g_slider,
    "spin": g_spin,
    "splitter": g_splitter,
    "spreadsheet": g_spreadsheet,
    "statusbar": g_statusbar,
    "stockchart": g_stockchart,
    "tabs": g_tabs,
    "textview": g_textview,
    "toolbar": g_toolbar,
    "trackbar": g_trackbar,
    "tree": g_tree,
    "window": g_window,
}


def main():
    outdir = os.path.join(ROOT, "icons")
    os.makedirs(outdir, exist_ok=True)
    for key, (glyph, accent) in sorted(MAPPING.items()):
        img = Image.new("RGB", (16, 16), WHITE)
        d = ImageDraw.Draw(img)
        GLYPHS[glyph](d, accent)
        # Exact white -> transparent: the glyph then sits on the button
        # face instead of inside a white box.
        img = img.convert("RGBA")
        px = [(r, g, b, 0) if (r, g, b) == WHITE else (r, g, b, 255)
              for (r, g, b, a) in img.getdata()]
        img.putdata(px)
        img.save(os.path.join(outdir, key + ".png"))
    print(f"icons written: {len(MAPPING)} in {outdir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

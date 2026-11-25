# FDesigner — C++/GTK conversion design

## What we're building

A C++ rebuild of Chris's fdesigner (Tkinter Rapid Designer). The original is a
~7,000-line Python/Tkinter app (`original/tkinter_designer/fdesigner7.py`):
place widgets on a canvas at absolute positions, edit their properties, then
generate runnable source code for the layout.

Per Chris's decision (2026-11-25) the C++ version **generates C++/GTK code**,
not Tkinter Python. The designer's own UI is also GTK, defined in Glade .ui
files. No Qt anywhere.

## Stack

- **GTK 3.24** — Glade's target toolkit. (GTK 4 has no Glade; its successor is
  Cambalache. Glade means GTK 3.)
- **C++ with the GTK C API**, windows loaded via **GtkBuilder** from .ui files
  authored in Glade. No gtkmm: GtkBuilder is the native path, and this keeps
  the generated code and the designer itself on the same API.
- Runs on **X11** (GTK 3's X11 backend), same as the original Tk app.
- Build: g++ + pkg-config (`gtk+-3.0`), plain Makefile, like Chris's other
  tools.
- Toolchain verified on Muse's VM 2026-11-25: GTK 3.24.41 headers + Glade
  installed; a C++ test program including <gtk/gtk.h> compiles, links, and
  runs under Xvfb. (VM quirk: apt's libzstd-dev version pin conflicts with the
  VM's patched libzstd1, so the -dev packages were installed from downloaded
  .debs with `dpkg --force-depends` — headers only, no runtime change. On
  Chris's Parrot box a plain `apt install libgtk-3-dev glade` should work.)

## Generated output

For each designed window the generator emits two files, mirroring the spirit
of the Python tool's generated class + stub handlers:

1. **`<name>.ui`** — a Glade/GtkBuilder file holding the layout: a GtkWindow
   containing a GtkFixed, with each widget placed at its designed x/y and
   sized with width/height requests (the GTK equivalent of Tk's `.place()`).
2. **`<name>.h` / `<name>.cpp`** — a C++ class that loads the .ui with
   GtkBuilder, fetches each widget by id, and declares one stub signal
   handler per connected signal (`// Code block here`, like the original's
   stubs), ready for the user to fill in.

The generated project compiles standalone with:
`g++ -o app *.cpp $(pkg-config --cflags --libs gtk+-3.0)`

## Widget mapping (Tk -> GTK)

| Tk (original palette) | GTK 3 |
|---|---|
| Root / Toplevel | GtkWindow |
| Frame / Labelframe | GtkFrame |
| Button | GtkButton |
| Label | GtkLabel |
| Entry | GtkEntry |
| Checkbutton | GtkCheckButton |
| Radiobutton | GtkRadioButton |
| Combobox (ttk) | GtkComboBoxText |
| Listbox | GtkListBox (inside GtkScrolledWindow) |
| Menu | GtkMenu / GtkMenuBar |
| Menubutton | GtkMenuButton |
| Canvas | GtkDrawingArea |
| Scale | GtkScale |
| Scrollbar | GtkScrollbar |
| Text | GtkTextView (inside GtkScrolledWindow) |
| Spreadsheet (custom) | GtkGrid of GtkEntry (later phase) |

## Internal model

```cpp
struct Widget {
    std::string name;                 // e.g. "Button_001"
    std::string type;                 // palette type key, e.g. "Button"
    int x, y, width, height;          // canvas geometry
    std::map<std::string,std::string> props;  // GTK properties, as strings
    std::vector<std::string> signals; // signals to stub out in generated code
};
struct Project {
    std::string title, class_name;
    int win_width, win_height;
    std::string background;           // window background colour
    std::vector<Widget> widgets;      // Root is implicit (the window itself)
};
```

## Project file format

**JSON** (`.fdj`), replacing Python pickle, which C++ cannot read. If Chris
has old .pkl designs he wants to keep, a one-off Python converter script
(pkl -> fdj) can be written later; pickle stays on the Python side only.

## What does not port directly

- **pickle** save/load -> JSON (above).
- **pyperclip** -> GtkClipboard (built into GTK).
- **openpyxl** spreadsheet modeller -> later phase; the on-canvas spreadsheet
  widget is a GtkGrid; xlsx import, if wanted, needs a separate library.
- The huge generated colour files in `library/` (super_color.py ~31k lines,
  colormenu.py ~15k lines) are data, not logic: they become colour-name data
  tables / GTK colour chooser integration, not hand-ported code.

## Phases

1. **Skeleton + canvas**: main window from Glade .ui; palette adds widgets to
   a GtkFixed canvas; click to select, drag to move, delete. *(skeleton done)*
2. **Property editor**: per-type property tables (ported from the original's
   Entry_001..019 mapping), plus the original's bulk operations — set
   parameter on all / selected / filtered widgets, clear parameter.
3. **Code generator**: emit .ui + .h/.cpp per the format above; generated
   project compiles and runs.
4. **Save/load**: JSON project files; optional .pkl -> .fdj converter.
5. **Extras**: colour palette, menu designer, spreadsheet widget, canvas
   widget (drawing area) support.

**Status 2026-11-25: all five phases built and verified.** The self-test
(`FDESIGNER_SELFTEST=1 ./fdesigner`) exercises placement, properties, bulk
ops, generation, save/load round-trips, and the Phase 5 extras; generated
projects compile with their own Makefile and run. Known v1 limits:
designing *inside* a Toplevel or a generated menu's submenus is not
supported (Toplevel content starts as an empty GtkFixed; menus are a
one-level text spec), and there is no .pkl converter unless old designs
need importing.

**Generator revision 2026-11-25 (Chris's direction):** generated projects
are ALL CODE (no .ui — so a stepping debugger can walk construction) and
every widget is a STANDALONE CLASS in its own .h/.cpp pair, mirroring the
Python generator's boilerplate: per-widget `// #region`, creation +
`.place()`-equivalent + config in the constructor, a `util_error_print`
helper, and the same nine mouse handlers per widget (buttonPress1-3,
dblbuttonPress1-3, buttonRelease1-3) routed by two dispatchers; the
window class keeps the Root_001 handler set and just instantiates the
widget classes. Verified by `tests/run_matrix.sh`: 13 generated designs
(every type, every property, every enum value, menus, hostile strings,
class-name edge cases, 40 widgets, spreadsheet extremes) each compile
with -Wall -Wextra, zero warnings, and run clean. The matrix caught a
real bug: property ranges (e.g. Label xalign 0..1) were not enforced, so
an out-of-range value could reach GTK — canonicalize() now enforces the
property tables' lo/hi for Int and Double.

**StockChart widget added 2026-11-25:** a candle chart for Massive
aggregates CSVs (header `ticker,volume,open,close,high,low,window_start,
transactions`, matched by column name). Properties: `data-file`,
`up-color`, `down-color`, `max-candles`. The designer plots a built-in
preview series on the canvas and swaps in real data when data-file is
set; the generated StockChart class is self-contained — it embeds the
CSV loader (`load_csv`, plus `set_candles`/`candle_count`) and cairo
plotting (wicks + bodies, min/max scaled). Fake data for trying it:
`tests/data/SPY_fake.csv` (120 synthetic SPY minute bars). The matrix
grew to 14 cases plus a render test that draws the generated chart
offscreen and counts up/down-coloured pixels, proving it plots.

**Indicator submodules added 2026-11-25:** `src/indicators.h` is ONE
self-contained header holding **250 named technical indicators** —
moving averages and Ehlers/Hilbert filters, bands/channels, five pivot
families, momentum oscillators, volume indicators, volatility and
statistics, and all 61 TA-Lib candlestick patterns plus 5 extras. Each
is a registry module (namespace `fdi`) that consumes the chart's OHLCV
bars and draws itself: price-pane overlays share the price scale,
oscillators get their own labelled pane strip, patterns draw markers.
They have no palette button; a StockChart enables them via its
`indicators` property (`"SMA(20), RSI, MACD(12,26,9), Engulfing"`) in
the designer, or `add_indicator("SMA(20)")` in generated code. The
generator embeds the header text (via `src/indicators_embedded.h`,
regenerated by `tools/embed_indicators.sh` from the Makefile rule) and
copies it into any generated project that contains a StockChart.
Candle structs carry `volume` (OBV/VWAP/MFI need it). Testing:
`tests/indicator_test.cpp` computes and renders every indicator over
`tests/data/SPY_fake_long.csv` (1,170 bars, 3 days), counts palette-ink
pixels per render, hand-checks 18 patterns on crafted candle sequences
and bounded oscillators against their legal ranges, and writes a
labelled contact sheet of all 250 plots to
`tests/out/indicators_contact_sheet.png`. Result: 244 render-verified
on data; 6 ultra-rare gap patterns verified by crafted sequences.
Matrix case 15 covers a generated chart with indicators attached.

**StockChart Pro added 2026-11-25:** the chart gained a full
annotation system, comparative plotting, a percent scale, a time axis
and wheel zoom, in both the designer preview and generated code.

- `src/drawings.h` is ONE self-contained header (namespace `fdw`)
  holding the drawing module: 11 kinds — trend, ray, horizontal and
  vertical lines, rectangle, ellipse, arrow, text labels, Fibonacci
  retracement, freehand, and a measure tool (Δ price / % / bars).
  Anchors are DATA coordinates (bar index, price), never pixels, so
  annotations survive widget resizes, zooms and pans; every render
  re-maps through `fdw::Map`. Drawings come from text specs
  (`"trend 10 655 200 672.5 red; hline 650 blue dash"`) via
  `fdw::parse_specs`, or interactively in generated apps: keys
  T H V R L A X F D M E S select trend/hline/vline/rect/ellipse/arrow/
  text/fib/freehand/measure/erase/select-move, Esc returns to pan,
  Delete removes the selected drawing, and a text tool click asks for
  the label in a dialog. The generator embeds the header
  (`src/drawings_embedded.h` via `tools/embed_drawings.sh`) and copies
  `drawings.h` into StockChart projects, like indicators.h.
- New StockChart properties: `scale-mode` (Price|Percent — percent
  relabels the right axis as change from the first visible close),
  `compare-files` (comma-separated CSVs of other tickers, aligned by
  timestamp and rebased onto the first visible close, drawn as
  coloured lines with a legend), and `drawings` (semicolon-separated
  specs). The bottom axis plots UTC time from `window_start` (date
  shown at day changes); gridlines use nice 1/2/5 steps.
- View: the mouse wheel zooms around the cursor (`zoom_at(factor,
  frac)` in code), drag pans with no tool active, `reset_view()`
  restores the max-candles window. The designer preview zooms with
  the wheel too.
- Testing: `tests/drawings_test.cpp` renders every kind and checks
  palette ink, verifies a trend line's endpoints remap at 1x and 2x
  plot sizes, and exercises hit testing. Matrix case 16
  (`16_stockchart_pro`, a 1024x768 window) designs a percent-mode
  chart with a QQQ comparison (`tests/data/QQQ_fake_long.csv`, same
  timestamp grid as SPY, made by `tests/make_compare_data.py`), SMA +
  RSI and 7 annotations; `tests/chart_pro_render_test.cpp` asserts
  candle/comparison/arrow/fib ink, percent and time axis labels,
  zoom in/out/clamp/reset, and that arrow + fib still draw after the
  widget is resized smaller. `tests/chart_screenshot.cpp` captures
  the generated app at exactly 1024x768 to
  `tests/out/chart_pro_1024x768.png`. Full regression after the
  change: designer self-test PASS, matrix 21/21.
- Gotcha found by the matrix: the generated chart's event handlers
  must not be named `on_press`/`on_release` — those are the widget
  stub dispatchers. They are `on_chart_press` etc.

## Layout of this workspace

- `original/` — the Python source, read-only reference (do not run it here).
- `ui/designer.ui` — the designer's own main window (Glade).
- `src/main.cpp` — entry point; loads the .ui via GtkBuilder.
- `Makefile` — builds `./fdesigner`.

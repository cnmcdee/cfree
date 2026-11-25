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

## Stepping debugger (GDB/MI)

**Debug menu → Open Debugger…** opens a separate debugger window (not a
panel in the main window). It drives real gdb over the MI protocol
(`gdb -q -i=mi2`, see `src/debugger.h` / `src/debugger.cpp`).

- **Toolbar:** Run, Continue, Step Over, Step Into, Step Out, Stop
  (interrupts a running target; press again to kill it), Restart, Settings.
- **Breakpoints:** type `file.cpp:123` or a function name and press Add BP;
  the list shows id / location / enabled toggle / hit count. Enable/disable
  via the toggle, Remove deletes. Breakpoints are saved to
  `~/.config/fdesigner/debugger_bps.conf` and restored on the next run.
- **Variables:** Locals (name / type / value) and a watch list (add any
  expression, e.g. `sum + 1`) refresh on every stop; the Stack pane shows
  the current frames, and the status line shows the stopped file:line and
  function. Target stdout/stderr appears in the bottom pane.
- **Settings:** the Settings toolbar button, **Debug → Debugger Settings…**,
  and the **Debug → Debugger** sub-menu (which also has Break on main /
  Stop on entry / Pretty-print values toggles). Settings live in
  `~/.config/fdesigner/debugger.conf`: `gdb_path`, `break_on_main`,
  `stop_on_entry`, `pretty_print`, `print_all_locals`, `default_args`.
  They take effect on the next Run (pretty-printing is applied when the
  gdb session starts). The same settings are honoured by the backend
  (`GdbSession`), so the headless tests exercise them too.
- **Targets:** any program compiled with `-g` — console programs and GTK
  apps alike (the GTK app runs under the same X display as the designer).
- **Tests:** `tests/debugger_tests.sh` (console suite, GTK suite under
  Xvfb, GUI smoke test) — see `tests/debugger/` for the targets and the
  headless harness `dbg_harness.cpp`, which drives `GdbSession` directly.

## CMake handling (2026-11-26)

The generator now also emits a `CMakeLists.txt` next to the `Makefile` in
every generated project, so a generated project builds with either tool.
The file sets `cmake_minimum_required(VERSION 3.10)`,
`project(<ClassName> CXX)`, C++17, `find_package(PkgConfig)` /
`pkg_check_modules(GTK3 REQUIRED gtk+-3.0)`, lists every generated `.cpp`
explicitly in `add_executable(app …)`, and wires the GTK include dirs /
compile options / libraries onto that target. The executable is named
`app`, matching the Makefile target. Build it with
`cmake -S . -B build && cmake --build build`.

In the designer, **File → Generate C++/GTK…** now offers **Build with
CMake** right after generating (CMake is the default build path for a
generated project); **File → Build with CMake…** rebuilds the last
generated project (or asks for a folder), and **File → Open CMake
Project…** points the designer at any directory holding a
`CMakeLists.txt` — generated or hand-written — and builds it (configure +
build), reporting success/failure plus the executable path in a log
dialog. A missing or broken `CMakeLists.txt`, or a compile error, is
reported as a failure, never as success.

The logic lives in new files `src/cmake_build.h/.cpp`
(`cmake_lists_text`, `cmake_build_project`, `cmake_project_exists`,
`CMakeSettings`); the generator only gains the emit call, `main.cpp`
only the menu callbacks, and `ui/designer.ui` the three File-menu items.
Settings (cmake command, generator — empty = CMake default, build type,
build directory) persist in `~/.config/fdesigner/cmake.conf` (key=value,
same pattern as `debugger.conf`; `FDESIGNER_CMAKE_CONF` overrides the
path for tests) and are edited via **File → CMake Settings…**.

Tests: `tests/cmake_tests.sh` builds a headless probe
(`tests/cmake_probe.cpp`) that checks the generated `CMakeLists.txt`
contents, configures + builds a generated GTK project and runs it under
Xvfb, builds + runs a hand-written console CMake project and a
hand-written GTK one, verifies settings save/load and custom build
directories, checks failure reporting (missing/broken `CMakeLists.txt`,
compile error), and builds a project in a directory with a space in its
name. The designer self-test gained a CMake phase (settings round-trip,
`CMakeLists.txt` markers, real console build, broken-file failure, and a
cmake build of the generated GTK project).

## Lazarus-style widgets (2026-11-25)

Twenty Lazarus-palette widgets were added alongside the original Tk set.
Each is placeable from the palette (grouped sections in the left pane),
selectable/draggable/deletable, has a per-type property table in `props.h`,
and is emitted by the generator as a standalone class with working GTK code.

| Lazarus | GTK 3 | Key properties |
|---|---|---|
| Memo | GtkTextView in GtkScrolledWindow | text, editable, wrap-mode |
| GroupBox | GtkFrame | label, shadow-type |
| Panel | GtkFrame | label, shadow-type |
| Image | GtkImage (file, else missing-image icon) | file, pixel-size |
| Shape | GtkDrawingArea, cairo draw | shape (Rectangle/Ellipse/Line/Triangle), fill-color |
| Notebook (PageControl) | GtkNotebook | pages ("Tab 1|Tab 2"), page |
| ScrollBox | GtkScrolledWindow + GtkBox | label |
| Splitter | GtkPaned | position, orientation |
| StatusBar | GtkStatusbar | text |
| ToolBar | GtkToolbar | items ("New|Open|Save"), toolbar-style |
| MainMenu (MenuBar) | GtkMenuBar | menus ("File|Edit|Help") |
| SpinEdit | GtkSpinButton | minimum, maximum, step, value, digits, numeric, wrap |
| TrackBar | GtkScale | minimum, maximum, step, value, orientation |
| ProgressBar | GtkProgressBar | fraction, show-text, text |
| DateTimePicker | GtkCalendar + entry | date (YYYY-MM-DD), show-heading, show-day-names |
| ColorBox | GtkColorButton | color, use-alpha, title |
| ListView | GtkTreeView + GtkListStore | columns ("Name|Value"), items ("a,1;b,2"), headers-visible |
| TreeView | GtkTreeView + GtkTreeStore | items ("Root/Child;Other"), headers-visible |
| StringGrid | GtkTreeView + GtkListStore | rows, columns, headers-visible |
| Plot | GtkDrawingArea, cairo sine wave | title, line-color |

Already present before this change: Button, Label, Entry (Edit), CheckButton,
RadioButton, ComboBox, ListBox, Frame, Scale, Text, StockChart (chart/plot),
Spreadsheet, Toplevel, Canvas, Scrollbar, MenuButton.

Tests: the self-test gained a Phase 7 block (place + edit + drag/delete +
generate + per-widget code markers); `tests/widget_tests.sh` builds a
project holding every new widget, checks the markers, compiles the
generated app with `-Wall -Wextra`, and runs it briefly. The generator
matrix (`tests/run_matrix.sh` cases 02/03) also covers every new type
automatically via `kTypes`.

## Global font settings (2026-11-26)

**Edit → Preferences…** opens a font dialog (interface font + code /
text-view font, via GtkFontButton). Choices persist in
`~/.config/fdesigner/settings.conf` (`font=`, `code_font=`, Pango
descriptions; `FDESIGNER_SETTINGS_CONF` overrides the path for tests) and
are applied live at startup and on Apply: the interface font via a
screen-wide CSS provider, the code font via a `textview` CSS rule (so
Text/Memo views and any other GtkTextView use it). Invalid sizes are
clamped to 6–48 pt and empty values fall back to the defaults. Logic lives
in `src/preferences.h/.cpp`; tests: a font block in the self-test plus
`tests/font_tests.sh` (harness + self-test + Preferences dialog smoke test).
## Color themes + code view (2026-11-26)

View -> Code View shows the generated C++ for the current canvas in a
`CodeView` (`src/codeview.h/.cpp`): a `GtkTextView` with a gutter
(`GtkDrawingArea` beside it in one `GtkScrolledWindow`, so it scrolls in
lockstep). The gutter shows line numbers, red-dot breakpoints, and `[-]` /
`[+]` fold toggles. Clicking the fold-marker column folds/unfolds; clicking
elsewhere in the gutter toggles a breakpoint at that line through the real
debugger backend (`debugger_toggle_breakpoint()` in `src/debugger_ui.h/.cpp`,
which stages/inserts into the shared `GdbSession` and persists to
`debugger_bps.conf`); `GdbSession::remove_staged_breakpoint()` was added
because `remove_breakpoint()` needs a live gdb. The current execution line
can be highlighted via `codeview_set_current_line()`.

Color themes (`src/theme.h/.cpp`): four built-ins — Light, Dark, Monokai,
Solarized Dark — colouring keywords, types, strings, comments, numbers,
preprocessor lines, background/foreground and the current line. View ->
Color Theme... opens the picker (built-in combo + per-role colour buttons);
the choice persists to `~/.config/fdesigner/theme.conf`
(`FDESIGNER_THEME_CONF` overrides). Deliberately separate from the
font-settings branch's `settings.conf` / Preferences dialog so the two
merge cleanly; a later pass can move the picker into Preferences.
Gotcha: GTK 3.20+ draws the text area from the `text` subnode, so the
theme CSS must set `textview text`, not just `textview`, or the
background stays white while the tag colours change. Also on this GTK,
`GtkTextTag` "foreground" is write-only and "foreground-rgba" does not
read back — tests read the colour via "foreground-gdk" instead.

Folding (`codeview_find_regions()`, pure, unit-tested) covers every
natural foldable unit: `#region` / `#endregion` (bare, and the
`// #region` comment form the generator emits), every matched-brace span
(functions, if/else, loops, classes, structs, namespaces, initializers),
multi-line `/* */` comments, and `#if` / `#ifdef` / `#ifndef` ... `#endif`
blocks; nesting works in all directions (braces in regions and vice
versa). Braces in strings, character literals, raw strings and comments
are ignored. Unbalanced input fails gracefully: an unmatched `}` is
ignored and an unclosed `{` / `#region` / `#if` at EOF produces no fold.
Folded lines are hidden with an `invisible` text tag (proven to collapse
them to zero height), so file line numbers stay exact. The fold end line
stays visible (e.g. the closing `}`).

Tests: `tests/theme_tests.sh` (harness `tests/theme_tests.cpp`, 57 checks:
themes, persistence incl. default path, tokeniser, regions incl. nested
and unbalanced, visible lines, widget-level fold/gutter/current-line,
real gutter->debugger toggle with HOME redirected) + self-test Phase 8 +
xdotool smoke tests for the Code View window and the theme dialog.
A live xdotool gutter click on the real app set
`/tmp/fdesigner_codeview/MyWindow.cpp:6|1` in the persisted bps file, and
a fold click collapsed the constructor body (lines 7-20 hidden, 6 -> 21).
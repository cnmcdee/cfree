// FDesigner (C++/GTK) — Phase 2: property editor + bulk parameter ops.
// Main window loads from the Glade file ui/designer.ui via GtkBuilder.
//
// Phase 1: palette places real GTK widgets on a GtkFixed canvas; click to
// select, drag to move, Delete removes.
// Phase 2: the property pane edits the selected widget's properties
// (per-type tables in props.h, applied live), edits the window/root
// properties when nothing is selected, and offers the original's bulk
// operations: set a parameter on all / selected / filtered widgets, and
// clear a parameter everywhere.
//
// Headless check: FDESIGNER_SELFTEST=1 exercises both phases through the
// same functions the UI calls, prints SELFTEST PASS/FAIL.

#include <gtk/gtk.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "model.h"
#include "props.h"
#include "events.h"
#include "drawings.h"
#include "generator.h"
#include "cmake_build.h"
#include "indicators.h"
#include "project_io.h"
#include "debugger.h"
#include "debugger_ui.h"
#include "preferences.h"  // [font-settings]
#include "theme.h"
#include "codeview.h"
// ---------------------------------------------------------------- state

// One OHLC bar (Massive aggregates format).
struct Candle {
    double open = 0, high = 0, low = 0, close = 0, volume = 0;
    long long time = 0;  // window_start, ns
};

// Parse a Massive-format aggregates CSV (columns matched by header name:
// ticker,volume,open,close,high,low,window_start,transactions).
static std::vector<Candle> parse_candles_csv(const std::string& path) {
    std::vector<Candle> out;
    std::ifstream f(path);
    if (!f) return out;
    auto strip = [](std::string s) {
        while (!s.empty() &&
               (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' ||
                s.back() == '\n'))
            s.pop_back();
        size_t b = s.find_first_not_of(" \t");
        return b == std::string::npos ? std::string() : s.substr(b);
    };
    auto split = [&](const std::string& line) {
        std::vector<std::string> fields;
        std::stringstream ss(line);
        std::string field;
        while (std::getline(ss, field, ',')) fields.push_back(strip(field));
        return fields;
    };
    std::string header;
    if (!std::getline(f, header)) return out;
    auto cols = split(header);
    auto index_of = [&](const char* name) {
        for (size_t i = 0; i < cols.size(); ++i)
            if (cols[i] == name) return (int)i;
        return -1;
    };
    int io = index_of("open"), ih = index_of("high"), il = index_of("low"),
        ic = index_of("close"), it = index_of("window_start"),
        iv = index_of("volume");
    if (io < 0 || ih < 0 || il < 0 || ic < 0) return out;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        auto fields = split(line);
        int need = std::max(std::max(io, ih), std::max(il, ic));
        if ((int)fields.size() <= need) continue;
        Candle c;
        c.open = std::atof(fields[io].c_str());
        c.high = std::atof(fields[ih].c_str());
        c.low = std::atof(fields[il].c_str());
        c.close = std::atof(fields[ic].c_str());
        if (iv >= 0 && (int)fields.size() > iv)
            c.volume = std::atof(fields[iv].c_str());
        if (it >= 0 && (int)fields.size() > it)
            c.time = std::atoll(fields[it].c_str());
        if (c.high == 0 && c.low == 0 && c.close == 0) continue;
        out.push_back(c);
    }
    return out;
}

// The ticker symbol from a Massive CSV's first data row ("" if none).
static std::string csv_ticker(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";
    std::string header, line;
    if (!std::getline(f, header)) return "";
    int ik = -1, idx = 0;
    std::stringstream hs(header);
    std::string col;
    while (std::getline(hs, col, ',')) {
        if (col == "ticker") ik = idx;
        ++idx;
    }
    if (ik < 0 || !std::getline(f, line)) return "";
    std::stringstream ls(line);
    std::string field;
    for (int i = 0; std::getline(ls, field, ','); ++i)
        if (i == ik) return field;
    return "";
}

// Close at-or-before time t in a time-sorted series (NaN if the series
// has not started) — comparison series align by timestamp.
static double close_at_or_before(const std::vector<Candle>& rows,
                                 long long t) {
    if (rows.empty() || t < rows.front().time)
        return std::numeric_limits<double>::quiet_NaN();
    size_t lo = 0, hi = rows.size();
    while (lo + 1 < hi) {
        size_t mid = (lo + hi) / 2;
        if (rows[mid].time <= t)
            lo = mid;
        else
            hi = mid;
    }
    return rows[lo].close;
}

// Deterministic pseudo-random walk for the design-time chart preview.
static std::vector<Candle> fake_candles(int n = 90) {
    std::vector<Candle> out;
    unsigned s = 12345;
    auto rnd = [&]() {
        s = s * 1103515245u + 12345u;
        return ((s >> 16) & 0x7fff) / 32768.0;  // 0..1
    };
    double price = 660.0;
    for (int i = 0; i < n; ++i) {
        Candle c;
        c.open = price;
        c.close = price + (rnd() - 0.48) * 1.8;
        c.high = std::max(c.open, c.close) + rnd() * 0.45;
        c.low = std::min(c.open, c.close) - rnd() * 0.45;
        c.volume = 5000 + rnd() * 20000;
        c.time = 1794493800000000000LL + (long long)i * 60000000000LL;
        out.push_back(c);
        price = c.close;
    }
    return out;
}

struct Item {
    Widget model;             // name/type/geometry/props (source of truth)
    GtkWidget* eventbox;      // canvas child: intercepts events, shows selection
    GtkWidget* inner;         // the real widget (or its scrolled wrapper)
    GtkWidget* target;        // widget that receives g_object_set properties
    GtkCssProvider* css = nullptr;  // per-widget bg/fg provider
    std::vector<Candle> candles;    // StockChart data (preview or loaded)
    // StockChart extras: comparison series (timestamp-aligned), the
    // main series' ticker, and the wheel-zoom view (transient).
    std::vector<std::vector<Candle>> compares;
    std::vector<std::string> compare_names;
    std::string ticker;
    double chart_begin = -1, chart_count = 0;
};

static Project g_project;
static GtkWidget* g_window = nullptr;
static GtkWidget* g_palette_window = nullptr;  // top-level palette window
static GtkWidget* g_palette_menu_item = nullptr;  // View -> Widget Palette
static GtkWidget* g_canvas = nullptr;      // GtkFixed
static GtkWidget* g_viewport = nullptr;    // canvas parent (window background)
static GtkWidget* g_statusbar = nullptr;
static GtkWidget* g_props_grid = nullptr;  // GtkGrid in the property pane
static GtkWidget* g_props_box = nullptr;
static GtkWidget* g_selected_label = nullptr;
static guint g_status_ctx = 0;
static GtkCssProvider* g_canvas_css = nullptr;

static std::vector<Item*> g_items;
static Item* g_selected = nullptr;
static std::string g_current_file;  // .fdj path, "" = untitled
// ---- CMake (cmake_build.h) ----
static std::string g_last_generated_dir;  // folder of the last Generate
static std::map<std::string, int> g_counters;  // per-type name counters
static int g_cascade = 0;

// Drag state
static bool g_dragging = false;
static double g_press_root_x = 0, g_press_root_y = 0;
static int g_drag_start_x = 0, g_drag_start_y = 0;

// Property-pane widgets and their bindings (freed on every pane rebuild)
static GtkWidget *g_spin_x = nullptr, *g_spin_y = nullptr;
static GtkWidget *g_spin_w = nullptr, *g_spin_h = nullptr;
static bool g_syncing_props = false;

struct PropBinding {
    Item* item;  // nullptr = window/root property
    const PropDef* def;
};
struct PickBinding {
    GtkEntry* entry;
};
// One Events-pane check button: which widget and which GTK signal it
// connects. (The signal string is copied — the EventDef table is static,
// but the binding owns its copy for safety.)
struct EventBinding {
    Item* item;
    std::string signal;
};
static std::vector<PropBinding*> g_prop_bindings;
static std::vector<PickBinding*> g_pick_bindings;
static std::vector<EventBinding*> g_event_bindings;

// Bulk-parameter inputs
static GtkWidget* g_bulk_param = nullptr;   // editable combo
static GtkWidget* g_bulk_value = nullptr;   // entry
static GtkWidget* g_bulk_filter = nullptr;  // entry

static int g_selftest_rc = 1;  // set to 0 by the self-test on success

// ---------------------------------------------------------------- helpers

static void status(const std::string& text) {
    gtk_statusbar_pop(GTK_STATUSBAR(g_statusbar), g_status_ctx);
    gtk_statusbar_push(GTK_STATUSBAR(g_statusbar), g_status_ctx, text.c_str());
}

static std::string next_name(const std::string& type) {
    int n = ++g_counters[type];
    char buf[64];
    std::snprintf(buf, sizeof buf, "%s_%03d", type.c_str(), n);
    return buf;
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// ------------------------------------------------------- property engine

// Validate + canonicalize a raw value for a property definition.
static bool canonicalize(const PropDef& def, const std::string& raw,
                         std::string& out) {
    std::string v = trim(raw);
    switch (def.kind) {
        case PropKind::Str:
            out = v;
            return true;
        case PropKind::Bool: {
            std::string l = lower(v);
            if (l == "true" || l == "1" || l == "yes" || l == "on") {
                out = "true";
                return true;
            }
            if (l == "false" || l == "0" || l == "no" || l == "off") {
                out = "false";
                return true;
            }
            return false;
        }
        case PropKind::Int: {
            if (v.empty()) return false;
            char* end = nullptr;
            long n = std::strtol(v.c_str(), &end, 10);
            if (!end || *end) return false;
            // Enforce the table's range when it carries one: out-of-range
            // values make GTK reject the property at runtime.
            if (def.hi > def.lo && (n < def.lo || n > def.hi)) return false;
            out = std::to_string(n);
            return true;
        }
        case PropKind::Double: {
            if (v.empty()) return false;
            char* end = nullptr;
            double d = std::strtod(v.c_str(), &end);
            if (!end || *end) return false;
            if (def.hi > def.lo && (d < def.lo || d > def.hi)) return false;
            char buf[64];
            std::snprintf(buf, sizeof buf, "%g", d);
            out = buf;
            return true;
        }
        case PropKind::Color: {
            GdkRGBA rgba;
            if (!gdk_rgba_parse(&rgba, v.c_str())) return false;
            gchar* s = gdk_rgba_to_string(&rgba);
            out = s;
            g_free(s);
            return true;
        }
        case PropKind::Enum: {
            // Accept a numeric value from the table, or a label.
            if (!v.empty() && (std::isdigit((unsigned char)v[0]) || v[0] == '-')) {
                char* end = nullptr;
                long n = std::strtol(v.c_str(), &end, 10);
                if (end && !*end) {
                    for (int i = 0; def.enum_labels; ++i) {
                        // count labels to bound the values array
                        (void)i;
                        break;
                    }
                    out = std::to_string(n);
                    return true;
                }
            }
            if (def.enum_labels) {
                std::string labels = def.enum_labels;
                size_t pos = 0;
                int idx = 0;
                while (true) {
                    size_t bar = labels.find('|', pos);
                    std::string one = labels.substr(
                        pos, bar == std::string::npos ? bar : bar - pos);
                    if (lower(one) == lower(v)) {
                        out = std::to_string(def.enum_values[idx]);
                        return true;
                    }
                    if (bar == std::string::npos) break;
                    pos = bar + 1;
                    ++idx;
                }
            }
            return false;
        }
    }
    return false;
}

// Rebuild an item's bg/fg CSS from its stored props.
static void refresh_item_css(Item* it) {
    if (!it->css) return;
    std::string css;
    auto bg = it->model.props.find("background");
    auto fg = it->model.props.find("foreground");
    if (bg != it->model.props.end() || fg != it->model.props.end()) {
        css = "* { ";
        if (bg != it->model.props.end()) css += "background: " + bg->second + "; ";
        if (fg != it->model.props.end()) css += "color: " + fg->second + "; ";
        css += "}";
    }
    gtk_css_provider_load_from_data(it->css, css.c_str(), -1, nullptr);
}

// Read a string property from the model with a fallback.
static std::string prop_or_str(Item* it, const char* name,
                               const std::string& dflt) {
    auto f = it->model.props.find(name);
    return f == it->model.props.end() ? dflt : f->second;
}

// Read an int property from the model with a fallback.
static int prop_int(Item* it, const char* name, int dflt) {
    auto f = it->model.props.find(name);
    return f == it->model.props.end() ? dflt : std::atoi(f->second.c_str());
}

// (Re)build the design-time cell grid of a Spreadsheet widget.
static void build_spreadsheet_grid(GtkWidget* grid, int rows, int cols) {
    GList* kids = gtk_container_get_children(GTK_CONTAINER(grid));
    for (GList* l = kids; l; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(kids);
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            GtkWidget* e = gtk_entry_new();
            gtk_entry_set_width_chars(GTK_ENTRY(e), 8);
            gtk_widget_set_sensitive(e, FALSE);
            if (r == 0 && c == 0) gtk_entry_set_text(GTK_ENTRY(e), "SS");
            gtk_grid_attach(GTK_GRID(grid), e, c, r, 1, 1);
        }
    gtk_widget_show_all(grid);
}

// The GtkGrid inside a Spreadsheet's scrolled window (viewport-unwrapped).
static GtkWidget* spreadsheet_grid(Item* it) {
    GtkWidget* child = gtk_bin_get_child(GTK_BIN(it->inner));
    if (child && GTK_IS_VIEWPORT(child))
        child = gtk_bin_get_child(GTK_BIN(child));
    return child;
}

// The title label inside a Toplevel's design-time frame.
static GtkWidget* toplevel_header(Item* it) {
    GtkWidget* box = gtk_bin_get_child(GTK_BIN(it->inner));
    if (!box) return nullptr;
    GList* kids = gtk_container_get_children(GTK_CONTAINER(box));
    GtkWidget* first = kids ? GTK_WIDGET(kids->data) : nullptr;
    g_list_free(kids);
    return first;
}

// ------------------------------------------------- Lazarus widget helpers
// (design-time rebuilds and draws; the generator has its own emitters).

// Split a spec on one separator, trimming each part, dropping empties.
static std::vector<std::string> split_spec(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == sep) {
            std::string one = trim(s.substr(start, i - start));
            if (!one.empty()) out.push_back(one);
            start = i + 1;
        }
    }
    return out;
}

// "YYYY-MM-DD" -> y/m/d, false on garbage.
static bool parse_ymd(const std::string& s, int& y, int& m, int& d) {
    if (std::sscanf(s.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return false;
    return y >= 1 && m >= 1 && m <= 12 && d >= 1 && d <= 31;
}

// Rebuild a Notebook's pages from its "pages" prop ("Tab 1|Tab 2").
static void rebuild_notebook(Item* it) {
    GtkNotebook* nb = GTK_NOTEBOOK(it->target);
    while (gtk_notebook_get_n_pages(nb) > 0)
        gtk_notebook_remove_page(nb, 0);
    for (const std::string& page :
         split_spec(prop_or_str(it, "pages", "Tab 1|Tab 2|Tab 3"), '|')) {
        GtkWidget* body = gtk_label_new(page.c_str());
        gtk_widget_show(body);
        gtk_notebook_append_page(nb, body, gtk_label_new(page.c_str()));
    }
    gtk_widget_show_all(it->inner);
}

// Rebuild a ToolBar's buttons from its "items" prop ("New|Open|Save").
static void rebuild_toolbar(Item* it) {
    GtkToolbar* tb = GTK_TOOLBAR(it->target);
    GList* kids = gtk_container_get_children(GTK_CONTAINER(tb));
    for (GList* l = kids; l; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(kids);
    for (const std::string& name :
         split_spec(prop_or_str(it, "items", "New|Open|Save"), '|')) {
        GtkToolItem* ti = gtk_tool_button_new(nullptr, name.c_str());
        gtk_toolbar_insert(tb, ti, -1);
    }
    gtk_widget_show_all(it->inner);
}

// Rebuild a MenuBar's top-level items from its "menus" prop.
static void rebuild_menubar(Item* it) {
    GtkWidget* bar = it->target;
    GList* kids = gtk_container_get_children(GTK_CONTAINER(bar));
    for (GList* l = kids; l; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(kids);
    for (const std::string& name :
         split_spec(prop_or_str(it, "menus", "File|Edit|Help"), '|')) {
        GtkWidget* mi = gtk_menu_item_new_with_label(name.c_str());
        gtk_menu_shell_append(GTK_MENU_SHELL(bar), mi);
    }
    gtk_widget_show_all(it->inner);
}

// Drop every column of a TreeView (helper for the ListView/TreeView/
// StringGrid rebuilds below).
static void drop_tree_columns(GtkTreeView* tv) {
    GList* old = gtk_tree_view_get_columns(tv);
    for (GList* l = old; l; l = l->next)
        gtk_tree_view_remove_column(tv, GTK_TREE_VIEW_COLUMN(l->data));
    g_list_free(old);
}

// Rebuild a ListView's ListStore + columns. Columns: "Name|Value".
// Items: rows separated by ';', cells by ','.
static void rebuild_listview(Item* it) {
    GtkTreeView* tv = GTK_TREE_VIEW(it->target);
    std::vector<std::string> cols =
        split_spec(prop_or_str(it, "columns", "Name|Value"), '|');
    if (cols.empty()) cols.push_back("Name");
    int n = (int)cols.size();
    std::vector<GType> types(n, G_TYPE_STRING);
    GtkListStore* store = gtk_list_store_newv(n, types.data());
    for (const std::string& row : split_spec(
             prop_or_str(it, "items", "Row 1,Value 1;Row 2,Value 2"), ';')) {
        std::vector<std::string> cells = split_spec(row, ',');
        GtkTreeIter iter;
        gtk_list_store_append(store, &iter);
        for (int c = 0; c < n && c < (int)cells.size(); ++c)
            gtk_list_store_set(store, &iter, c, cells[c].c_str(), -1);
    }
    drop_tree_columns(tv);
    gtk_tree_view_set_model(tv, GTK_TREE_MODEL(store));
    for (int c = 0; c < n; ++c) {
        GtkCellRenderer* r = gtk_cell_renderer_text_new();
        gtk_tree_view_append_column(
            tv, gtk_tree_view_column_new_with_attributes(cols[c].c_str(), r,
                                                         "text", c, nullptr));
    }
    g_object_unref(store);
    gtk_widget_show_all(it->inner);
}

// Rebuild a TreeView's TreeStore from hierarchical "Root/Child;Other".
static void rebuild_treeview(Item* it) {
    GtkTreeView* tv = GTK_TREE_VIEW(it->target);
    GtkTreeStore* store = gtk_tree_store_new(1, G_TYPE_STRING);
    std::map<std::string, GtkTreeIter> nodes;
    for (const std::string& path :
         split_spec(prop_or_str(it, "items",
                                "Root/Child 1;Root/Child 2;Other"),
                    ';')) {
        std::vector<std::string> parts = split_spec(path, '/');
        std::string prefix;
        GtkTreeIter* parent = nullptr;
        for (const std::string& part : parts) {
            if (!prefix.empty()) prefix += "/";
            prefix += part;
            auto found = nodes.find(prefix);
            if (found != nodes.end()) {
                parent = &found->second;
                continue;
            }
            GtkTreeIter iter;
            gtk_tree_store_append(store, &iter, parent);
            gtk_tree_store_set(store, &iter, 0, part.c_str(), -1);
            nodes[prefix] = iter;
            parent = &nodes[prefix];
        }
    }
    drop_tree_columns(tv);
    gtk_tree_view_set_model(tv, GTK_TREE_MODEL(store));
    GtkCellRenderer* r = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(
        tv, gtk_tree_view_column_new_with_attributes("Tree", r, "text", 0,
                                                     nullptr));
    g_object_unref(store);
    gtk_widget_show_all(it->inner);
}

// Rebuild a StringGrid's ListStore: N text columns, R rows of "RrCc".
static void rebuild_stringgrid(Item* it) {
    GtkTreeView* tv = GTK_TREE_VIEW(it->target);
    int rows = prop_int(it, "rows", 5);
    int cols = prop_int(it, "columns", 4);
    if (rows < 1) rows = 1;
    if (cols < 1) cols = 1;
    std::vector<GType> types(cols, G_TYPE_STRING);
    GtkListStore* store = gtk_list_store_newv(cols, types.data());
    for (int r = 0; r < rows; ++r) {
        GtkTreeIter iter;
        gtk_list_store_append(store, &iter);
        for (int c = 0; c < cols; ++c) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "R%dC%d", r + 1, c + 1);
            gtk_list_store_set(store, &iter, c, buf, -1);
        }
    }
    drop_tree_columns(tv);
    gtk_tree_view_set_model(tv, GTK_TREE_MODEL(store));
    for (int c = 0; c < cols; ++c) {
        char title[32];
        std::snprintf(title, sizeof title, "Col %d", c + 1);
        GtkCellRenderer* r = gtk_cell_renderer_text_new();
        gtk_tree_view_append_column(
            tv, gtk_tree_view_column_new_with_attributes(title, r, "text", c,
                                                         nullptr));
    }
    g_object_unref(store);
    gtk_widget_show_all(it->inner);
}

// The label inside a ScrollBox's scrolled window (viewport -> box -> label).
static GtkWidget* scrollbox_label(Item* it) {
    GtkWidget* child = gtk_bin_get_child(GTK_BIN(it->inner));
    if (child && GTK_IS_VIEWPORT(child))
        child = gtk_bin_get_child(GTK_BIN(child));
    if (!child) return nullptr;
    GList* kids = gtk_container_get_children(GTK_CONTAINER(child));
    GtkWidget* first = kids ? GTK_WIDGET(kids->data) : nullptr;
    g_list_free(kids);
    return first;
}

// The entry under a DateTimePicker's calendar (box: calendar, entry).
static GtkWidget* datetime_entry(Item* it) {
    GList* kids = gtk_container_get_children(GTK_CONTAINER(it->inner));
    GtkWidget* second = nullptr;
    if (kids && kids->next) second = GTK_WIDGET(kids->next->data);
    g_list_free(kids);
    return second;
}

// Push one stored (already canonical) value onto the live widget.
static void apply_prop(Item* it, const PropDef& def, const std::string& value) {
    const char* name = def.name;
    if (std::strcmp(name, "background") == 0 ||
        std::strcmp(name, "foreground") == 0) {
        refresh_item_css(it);
        return;
    }
    // GtkRange has no "value" GObject property in GTK 3 — the value lives
    // in its adjustment.
    if (std::strcmp(name, "value") == 0 && GTK_IS_RANGE(it->target)) {
        gtk_range_set_value(GTK_RANGE(it->target), std::atof(value.c_str()));
        return;
    }
    // Toplevel is a window, not a child widget: its title goes to the
    // design-time header label; other props (resizable) are model-only.
    if (it->model.type == "Toplevel") {
        if (std::strcmp(name, "title") == 0) {
            GtkWidget* hdr = toplevel_header(it);
            if (hdr)
                gtk_label_set_text(
                    GTK_LABEL(hdr),
                    value.empty() ? it->model.name.c_str() : value.c_str());
        }
        return;
    }
    // StockChart: a data-file (re)loads the candles; compare-files
    // (re)loads the comparison series; everything redraws.
    if (it->model.type == "StockChart") {
        if (std::strcmp(name, "data-file") == 0 && !value.empty()) {
            std::vector<Candle> rows = parse_candles_csv(value);
            if (!rows.empty()) {
                it->candles = rows;
                it->ticker = csv_ticker(value);
                it->chart_begin = -1;
                it->chart_count = 0;
            }
        }
        if (std::strcmp(name, "data-file") == 0 ||
            std::strcmp(name, "compare-files") == 0) {
            it->compares.clear();
            it->compare_names.clear();
            std::string spec = prop_or_str(it, "compare-files", "");
            size_t start = 0;
            for (size_t i = 0; i <= spec.size(); ++i) {
                if (i != spec.size() && spec[i] != ',') continue;
                std::string one = spec.substr(start, i - start);
                start = i + 1;
                size_t b = one.find_first_not_of(" \t");
                size_t e = one.find_last_not_of(" \t");
                if (b == std::string::npos) continue;
                one = one.substr(b, e - b + 1);
                std::vector<Candle> rows = parse_candles_csv(one);
                if (rows.empty()) continue;
                std::string nm = csv_ticker(one);
                if (nm.empty()) nm = one;
                it->compares.push_back(rows);
                it->compare_names.push_back(nm);
            }
        }
        gtk_widget_queue_draw(it->target);
        return;
    }
    // Spreadsheet rows/columns rebuild the design-time grid.
    if (it->model.type == "Spreadsheet" &&
        (std::strcmp(name, "rows") == 0 ||
         std::strcmp(name, "columns") == 0)) {
        GtkWidget* grid = spreadsheet_grid(it);
        if (grid)
            build_spreadsheet_grid(grid, prop_int(it, "rows", 5),
                                   prop_int(it, "columns", 4));
        return;
    }
    // Lazarus-style widgets: properties consumed by a rebuild, a redraw,
    // or a non-GObject setter — not plain g_object_set properties.
    {
        const std::string& ty = it->model.type;
        if (ty == "Memo" && std::strcmp(name, "text") == 0) {
            gtk_text_buffer_set_text(
                gtk_text_view_get_buffer(GTK_TEXT_VIEW(it->target)),
                value.c_str(), -1);
            return;
        }
        if (ty == "Image" && std::strcmp(name, "file") == 0) {
            if (!value.empty() &&
                g_file_test(value.c_str(), G_FILE_TEST_EXISTS))
                gtk_image_set_from_file(GTK_IMAGE(it->target), value.c_str());
            else
                gtk_image_set_from_icon_name(GTK_IMAGE(it->target),
                                             "image-missing",
                                             GTK_ICON_SIZE_DIALOG);
            return;
        }
        if ((ty == "Shape" &&
             (std::strcmp(name, "shape") == 0 ||
              std::strcmp(name, "fill-color") == 0)) ||
            (ty == "Plot" &&
             (std::strcmp(name, "title") == 0 ||
              std::strcmp(name, "line-color") == 0))) {
            gtk_widget_queue_draw(it->target);
            return;
        }
        if (ty == "Notebook" && std::strcmp(name, "pages") == 0) {
            rebuild_notebook(it);
            return;
        }
        if (ty == "ScrollBox" && std::strcmp(name, "label") == 0) {
            GtkWidget* l = scrollbox_label(it);
            if (l) gtk_label_set_text(GTK_LABEL(l), value.c_str());
            return;
        }
        if (ty == "StatusBar" && std::strcmp(name, "text") == 0) {
            gtk_statusbar_remove_all(GTK_STATUSBAR(it->target), 0);
            if (!value.empty())
                gtk_statusbar_push(GTK_STATUSBAR(it->target), 0,
                                   value.c_str());
            return;
        }
        if (ty == "ToolBar" && std::strcmp(name, "items") == 0) {
            rebuild_toolbar(it);
            return;
        }
        if (ty == "MenuBar" && std::strcmp(name, "menus") == 0) {
            rebuild_menubar(it);
            return;
        }
        if (ty == "SpinEdit" &&
            (std::strcmp(name, "minimum") == 0 ||
             std::strcmp(name, "maximum") == 0 ||
             std::strcmp(name, "step") == 0)) {
            int mn = std::min(prop_int(it, "minimum", 0),
                              prop_int(it, "maximum", 100));
            int mx = std::max(prop_int(it, "minimum", 0),
                              prop_int(it, "maximum", 100));
            gtk_spin_button_set_range(GTK_SPIN_BUTTON(it->target), mn, mx);
            double step = prop_int(it, "step", 1);
            gtk_spin_button_set_increments(GTK_SPIN_BUTTON(it->target), step,
                                           step * 10);
            return;
        }
        if (ty == "TrackBar" &&
            (std::strcmp(name, "minimum") == 0 ||
             std::strcmp(name, "maximum") == 0 ||
             std::strcmp(name, "step") == 0)) {
            int mn = std::min(prop_int(it, "minimum", 0),
                              prop_int(it, "maximum", 100));
            int mx = std::max(prop_int(it, "minimum", 0),
                              prop_int(it, "maximum", 100));
            gtk_range_set_range(GTK_RANGE(it->target), mn, mx);
            double step = prop_int(it, "step", 1);
            gtk_range_set_increments(GTK_RANGE(it->target), step, step * 10);
            return;
        }
        if (ty == "DateTimePicker" && std::strcmp(name, "date") == 0) {
            int y = 0, m = 0, d = 0;
            if (parse_ymd(value, y, m, d)) {
                gtk_calendar_select_month(GTK_CALENDAR(it->target), m - 1, y);
                gtk_calendar_select_day(GTK_CALENDAR(it->target), d);
            }
            GtkWidget* e = datetime_entry(it);
            if (e) gtk_entry_set_text(GTK_ENTRY(e), value.c_str());
            return;
        }
        if (ty == "ColorBox" && std::strcmp(name, "color") == 0) {
            GdkRGBA rgba;
            if (gdk_rgba_parse(&rgba, value.c_str()))
                gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(it->target),
                                           &rgba);
            return;
        }
        if (ty == "ListView" &&
            (std::strcmp(name, "columns") == 0 ||
             std::strcmp(name, "items") == 0)) {
            rebuild_listview(it);
            return;
        }
        if (ty == "TreeView" && std::strcmp(name, "items") == 0) {
            rebuild_treeview(it);
            return;
        }
        if (ty == "StringGrid" &&
            (std::strcmp(name, "rows") == 0 ||
             std::strcmp(name, "columns") == 0)) {
            rebuild_stringgrid(it);
            return;
        }
    }
    GObject* o = G_OBJECT(it->target);
    switch (def.kind) {
        case PropKind::Str:
            g_object_set(o, name, value.c_str(), nullptr);
            break;
        case PropKind::Bool:
            g_object_set(o, name, value == "true" ? TRUE : FALSE, nullptr);
            break;
        case PropKind::Int:
        case PropKind::Enum:
            g_object_set(o, name, std::atoi(value.c_str()), nullptr);
            break;
        case PropKind::Double:
            g_object_set(o, name, std::atof(value.c_str()), nullptr);
            break;
        case PropKind::Color:
            break;  // only the pseudo-properties are colours
    }
}

// Set a property from raw user input: validate, store, apply.
static bool set_prop(Item* it, const std::string& name,
                     const std::string& raw, std::string& err) {
    const PropDef* def = find_prop(it->model.type, name);
    if (!def) {
        err = it->model.type + " has no property '" + name + "'";
        return false;
    }
    std::string canon;
    if (!canonicalize(*def, raw, canon)) {
        err = "'" + raw + "' is not a valid value for " + name;
        return false;
    }
    it->model.props[name] = canon;
    apply_prop(it, *def, canon);
    return true;
}

// Remove a property: drop it from the model and restore the default live.
static bool clear_prop(Item* it, const std::string& name) {
    const PropDef* def = find_prop(it->model.type, name);
    if (!def) return false;
    it->model.props.erase(name);
    if (name == "background" || name == "foreground") {
        refresh_item_css(it);
    } else if (def->def && def->def[0]) {
        apply_prop(it, *def, def->def);
    } else if (def->kind == PropKind::Str) {
        apply_prop(it, *def, "");
    }
    return true;
}

// ------------------------------------------------------- bulk operations

enum class BulkMode { All, Selected, Filtered };

static bool bulk_match(Item* it, BulkMode mode, const std::string& filter) {
    if (mode == BulkMode::All) return true;
    if (mode == BulkMode::Selected) return it == g_selected;
    std::string f = lower(filter);
    return lower(it->model.type).find(f) != std::string::npos ||
           lower(it->model.name).find(f) != std::string::npos;
}

// Set one parameter on every matching widget that supports it.
// Returns the number of widgets changed, or -1 on a value error.
static int bulk_set(const std::string& param, const std::string& value,
                    BulkMode mode, const std::string& filter,
                    std::string& err) {
    int n = 0;
    for (Item* it : g_items) {
        if (!bulk_match(it, mode, filter)) continue;
        if (!find_prop(it->model.type, param)) continue;
        if (!set_prop(it, param, value, err)) return -1;
        ++n;
    }
    return n;
}

static int bulk_clear(const std::string& param) {
    int n = 0;
    for (Item* it : g_items)
        if (clear_prop(it, param)) ++n;
    return n;
}

// ------------------------------------------------------- window properties

static void set_project_title(const std::string& t) { g_project.title = t; }

static void set_project_class(const std::string& c) { g_project.class_name = c; }

static void set_project_size(int w, int h) {
    g_project.win_width = w;
    g_project.win_height = h;
    gtk_widget_set_size_request(g_canvas, w, h);
}

static void set_project_background(const std::string& raw) {
    GdkRGBA rgba;
    if (!raw.empty() && !gdk_rgba_parse(&rgba, raw.c_str())) return;
    g_project.background = raw;
    std::string css = raw.empty() ? "" : "* { background: " + raw + "; }";
    if (g_canvas_css && g_viewport)
        gtk_css_provider_load_from_data(g_canvas_css, css.c_str(), -1, nullptr);
}

// ------------------------------------------------------- chart plotting

// Parse the StockChart "indicators" property ("SMA(20), RSI, ...") into
// indicator submodules from indicators.h.
static std::vector<std::unique_ptr<fdi::Indicator>> chart_indicators(
    Item* it) {
    std::vector<std::unique_ptr<fdi::Indicator>> out;
    std::string spec = prop_or_str(it, "indicators", "");
    // Split on commas that are not inside parentheses.
    std::vector<std::string> parts;
    int depth = 0;
    size_t start = 0;
    for (size_t i = 0; i <= spec.size(); ++i) {
        if (i == spec.size() || (spec[i] == ',' && depth == 0)) {
            parts.push_back(spec.substr(start, i - start));
            start = i + 1;
        } else if (spec[i] == '(') {
            ++depth;
        } else if (spec[i] == ')') {
            --depth;
        }
    }
    for (std::string one : parts) {
        size_t b = one.find_first_not_of(" \t");
        size_t e = one.find_last_not_of(" \t");
        if (b == std::string::npos) continue;
        auto ind = fdi::make_indicator(one.substr(b, e - b + 1));
        if (ind) out.push_back(std::move(ind));
    }
    return out;
}

// Draw the chart: candles in the price pane, overlay indicators on the
// same scale, pane indicators in labelled strips below, comparison
// series rebased onto the first visible close, annotations from the
// drawings property, a price/percent value axis on the right and a
// time axis along the bottom (same layout as the generated class).
static void plot_chart(cairo_t* cr, int width, int height, Item* it) {
    const std::vector<Candle>& all = it->candles;
    std::string up_s = prop_or_str(it, "up-color", "rgb(0,200,0)");
    std::string down_s = prop_or_str(it, "down-color", "rgb(220,0,0)");
    GdkRGBA up, down;
    if (!gdk_rgba_parse(&up, up_s.c_str())) gdk_rgba_parse(&up, "rgb(0,200,0)");
    if (!gdk_rgba_parse(&down, down_s.c_str()))
        gdk_rgba_parse(&down, "rgb(220,0,0)");
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    if (all.empty()) {
        cairo_set_source_rgb(cr, 0.4, 0.4, 0.4);
        cairo_move_to(cr, 8, 20);
        cairo_show_text(cr, "StockChart — no data");
        return;
    }
    const double axis_w = 56, axis_h = 18, pad = 4;
    double plot_w = std::max(10.0, width - axis_w);
    double total_h = std::max(10.0, height - axis_h);
    int max_n = 120;
    auto mf = it->model.props.find("max-candles");
    if (mf != it->model.props.end()) max_n = std::atoi(mf->second.c_str());
    if (max_n < 1) max_n = 1;
    size_t begin, n;
    if (it->chart_count > 0) {
        double vc = std::min(it->chart_count, (double)all.size());
        double vb = it->chart_begin;
        if (vb < 0) vb = 0;
        if (vb + vc > (double)all.size()) vb = (double)all.size() - vc;
        begin = (size_t)vb;
        n = (size_t)vc;
    } else {
        begin = all.size() > (size_t)max_n ? all.size() - max_n : 0;
        n = all.size() - begin;
    }
    bool percent = prop_or_str(it, "scale-mode", "0") == "1";
    double base = all[begin].close;
    if (base == 0) base = all[begin].open;

    auto inds = chart_indicators(it);
    std::vector<fdi::Bar> bars(all.size());
    for (size_t i = 0; i < all.size(); ++i) {
        bars[i].open = all[i].open;
        bars[i].high = all[i].high;
        bars[i].low = all[i].low;
        bars[i].close = all[i].close;
        bars[i].volume = all[i].volume;
        bars[i].time = all[i].time;
    }
    int panes = 0;
    for (auto& ind : inds)
        if (ind->kind() == fdi::PlotKind::Pane) ++panes;
    double price_h = panes ? total_h * 0.66 : total_h;
    double pane_h = panes ? (total_h - price_h) / panes : 0;

    double lo = all[begin].low, hi = all[begin].high;
    for (size_t i = begin; i < begin + n; ++i) {
        lo = std::min(lo, all[i].low);
        hi = std::max(hi, all[i].high);
    }
    // Comparison series extend the range with their rebased values.
    std::vector<double> cmp_base;
    for (const auto& rows : it->compares) {
        double c0 = std::numeric_limits<double>::quiet_NaN();
        for (size_t i = begin; i < begin + n; ++i) {
            double v = close_at_or_before(rows, all[i].time);
            if (v == v && v != 0) {
                c0 = v;
                break;
            }
        }
        cmp_base.push_back(c0);
        if (c0 != c0 || c0 == 0 || base == 0) continue;
        for (size_t i = begin; i < begin + n; ++i) {
            double v = close_at_or_before(rows, all[i].time);
            if (v == v) {
                double eq = base * v / c0;
                lo = std::min(lo, eq);
                hi = std::max(hi, eq);
            }
        }
    }
    if (hi - lo < 1e-9) hi = lo + 1;
    auto y = [&](double v) {
        return pad + (hi - v) / (hi - lo) * (price_h - 2 * pad);
    };
    double slot = plot_w / (double)n;

    // Gridlines + value axis (price or percent).
    {
        double raw = (hi - lo) / 5;
        double p10 = std::pow(10, std::floor(std::log10(raw)));
        double m = raw / p10;
        double step = (m < 1.5 ? 1 : m < 3.5 ? 2 : m < 7.5 ? 5 : 10) * p10;
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 9.5);
        for (double v = std::ceil(lo / step) * step; v <= hi; v += step) {
            double gy = y(v);
            cairo_set_source_rgba(cr, 0, 0, 0, 0.08);
            cairo_set_line_width(cr, 1);
            cairo_move_to(cr, 0, gy);
            cairo_line_to(cr, plot_w, gy);
            cairo_stroke(cr);
            char buf[32];
            if (percent && base > 0)
                std::snprintf(buf, sizeof buf, "%+.1f%%",
                              (v / base - 1) * 100);
            else
                std::snprintf(buf, sizeof buf, "%.2f", v);
            cairo_set_source_rgb(cr, 0.35, 0.35, 0.35);
            cairo_move_to(cr, plot_w + 4, gy + 3);
            cairo_show_text(cr, buf);
        }
    }
    // Time axis (UTC; date on the first tick and at day changes).
    if (all[begin].time > 0) {
        static const long kSteps[] = {1,   2,   5,    10,   15,  30,
                                      60,  120, 240,  480,  960, 1920,
                                      3840, 7680};
        long want = std::max(1L, (long)n / 6);
        long step_b = kSteps[sizeof(kSteps) / sizeof(kSteps[0]) - 1];
        for (long s : kSteps)
            if (s >= want) {
                step_b = s;
                break;
            }
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 9.5);
        long long prev_day = -1;
        size_t first = (begin + (size_t)step_b - 1) / (size_t)step_b *
                       (size_t)step_b;
        for (size_t i = first; i < begin + n; i += (size_t)step_b) {
            double gx = slot * (double)(i - begin) + slot / 2;
            cairo_set_source_rgba(cr, 0, 0, 0, 0.06);
            cairo_set_line_width(cr, 1);
            cairo_move_to(cr, gx, 0);
            cairo_line_to(cr, gx, price_h);
            cairo_stroke(cr);
            long long day = all[i].time / 86400000000000LL;
            bool with_date = prev_day < 0 || day != prev_day;
            prev_day = day;
            std::time_t t = (std::time_t)(all[i].time / 1000000000LL);
            std::tm tmv;
            gmtime_r(&t, &tmv);
            char buf[24];
            std::strftime(buf, sizeof buf,
                          with_date ? "%m-%d %H:%M" : "%H:%M", &tmv);
            cairo_text_extents_t te;
            cairo_text_extents(cr, buf, &te);
            double tx = std::max(
                0.0, std::min(gx - te.width / 2, (double)width - te.width));
            cairo_set_source_rgb(cr, 0.35, 0.35, 0.35);
            cairo_move_to(cr, tx, height - 5);
            cairo_show_text(cr, buf);
        }
        cairo_set_source_rgba(cr, 0, 0, 0, 0.25);
        cairo_set_line_width(cr, 1);
        cairo_move_to(cr, 0, height - axis_h + 0.5);
        cairo_line_to(cr, plot_w, height - axis_h + 0.5);
        cairo_stroke(cr);
    }

    // Candles.
    double body_w = std::max(1.0, slot * 0.7);
    for (size_t k = 0; k < n; ++k) {
        const Candle& c = all[begin + k];
        const GdkRGBA& col = c.close >= c.open ? up : down;
        double cx = slot * (double)k + slot / 2;
        cairo_set_source_rgba(cr, col.red, col.green, col.blue, 1.0);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, cx, y(c.high));
        cairo_line_to(cr, cx, y(c.low));
        cairo_stroke(cr);
        double y1 = y(std::max(c.open, c.close));
        double y2 = y(std::min(c.open, c.close));
        if (y2 - y1 < 1) y2 = y1 + 1;
        cairo_rectangle(cr, cx - body_w / 2, y1, body_w, y2 - y1);
        cairo_fill(cr);
    }

    // Comparison lines (same palette as the generated class).
    static const double kCmpCols[][3] = {{0.90, 0.45, 0.05},
                                         {0.55, 0.20, 0.75},
                                         {0.00, 0.55, 0.60},
                                         {0.85, 0.20, 0.65},
                                         {0.35, 0.35, 0.35}};
    for (size_t ci = 0; ci < it->compares.size(); ++ci) {
        double c0 = cmp_base[ci];
        if (c0 != c0 || c0 == 0 || base == 0) continue;
        const double* col = kCmpCols[ci % 5];
        cairo_set_source_rgb(cr, col[0], col[1], col[2]);
        cairo_set_line_width(cr, 1.5);
        bool started = false;
        for (size_t k = 0; k < n; ++k) {
            double v = close_at_or_before(it->compares[ci],
                                          all[begin + k].time);
            if (v != v) {
                started = false;
                continue;
            }
            double eq = base * v / c0;
            double px = slot * (double)k + slot / 2;
            if (!started) {
                cairo_move_to(cr, px, y(eq));
                started = true;
            } else {
                cairo_line_to(cr, px, y(eq));
            }
        }
        cairo_stroke(cr);
    }

    // Indicators.
    int pane_idx = 0;
    for (auto& ind : inds) {
        fdi::PlotArea area;
        area.cr = cr;
        area.begin = begin;
        area.count = n;
        if (ind->kind() == fdi::PlotKind::Pane) {
            area.x = 0;
            area.y = price_h + pane_idx * pane_h + 10;
            area.w = plot_w;
            area.h = pane_h - 12;
            cairo_set_source_rgb(cr, 0.75, 0.75, 0.75);
            cairo_move_to(cr, 0, price_h + pane_idx * pane_h);
            cairo_line_to(cr, plot_w, price_h + pane_idx * pane_h);
            cairo_stroke(cr);
            cairo_set_source_rgb(cr, 0.35, 0.35, 0.35);
            cairo_move_to(cr, 2, price_h + pane_idx * pane_h + 9);
            cairo_show_text(cr, ind->name());
            ind->draw(area, bars);
            ++pane_idx;
        } else {
            area.x = 0;
            area.y = pad;
            area.w = plot_w;
            area.h = price_h - 2 * pad;
            area.lo = lo;
            area.hi = hi;
            ind->draw(area, bars);
        }
    }

    // Annotations from the drawings property (data coordinates).
    {
        std::vector<fdw::Drawing> ds =
            fdw::parse_specs(prop_or_str(it, "drawings", ""));
        fdw::Map map;
        map.x = 0;
        map.y = pad;
        map.w = plot_w;
        map.h = price_h - 2 * pad;
        map.begin = (double)begin;
        map.count = (double)n;
        map.lo = lo;
        map.hi = hi;
        for (const fdw::Drawing& d : ds) fdw::draw(cr, map, d);
    }

    // Legend.
    {
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 11);
        std::vector<std::pair<std::string, GdkRGBA>> entries;
        std::string main_name =
            it->ticker.empty() ? std::string("MAIN") : it->ticker;
        char lastbuf[48];
        double last = all[begin + n - 1].close;
        if (percent && base > 0)
            std::snprintf(lastbuf, sizeof lastbuf, " %+.2f%%",
                          (last / base - 1) * 100);
        else
            std::snprintf(lastbuf, sizeof lastbuf, " %.2f", last);
        entries.push_back(
            {main_name + lastbuf, GdkRGBA{0.15, 0.15, 0.15, 1.0}});
        for (size_t ci = 0; ci < it->compare_names.size(); ++ci) {
            const double* col = kCmpCols[ci % 5];
            entries.push_back({it->compare_names[ci],
                               GdkRGBA{col[0], col[1], col[2], 1.0}});
        }
        double tw = 0;
        for (auto& e : entries) {
            cairo_text_extents_t te;
            cairo_text_extents(cr, e.first.c_str(), &te);
            tw = std::max(tw, te.width);
        }
        cairo_set_source_rgba(cr, 1, 1, 1, 0.78);
        cairo_rectangle(cr, 4, 4, tw + 10,
                        (double)entries.size() * 14 + 6);
        cairo_fill(cr);
        double ly = 15;
        for (auto& e : entries) {
            cairo_set_source_rgba(cr, e.second.red, e.second.green,
                                  e.second.blue, 1.0);
            cairo_move_to(cr, 8, ly);
            cairo_show_text(cr, e.first.c_str());
            ly += 14;
        }
    }
}

// Mouse wheel zooms the design-time chart preview around the cursor
// (the generated widget has the same behaviour, plus drag panning).
static gboolean on_chart_scroll(GtkWidget*, GdkEventScroll* ev,
                                gpointer data) {
    Item* it = (Item*)data;
    if (it->candles.empty()) return FALSE;
    GtkAllocation alloc;
    gtk_widget_get_allocation(it->target, &alloc);
    double plot_w = std::max(10.0, alloc.width - 56.0);
    double frac = std::max(0.0, std::min(1.0, ev->x / plot_w));
    if (it->chart_count <= 0) {
        int max_n = 120;
        auto mf = it->model.props.find("max-candles");
        if (mf != it->model.props.end())
            max_n = std::atoi(mf->second.c_str());
        if (max_n < 1) max_n = 1;
        it->chart_count =
            std::min((double)max_n, (double)it->candles.size());
        it->chart_begin = (double)it->candles.size() - it->chart_count;
    }
    double factor;
    if (ev->direction == GDK_SCROLL_UP)
        factor = 0.8;
    else if (ev->direction == GDK_SCROLL_DOWN)
        factor = 1.25;
    else
        factor = ev->delta_y < 0 ? 0.8 : 1.25;
    double anchor = it->chart_begin + frac * it->chart_count;
    double nc = std::round(std::min(
        std::max(it->chart_count * factor, 5.0),
        (double)it->candles.size()));
    double nb = std::round(anchor - frac * nc);
    if (nb < 0) nb = 0;
    if (nb + nc > (double)it->candles.size())
        nb = (double)it->candles.size() - nc;
    it->chart_begin = nb;
    it->chart_count = nc;
    gtk_widget_queue_draw(it->target);
    return TRUE;
}
static gboolean on_chart_draw(GtkWidget*, cairo_t* cr, gpointer data) {
    Item* it = (Item*)data;
    GtkAllocation alloc;
    gtk_widget_get_allocation(it->target, &alloc);
    plot_chart(cr, alloc.width, alloc.height, it);
    return FALSE;
}

// Design-time draw for a Shape: rectangle / ellipse / line / triangle
// in the fill-color property.
static gboolean on_shape_draw(GtkWidget* w, cairo_t* cr, gpointer data) {
    Item* it = (Item*)data;
    GtkAllocation a;
    gtk_widget_get_allocation(w, &a);
    GdkRGBA fill{0.21, 0.51, 0.89, 1.0};
    gdk_rgba_parse(&fill, prop_or_str(it, "fill-color", "").c_str());
    cairo_set_source_rgba(cr, fill.red, fill.green, fill.blue, fill.alpha);
    double x = 6, y = 6, ww = std::max(1.0, a.width - 12.0),
           hh = std::max(1.0, a.height - 12.0);
    switch (prop_int(it, "shape", 0)) {
        case 1:  // ellipse
            cairo_save(cr);
            cairo_translate(cr, x + ww / 2, y + hh / 2);
            cairo_scale(cr, ww / 2, hh / 2);
            cairo_arc(cr, 0, 0, 1, 0, 2 * G_PI);
            cairo_restore(cr);
            cairo_fill(cr);
            break;
        case 2:  // line
            cairo_set_line_width(cr, 3);
            cairo_move_to(cr, x, y + hh);
            cairo_line_to(cr, x + ww, y);
            cairo_stroke(cr);
            break;
        case 3:  // triangle
            cairo_move_to(cr, x + ww / 2, y);
            cairo_line_to(cr, x + ww, y + hh);
            cairo_line_to(cr, x, y + hh);
            cairo_close_path(cr);
            cairo_fill(cr);
            break;
        default:  // rectangle
            cairo_rectangle(cr, x, y, ww, hh);
            cairo_fill(cr);
            break;
    }
    return FALSE;
}

// Design-time draw for a Plot: title + a sine wave in the line-color.
static gboolean on_plot_draw(GtkWidget* w, cairo_t* cr, gpointer data) {
    Item* it = (Item*)data;
    GtkAllocation a;
    gtk_widget_get_allocation(w, &a);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 12);
    cairo_move_to(cr, 8, 16);
    cairo_show_text(cr, prop_or_str(it, "title", it->model.name).c_str());
    GdkRGBA line{0.86, 0.0, 0.0, 1.0};
    gdk_rgba_parse(&line, prop_or_str(it, "line-color", "").c_str());
    cairo_set_source_rgba(cr, line.red, line.green, line.blue, line.alpha);
    cairo_set_line_width(cr, 2);
    double mid = a.height / 2.0 + 8, amp = std::max(1.0, a.height / 2.0 - 24);
    for (int px = 0; px < a.width; ++px) {
        double t = (double)px / std::max(1, a.width) * 4 * G_PI;
        double y = mid - std::sin(t) * amp;
        if (px == 0) cairo_move_to(cr, px, y);
        else cairo_line_to(cr, px, y);
    }
    cairo_stroke(cr);
    return FALSE;
}

// ------------------------------------------------------- widget factory

// Create the real GTK widget for a palette type, in its design-time dress.
static GtkWidget* create_inner(const TypeInfo& t, const std::string& name) {
    const std::string key = t.key;
    if (key == "Button") return gtk_button_new_with_label(name.c_str());
    if (key == "Label") return gtk_label_new(name.c_str());
    if (key == "Entry") {
        GtkWidget* e = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(e), "Entry");
        return e;
    }
    if (key == "Checkbutton")
        return gtk_check_button_new_with_label(name.c_str());
    if (key == "Radiobutton")
        return gtk_radio_button_new_with_label(nullptr, name.c_str());
    if (key == "Frame") return gtk_frame_new(name.c_str());
    if (key == "Combobox") {
        GtkWidget* c = gtk_combo_box_text_new();
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c), "Item 1");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c), "Item 2");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c), "Item 3");
        gtk_combo_box_set_active(GTK_COMBO_BOX(c), 0);
        return c;
    }
    if (key == "Listbox") {
        GtkWidget* sw = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                       GTK_POLICY_AUTOMATIC,
                                       GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
                                            GTK_SHADOW_IN);
        GtkWidget* lb = gtk_list_box_new();
        for (int i = 1; i <= 3; ++i) {
            char txt[32];
            std::snprintf(txt, sizeof txt, "Item %d", i);
            gtk_list_box_insert(GTK_LIST_BOX(lb), gtk_label_new(txt), -1);
        }
        gtk_container_add(GTK_CONTAINER(sw), lb);
        return sw;
    }
    if (key == "Menubutton") {
        GtkWidget* b = gtk_menu_button_new();
        gtk_button_set_label(GTK_BUTTON(b), name.c_str());
        return b;
    }
    if (key == "Canvas" || key == "StockChart") {
        GtkWidget* f = gtk_frame_new(nullptr);
        gtk_frame_set_shadow_type(GTK_FRAME(f), GTK_SHADOW_IN);
        gtk_container_add(GTK_CONTAINER(f), gtk_drawing_area_new());
        return f;
    }
    if (key == "Scale")
        return gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    if (key == "Scrollbar")
        return gtk_scrollbar_new(GTK_ORIENTATION_HORIZONTAL,
                                 gtk_adjustment_new(0, 0, 100, 1, 10, 10));
    if (key == "Text") {
        GtkWidget* sw = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                       GTK_POLICY_AUTOMATIC,
                                       GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
                                            GTK_SHADOW_IN);
        GtkWidget* tv = gtk_text_view_new();
        gtk_text_buffer_set_text(
            gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv)), name.c_str(), -1);
        gtk_container_add(GTK_CONTAINER(sw), tv);
        return sw;
    }
    if (key == "Toplevel") {
        GtkWidget* frame = gtk_frame_new(nullptr);
        gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_OUT);
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        GtkWidget* title = gtk_label_new(name.c_str());
        GtkWidget* hint = gtk_label_new("(secondary window)");
        gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 2);
        gtk_box_pack_start(GTK_BOX(box), hint, TRUE, TRUE, 0);
        gtk_container_add(GTK_CONTAINER(frame), box);
        return frame;
    }
    if (key == "Spreadsheet") {
        GtkWidget* sw = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                       GTK_POLICY_AUTOMATIC,
                                       GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
                                            GTK_SHADOW_IN);
        GtkWidget* grid = gtk_grid_new();
        build_spreadsheet_grid(grid, 5, 4);
        gtk_container_add(GTK_CONTAINER(sw), grid);
        return sw;
    }
    if (key == "Memo") {
        GtkWidget* sw = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                       GTK_POLICY_AUTOMATIC,
                                       GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
                                            GTK_SHADOW_IN);
        GtkWidget* tv = gtk_text_view_new();
        gtk_text_buffer_set_text(
            gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv)), name.c_str(), -1);
        gtk_container_add(GTK_CONTAINER(sw), tv);
        return sw;
    }
    if (key == "GroupBox") return gtk_frame_new(name.c_str());
    if (key == "Panel") return gtk_frame_new(nullptr);
    if (key == "Image")
        return gtk_image_new_from_icon_name("image-missing",
                                            GTK_ICON_SIZE_DIALOG);
    if (key == "Shape" || key == "Plot") {
        GtkWidget* f = gtk_frame_new(nullptr);
        gtk_frame_set_shadow_type(GTK_FRAME(f), GTK_SHADOW_IN);
        gtk_container_add(GTK_CONTAINER(f), gtk_drawing_area_new());
        return f;
    }
    if (key == "Notebook") return gtk_notebook_new();
    if (key == "ScrollBox") {
        GtkWidget* sw = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                       GTK_POLICY_AUTOMATIC,
                                       GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
                                            GTK_SHADOW_IN);
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_box_pack_start(GTK_BOX(box), gtk_label_new(name.c_str()), FALSE,
                           FALSE, 4);
        gtk_container_add(GTK_CONTAINER(sw), box);
        return sw;
    }
    if (key == "Splitter") {
        GtkWidget* p = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_paned_pack1(GTK_PANED(p), gtk_label_new("Pane 1"), TRUE, FALSE);
        gtk_paned_pack2(GTK_PANED(p), gtk_label_new("Pane 2"), TRUE, FALSE);
        return p;
    }
    if (key == "StatusBar") {
        GtkWidget* s = gtk_statusbar_new();
        gtk_statusbar_push(GTK_STATUSBAR(s), 0, "Ready");
        return s;
    }
    if (key == "ToolBar") {
        GtkWidget* tb = gtk_toolbar_new();
        const char* names[] = {"New", "Open", "Save"};
        for (const char* n : names) {
            GtkToolItem* ti = gtk_tool_button_new(nullptr, n);
            gtk_toolbar_insert(GTK_TOOLBAR(tb), ti, -1);
        }
        return tb;
    }
    if (key == "MenuBar") {
        GtkWidget* bar = gtk_menu_bar_new();
        const char* names[] = {"File", "Edit", "Help"};
        for (const char* n : names) {
            GtkWidget* mi = gtk_menu_item_new_with_label(n);
            gtk_menu_shell_append(GTK_MENU_SHELL(bar), mi);
        }
        return bar;
    }
    if (key == "SpinEdit")
        return gtk_spin_button_new_with_range(0, 100, 1);
    if (key == "TrackBar")
        return gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    if (key == "ProgressBar") return gtk_progress_bar_new();
    if (key == "DateTimePicker") {
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        GtkWidget* cal = gtk_calendar_new();
        GtkWidget* e = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(e), "YYYY-MM-DD");
        gtk_box_pack_start(GTK_BOX(box), cal, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(box), e, FALSE, FALSE, 0);
        return box;
    }
    if (key == "ColorBox") {
        GdkRGBA rgba{0.21, 0.51, 0.89, 1.0};
        return gtk_color_button_new_with_rgba(&rgba);
    }
    if (key == "ListView" || key == "TreeView" || key == "StringGrid") {
        GtkWidget* sw = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                       GTK_POLICY_AUTOMATIC,
                                       GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
                                            GTK_SHADOW_IN);
        GtkWidget* tv = gtk_tree_view_new();
        gtk_container_add(GTK_CONTAINER(sw), tv);
        return sw;
    }
    return gtk_label_new(name.c_str());  // fallback
}

// The widget that receives properties: unwrap scrolled wrappers.
static GtkWidget* prop_target(const std::string& type, GtkWidget* inner) {
    if (type == "Listbox" || type == "Text" || type == "Memo" ||
        type == "ListView" || type == "TreeView" || type == "StringGrid") {
        GtkWidget* child =
            gtk_bin_get_child(GTK_BIN(inner));  // viewport or direct child
        if (child && GTK_IS_VIEWPORT(child))
            child = gtk_bin_get_child(GTK_BIN(child));
        return child ? child : inner;
    }
    if (type == "Canvas" || type == "StockChart" || type == "Shape" ||
        type == "Plot") {
        GtkWidget* child = gtk_bin_get_child(GTK_BIN(inner));
        return child ? child : inner;
    }
    if (type == "DateTimePicker") {
        // The box holds calendar first, then the date entry.
        GList* kids = gtk_container_get_children(GTK_CONTAINER(inner));
        GtkWidget* first = kids ? GTK_WIDGET(kids->data) : nullptr;
        g_list_free(kids);
        return first ? first : inner;
    }
    return inner;
}

// ------------------------------------------------------- property pane

static void on_spin_changed(GtkSpinButton*, gpointer which);
static void on_delete_clicked(GtkButton*, gpointer);
static void on_prop_entry_changed(GtkEntry*, gpointer binding);
static void on_prop_bool_toggled(GtkToggleButton*, gpointer binding);
static void on_event_toggled(GtkToggleButton*, gpointer binding);
static void on_prop_spin_changed(GtkSpinButton*, gpointer binding);
static void on_prop_enum_changed(GtkComboBoxText*, gpointer binding);
static void on_prop_pick_clicked(GtkButton*, gpointer binding);
static void on_win_entry_changed(GtkEntry*, gpointer which);
static void on_win_spin_changed(GtkSpinButton*, gpointer which);
static void on_edit_menus_clicked(GtkButton*, gpointer);
static void on_win_bg_pick_clicked(GtkButton*, gpointer entry_ptr);

static void clear_props() {
    GList* kids = gtk_container_get_children(GTK_CONTAINER(g_props_grid));
    for (GList* p = kids; p; p = p->next)
        gtk_container_remove(GTK_CONTAINER(g_props_grid), GTK_WIDGET(p->data));
    g_list_free(kids);
    for (PropBinding* b : g_prop_bindings) delete b;
    for (PickBinding* b : g_pick_bindings) delete b;
    for (EventBinding* b : g_event_bindings) delete b;
    g_prop_bindings.clear();
    g_pick_bindings.clear();
    g_event_bindings.clear();
    g_spin_x = g_spin_y = g_spin_w = g_spin_h = nullptr;
}

static void add_row(int row, const char* label, GtkWidget* control) {
    GtkWidget* lab = gtk_label_new(label);
    gtk_widget_set_halign(lab, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(g_props_grid), lab, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(g_props_grid), control, 1, row, 1, 1);
}

static GtkWidget* make_geo_spin(double value, double lo, double hi,
                                const char* which) {
    GtkWidget* s = gtk_spin_button_new_with_range(lo, hi, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(s), value);
    g_signal_connect(s, "value-changed", G_CALLBACK(on_spin_changed),
                     (gpointer)which);
    return s;
}

// One editor widget for a property definition, bound to an item.
static GtkWidget* make_prop_editor(Item* it, const PropDef& def) {
    PropBinding* b = new PropBinding{it, &def};
    g_prop_bindings.push_back(b);
    auto cur = it->model.props.find(def.name);
    std::string value =
        cur != it->model.props.end() ? cur->second : (def.def ? def.def : "");
    switch (def.kind) {
        case PropKind::Str: {
            GtkWidget* e = gtk_entry_new();
            gtk_entry_set_text(GTK_ENTRY(e), value.c_str());
            g_signal_connect(e, "changed", G_CALLBACK(on_prop_entry_changed),
                             b);
            return e;
        }
        case PropKind::Color: {
            GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
            GtkWidget* e = gtk_entry_new();
            gtk_entry_set_text(GTK_ENTRY(e), value.c_str());
            g_signal_connect(e, "changed", G_CALLBACK(on_prop_entry_changed),
                             b);
            GtkWidget* pick = gtk_button_new_with_label("Pick…");
            PickBinding* pb = new PickBinding{GTK_ENTRY(e)};
            g_pick_bindings.push_back(pb);
            g_signal_connect(pick, "clicked",
                             G_CALLBACK(on_prop_pick_clicked), pb);
            gtk_box_pack_start(GTK_BOX(box), e, TRUE, TRUE, 0);
            gtk_box_pack_start(GTK_BOX(box), pick, FALSE, FALSE, 0);
            return box;
        }
        case PropKind::Bool: {
            GtkWidget* c = gtk_check_button_new();
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(c),
                                         value == "true");
            g_signal_connect(c, "toggled", G_CALLBACK(on_prop_bool_toggled),
                             b);
            return c;
        }
        case PropKind::Int: {
            GtkWidget* s =
                gtk_spin_button_new_with_range(def.lo, def.hi, 1);
            gtk_spin_button_set_value(
                GTK_SPIN_BUTTON(s), value.empty() ? 0 : std::atof(value.c_str()));
            g_signal_connect(s, "value-changed",
                             G_CALLBACK(on_prop_spin_changed), b);
            return s;
        }
        case PropKind::Double: {
            double step = def.hi <= 1.0 ? 0.05 : 1.0;
            GtkWidget* s =
                gtk_spin_button_new_with_range(def.lo, def.hi, step);
            gtk_spin_button_set_digits(GTK_SPIN_BUTTON(s), 2);
            gtk_spin_button_set_value(
                GTK_SPIN_BUTTON(s), value.empty() ? 0 : std::atof(value.c_str()));
            g_signal_connect(s, "value-changed",
                             G_CALLBACK(on_prop_spin_changed), b);
            return s;
        }
        case PropKind::Enum: {
            GtkWidget* c = gtk_combo_box_text_new();
            std::string labels = def.enum_labels ? def.enum_labels : "";
            size_t pos = 0;
            int idx = 0, active = 0;
            while (true) {
                size_t bar = labels.find('|', pos);
                std::string one = labels.substr(
                    pos, bar == std::string::npos ? bar : bar - pos);
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c),
                                               one.c_str());
                if (!value.empty() &&
                    def.enum_values[idx] == std::atoi(value.c_str()))
                    active = idx;
                if (bar == std::string::npos) break;
                pos = bar + 1;
                ++idx;
            }
            gtk_combo_box_set_active(GTK_COMBO_BOX(c), active);
            g_signal_connect(c, "changed", G_CALLBACK(on_prop_enum_changed),
                             b);
            return c;
        }
    }
    return gtk_label_new("");
}

// Rebuild the property pane: window properties when nothing is selected,
// geometry + property editors for the selected widget otherwise.
static void refresh_props() {
    clear_props();
    g_syncing_props = true;
    int row = 0;
    if (!g_selected) {
        gtk_label_set_text(GTK_LABEL(g_selected_label), "Window (Root)");
        GtkWidget* title = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(title), g_project.title.c_str());
        g_signal_connect(title, "changed", G_CALLBACK(on_win_entry_changed),
                         (gpointer) "title");
        add_row(row++, "title", title);
        GtkWidget* cls = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(cls), g_project.class_name.c_str());
        g_signal_connect(cls, "changed", G_CALLBACK(on_win_entry_changed),
                         (gpointer) "class");
        add_row(row++, "class name", cls);
        GtkWidget* w = gtk_spin_button_new_with_range(200, 5000, 1);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(w), g_project.win_width);
        g_signal_connect(w, "value-changed", G_CALLBACK(on_win_spin_changed),
                         (gpointer) "w");
        add_row(row++, "width", w);
        GtkWidget* h = gtk_spin_button_new_with_range(200, 5000, 1);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(h), g_project.win_height);
        g_signal_connect(h, "value-changed", G_CALLBACK(on_win_spin_changed),
                         (gpointer) "h");
        add_row(row++, "height", h);
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        GtkWidget* bg = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(bg), g_project.background.c_str());
        g_signal_connect(bg, "changed", G_CALLBACK(on_win_entry_changed),
                         (gpointer) "background");
        GtkWidget* pick = gtk_button_new_with_label("Pick…");
        PickBinding* pb = new PickBinding{GTK_ENTRY(bg)};
        g_pick_bindings.push_back(pb);
        g_signal_connect(pick, "clicked",
                         G_CALLBACK(on_win_bg_pick_clicked), pb);
        gtk_box_pack_start(GTK_BOX(box), bg, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(box), pick, FALSE, FALSE, 0);
        add_row(row++, "background", box);
        GtkWidget* menus = gtk_button_new_with_label("Edit menus…");
        g_signal_connect(menus, "clicked",
                         G_CALLBACK(on_edit_menus_clicked), nullptr);
        add_row(row++, "menu bar", menus);
        gtk_widget_show_all(g_props_grid);
        g_syncing_props = false;
        return;
    }
    const Widget& m = g_selected->model;
    gtk_label_set_text(GTK_LABEL(g_selected_label),
                       (m.name + "  (" + m.type + ")").c_str());
    g_spin_x = make_geo_spin(m.x, -100, 5000, "x");
    add_row(row++, "x", g_spin_x);
    g_spin_y = make_geo_spin(m.y, -100, 5000, "y");
    add_row(row++, "y", g_spin_y);
    g_spin_w = make_geo_spin(m.width, 10, 5000, "w");
    add_row(row++, "width", g_spin_w);
    g_spin_h = make_geo_spin(m.height, 10, 5000, "h");
    add_row(row++, "height", g_spin_h);

    GtkWidget* hdr = gtk_label_new("<b>Widget properties</b>");
    gtk_label_set_use_markup(GTK_LABEL(hdr), TRUE);
    gtk_widget_set_halign(hdr, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(g_props_grid), hdr, 0, row++, 2, 1);

    std::vector<const PropDef*> defs;
    for (const PropDef& d : props_for(m.type)) defs.push_back(&d);
    for (const PropDef& d : common_props()) defs.push_back(&d);
    for (const PropDef* d : defs)
        add_row(row++, d->name, make_prop_editor(g_selected, *d));

    // Events: one check button per natural GTK signal of this type.
    // Checked = connected: the generator emits a stub handler and a
    // g_signal_connect for it. Types with no natural signal get an
    // explicit note instead (see events.h — none left unexamined).
    GtkWidget* ev_hdr = gtk_label_new("<b>Events</b>");
    gtk_label_set_use_markup(GTK_LABEL(ev_hdr), TRUE);
    gtk_widget_set_halign(ev_hdr, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(g_props_grid), ev_hdr, 0, row++, 2, 1);
    const std::vector<EventDef>& evs = events_for(m.type);
    if (evs.empty()) {
        GtkWidget* note = gtk_label_new(no_events_note(m.type).c_str());
        gtk_label_set_line_wrap(GTK_LABEL(note), TRUE);
        gtk_widget_set_halign(note, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(g_props_grid), note, 0, row++, 2, 1);
    }
    for (const EventDef& d : evs) {
        std::string text = std::string(d.signal) + "  →  " + d.handler +
                           "()";
        if (d.builtin) text += "  (always connected)";
        GtkWidget* c = gtk_check_button_new_with_label(text.c_str());
        bool on = d.builtin ||
                  std::find(m.signals.begin(), m.signals.end(),
                            d.signal) != m.signals.end();
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(c), on);
        if (d.builtin) {
            gtk_widget_set_sensitive(c, FALSE);
        } else {
            EventBinding* b = new EventBinding{g_selected, d.signal};
            g_event_bindings.push_back(b);
            g_signal_connect(c, "toggled", G_CALLBACK(on_event_toggled), b);
        }
        gtk_widget_set_halign(c, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(g_props_grid), c, 0, row++, 2, 1);
    }

    GtkWidget* del = gtk_button_new_with_label("Delete widget");
    g_signal_connect(del, "clicked", G_CALLBACK(on_delete_clicked), nullptr);
    gtk_grid_attach(GTK_GRID(g_props_grid), del, 0, row, 2, 1);
    gtk_widget_show_all(g_props_grid);
    g_syncing_props = false;
}

// Push the selected item's geometry into the spins (during drags).
static void sync_spins() {
    if (!g_selected || !g_spin_x) return;
    g_syncing_props = true;
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(g_spin_x), g_selected->model.x);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(g_spin_y), g_selected->model.y);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(g_spin_w),
                              g_selected->model.width);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(g_spin_h),
                              g_selected->model.height);
    g_syncing_props = false;
}

// ------------------------------------------------------- canvas ops

static void move_item(Item* it, int x, int y) {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    it->model.x = x;
    it->model.y = y;
    gtk_fixed_move(GTK_FIXED(g_canvas), it->eventbox, x, y);
}

static void resize_item(Item* it, int w, int h) {
    if (w < 10) w = 10;
    if (h < 10) h = 10;
    it->model.width = w;
    it->model.height = h;
    gtk_widget_set_size_request(it->eventbox, w, h);
}

static void select_item(Item* it) {
    if (g_selected)
        gtk_style_context_remove_class(
            gtk_widget_get_style_context(g_selected->eventbox), "sel");
    g_selected = it;
    if (g_selected) {
        gtk_style_context_add_class(
            gtk_widget_get_style_context(g_selected->eventbox), "sel");
        status(g_selected->model.name + " selected — drag to move, " +
               "Delete removes it");
    }
    refresh_props();
}

static void delete_item(Item* it) {
    if (!it) return;
    std::string name = it->model.name;
    if (g_selected == it) select_item(nullptr);
    gtk_container_remove(GTK_CONTAINER(g_canvas), it->eventbox);
    for (auto p = g_items.begin(); p != g_items.end(); ++p)
        if (*p == it) {
            g_items.erase(p);
            break;
        }
    if (it->css) g_object_unref(it->css);
    delete it;
    status(name + " deleted — " + std::to_string(g_items.size()) +
           " widget(s) on canvas");
}

// Event handlers on each placed widget's event box.
static gboolean on_item_press(GtkWidget*, GdkEventButton* e, gpointer data) {
    Item* it = (Item*)data;
    if (e->button != 1) return FALSE;
    select_item(it);
    g_dragging = true;
    g_press_root_x = e->x_root;
    g_press_root_y = e->y_root;
    g_drag_start_x = it->model.x;
    g_drag_start_y = it->model.y;
    return TRUE;
}

static gboolean on_item_motion(GtkWidget*, GdkEventMotion* e, gpointer data) {
    Item* it = (Item*)data;
    if (!g_dragging || !(e->state & GDK_BUTTON1_MASK)) return FALSE;
    int nx = g_drag_start_x + (int)(e->x_root - g_press_root_x);
    int ny = g_drag_start_y + (int)(e->y_root - g_press_root_y);
    move_item(it, nx, ny);
    sync_spins();
    return TRUE;
}

static gboolean on_item_release(GtkWidget*, GdkEventButton* e, gpointer) {
    if (e->button == 1) g_dragging = false;
    return FALSE;
}

// Build a canvas item (widgets, event box, signals) without properties.
static Item* create_item(const TypeInfo& t, const std::string& name, int x,
                         int y, int w, int h) {
    Item* it = new Item();
    it->model.name = name;
    it->model.type = t.key;
    it->model.x = x;
    it->model.y = y;
    it->model.width = w;
    it->model.height = h;

    it->inner = create_inner(t, name);
    it->target = prop_target(t.key, it->inner);
    it->css = gtk_css_provider_new();
    gtk_style_context_add_provider(gtk_widget_get_style_context(it->inner),
                                   GTK_STYLE_PROVIDER(it->css),
                                   GTK_STYLE_PROVIDER_PRIORITY_USER);

    it->eventbox = gtk_event_box_new();
    gtk_event_box_set_above_child(GTK_EVENT_BOX(it->eventbox), TRUE);
    gtk_widget_add_events(it->eventbox,
                          GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                              GDK_POINTER_MOTION_MASK);
    gtk_container_add(GTK_CONTAINER(it->eventbox), it->inner);
    gtk_widget_set_size_request(it->eventbox, w, h);
    g_signal_connect(it->eventbox, "button-press-event",
                     G_CALLBACK(on_item_press), it);
    g_signal_connect(it->eventbox, "motion-notify-event",
                     G_CALLBACK(on_item_motion), it);
    g_signal_connect(it->eventbox, "button-release-event",
                     G_CALLBACK(on_item_release), it);

    gtk_fixed_put(GTK_FIXED(g_canvas), it->eventbox, x, y);
    gtk_widget_show_all(it->eventbox);
    if (std::strcmp(t.key, "StockChart") == 0) {
        it->candles = fake_candles();  // preview until a data-file loads
        g_signal_connect(it->target, "draw", G_CALLBACK(on_chart_draw), it);
        gtk_widget_add_events(it->target, GDK_SCROLL_MASK);
        g_signal_connect(it->target, "scroll-event",
                         G_CALLBACK(on_chart_scroll), it);
    }
    if (std::strcmp(t.key, "Shape") == 0) {
        g_signal_connect(it->target, "draw", G_CALLBACK(on_shape_draw), it);
    }
    if (std::strcmp(t.key, "Plot") == 0) {
        g_signal_connect(it->target, "draw", G_CALLBACK(on_plot_draw), it);
    }
    g_items.push_back(it);
    return it;
}

// Add a widget of the given palette type to the canvas. Used by the palette
// buttons and by the self-test.
static Item* add_widget(const std::string& type_key) {
    const TypeInfo* t = find_type(type_key);
    if (!t) return nullptr;
    if (!t->phase1) {
        status(std::string(t->key) + " arrives in Phase 5 — not placed");
        return nullptr;
    }
    int off = 20 + (g_cascade++ % 8) * 24;
    Item* it = create_item(*t, next_name(type_key), off, off, t->def_width,
                           t->def_height);

    // Seed the model's property defaults (the label-ish types were created
    // showing their name, which is their label default).
    std::vector<const PropDef*> defs;
    for (const PropDef& d : props_for(type_key)) defs.push_back(&d);
    for (const PropDef& d : common_props()) defs.push_back(&d);
    for (const PropDef* d : defs) {
        if (std::strcmp(d->name, "label") == 0) {
            it->model.props["label"] = it->model.name;
        } else if (d->def && d->def[0]) {
            it->model.props[d->name] = d->def;
            apply_prop(it, *d, d->def);
        }
    }

    select_item(it);
    status(it->model.name + " placed — " +
           std::to_string(g_items.size()) + " widget(s) on canvas");
    return it;
}

// Recreate one widget from a loaded project: same name and geometry, with
// every stored property re-validated through set_prop as it is applied.
static Item* add_widget_loaded(const Widget& m) {
    const TypeInfo* t = find_type(m.type);
    if (!t || !t->phase1) return nullptr;
    Item* it = create_item(*t, m.name, m.x, m.y, m.width, m.height);
    it->model = m;
    if (find_prop(m.type, "label") && !it->model.props.count("label"))
        it->model.props["label"] = m.name;
    auto props_copy = it->model.props;
    it->model.props.clear();
    std::string err;
    for (const auto& [k, v] : props_copy) set_prop(it, k, v, err);
    return it;
}

static void clear_canvas() {
    while (!g_items.empty()) delete_item(g_items.front());
    g_counters.clear();
    g_cascade = 0;
    select_item(nullptr);
}

// After a load, name counters must continue past the loaded names so new
// widgets don't collide (Button_001 loaded -> next new one is Button_002).
static void recompute_counters() {
    g_counters.clear();
    for (Item* it : g_items) {
        const std::string& n = it->model.name;
        size_t us = n.rfind('_');
        if (us == std::string::npos) continue;
        std::string num = n.substr(us + 1);
        if (num.empty() ||
            !std::all_of(num.begin(), num.end(),
                         [](unsigned char c) { return std::isdigit(c); }))
            continue;
        std::string type = n.substr(0, us);
        int v = std::atoi(num.c_str());
        if (v > g_counters[type]) g_counters[type] = v;
    }
}

// Replace the whole canvas + window state with a loaded project.
static void load_into_canvas(const Project& p) {
    clear_canvas();
    g_project.title = p.title;
    g_project.class_name = p.class_name;
    set_project_size(p.win_width, p.win_height);
    g_project.background = p.background;
    set_project_background(p.background);
    int skipped = 0;
    for (const Widget& w : p.widgets)
        if (!add_widget_loaded(w)) ++skipped;
    recompute_counters();
    select_item(nullptr);
    refresh_props();
    status("Loaded " + std::to_string(g_items.size()) + " widget(s)" +
           (skipped ? " — " + std::to_string(skipped) +
                          " unknown widget(s) skipped"
                    : ""));
}

// ------------------------------------------------------- pane callbacks

static void on_spin_changed(GtkSpinButton* spin, gpointer which) {
    if (g_syncing_props || !g_selected) return;
    int v = gtk_spin_button_get_value_as_int(spin);
    const char* w = (const char*)which;
    if (std::strcmp(w, "x") == 0)
        move_item(g_selected, v, g_selected->model.y);
    else if (std::strcmp(w, "y") == 0)
        move_item(g_selected, g_selected->model.x, v);
    else if (std::strcmp(w, "w") == 0)
        resize_item(g_selected, v, g_selected->model.height);
    else if (std::strcmp(w, "h") == 0)
        resize_item(g_selected, g_selected->model.width, v);
}

static void on_delete_clicked(GtkButton*, gpointer) { delete_item(g_selected); }

static void report_set(Item* it, const std::string& name, bool ok,
                       const std::string& err) {
    if (ok)
        status(it->model.name + ": " + name + " = " +
               it->model.props[name]);
    else if (!err.empty())
        status("Not set — " + err);
}

static void on_prop_entry_changed(GtkEntry* e, gpointer binding) {
    if (g_syncing_props) return;
    PropBinding* b = (PropBinding*)binding;
    std::string err;
    bool ok = set_prop(b->item, b->def->name, gtk_entry_get_text(e), err);
    if (b->def->kind == PropKind::Str)
        report_set(b->item, b->def->name, ok, err);
    else if (ok)
        status(b->item->model.name + ": " + b->def->name + " = " +
               b->item->model.props[b->def->name]);
}

static void on_prop_bool_toggled(GtkToggleButton* t, gpointer binding) {
    if (g_syncing_props) return;
    PropBinding* b = (PropBinding*)binding;
    std::string err;
    set_prop(b->item, b->def->name,
             gtk_toggle_button_get_active(t) ? "true" : "false", err);
}

// Events pane: connect/disconnect one GTK signal on the selected widget.
// The generator emits a stub handler + g_signal_connect for each entry in
// Widget.signals, so this checkbox is the whole UI for event wiring.
static void on_event_toggled(GtkToggleButton* t, gpointer binding) {
    if (g_syncing_props) return;
    EventBinding* b = (EventBinding*)binding;
    bool on = gtk_toggle_button_get_active(t) != 0;
    if (set_widget_signal(b->item->model, b->signal, on))
        status(b->item->model.name + ": signal \"" + b->signal + "\" " +
               (on ? "connected" : "disconnected"));
}

static void on_prop_spin_changed(GtkSpinButton* s, gpointer binding) {
    if (g_syncing_props) return;
    PropBinding* b = (PropBinding*)binding;
    char buf[64];
    if (b->def->kind == PropKind::Int)
        std::snprintf(buf, sizeof buf, "%d",
                      gtk_spin_button_get_value_as_int(s));
    else
        std::snprintf(buf, sizeof buf, "%g",
                      gtk_spin_button_get_value(s));
    std::string err;
    set_prop(b->item, b->def->name, buf, err);
}

static void on_prop_enum_changed(GtkComboBoxText* c, gpointer binding) {
    if (g_syncing_props) return;
    PropBinding* b = (PropBinding*)binding;
    int idx = gtk_combo_box_get_active(GTK_COMBO_BOX(c));
    if (idx < 0) return;
    std::string err;
    set_prop(b->item, b->def->name, std::to_string(b->def->enum_values[idx]),
             err);
}

// Shared colour-picker: fills the bound entry with the chosen #rrggbb.
static void run_color_pick(GtkEntry* entry) {
    GtkWidget* dlg = gtk_color_chooser_dialog_new("Pick a colour",
                                                  GTK_WINDOW(g_window));
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK) {
        GdkRGBA rgba;
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(dlg), &rgba);
        char buf[32];
        std::snprintf(buf, sizeof buf, "#%02x%02x%02x",
                      (int)(rgba.red * 255 + 0.5),
                      (int)(rgba.green * 255 + 0.5),
                      (int)(rgba.blue * 255 + 0.5));
        gtk_entry_set_text(entry, buf);  // fires "changed" -> applies
    }
    gtk_widget_destroy(dlg);
}

static void on_prop_pick_clicked(GtkButton*, gpointer binding) {
    run_color_pick(((PickBinding*)binding)->entry);
}

static void on_win_bg_pick_clicked(GtkButton*, gpointer binding) {
    run_color_pick(((PickBinding*)binding)->entry);
}

static void on_win_entry_changed(GtkEntry* e, gpointer which) {
    if (g_syncing_props) return;
    const char* w = (const char*)which;
    const char* text = gtk_entry_get_text(e);
    if (std::strcmp(w, "title") == 0) set_project_title(text);
    if (std::strcmp(w, "class") == 0) set_project_class(text);
    if (std::strcmp(w, "background") == 0) set_project_background(text);
}

static void on_win_spin_changed(GtkSpinButton* s, gpointer which) {
    if (g_syncing_props) return;
    int v = gtk_spin_button_get_value_as_int(s);
    if (std::strcmp((const char*)which, "w") == 0)
        set_project_size(v, g_project.win_height);
    else
        set_project_size(g_project.win_width, v);
}

// ------------------------------------------------------- bulk UI callbacks

static std::string bulk_param_text() {
    gchar* s = gtk_combo_box_text_get_active_text(
        GTK_COMBO_BOX_TEXT(g_bulk_param));
    std::string out = s ? trim(s) : "";
    if (s) g_free(s);
    return out;
}

static void on_bulk_set_clicked(GtkButton*, gpointer mode_ptr) {
    BulkMode mode = (BulkMode)(long)mode_ptr;
    std::string param = bulk_param_text();
    std::string value = gtk_entry_get_text(GTK_ENTRY(g_bulk_value));
    std::string filter = gtk_entry_get_text(GTK_ENTRY(g_bulk_filter));
    if (param.empty()) {
        status("Bulk: type a parameter name first");
        return;
    }
    if (mode == BulkMode::Selected && !g_selected) {
        status("Bulk: nothing selected");
        return;
    }
    std::string err;
    int n = bulk_set(param, value, mode, filter, err);
    if (n < 0) {
        status("Bulk not applied — " + err);
        return;
    }
    status("Bulk: set " + param + " on " + std::to_string(n) + " widget(s)");
    if (g_selected) refresh_props();
}

static void on_bulk_clear_clicked(GtkButton*, gpointer) {
    std::string param = bulk_param_text();
    if (param.empty()) {
        status("Bulk: type a parameter name first");
        return;
    }
    int n = bulk_clear(param);
    status("Bulk: cleared " + param + " on " + std::to_string(n) +
           " widget(s)");
    if (g_selected) refresh_props();
}

static void build_bulk_ui() {
    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(g_props_box), sep, FALSE, FALSE, 6);
    GtkWidget* hdr = gtk_label_new("<b>Bulk parameter</b>");
    gtk_label_set_use_markup(GTK_LABEL(hdr), TRUE);
    gtk_widget_set_halign(hdr, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(g_props_box), hdr, FALSE, FALSE, 0);

    g_bulk_param = gtk_combo_box_text_new_with_entry();
    std::set<std::string> names;
    for (const TypeInfo& t : kTypes) {
        for (const PropDef& d : props_for(t.key)) names.insert(d.name);
    }
    for (const PropDef& d : common_props()) names.insert(d.name);
    for (const std::string& n : names)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(g_bulk_param),
                                       n.c_str());
    gtk_box_pack_start(GTK_BOX(g_props_box), g_bulk_param, FALSE, FALSE, 0);

    g_bulk_value = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_bulk_value), "value");
    gtk_box_pack_start(GTK_BOX(g_props_box), g_bulk_value, FALSE, FALSE, 0);

    g_bulk_filter = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_bulk_filter),
                                   "filter: type or name contains…");
    gtk_box_pack_start(GTK_BOX(g_props_box), g_bulk_filter, FALSE, FALSE, 0);

    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 4);
    struct {
        const char* label;
        BulkMode mode;
    } sets[] = {{"Set all", BulkMode::All},
                {"Set selected", BulkMode::Selected},
                {"Set filtered", BulkMode::Filtered}};
    for (int i = 0; i < 3; ++i) {
        GtkWidget* b = gtk_button_new_with_label(sets[i].label);
        g_signal_connect(b, "clicked", G_CALLBACK(on_bulk_set_clicked),
                         (gpointer)(long)sets[i].mode);
        gtk_grid_attach(GTK_GRID(grid), b, i % 2, i / 2, 1, 1);
    }
    GtkWidget* clr = gtk_button_new_with_label("Clear param");
    g_signal_connect(clr, "clicked", G_CALLBACK(on_bulk_clear_clicked),
                     nullptr);
    gtk_grid_attach(GTK_GRID(grid), clr, 1, 1, 1, 1);
    gtk_box_pack_start(GTK_BOX(g_props_box), grid, FALSE, FALSE, 0);
}

// ------------------------------------------------------- colour palette

static GtkWidget* g_colour_target = nullptr;

// Apply a colour from the palette: to the selected widget, or to the
// window background when nothing is selected (background target only).
static void apply_colour(const std::string& hex, const std::string& which) {
    std::string err;
    if (g_selected) {
        if (set_prop(g_selected, which, hex, err))
            status(which + " of " + g_selected->model.name + " set to " +
                   g_selected->model.props[which]);
        else
            status("Colour: " + err);
    } else if (which == "background") {
        set_project_background(hex);
        if (g_project.background == hex)
            status("Window background set to " + hex);
        else
            status("Colour: '" + hex + "' is not a colour");
    } else {
        status("Select a widget first to set its foreground");
    }
}

static void on_swatch_clicked(GtkButton* b, gpointer) {
    const char* hex =
        (const char*)g_object_get_data(G_OBJECT(b), "fd-colour");
    const gchar* which =
        g_colour_target
            ? gtk_combo_box_get_active_id(GTK_COMBO_BOX(g_colour_target))
            : nullptr;
    apply_colour(hex ? hex : "", which ? which : "background");
}

static void build_colour_ui() {
    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(g_props_box), sep, FALSE, FALSE, 6);
    GtkWidget* hdr = gtk_label_new("<b>Colour palette</b>");
    gtk_label_set_use_markup(GTK_LABEL(hdr), TRUE);
    gtk_widget_set_halign(hdr, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(g_props_box), hdr, FALSE, FALSE, 0);

    g_colour_target = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(g_colour_target),
                              "background", "Background");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(g_colour_target),
                              "foreground", "Foreground");
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(g_colour_target), "background");
    gtk_box_pack_start(GTK_BOX(g_props_box), g_colour_target, FALSE, FALSE, 0);

    static const char* kColours[] = {
        "#000000", "#ffffff", "#c0c0c0", "#808080", "#ff0000", "#ff8000",
        "#ffff00", "#00ff00", "#00ffff", "#0000ff", "#ff00ff", "#800080",
        "#008080", "#000080", "#808000", "#800000",
    };
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 3);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 3);
    for (int i = 0; i < 16; ++i) {
        GtkWidget* b = gtk_button_new();
        gtk_widget_set_size_request(b, 24, 24);
        GtkCssProvider* p = gtk_css_provider_new();
        std::string css =
            "button { background: " + std::string(kColours[i]) + "; }";
        gtk_css_provider_load_from_data(p, css.c_str(), -1, nullptr);
        gtk_style_context_add_provider(gtk_widget_get_style_context(b),
                                       GTK_STYLE_PROVIDER(p),
                                       GTK_STYLE_PROVIDER_PRIORITY_USER);
        g_object_set_data(G_OBJECT(b), "fd-colour", (gpointer)kColours[i]);
        g_signal_connect(b, "clicked", G_CALLBACK(on_swatch_clicked),
                         nullptr);
        gtk_grid_attach(GTK_GRID(grid), b, i % 8, i / 8, 1, 1);
    }
    gtk_box_pack_start(GTK_BOX(g_props_box), grid, FALSE, FALSE, 0);
    GtkWidget* hint = gtk_label_new(
        "Applies to the selected widget,\nor the window background if none.");
    gtk_label_set_xalign(GTK_LABEL(hint), 0);
    gtk_box_pack_start(GTK_BOX(g_props_box), hint, FALSE, FALSE, 0);
}

// ------------------------------------------------------- menu designer

static void on_edit_menus_clicked(GtkButton*, gpointer) {
    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "Menu bar", GTK_WINDOW(g_window), GTK_DIALOG_MODAL, "_Cancel",
        GTK_RESPONSE_CANCEL, "_OK", GTK_RESPONSE_OK, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 440, 260);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget* help = gtk_label_new(
        "One menu per line:   File: New | Open | - | Quit\n"
        "\"-\" is a separator. Leave empty for no menu bar.\n"
        "The menu bar appears in generated projects.");
    gtk_label_set_xalign(GTK_LABEL(help), 0);
    gtk_box_pack_start(GTK_BOX(content), help, FALSE, FALSE, 4);
    GtkWidget* sw = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
                                        GTK_SHADOW_IN);
    GtkWidget* tv = gtk_text_view_new();
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv)),
                             g_project.menubar.c_str(), -1);
    gtk_container_add(GTK_CONTAINER(sw), tv);
    gtk_box_pack_start(GTK_BOX(content), sw, TRUE, TRUE, 4);
    gtk_widget_show_all(content);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK) {
        GtkTextBuffer* buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv));
        GtkTextIter a, b;
        gtk_text_buffer_get_bounds(buf, &a, &b);
        gchar* txt = gtk_text_buffer_get_text(buf, &a, &b, FALSE);
        g_project.menubar = txt ? txt : "";
        g_free(txt);
        while (!g_project.menubar.empty() &&
               std::isspace((unsigned char)g_project.menubar.back()))
            g_project.menubar.pop_back();
        status(g_project.menubar.empty()
                   ? "Menu bar cleared"
                   : "Menu bar updated — see it in generated projects");
    }
    gtk_widget_destroy(dlg);
}

// ------------------------------------------------------- other callbacks

static void on_palette_clicked(GtkButton*, gpointer data) {
    add_widget((const char*)data);
}

// ------------------------------------------------------- palette window
// The widget palette lives in its own top-level window ("FDesigner —
// Widgets"), opened together with the designer. It is a plain top-level
// window, never transient/modal, so design, palette and code windows
// coexist. Closing it only hides it; View -> Widget Palette brings it
// back.

static void palette_window_show() {
    if (!g_palette_window) return;
    gtk_widget_show_all(g_palette_window);
    gtk_window_present(GTK_WINDOW(g_palette_window));
}

static void palette_window_hide() {
    if (g_palette_window) gtk_widget_hide(g_palette_window);
}

static gboolean on_palette_window_delete(GtkWidget*, GdkEvent*,
                                         gpointer) {
    // Close button hides the palette instead of destroying it, so the
    // app keeps running and the palette can be reopened.
    if (g_palette_menu_item)
        gtk_check_menu_item_set_active(
            GTK_CHECK_MENU_ITEM(g_palette_menu_item), FALSE);
    gtk_widget_hide(g_palette_window);
    return TRUE;  // do not destroy
}

static void on_view_palette_toggled(GtkCheckMenuItem* item, gpointer) {
    if (gtk_check_menu_item_get_active(item)) palette_window_show();
    else palette_window_hide();
}

static gboolean on_canvas_press(GtkWidget*, GdkEventButton* e, gpointer) {
    if (e->button == 1) select_item(nullptr);  // clicked empty canvas
    return FALSE;
}

static gboolean on_key_press(GtkWidget*, GdkEventKey* e, gpointer) {
    if ((e->keyval == GDK_KEY_Delete || e->keyval == GDK_KEY_KP_Delete) &&
        g_selected) {
        // Don't eat Delete while typing in a property entry.
        GtkWidget* focus = gtk_window_get_focus(GTK_WINDOW(g_window));
        if (focus && GTK_IS_ENTRY(focus)) return FALSE;
        delete_item(g_selected);
        return TRUE;
    }
    return FALSE;
}

static void on_quit(GtkMenuItem*, gpointer) { gtk_main_quit(); }

// ------------------------------------------------------- code generation

// Snapshot the live canvas as a Project for the generator / saver.
static Project snapshot_project() {
    Project p = g_project;
    p.widgets.clear();
    for (Item* it : g_items) p.widgets.push_back(it->model);
    return p;
}

// ---- CMake (cmake_build.h) ----
// Forward declaration: defined just below on_generate.
static bool cmake_build_dir(const std::string& dir);
// ------------------------------------------------------- code view (View menu)
//
// View -> Code View shows the generated C++ for the current canvas in a
// CodeView (line numbers, breakpoint dots, folding, themed highlighting).
// View -> Color Theme opens the theme picker; applying repaints this view.

static GtkWidget* g_codeview_window = nullptr;
static GtkWidget* g_codeview = nullptr;

static void refresh_code_view() {
    if (!g_codeview) return;
    Project p = snapshot_project();
    GenResult r = generate_project(p, p.widgets, "/tmp/fdesigner_codeview");
    std::string path, text;
    if (r.ok) {
        for (const auto& f : r.files)
            if (f.size() > 4 && f.compare(f.size() - 4, 4, ".cpp") == 0 &&
                f.find("MyWindow") != std::string::npos) {
                path = f;
                break;
            }
        if (path.empty())
            for (const auto& f : r.files)
                if (f.size() > 4 && f.compare(f.size() - 4, 4, ".cpp") == 0) {
                    path = f;
                    break;
                }
        if (!path.empty()) {
            gchar* c = nullptr;
            if (g_file_get_contents(path.c_str(), &c, nullptr, nullptr) &&
                c) {
                text = c;
                g_free(c);
            }
        }
    }
    if (path.empty()) {
        path = "/tmp/fdesigner_codeview/MyWindow.cpp";
        text = "// Generate a design first: File -> Generate C++/GTK...\n";
    }
    codeview_set_text(g_codeview, text, path);
    std::set<int> bps;
    for (int ln : debugger_breakpoint_lines(path)) bps.insert(ln);
    codeview_set_breakpoint_lines(g_codeview, bps);
    status("Code view - " + path);
}

// --- code window <-> debugger sync ------------------------------------
// Polls the SHARED GdbSession (owned by the debugger window) and keeps
// the code window's paused line + inspection pane in step with it:
//   Running -> inspection shows "running", paused line cleared (never
//              present stale values as current)
//   Stopped -> refresh locals, load the stopped file into the code view
//              if it is not already shown, red paused-line background,
//              scope label + locals in the inspection pane
//   Exited  -> paused line cleared, inspection shows the exit code
//   Idle    -> inspection shows "no target"
// Only acts when (state, stop_seq) changed, or when `force` is set
// (reopen / theme change / startup). The debugger window's own poll
// drains the same gdb queue; this poll just reads the shared session.
static unsigned long s_cw_last_seq = 0;
static DbgState s_cw_last_state = DbgState::Idle;

static const char* cw_state_name(DbgState st) {
    switch (st) {
        case DbgState::Idle: return "Idle";
        case DbgState::Starting: return "Starting";
        case DbgState::Running: return "Running";
        case DbgState::Stopped: return "Stopped";
        case DbgState::Exited: return "Exited";
        case DbgState::Error: return "Error";
    }
    return "Idle";
}

// Test hook: FDESIGNER_CODEWIN_DUMP=<path> writes a snapshot of the
// code window's debug state (state, scope, file, paused line, locals)
// every time the sync applies a change. Used by
// tests/code_window_tests.sh to assert on the live app.
static void codewin_dump_snapshot(DbgState st) {
    const char* path = std::getenv("FDESIGNER_CODEWIN_DUMP");
    if (!path || !*path || !g_codeview) return;
    std::string out = "state=";
    out += cw_state_name(st);
    out += "\n";
    out += "scope=" + codeview_inspection_scope(g_codeview) + "\n";
    out += "file=" + codeview_file_path(g_codeview) + "\n";
    out += "paused_line=" +
           std::to_string(codeview_paused_line(g_codeview)) + "\n";
    out += "current_line=" +
           std::to_string(codeview_current_line(g_codeview)) + "\n";
    for (const auto& v : codeview_inspection_vars(g_codeview))
        out += "var=" + v.name + "|" + v.type + "|" + v.value + "\n";
    std::string tmp = std::string(path) + ".tmp";
    std::ofstream f(tmp, std::ios::trunc);
    if (!f) return;
    f << out;
    f.close();
    std::rename(tmp.c_str(), path);
}

static void codewin_sync_from_debugger(bool force) {
    if (!g_codeview) return;
    debugger_poll_events();
    DbgState st = debugger_session_state();
    unsigned long seq = debugger_stop_seq();
    bool changed = force || st != s_cw_last_state || seq != s_cw_last_seq;
    if (!changed) return;
    s_cw_last_state = st;
    s_cw_last_seq = seq;
    StopInfo stop = debugger_last_stop();
    switch (st) {
        case DbgState::Running:
            codeview_set_paused_line(g_codeview, 0);
            codeview_set_current_line(g_codeview, 0);
            codeview_set_inspection(g_codeview,
                                    CodeViewInspectState::Running, "", {});
            break;
        case DbgState::Stopped: {
            debugger_refresh_on_stop();
            stop = debugger_last_stop();
            if (!stop.frame.file.empty() &&
                stop.frame.file != codeview_file_path(g_codeview)) {
                gchar* c = nullptr;
                if (g_file_get_contents(stop.frame.file.c_str(), &c,
                                        nullptr, nullptr) &&
                    c) {
                    codeview_set_text(g_codeview, c, stop.frame.file);
                    g_free(c);
                    std::set<int> bps;
                    for (int ln :
                         debugger_breakpoint_lines(stop.frame.file))
                        bps.insert(ln);
                    codeview_set_breakpoint_lines(g_codeview, bps);
                }
            }
            std::string scope =
                stop.frame.function + "() \u2014 " + stop.frame.file +
                ":" + std::to_string(stop.frame.line);
            if (!stop.reason.empty()) scope += " (" + stop.reason + ")";
            codeview_set_inspection(g_codeview,
                                    CodeViewInspectState::Stopped, scope,
                                    debugger_locals());
            // Red paused-line background only on the file actually
            // shown; otherwise the line number would be meaningless.
            if (!stop.frame.file.empty() &&
                stop.frame.file == codeview_file_path(g_codeview)) {
                // Unfold any region hiding the paused line.
                int l0 = stop.frame.line - 1;
                for (const auto& r : codeview_regions(g_codeview))
                    if (r.start_line < l0 && l0 < r.end_line &&
                        codeview_is_folded(g_codeview, r.start_line + 1))
                        codeview_toggle_fold_at(g_codeview,
                                                r.start_line + 1);
                codeview_set_current_line(g_codeview, stop.frame.line);
                codeview_set_paused_line(g_codeview, stop.frame.line);
            } else {
                codeview_set_current_line(g_codeview, 0);
                codeview_set_paused_line(g_codeview, 0);
            }
            break;
        }
        case DbgState::Exited: {
            codeview_set_paused_line(g_codeview, 0);
            codeview_set_current_line(g_codeview, 0);
            std::string scope = "Target exited";
            if (stop.exit_code >= 0)
                scope += " (code " + std::to_string(stop.exit_code) + ")";
            scope += ".";
            codeview_set_inspection(g_codeview,
                                    CodeViewInspectState::Exited, scope, {});
            break;
        }
        default:
            codeview_set_paused_line(g_codeview, 0);
            codeview_set_current_line(g_codeview, 0);
            codeview_set_inspection(g_codeview,
                                    CodeViewInspectState::NoTarget, "", {});
            break;
    }
    codewin_dump_snapshot(st);
}

static gboolean on_codewin_poll(gpointer) {
    codewin_sync_from_debugger(false);
    return G_SOURCE_CONTINUE;
}

static void open_code_view() {
    if (!g_codeview_window) {
        g_codeview_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(g_codeview_window),
                             "FDesigner \u2014 Code");
        gtk_window_set_default_size(GTK_WINDOW(g_codeview_window), 1000,
                                    700);
        // Deliberately NOT transient for the designer: this is its own
        // top-level window, listed separately by the window manager.
        g_codeview = codeview_new();
        codeview_set_theme(g_codeview, theme_current());
        codeview_set_breakpoint_callback(g_codeview,
                                         debugger_toggle_breakpoint);
        gtk_container_add(GTK_CONTAINER(g_codeview_window), g_codeview);
        // Closing the window hides it (the app keeps running);
        // reopening from View -> Code View restores it, refreshed.
        g_signal_connect(
            g_codeview_window, "delete-event",
            G_CALLBACK(+[](GtkWidget* w, GdkEvent*, gpointer) -> gboolean {
                gtk_widget_hide(w);
                return TRUE;
            }),
            nullptr);
        g_signal_connect(
            g_codeview_window, "destroy",
            G_CALLBACK(+[](GtkWidget*, gpointer) {
                g_codeview_window = nullptr;
                g_codeview = nullptr;
            }),
            nullptr);
        gtk_widget_show_all(g_codeview_window);
        // [round-3 integration] Park the code window beside the other
        // top-level windows instead of stacking it exactly over the
        // design window: its 1000x700 default at 0,0 hid the canvas and
        // the menu bar, so palette clicks and the View menu could not
        // reach the designer. First choice: right of the palette window
        // when it is visible; else right of the design window; else
        // below the design window; else a small cascade offset. Only
        // parked on first creation — reopening keeps the user's spot.
        while (gtk_events_pending()) gtk_main_iteration();
        {
            GdkRectangle mgeo = {0, 0, 1920, 1080};
            GdkMonitor* mon = gdk_display_get_primary_monitor(
                gdk_screen_get_display(gdk_screen_get_default()));
            if (mon) gdk_monitor_get_geometry(mon, &mgeo);
            gint mx = 0, my = 0, mw = 0, mh = 0;
            gtk_window_get_position(GTK_WINDOW(g_window), &mx, &my);
            gtk_window_get_size(GTK_WINDOW(g_window), &mw, &mh);
            gint right = mx + mw;
            if (g_palette_window &&
                gtk_widget_get_visible(g_palette_window)) {
                gint ppx = 0, ppw = 0;
                gtk_window_get_position(GTK_WINDOW(g_palette_window),
                                        &ppx, nullptr);
                gtk_window_get_size(GTK_WINDOW(g_palette_window), &ppw,
                                    nullptr);
                if (ppx + ppw > right) right = ppx + ppw;
            }
            const gint cw = 1000, ch = 700;
            gint cx = mx + 48, cy = my + 48;  // cascade fallback
            if (right + 12 + cw <= mgeo.x + mgeo.width) {
                cx = right + 12;
                cy = my;
            } else if (my + mh + 12 + ch <= mgeo.y + mgeo.height) {
                cx = mx;
                cy = my + mh + 12;
            }
            gtk_window_move(GTK_WINDOW(g_codeview_window), cx, cy);
        }
    } else {
        gtk_widget_show(g_codeview_window);
        gtk_window_present(GTK_WINDOW(g_codeview_window));
    }
    refresh_code_view();
    codewin_sync_from_debugger(true);
}

// View -> Code View toggles the separate top-level code window.
static void toggle_code_view() {
    if (g_codeview_window && gtk_widget_get_visible(g_codeview_window))
        gtk_widget_hide(g_codeview_window);
    else
        open_code_view();
}
static void on_generate(GtkMenuItem*, gpointer) {
    GtkWidget* dlg = gtk_file_chooser_dialog_new(
        "Generate C++/GTK — choose a folder", GTK_WINDOW(g_window),
        GTK_FILE_CHOOSER_ACTION_CREATE_FOLDER, "_Cancel", GTK_RESPONSE_CANCEL,
        "_Generate", GTK_RESPONSE_ACCEPT, nullptr);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) != GTK_RESPONSE_ACCEPT) {
        gtk_widget_destroy(dlg);
        return;
    }
    gchar* folder =
        gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
    gtk_widget_destroy(dlg);
    if (!folder) return;
    std::string folder_s = folder;
    g_free(folder);
    Project p = snapshot_project();
    GenResult r = generate_project(p, p.widgets, folder_s);
    if (!r.ok) {
        status("Generate failed — " + r.error);
        return;
    }
    // ---- CMake (cmake_build.h) ----
    g_last_generated_dir = folder_s;
    std::string list;
    for (const std::string& f : r.files) list += f + "\n";
    status("Generated " + std::to_string(r.files.size()) +
           " files — build with make, or CMake, in that folder");
    GtkWidget* msg = gtk_dialog_new_with_buttons(
        "Generated C++/GTK project", GTK_WINDOW(g_window),
        GTK_DIALOG_MODAL, "_Build with CMake", GTK_RESPONSE_ACCEPT, "_Close",
        GTK_RESPONSE_CLOSE, nullptr);
    GtkWidget* label = gtk_label_new(
        ("Generated C++/GTK project:\n\n" + list).c_str());
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(
                          GTK_DIALOG(msg))),
                      label);
    gtk_widget_show_all(msg);
    // CMake is the default build path for a generated project: one click
    // here configures + builds it (File -> Build with CMake… does the
    // same later, for the last generated folder).
    bool build_now = gtk_dialog_run(GTK_DIALOG(msg)) == GTK_RESPONSE_ACCEPT;
    gtk_widget_destroy(msg);
    if (build_now) cmake_build_dir(folder_s);
}

// ------------------------------------------------------- CMake
// (cmake_build.h) Builds a project directory with CMake instead of the
// plain g++ / make command. Settings (generator, build type, build
// directory) live in ~/.config/fdesigner/cmake.conf.

static void show_build_result(const std::string& title,
                              const CMakeBuildResult& r) {
    status(r.ok ? ("CMake build OK — " + r.exe_path)
                : "CMake build FAILED — see log");
    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        title.c_str(), GTK_WINDOW(g_window), GTK_DIALOG_MODAL, "_Close",
        GTK_RESPONSE_CLOSE, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 700, 420);
    std::string head =
        r.ok ? std::string("Build succeeded.\nExecutable: ") + r.exe_path +
                   "\n\n"
             : std::string("Build FAILED.\n\n");
    // Keep the dialog readable: show the tail of the log.
    std::string log = r.output;
    if (log.size() > 6000) log = "[…]\n" + log.substr(log.size() - 6000);
    GtkWidget* view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    GtkTextBuffer* buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    gtk_text_buffer_set_text(buf, (head + log).c_str(), -1);
    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_container_add(GTK_CONTAINER(scroll), view);
    gtk_container_add(
        GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dlg))), scroll);
    gtk_widget_show_all(dlg);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

// Configure + build `dir` with the saved CMake settings and report the
// result. Returns false when `dir` holds no CMakeLists.txt.
static bool cmake_build_dir(const std::string& dir) {
    if (!cmake_project_exists(dir)) {
        status("No CMakeLists.txt in " + dir);
        GtkWidget* msg = gtk_message_dialog_new(
            GTK_WINDOW(g_window), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
            GTK_BUTTONS_OK, "No CMakeLists.txt in:\n%s", dir.c_str());
        gtk_dialog_run(GTK_DIALOG(msg));
        gtk_widget_destroy(msg);
        return false;
    }
    CMakeSettings s;
    s.load();
    status("Building with CMake in " + dir + " …");
    CMakeBuildResult r = cmake_build_project(dir, s);
    show_build_result("CMake build — " + dir, r);
    return r.ok;
}

// File -> Build with CMake… : rebuilds the last generated project, or
// asks for a project folder when there is none yet.
static void on_cmake_build(GtkMenuItem*, gpointer) {
    std::string dir = g_last_generated_dir;
    if (dir.empty() || !cmake_project_exists(dir)) {
        GtkWidget* dlg = gtk_file_chooser_dialog_new(
            "Build with CMake — choose a project folder",
            GTK_WINDOW(g_window), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
            "_Cancel", GTK_RESPONSE_CANCEL, "_Build", GTK_RESPONSE_ACCEPT,
            nullptr);
        if (gtk_dialog_run(GTK_DIALOG(dlg)) != GTK_RESPONSE_ACCEPT) {
            gtk_widget_destroy(dlg);
            return;
        }
        gchar* folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        gtk_widget_destroy(dlg);
        if (!folder) return;
        dir = folder;
        g_free(folder);
    }
    cmake_build_dir(dir);
}

// File -> Open CMake Project… : point the designer at any directory
// holding a CMakeLists.txt (generated or hand-written) and build it.
static void on_cmake_open(GtkMenuItem*, gpointer) {
    GtkWidget* dlg = gtk_file_chooser_dialog_new(
        "Open CMake Project — choose a folder with a CMakeLists.txt",
        GTK_WINDOW(g_window), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Open & Build", GTK_RESPONSE_ACCEPT,
        nullptr);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) != GTK_RESPONSE_ACCEPT) {
        gtk_widget_destroy(dlg);
        return;
    }
    gchar* folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
    gtk_widget_destroy(dlg);
    if (!folder) return;
    std::string dir = folder;
    g_free(folder);
    g_last_generated_dir = dir;  // so Build with CMake… rebuilds it
    cmake_build_dir(dir);
}

// File -> CMake Settings… : generator / build type / build directory /
// cmake command, persisted to ~/.config/fdesigner/cmake.conf.
static void on_cmake_settings(GtkMenuItem*, gpointer) {
    CMakeSettings s;
    s.load();
    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "CMake Settings", GTK_WINDOW(g_window), GTK_DIALOG_MODAL, "_Cancel",
        GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, nullptr);
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 12);
    auto add_row = [&](int row, const char* label, GtkWidget* entry) {
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new(label), 0, row, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), entry, 1, row, 1, 1);
    };
    GtkWidget* e_cmake = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e_cmake), s.cmake_path.c_str());
    add_row(0, "CMake command:", e_cmake);
    GtkWidget* e_gen = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e_gen), s.generator.c_str());
    gtk_entry_set_placeholder_text(GTK_ENTRY(e_gen),
                                   "empty = CMake default");
    add_row(1, "Generator:", e_gen);
    GtkWidget* e_type = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e_type), s.build_type.c_str());
    add_row(2, "Build type:", e_type);
    GtkWidget* e_dir = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e_dir), s.build_dir.c_str());
    add_row(3, "Build directory:", e_dir);
    gtk_container_add(
        GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dlg))), grid);
    gtk_widget_show_all(dlg);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        s.cmake_path = gtk_entry_get_text(GTK_ENTRY(e_cmake));
        s.generator = gtk_entry_get_text(GTK_ENTRY(e_gen));
        s.build_type = gtk_entry_get_text(GTK_ENTRY(e_type));
        s.build_dir = gtk_entry_get_text(GTK_ENTRY(e_dir));
        if (s.save())
            status("CMake settings saved to " + CMakeSettings::default_path());
        else
            status("CMake settings save FAILED");
    }
    gtk_widget_destroy(dlg);
}

// ------------------------------------------------------- file operations

static void update_title() {
    std::string name = "Untitled";
    if (!g_current_file.empty()) {
        size_t slash = g_current_file.find_last_of('/');
        name = slash == std::string::npos
                   ? g_current_file
                   : g_current_file.substr(slash + 1);
    }
    gtk_window_set_title(GTK_WINDOW(g_window),
                         ("FDesigner — C++/GTK — " + name).c_str());
}

static bool save_to(const std::string& path) {
    Project p = snapshot_project();
    std::string err;
    if (!save_project_file(path, p, err)) {
        status("Save failed — " + err);
        return false;
    }
    g_current_file = path;
    update_title();
    status("Saved " + path);
    return true;
}

static GtkWidget* make_fdj_filter_dialog(const char* title,
                                         GtkFileChooserAction action) {
    GtkWidget* dlg = gtk_file_chooser_dialog_new(
        title, GTK_WINDOW(g_window), action, "_Cancel", GTK_RESPONSE_CANCEL,
        action == GTK_FILE_CHOOSER_ACTION_SAVE ? "_Save" : "_Open",
        GTK_RESPONSE_ACCEPT, nullptr);
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "FDesigner projects (*.fdj)");
    gtk_file_filter_add_pattern(filter, "*.fdj");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), filter);
    return dlg;
}

static void on_save(GtkMenuItem*, gpointer) {
    if (!g_current_file.empty()) {
        save_to(g_current_file);
        return;
    }
    GtkWidget* dlg =
        make_fdj_filter_dialog("Save project", GTK_FILE_CHOOSER_ACTION_SAVE);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dlg),
                                                   TRUE);
    std::string defname = lower(g_project.class_name) + ".fdj";
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dlg), defname.c_str());
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        gchar* path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if (path) {
            std::string p = path;
            g_free(path);
            if (p.size() < 4 || p.substr(p.size() - 4) != ".fdj") p += ".fdj";
            save_to(p);
        }
    }
    gtk_widget_destroy(dlg);
}

static void on_open(GtkMenuItem*, gpointer) {
    GtkWidget* dlg =
        make_fdj_filter_dialog("Open project", GTK_FILE_CHOOSER_ACTION_OPEN);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        gchar* path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if (path) {
            std::string p = path;
            g_free(path);
            Project proj;
            std::string err;
            if (load_project_file(p, proj, err)) {
                load_into_canvas(proj);
                g_current_file = p;
                update_title();
            } else {
                status("Open failed — " + err);
            }
        }
    }
    gtk_widget_destroy(dlg);
}

static void on_new(GtkMenuItem*, gpointer) {
    if (!g_items.empty()) {
        GtkWidget* ask = gtk_message_dialog_new(
            GTK_WINDOW(g_window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
            GTK_BUTTONS_YES_NO,
            "Start a new design? The current canvas will be cleared.");
        if (gtk_dialog_run(GTK_DIALOG(ask)) != GTK_RESPONSE_YES) {
            gtk_widget_destroy(ask);
            return;
        }
        gtk_widget_destroy(ask);
    }
    clear_canvas();
    g_project = Project();
    set_project_size(g_project.win_width, g_project.win_height);
    set_project_background("");
    g_current_file.clear();
    update_title();
    refresh_props();
    status("New design — pick a widget from the palette to place it");
}

// ------------------------------------------------------- self-test

static gboolean selftest_step(gpointer) {
    bool ok = true;
    auto check = [&](bool cond) {
        if (!cond) ok = false;
    };
    std::string err;

    // Phase 1: add / select / move / resize / delete
    Item* btn = add_widget("Button");
    Item* lbl = add_widget("Label");
    check(btn && lbl && g_items.size() == 2);
    select_item(btn);
    move_item(btn, 120, 90);
    resize_item(btn, 140, 40);
    delete_item(lbl);
    check(g_items.size() == 1 && g_selected == btn && btn->model.x == 120 &&
          btn->model.y == 90 && btn->model.width == 140 &&
          btn->model.height == 40 && btn->model.name == "Button_001");

    // Phase 2: single-property set, live on the widget
    check(set_prop(btn, "label", "Hello", err));
    check(btn->model.props["label"] == "Hello");
    check(std::strcmp(gtk_button_get_label(GTK_BUTTON(btn->inner)),
                      "Hello") == 0);
    check(set_prop(btn, "relief", "None", err));
    check(btn->model.props["relief"] == "2");
    check(!set_prop(btn, "relief", "bogus", err));
    check(!set_prop(btn, "nosuchprop", "x", err));

    // Phase 2: bulk ops
    Item* ent = add_widget("Entry");
    Item* lbl2 = add_widget("Label");
    check(ent && lbl2 && g_items.size() == 3);
    check(bulk_set("background", "#00ff00", BulkMode::All, "", err) == 3);
    check(btn->model.props["background"] == "rgb(0,255,0)");
    select_item(btn);
    check(bulk_set("label", "SelBtn", BulkMode::Selected, "", err) == 1);
    check(btn->model.props["label"] == "SelBtn");
    check(bulk_set("label", "Clicked", BulkMode::Filtered, "Butt", err) == 1);
    check(btn->model.props["label"] == "Clicked");
    check(lbl2->model.props["label"] == lbl2->model.name);
    check(bulk_clear("background") == 3);
    check(btn->model.props.count("background") == 0);

    // Phase 2: window properties
    set_project_title("Test Title");
    set_project_size(1024, 700);
    set_project_background("#102030");
    check(g_project.title == "Test Title" && g_project.win_width == 1024 &&
          g_project.win_height == 700 && g_project.background == "#102030");

    // Phase 3: generate a project from the current canvas and verify the
    // output shape (the caller compiles + runs it as the real proof).
    {
        Item* combo = add_widget("Combobox");
        Item* scale = add_widget("Scale");
        Item* text = add_widget("Text");
        check(combo && scale && text);
        Project snap = snapshot_project();
        GenResult gr = generate_project(snap, snap.widgets,
                                        "/tmp/fdesigner_gen_test");
        check(gr.ok && gr.files.size() == 5 + 2 * snap.widgets.size());
        check(!g_file_test("/tmp/fdesigner_gen_test/mywindow.ui",
                           G_FILE_TEST_EXISTS));  // all code, no .ui
        // ---- CMake (cmake_build.h) ----
        // The generator also emits a CMakeLists.txt for the project.
        gchar* cmake_text = nullptr;
        check(g_file_get_contents("/tmp/fdesigner_gen_test/CMakeLists.txt",
                                  &cmake_text, nullptr, nullptr) &&
              cmake_text != nullptr);
        if (cmake_text) {
            std::string cm = cmake_text;
            check(cm.find("cmake_minimum_required(VERSION 3.10)") !=
                  std::string::npos);
            check(cm.find("project(MyWindow CXX)") != std::string::npos);
            check(cm.find("pkg_check_modules(GTK3 REQUIRED gtk+-3.0)") !=
                  std::string::npos);
            check(cm.find("add_executable(app") != std::string::npos);
            check(cm.find("main.cpp") != std::string::npos &&
                  cm.find("MyWindow.cpp") != std::string::npos &&
                  cm.find("Button_001.cpp") != std::string::npos);
            check(cm.find("target_link_libraries(app PRIVATE "
                          "${GTK3_LIBRARIES})") != std::string::npos);
            g_free(cmake_text);
        }
        gchar* code_text = nullptr;
        check(g_file_get_contents("/tmp/fdesigner_gen_test/MyWindow.cpp",
                                  &code_text, nullptr, nullptr) &&
              code_text != nullptr);
        if (code_text) {
            std::string code = code_text;
            check(code.find("new Button_001(fixed_)") != std::string::npos);
            check(code.find("Root_001_buttonRelease3") !=
                  std::string::npos);
            check(code.find("\"Test Title\"") != std::string::npos);
            g_free(code_text);
        }
        gchar* btn_text = nullptr;
        check(g_file_get_contents("/tmp/fdesigner_gen_test/Button_001.cpp",
                                  &btn_text, nullptr, nullptr) &&
              btn_text != nullptr);
        if (btn_text) {
            std::string code = btn_text;
            check(code.find("gtk_button_new_with_label") !=
                  std::string::npos);
            check(code.find("gtk_fixed_put") != std::string::npos);
            check(code.find("Button_001::buttonPress1") !=
                  std::string::npos);
            check(code.find("util_error_print") != std::string::npos);
            g_free(btn_text);
        }
        std::printf("GENERATED /tmp/fdesigner_gen_test\n");
    }

    // Phase 4: save -> clear -> load round-trip, counter continuation,
    // and bad-file rejection.
    {
        Project snap = snapshot_project();
        std::string err;
        check(save_project_file("/tmp/fdesigner_test.fdj", snap, err));
        clear_canvas();
        check(g_items.empty());
        Project loaded;
        check(load_project_file("/tmp/fdesigner_test.fdj", loaded, err));
        load_into_canvas(loaded);
        check(g_items.size() == 6);
        Item* b = nullptr;
        for (Item* it : g_items)
            if (it->model.name == "Button_001") b = it;
        check(b && b->model.x == 120 && b->model.y == 90 &&
              b->model.width == 140 && b->model.height == 40 &&
              b->model.props["label"] == "Clicked" &&
              b->model.props["relief"] == "2");
        check(g_project.title == "Test Title" &&
              g_project.win_width == 1024 && g_project.win_height == 700 &&
              g_project.background == "#102030");
        Item* b2 = add_widget("Button");
        check(b2 && b2->model.name == "Button_002");
        delete_item(b2);
        check(g_items.size() == 6);
        check(g_file_set_contents("/tmp/fdesigner_bad.fdj",
                                  "this is not json", -1, nullptr));
        Project junk;
        check(!load_project_file("/tmp/fdesigner_bad.fdj", junk, err));
    }

    // Phase 5: Toplevel + Spreadsheet widgets, colour palette, menu bar.
    {
        std::string err;
        // Property ranges are enforced: out-of-range values are rejected
        // so generated code never hands GTK an invalid property value.
        Item* lab = nullptr;
        Item* ent = nullptr;
        for (Item* it : g_items) {
            if (it->model.type == "Label") lab = it;
            if (it->model.type == "Entry") ent = it;
        }
        check(lab && !set_prop(lab, "xalign", "42.5", err));
        check(lab && set_prop(lab, "xalign", "0.5", err));
        check(ent && !set_prop(ent, "max-length", "-5", err));

        Item* top = add_widget("Toplevel");
        check(top != nullptr);
        check(set_prop(top, "title", "Settings", err));
        check(top->model.props["title"] == "Settings");
        GtkWidget* hdr = toplevel_header(top);
        check(hdr && std::string(gtk_label_get_text(GTK_LABEL(hdr))) ==
                         "Settings");

        Item* ss = add_widget("Spreadsheet");
        check(ss != nullptr);
        check(set_prop(ss, "rows", "3", err));
        check(set_prop(ss, "columns", "2", err));
        GtkWidget* grid = spreadsheet_grid(ss);
        check(grid != nullptr);
        GList* cells = gtk_container_get_children(GTK_CONTAINER(grid));
        check(g_list_length(cells) == 6);
        g_list_free(cells);

        // Colour palette path: applies to the current selection.
        Item* btn = nullptr;
        for (Item* it : g_items)
            if (it->model.name == "Button_001") btn = it;
        check(btn != nullptr);
        select_item(btn);
        apply_colour("#ff0000", "background");
        check(btn->model.props["background"] == "rgb(255,0,0)");
        apply_colour("#0000ff", "foreground");
        check(btn->model.props["foreground"] == "rgb(0,0,255)");

        // Menu bar + full Phase 5 generation.
        g_project.menubar = "File: New | Open | - | Quit\nHelp: About";
        Project snap = snapshot_project();
        GenResult gr = generate_project(snap, snap.widgets,
                                        "/tmp/fdesigner_gen_test5");
        check(gr.ok);
        gchar* code_text = nullptr;
        check(g_file_get_contents("/tmp/fdesigner_gen_test5/MyWindow.cpp",
                                  &code_text, nullptr, nullptr) &&
              code_text != nullptr);
        if (code_text) {
            std::string code = code_text;
            check(code.find("gtk_menu_bar_new") != std::string::npos);
            check(code.find("menu_file_quit") != std::string::npos);
            check(code.find("gtk_separator_menu_item_new") !=
                  std::string::npos);
            g_free(code_text);
        }
        gchar* top_text = nullptr;
        check(g_file_get_contents(
                  "/tmp/fdesigner_gen_test5/Toplevel_001.cpp", &top_text,
                  nullptr, nullptr) &&
              top_text != nullptr);
        if (top_text) {
            check(std::string(top_text).find("\"Settings\"") !=
                  std::string::npos);
            g_free(top_text);
        }
        gchar* ss_text = nullptr;
        check(g_file_get_contents(
                  "/tmp/fdesigner_gen_test5/Spreadsheet_001.cpp", &ss_text,
                  nullptr, nullptr) &&
              ss_text != nullptr);
        if (ss_text) {
            check(std::string(ss_text).find("gtk_grid_new") !=
                  std::string::npos);
            g_free(ss_text);
        }
        std::printf("GENERATED5 /tmp/fdesigner_gen_test5\n");

        // Round-trip with the Phase 5 pieces included.
        check(save_project_file("/tmp/fdesigner_test5.fdj", snap, err));
        clear_canvas();
        Project loaded;
        check(load_project_file("/tmp/fdesigner_test5.fdj", loaded, err));
        load_into_canvas(loaded);
        check(g_items.size() == 8);
        check(g_project.menubar == snap.menubar);
        bool found_top = false;
        for (Item* it : g_items)
            if (it->model.type == "Toplevel" &&
                it->model.props["title"] == "Settings")
                found_top = true;
        check(found_top);
    }

    // Phase 6: StockChart — preview series, Massive CSV load, generation.
    {
        std::string err;
        Item* chart = add_widget("StockChart");
        check(chart != nullptr);
        {  // stage the fake Massive CSV (an earlier session left it in
           // /tmp; a fresh VM does not have it)
            std::ifstream srcf("tests/data/SPY_fake.csv",
                               std::ios::binary);
            std::ofstream dstf("/tmp/SPY_fake.csv", std::ios::binary);
            dstf << srcf.rdbuf();
        }
        check(!chart->candles.empty());  // built-in preview series
        check(set_prop(chart, "data-file", "/tmp/SPY_fake.csv", err));
        check(chart->candles.size() == 120);
        check(chart->candles[0].open > 659 &&
              chart->candles[0].open < 661);
        check(set_prop(chart, "up-color", "#00ff00", err));
        check(set_prop(chart, "indicators",
                       "SMA(20), RSI, MACD(12,26,9), Engulfing", err));
        // The designer-side parser resolves all four specs (two of them
        // carry commas inside their parameter lists).
        check(chart_indicators(chart).size() == 4);
        check(fdi::all_defs().size() == 250);
        // Phase 7: drawings module, comparison series, percent scale.
        check(fdw::parse_specs("trend 1 2 3 4 red; hline 650; "
                               "text 5 660 purple Breakout")
                  .size() == 3);
        check(set_prop(chart, "scale-mode", "1", err));
        check(set_prop(chart, "drawings",
                       "trend 2 660.5 100 668.25 red; hline 655 blue",
                       err));
        if (g_file_test("tests/data/QQQ_fake_long.csv",
                        G_FILE_TEST_EXISTS)) {
            check(set_prop(chart, "compare-files",
                           "tests/data/QQQ_fake_long.csv", err));
            check(chart->compares.size() == 1);
            check(chart->compare_names.size() == 1 &&
                  chart->compare_names[0] == "QQQ");
            check(chart->ticker == "SPY");
        }
        // Wheel zoom on the preview: one up-scroll shrinks the window.
        {
            GdkEventScroll ev{};
            ev.type = GDK_SCROLL;
            ev.direction = GDK_SCROLL_UP;
            ev.x = 60;
            ev.y = 10;
            on_chart_scroll(nullptr, &ev, chart);
            check(chart->chart_count > 0 &&
                  chart->chart_count < (double)chart->candles.size());
        }
        Project snap3 = snapshot_project();
        GenResult gr3 = generate_project(snap3, snap3.widgets,
                                         "/tmp/fdesigner_gen_test6");
        check(gr3.ok);
        check(g_file_test("/tmp/fdesigner_gen_test6/indicators.h",
                          G_FILE_TEST_EXISTS));
        check(g_file_test("/tmp/fdesigner_gen_test6/drawings.h",
                          G_FILE_TEST_EXISTS));
        gchar* chart_text = nullptr;
        check(g_file_get_contents(
                  "/tmp/fdesigner_gen_test6/StockChart_001.cpp",
                  &chart_text, nullptr, nullptr) &&
              chart_text != nullptr);
        if (chart_text) {
            std::string code = chart_text;
            check(code.find("load_csv") != std::string::npos);
            check(code.find("cairo_rectangle") != std::string::npos);
            check(code.find("add_indicator(\"SMA(20)\")") !=
                  std::string::npos);
            check(code.find("add_indicator(\"MACD(12,26,9)\")") !=
                  std::string::npos);
            check(code.find("add_drawing(\"trend 2 660.5 100 668.25 "
                            "red\")") != std::string::npos);
            check(code.find("set_scale_mode(1)") != std::string::npos);
            check(code.find("on_chart_scroll") != std::string::npos);
            check(code.find("fmt_axis_time") != std::string::npos);
            if (g_file_test("tests/data/QQQ_fake_long.csv",
                            G_FILE_TEST_EXISTS))
                check(code.find("add_compare(") != std::string::npos);
            g_free(chart_text);
        }
        gchar* chart_h = nullptr;
        check(g_file_get_contents(
                  "/tmp/fdesigner_gen_test6/StockChart_001.h", &chart_h,
                  nullptr, nullptr) &&
              chart_h != nullptr);
        if (chart_h) {
            check(std::string(chart_h).find("struct Candle") !=
                  std::string::npos);
            g_free(chart_h);
        }
    }

    // Phase 7: Lazarus-style widgets — place, edit a key property,
    // drag/delete, and generate code that names the right GTK calls.
    {
        std::string err;
        auto read_file = [](const std::string& path) {
            gchar* t = nullptr;
            if (!g_file_get_contents(path.c_str(), &t, nullptr, nullptr) ||
                !t)
                return std::string();
            std::string s = t;
            g_free(t);
            return s;
        };
        auto tree_rows = [](GtkWidget* tv) {
            GtkTreeModel* m = gtk_tree_view_get_model(GTK_TREE_VIEW(tv));
            return m ? gtk_tree_model_iter_n_children(m, nullptr) : -1;
        };
        const char* types7[] = {"Memo",       "GroupBox",   "Panel",
                                "Image",      "Shape",      "Notebook",
                                "ScrollBox",  "Splitter",   "StatusBar",
                                "ToolBar",    "MenuBar",    "SpinEdit",
                                "TrackBar",   "ProgressBar", "DateTimePicker",
                                "ColorBox",   "ListView",   "TreeView",
                                "StringGrid", "Plot"};
        std::map<std::string, Item*> placed;
        for (const char* ty : types7) {
            Item* it = add_widget(ty);
            check(it != nullptr);
            if (it) placed[ty] = it;
        }
        check(g_items.size() == 9 + 20);  // 9 carried over + 20 new
        // Drag + delete on a new widget.
        Item* nb0 = placed["Notebook"];
        check(nb0 != nullptr);
        move_item(nb0, 300, 200);
        check(nb0->model.x == 300 && nb0->model.y == 200);
        Item* tmp = add_widget("Shape");
        check(tmp != nullptr);
        delete_item(tmp);
        check(g_items.size() == 9 + 20);
        // Key property edits (live where cheap to observe).
        check(set_prop(placed["Memo"], "text", "Hello memo", err));
        {
            GtkTextBuffer* b = gtk_text_view_get_buffer(
                GTK_TEXT_VIEW(placed["Memo"]->target));
            GtkTextIter s, e;
            gtk_text_buffer_get_bounds(b, &s, &e);
            gchar* txt = gtk_text_buffer_get_text(b, &s, &e, FALSE);
            check(txt && std::strcmp(txt, "Hello memo") == 0);
            g_free(txt);
        }
        check(set_prop(placed["GroupBox"], "label", "Options", err));
        check(std::strcmp(gtk_frame_get_label(
                              GTK_FRAME(placed["GroupBox"]->target)),
                          "Options") == 0);
        check(set_prop(placed["Panel"], "label", "Panel A", err));
        check(placed["Panel"]->model.props["label"] == "Panel A");
        check(set_prop(placed["Image"], "file", "chart_pro_1024x768.png",
                       err));
        check(placed["Image"]->model.props["file"] ==
              "chart_pro_1024x768.png");
        check(set_prop(placed["Shape"], "shape", "Ellipse", err));
        check(placed["Shape"]->model.props["shape"] == "1");
        check(set_prop(placed["Shape"], "fill-color", "#ff0000", err));
        check(placed["Shape"]->model.props["fill-color"] == "rgb(255,0,0)");
        check(set_prop(placed["Notebook"], "pages", "One|Two", err));
        check(gtk_notebook_get_n_pages(
                  GTK_NOTEBOOK(placed["Notebook"]->target)) == 2);
        check(set_prop(placed["ScrollBox"], "label", "Scroll me", err));
        check(placed["ScrollBox"]->model.props["label"] == "Scroll me");
        check(set_prop(placed["Splitter"], "position", "150", err));
        check(placed["Splitter"]->model.props["position"] == "150");
        check(set_prop(placed["Splitter"], "orientation", "Vertical", err));
        check(placed["Splitter"]->model.props["orientation"] == "1");
        check(set_prop(placed["StatusBar"], "text", "All good", err));
        check(placed["StatusBar"]->model.props["text"] == "All good");
        check(set_prop(placed["ToolBar"], "items", "Cut|Copy|Paste", err));
        check(placed["ToolBar"]->model.props["items"] == "Cut|Copy|Paste");
        check(set_prop(placed["MenuBar"], "menus", "File|Tools", err));
        check(placed["MenuBar"]->model.props["menus"] == "File|Tools");
        check(set_prop(placed["SpinEdit"], "minimum", "0", err));
        check(set_prop(placed["SpinEdit"], "maximum", "50", err));
        check(set_prop(placed["SpinEdit"], "step", "5", err));
        check(set_prop(placed["SpinEdit"], "value", "42", err));
        check(gtk_spin_button_get_value(
                  GTK_SPIN_BUTTON(placed["SpinEdit"]->target)) == 42.0);
        check(set_prop(placed["TrackBar"], "value", "70", err));
        check(gtk_range_get_value(
                  GTK_RANGE(placed["TrackBar"]->target)) == 70.0);
        check(set_prop(placed["TrackBar"], "orientation", "Vertical", err));
        check(placed["TrackBar"]->model.props["orientation"] == "1");
        check(set_prop(placed["ProgressBar"], "fraction", "0.5", err));
        check(gtk_progress_bar_get_fraction(
                  GTK_PROGRESS_BAR(placed["ProgressBar"]->target)) == 0.5);
        check(set_prop(placed["DateTimePicker"], "date", "2026-11-25", err));
        {
            guint y = 0, m = 0, d = 0;
            gtk_calendar_get_date(
                GTK_CALENDAR(placed["DateTimePicker"]->target), &y, &m, &d);
            check(y == 2026 && m == 10 && d == 25);
        }
        check(set_prop(placed["ColorBox"], "color", "#00ff00", err));
        check(placed["ColorBox"]->model.props["color"] == "rgb(0,255,0)");
        check(set_prop(placed["ListView"], "columns", "A|B", err));
        check(set_prop(placed["ListView"], "items", "x,1;y,2", err));
        check(tree_rows(placed["ListView"]->target) == 2);
        check(set_prop(placed["TreeView"], "items", "Root/Leaf;Other", err));
        check(tree_rows(placed["TreeView"]->target) == 2);
        check(set_prop(placed["StringGrid"], "rows", "3", err));
        check(set_prop(placed["StringGrid"], "columns", "2", err));
        check(tree_rows(placed["StringGrid"]->target) == 3);
        check(set_prop(placed["Plot"], "title", "Sine", err));
        check(placed["Plot"]->model.props["title"] == "Sine");
        check(set_prop(placed["Plot"], "line-color", "#0000ff", err));
        check(placed["Plot"]->model.props["line-color"] == "rgb(0,0,255)");
        // Generate the whole canvas and check each new widget's code.
        Project snap7 = snapshot_project();
        GenResult gr7 = generate_project(snap7, snap7.widgets,
                                         "/tmp/fdesigner_gen_test7");
        check(gr7.ok);
        std::map<std::string, const char*> markers = {
            {"Memo_001", "gtk_text_view_new"},
            {"GroupBox_001", "gtk_frame_new"},
            {"Panel_001", "gtk_frame_new"},
            {"Image_001", "gtk_image_new_from_icon_name"},
            {"Shape_001", "cairo_rectangle"},
            {"Notebook_001", "gtk_notebook_append_page"},
            {"ScrollBox_001", "gtk_scrolled_window_new"},
            {"Splitter_001", "gtk_paned_new"},
            {"StatusBar_001", "gtk_statusbar_push"},
            {"ToolBar_001", "gtk_tool_button_new"},
            {"MenuBar_001", "gtk_menu_bar_new"},
            {"SpinEdit_001", "gtk_spin_button_new_with_range"},
            {"TrackBar_001", "gtk_scale_new_with_range"},
            {"ProgressBar_001", "gtk_progress_bar_new"},
            {"DateTimePicker_001", "gtk_calendar_new"},
            {"ColorBox_001", "gtk_color_button_new"},
            {"ListView_001", "gtk_list_store_new"},
            {"TreeView_001", "gtk_tree_store_new"},
            {"StringGrid_001", "gtk_list_store_new"},
            {"Plot_001", "sin("},
        };
        for (const auto& [name, marker] : markers) {
            std::string code =
                read_file("/tmp/fdesigner_gen_test7/" + name + ".cpp");
            check(!code.empty() &&
                  code.find(marker) != std::string::npos);
        }
        check(read_file("/tmp/fdesigner_gen_test7/Shape_001.cpp")
                  .find("Shape_001::on_draw") != std::string::npos);
        check(read_file("/tmp/fdesigner_gen_test7/Plot_001.cpp")
                  .find("Plot_001::on_draw") != std::string::npos);
        std::printf("GENERATED7 /tmp/fdesigner_gen_test7\n");
    }

    // Phase 8: event handlers — connect every natural signal on every
    // widget on the canvas, generate, and check the emitted code carries
    // a stub handler + g_signal_connect for each; types with no natural
    // signal get no event stubs; save/load keeps the connections.
    {
        auto read_file8 = [](const std::string& path) {
            gchar* t = nullptr;
            if (!g_file_get_contents(path.c_str(), &t, nullptr, nullptr) ||
                !t)
                return std::string();
            std::string s = t;
            g_free(t);
            return s;
        };
        // The Events pane itself: ticking Button_001's "clicked" check
        // button connects the signal through the real UI callback.
        {
            Item* b = nullptr;
            for (Item* it : g_items)
                if (it->model.name == "Button_001") b = it;
            check(b != nullptr);
            select_item(b);  // rebuilds the property pane
            GtkWidget* clicked_btn = nullptr;
            GList* kids =
                gtk_container_get_children(GTK_CONTAINER(g_props_grid));
            for (GList* p = kids; p; p = p->next) {
                GtkWidget* ch = GTK_WIDGET(p->data);
                if (GTK_IS_CHECK_BUTTON(ch)) {
                    const char* lab = gtk_button_get_label(GTK_BUTTON(ch));
                    if (lab &&
                        std::string(lab).find("clicked") !=
                            std::string::npos)
                        clicked_btn = ch;
                }
            }
            check(clicked_btn != nullptr);
            if (clicked_btn) {
                check(!gtk_toggle_button_get_active(
                    GTK_TOGGLE_BUTTON(clicked_btn)));
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(clicked_btn),
                                             TRUE);
                check(std::find(b->model.signals.begin(),
                                b->model.signals.end(),
                                "clicked") != b->model.signals.end());
            }
            g_list_free(kids);
        }
        for (Item* it : g_items)
            for (const EventDef& d : events_for(it->model.type))
                if (!d.builtin)
                    check(set_widget_signal(it->model, d.signal, true));
        check(!set_widget_signal(g_items.front()->model, "bogus-signal",
                                 true));
        {
            Widget scratch = g_items.front()->model;
            check(set_widget_signal(scratch, "clicked", false));
            check(std::find(scratch.signals.begin(), scratch.signals.end(),
                            "clicked") == scratch.signals.end());
        }
        Project snap8 = snapshot_project();
        check(snap8.widgets.size() == 29);
        GenResult gr8 = generate_project(snap8, snap8.widgets,
                                         "/tmp/fdesigner_gen_test8");
        check(gr8.ok);
        for (const Widget& w : snap8.widgets) {
            std::string cpp =
                read_file8("/tmp/fdesigner_gen_test8/" + w.name + ".cpp");
            std::string hdr =
                read_file8("/tmp/fdesigner_gen_test8/" + w.name + ".h");
            check(!cpp.empty() && !hdr.empty());
            const std::vector<EventDef>& evs = events_for(w.type);
            if (evs.empty()) {
                // No natural signal: no event stubs at all (the EVENT
                // trace marker only appears in event handler stubs).
                check(cpp.find("EVENT ") == std::string::npos);
                continue;
            }
            for (const EventDef& d : evs) {
                if (d.builtin) {
                    check(cpp.find("\"draw\"") != std::string::npos);
                    check(cpp.find("g_signal_connect") !=
                          std::string::npos);
                    continue;
                }
                check(cpp.find(std::string("\"") + d.signal + "\"") !=
                      std::string::npos);
                check(cpp.find("g_signal_connect") != std::string::npos);
                check(cpp.find("EVENT " + w.name + " " + d.signal) !=
                      std::string::npos);
                if (d.kind == EventKind::ItemClicked) {
                    check(cpp.find("on_clicked_") != std::string::npos);
                    check(cpp.find("tb_cut") != std::string::npos);
                    check(hdr.find("on_clicked_") != std::string::npos);
                } else if (d.kind == EventKind::ItemActivate) {
                    check(cpp.find("on_activate_") != std::string::npos);
                    check(cpp.find("mb_file") != std::string::npos);
                    check(hdr.find("on_activate_") != std::string::npos);
                } else {
                    check(cpp.find(w.name + "::" + d.handler) !=
                          std::string::npos);
                    check(hdr.find(d.handler) != std::string::npos);
                }
            }
        }
        // Save/load round-trip keeps the connections.
        check(save_project_file("/tmp/fdesigner_test8.fdj", snap8, err));
        clear_canvas();
        check(g_items.empty());
        Project loaded8;
        check(load_project_file("/tmp/fdesigner_test8.fdj", loaded8, err));
        load_into_canvas(loaded8);
        check(g_items.size() == 29);
        for (Item* it : g_items) {
            if (it->model.name == "Button_001")
                check(std::find(it->model.signals.begin(),
                                it->model.signals.end(),
                                "clicked") != it->model.signals.end());
            if (it->model.name == "Entry_001")
                check(it->model.signals.size() == 2);
            if (it->model.type == "Label")
                check(it->model.signals.empty());
        }
        std::printf("GENERATED8 /tmp/fdesigner_gen_test8\n");
    }

    // Debugger backend: settings round-trip, breakpoint-location
    // parsing, MI value parsing, staged breakpoints (no gdb needed).
    {
        DebuggerSettings ds;
        ds.gdb_path = "/usr/bin/gdb";
        ds.break_on_main = true;
        ds.pretty_print = false;
        ds.default_args = "--test-arg";
        check(ds.save("/tmp/fdesigner_dbg.conf"));
        DebuggerSettings ds2;
        check(ds2.load("/tmp/fdesigner_dbg.conf"));
        check(ds2.gdb_path == "/usr/bin/gdb" && ds2.break_on_main &&
              !ds2.pretty_print && ds2.default_args == "--test-arg");
        std::string f;
        int ln = 0;
        std::string fn;
        check(parse_breakpoint_location("foo.cpp:12", f, ln, fn) &&
              f == "foo.cpp" && ln == 12);
        check(parse_breakpoint_location("main", f, ln, fn) && fn == "main");
        check(!parse_breakpoint_location("", f, ln, fn));
        check(!parse_breakpoint_location("foo.cpp:abc", f, ln, fn));
        MiValue v;
        size_t pos = 0;
        check(mi_parse_value("{name=\"sum\",type=\"int\",value=\"0\"}",
                             pos, v) &&
              v.get_str("name") == "sum" && v.get_str("value") == "0");
        GdbSession sess;
        check(sess.stage_breakpoint("main"));
        check(sess.stage_breakpoint("foo.cpp:42"));
        check(!sess.stage_breakpoint(""));
        check(sess.breakpoints().size() == 2);
        check(sess.save_breakpoints("/tmp/fdesigner_dbg_bps.conf"));
        GdbSession sess2;
        check(sess2.load_breakpoints("/tmp/fdesigner_dbg_bps.conf"));
        check(sess2.breakpoints().size() == 2 &&
              sess2.breakpoints()[0].location == "main");
    }

    // [font-settings] Font settings: persistence round-trip,
    // sanitising/clamping, and live application to a text view.
    {
        FontSettings fs;
        fs.font = "DejaVu Sans 14";
        fs.code_font = "DejaVu Sans Mono 13";
        check(fs.save("/tmp/fdesigner_font.conf"));
        FontSettings fs2;
        check(fs2.load("/tmp/fdesigner_font.conf"));
        check(fs2.font == "DejaVu Sans 14" &&
              fs2.code_font == "DejaVu Sans Mono 13");
        check(FontSettings::sanitized("", "Sans 10") == "Sans 10");
        check(FontSettings::size_of(
                  FontSettings::sanitized("Sans 99", "Sans 10")) == 48);
        check(FontSettings::size_of(
                  FontSettings::sanitized("Sans 2", "Sans 10")) == 6);
        GtkWidget* tv = gtk_text_view_new();
        gtk_widget_show(tv);
        preferences_apply_to_text_view(tv, fs);
        while (gtk_events_pending()) gtk_main_iteration();
        std::string applied = preferences_applied_font(tv);
        check(FontSettings::family_of(applied) ==
                  "DejaVu Sans Mono" &&
              FontSettings::size_of(applied) == 13);
        gtk_widget_destroy(tv);
        preferences_apply(fs);  // screen-wide, must not crash
        preferences_apply(FontSettings());  // back to defaults
    }

    // ---- CMake (cmake_build.h) ---- Phase 9: settings round-trip,
    // CMakeLists.txt text, and a real configure+build of a small CMake
    // project (console, plus a generated GTK project built by cmake).
    {
        CMakeSettings cs;
        cs.generator = "Unix Makefiles";
        cs.build_type = "Debug";
        cs.build_dir = "out";
        cs.cmake_path = "cmake";
        check(cs.save("/tmp/fdesigner_cmake.conf"));
        CMakeSettings cs2;
        check(cs2.load("/tmp/fdesigner_cmake.conf"));
        check(cs2.generator == "Unix Makefiles" &&
              cs2.build_type == "Debug" && cs2.build_dir == "out" &&
              cs2.cmake_path == "cmake");
        check(cmake_lists_text("Demo", {"main.cpp", "Foo.cpp"}).find(
                  "add_executable(app") != std::string::npos);
        check(!cmake_project_exists("/tmp/fdesigner_cmake_nodir_xyz"));
        // Hand-written console CMake project: configure + build + run.
        std::system("rm -rf /tmp/fdesigner_cmake_console && mkdir -p "
                    "/tmp/fdesigner_cmake_console");
        {
            std::ofstream f("/tmp/fdesigner_cmake_console/CMakeLists.txt");
            f << "cmake_minimum_required(VERSION 3.10)\n"
              << "project(console_demo CXX)\n"
              << "add_executable(app main.cpp)\n";
        }
        {
            std::ofstream f("/tmp/fdesigner_cmake_console/main.cpp");
            f << "#include <cstdio>\nint main() { std::printf(\"hi\\n\"); "
                 "return 0; }\n";
        }
        CMakeSettings def;
        check(cmake_project_exists("/tmp/fdesigner_cmake_console"));
        CMakeBuildResult cr =
            cmake_build_project("/tmp/fdesigner_cmake_console", def);
        check(cr.ok && cr.configured);
        check(std::system("/tmp/fdesigner_cmake_console/build/app "
                          "> /tmp/fdesigner_cmake_console/run.log 2>&1") ==
              0);
        // A broken CMakeLists.txt must fail cleanly, not claim success.
        std::system("rm -rf /tmp/fdesigner_cmake_broken && mkdir -p "
                    "/tmp/fdesigner_cmake_broken");
        {
            std::ofstream f("/tmp/fdesigner_cmake_broken/CMakeLists.txt");
            f << "this is not valid cmake (\n";
        }
        CMakeBuildResult bad =
            cmake_build_project("/tmp/fdesigner_cmake_broken", def);
        check(!bad.ok);
        // The generated GTK project from Phase 3 builds with cmake too.
        CMakeBuildResult gr_cmake =
            cmake_build_project("/tmp/fdesigner_gen_test", def);
        check(gr_cmake.ok && gr_cmake.configured);
    }

    // Phase 10: color themes + code-view gutter / folding.
    {
        // Four distinct built-ins; unknown name falls back to Light.
        std::vector<std::string> tnames = CodeTheme::builtin_names();
        check(tnames.size() == 4);
        CodeTheme light = CodeTheme::builtin("Light");
        CodeTheme dark = CodeTheme::builtin("Dark");
        CodeTheme mono = CodeTheme::builtin("Monokai");
        CodeTheme solar = CodeTheme::builtin("Solarized Dark");
        check(light.keyword != dark.keyword &&
              dark.keyword != mono.keyword &&
              mono.keyword != solar.keyword);
        check(light.background != dark.background);
        check(CodeTheme::builtin("Nope").name == "Light");
        check(CodeTheme::valid_color("#1a2b3c") &&
              !CodeTheme::valid_color("red") &&
              !CodeTheme::valid_color("#12345"));
        // Persistence, including a customised colour on a named base.
        CodeTheme custom = dark;
        custom.keyword = "#ff0000";
        check(custom.save("/tmp/fdesigner_theme.conf"));
        CodeTheme reloaded;
        check(reloaded.load("/tmp/fdesigner_theme.conf"));
        check(reloaded.name == "Dark" && reloaded.keyword == "#ff0000" &&
              reloaded.background == dark.background);
        // Missing file: load fails, defaults kept.
        CodeTheme missing;
        check(!missing.load("/tmp/fdesigner_theme_no_such.conf"));
        check(missing.name == "Light");
        // Tokeniser spans on a representative snippet.
        std::string sample =
            "#include <gtk/gtk.h>\n// hi\nint main() { const char* s = "
            "\"x\"; return 42; }\n";
        std::vector<HlSpan> spans = theme_tokenize(sample);
        auto has_span = [&](const std::string& kind,
                            const std::string& word) {
            for (const auto& sp : spans)
                if (sp.kind == kind &&
                    sample.substr(sp.start, sp.end - sp.start) == word)
                    return true;
            return false;
        };
        check(has_span("preprocessor", "#include <gtk/gtk.h>"));
        check(has_span("comment", "// hi"));
        check(has_span("type", "int"));
        check(has_span("keyword", "return"));
        check(has_span("string", "\"x\""));
        check(has_span("number", "42"));
        // Folding: nested braces, region in/around braces, multi-line
        // comment, #if block, unbalanced braces fail gracefully.
        std::vector<std::string> flines = {
            "#region Outer",   // 0
            "class Foo {",     // 1
            "  void bar() {",  // 2
            "    if (x) {",    // 3
            "      work();",   // 4
            "    }",           // 5
            "    /* multi",    // 6
            "       line */",  // 7
            "  }",             // 8
            "};",              // 9
            "#endregion",     // 10
            "#if DEBUG",      // 11
            "int d = 1;",      // 12
            "#endif",         // 13
            "}",               // 14: extra close, ignored
            "int leftover() {",  // 15: never closed, no fold
            "  return 1;",     // 16
        };
        std::vector<FoldRegion> regs = codeview_find_regions(flines);
        check(regs.size() == 6);
        auto has_region = [&](int st, int en, const std::string& kind) {
            for (const auto& r : regs)
                if (r.start_line == st && r.end_line == en &&
                    r.kind == kind)
                    return true;
            return false;
        };
        check(has_region(0, 10, "region"));
        check(has_region(1, 9, "brace"));
        check(has_region(2, 8, "brace"));
        check(has_region(3, 5, "brace"));
        check(has_region(6, 7, "comment"));
        check(has_region(11, 13, "preproc"));
        // Depths nest: region 0, class 1, method 2, if 3.
        for (const auto& r : regs) {
            if (r.start_line == 0) check(r.depth == 0);
            if (r.start_line == 1) check(r.depth == 1);
            if (r.start_line == 3) check(r.depth == 3);
        }
        // Visible lines: folding the region hides 1..9 only.
        std::vector<int> vis = codeview_visible_lines(flines, {0});
        check(vis.size() == 8 && vis[0] == 0 && vis[1] == 10);
        vis = codeview_visible_lines(flines, {2});
        check(vis.size() == 12);
        vis = codeview_visible_lines(flines, {0, 2});
        check(vis.size() == 8);  // inner fold adds nothing under outer
        // Widget level: fold toggle, gutter clicks, current line, theme.
        std::string ftext;
        for (const auto& l : flines) ftext += l + "\n";
        GtkWidget* cv = codeview_new();
        codeview_set_text(cv, ftext, "/tmp/fdesigner_codeview_test.cpp");
        check(codeview_line_count(cv) == 17);
        check(codeview_regions(cv).size() == 6);
        check(codeview_toggle_fold_at(cv, 3));  // bar() starts line 3
        check(codeview_is_folded(cv, 3));
        check(!codeview_toggle_fold_at(cv, 5));  // line 5: no region start
        codeview_gutter_click(cv, 3, true);     // fold-zone click unfolds
        check(!codeview_is_folded(cv, 3));
        codeview_gutter_click(cv, 5, false);    // breakpoint dot on line 5
        check(codeview_breakpoint_lines(cv).count(5) == 1);
        codeview_gutter_click(cv, 5, false);
        check(codeview_breakpoint_lines(cv).empty());
        codeview_set_current_line(cv, 4);
        check(codeview_current_line(cv) == 4);
        // Round 3: red paused line (Lazarus-style execution-point
        // background) + automatic inspection pane.
        codeview_set_paused_line(cv, 5);
        check(codeview_paused_line(cv) == 5);
        {
            GtkTextBuffer* pbuf = gtk_text_view_get_buffer(
                GTK_TEXT_VIEW(codeview_text_view(cv)));
            GtkTextTagTable* ptab =
                gtk_text_buffer_get_tag_table(pbuf);
            GtkTextTag* paused = gtk_text_tag_table_lookup(
                ptab, "theme-paused-line");
            check(paused != nullptr);
            if (paused) {
                // "background"/"foreground" are write-only; the
                // readable form is "background-gdk"/"foreground-gdk"
                // (boxed GdkColor*, 16-bit channels), as for the
                // keyword tag below.
                GdkColor* bg = nullptr;
                GdkColor* fg = nullptr;
                g_object_get(paused, "background-gdk", &bg,
                             "foreground-gdk", &fg, NULL);
                // #c00000: red dominant; white text for readability.
                check(bg && fg && bg->red > 40000 && bg->green < 8000 &&
                      bg->blue < 8000);
                check(bg && fg && fg->red > 60000 && fg->green > 60000 &&
                      fg->blue > 60000);
                if (bg)
                    g_boxed_free(g_type_from_name("GdkColor"), bg);
                if (fg)
                    g_boxed_free(g_type_from_name("GdkColor"), fg);
                // Paused tag outranks the current-line tag, so red
                // wins when both mark the same line.
                GtkTextTag* cur = gtk_text_tag_table_lookup(
                    ptab, "theme-current-line");
                check(cur != nullptr);
                if (cur)
                    check(gtk_text_tag_get_priority(paused) >
                          gtk_text_tag_get_priority(cur));
            }
        }
        codeview_set_paused_line(cv, 0);
        check(codeview_paused_line(cv) == 0);
        // Inspection pane: states + scope + vars round-trip, and the
        // running state carries no (stale) values.
        check(codeview_inspection_state(cv) ==
              CodeViewInspectState::NoTarget);
        std::vector<Variable> ivars = {
            {"a", "int", "42"}, {"msg", "const char *", "\"hello\""}};
        codeview_set_inspection(cv, CodeViewInspectState::Stopped,
                                "main() \u2014 /tmp/x.c:8 (breakpoint-hit)",
                                ivars);
        check(codeview_inspection_state(cv) ==
              CodeViewInspectState::Stopped);
        check(codeview_inspection_scope(cv) ==
              "main() \u2014 /tmp/x.c:8 (breakpoint-hit)");
        check(codeview_inspection_vars(cv).size() == 2);
        check(codeview_inspection_vars(cv)[0].name == "a");
        check(codeview_inspection_vars(cv)[0].value == "42");
        check(codeview_inspection_vars(cv)[1].name == "msg");
        codeview_set_inspection(cv, CodeViewInspectState::Running, "", {});
        check(codeview_inspection_state(cv) ==
              CodeViewInspectState::Running);
        check(codeview_inspection_vars(cv).empty());
        codeview_set_inspection(cv, CodeViewInspectState::NoTarget, "",
                                {});
        codeview_set_theme(cv, dark);
        GtkTextBuffer* cbuf = gtk_text_view_get_buffer(
            GTK_TEXT_VIEW(codeview_text_view(cv)));
        GtkTextTag* kw = gtk_text_tag_table_lookup(
            gtk_text_buffer_get_tag_table(cbuf), "theme-keyword");
        // "foreground" is write-only on GtkTextTag and
        // "foreground-rgba" does not read back on this GTK; the
        // readable form is "foreground-gdk" (16-bit channels, 8-bit
        // values stored as v*257). Dark keyword #569cd6.
        GdkColor* col = nullptr;
        if (kw) g_object_get(kw, "foreground-gdk", &col, NULL);
        check(kw && col && col->red / 257 == 0x56 &&
              col->green / 257 == 0x9c && col->blue / 257 == 0xd6);
        if (col) g_boxed_free(g_type_from_name("GdkColor"), col);
        gtk_widget_destroy(cv);
        // Breakpoint staging through the real backend (temp file only).
        GdbSession bsess;
        check(bsess.stage_breakpoint("foo.cpp:42"));
        check(bsess.stage_breakpoint("foo.cpp:42"));  // dedup, still 1
        check(bsess.breakpoints().size() == 1);
        check(bsess.remove_staged_breakpoint("foo.cpp:42"));
        check(!bsess.remove_staged_breakpoint("foo.cpp:42"));
        check(bsess.breakpoints().empty());
        check(bsess.stage_breakpoint("foo.cpp:7"));
        check(bsess.save_breakpoints("/tmp/fdesigner_theme_bps.conf"));
        GdbSession bsess2;
        check(bsess2.load_breakpoints("/tmp/fdesigner_theme_bps.conf"));
        check(bsess2.breakpoints().size() == 1 &&
              bsess2.breakpoints()[0].location == "foo.cpp:7");
    }

    std::printf("SELFTEST %s items=%zu\n", ok ? "PASS" : "FAIL",
                g_items.size());
    std::fflush(stdout);
    g_selftest_rc = ok ? 0 : 1;
    gtk_main_quit();
    return G_SOURCE_REMOVE;
}

// ------------------------------------------------------- main

int main(int argc, char** argv) {
    gtk_init(&argc, &argv);

    // [font-settings] Apply saved global font settings before
    // building the UI, so the designer opens in the chosen font.
    preferences_apply(preferences_current());

    // Selection outline for placed widgets.
    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(
        css,
        ".sel { border: 2px solid #3584e4; "
        "background: rgba(53,132,228,0.10); }",
        -1, nullptr);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(), GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    GtkBuilder* builder = gtk_builder_new();
    GError* error = nullptr;
    if (!gtk_builder_add_from_file(builder, "ui/designer.ui", &error)) {
        g_clear_error(&error);
        if (!gtk_builder_add_from_file(builder, "../ui/designer.ui", &error)) {
            std::fprintf(stderr, "fdesigner: cannot load designer.ui: %s\n",
                         error ? error->message : "unknown error");
            return 1;
        }
    }

    g_window = GTK_WIDGET(gtk_builder_get_object(builder, "main_window"));
    g_canvas = GTK_WIDGET(gtk_builder_get_object(builder, "canvas"));
    g_statusbar = GTK_WIDGET(gtk_builder_get_object(builder, "statusbar"));
    g_props_grid = GTK_WIDGET(gtk_builder_get_object(builder, "props_grid"));
    g_props_box = GTK_WIDGET(gtk_builder_get_object(builder, "props_box"));
    g_selected_label =
        GTK_WIDGET(gtk_builder_get_object(builder, "selected_label"));
    g_palette_window =
        GTK_WIDGET(gtk_builder_get_object(builder, "palette_window"));
    if (!g_window || !g_canvas || !g_statusbar || !g_props_grid ||
        !g_props_box || !g_selected_label || !g_palette_window) {
        std::fprintf(stderr, "fdesigner: designer.ui is missing core objects\n");
        return 1;
    }
    g_status_ctx = gtk_statusbar_get_context_id(GTK_STATUSBAR(g_statusbar),
                                                "main");

    // Palette buttons -> add_widget(type)
    for (const TypeInfo& t : kTypes) {
        GObject* b = gtk_builder_get_object(builder, t.palette_id);
        if (b)
            g_signal_connect(b, "clicked", G_CALLBACK(on_palette_clicked),
                             (gpointer)t.key);
    }

    // Clicking empty canvas deselects: the GtkFixed has no window of its
    // own, so empty-area presses land on its parent GtkViewport.
    g_viewport = gtk_widget_get_parent(g_canvas);
    if (g_viewport) {
        gtk_widget_add_events(g_viewport, GDK_BUTTON_PRESS_MASK);
        g_signal_connect(g_viewport, "button-press-event",
                         G_CALLBACK(on_canvas_press), nullptr);
        g_canvas_css = gtk_css_provider_new();
        gtk_style_context_add_provider(
            gtk_widget_get_style_context(g_viewport),
            GTK_STYLE_PROVIDER(g_canvas_css), GTK_STYLE_PROVIDER_PRIORITY_USER);
    }

    GObject* quit = gtk_builder_get_object(builder, "mi_quit");
    if (quit) g_signal_connect(quit, "activate", G_CALLBACK(on_quit), nullptr);
    GObject* gen = gtk_builder_get_object(builder, "mi_generate");
    if (gen)
        g_signal_connect(gen, "activate", G_CALLBACK(on_generate), nullptr);
    GObject* mi_new = gtk_builder_get_object(builder, "mi_new");
    if (mi_new)
        g_signal_connect(mi_new, "activate", G_CALLBACK(on_new), nullptr);
    GObject* mi_open = gtk_builder_get_object(builder, "mi_open");
    if (mi_open)
        g_signal_connect(mi_open, "activate", G_CALLBACK(on_open), nullptr);
    GObject* mi_save = gtk_builder_get_object(builder, "mi_save");
    if (mi_save)
        g_signal_connect(mi_save, "activate", G_CALLBACK(on_save), nullptr);

    // [font-settings] Edit -> Preferences... opens the font
    // settings dialog (src/preferences.cpp).
    GObject* mi_prefs = gtk_builder_get_object(builder, "mi_preferences");
    if (mi_prefs)
        g_signal_connect(mi_prefs, "activate",
                         G_CALLBACK(+[](GtkMenuItem*, gpointer) {
                             preferences_open(g_window);
                         }),
                         nullptr);

    // ---- CMake (cmake_build.h) ----
    // File -> Build with CMake… / Open CMake Project… / CMake Settings…
    GObject* mi_cb = gtk_builder_get_object(builder, "mi_cmake_build");
    if (mi_cb)
        g_signal_connect(mi_cb, "activate", G_CALLBACK(on_cmake_build),
                         nullptr);
    GObject* mi_co = gtk_builder_get_object(builder, "mi_cmake_open");
    if (mi_co)
        g_signal_connect(mi_co, "activate", G_CALLBACK(on_cmake_open),
                         nullptr);
    GObject* mi_cs = gtk_builder_get_object(builder, "mi_cmake_settings");
    if (mi_cs)
        g_signal_connect(mi_cs, "activate", G_CALLBACK(on_cmake_settings),
                         nullptr);

    // View menu -> code view window / theme picker.
    GObject* mi_code = gtk_builder_get_object(builder, "mi_view_code");
    if (mi_code)
        g_signal_connect(mi_code, "activate",
                         G_CALLBACK(+[](GtkMenuItem*, gpointer) {
                             toggle_code_view();
                         }),
                         nullptr);
    GObject* mi_theme = gtk_builder_get_object(builder, "mi_view_theme");
    if (mi_theme)
        g_signal_connect(mi_theme, "activate",
                         G_CALLBACK(+[](GtkMenuItem*, gpointer) {
                             theme_picker_open(g_window,
                                               +[](const CodeTheme& t) {
                                                   if (g_codeview)
                                                       codeview_set_theme(
                                                           g_codeview, t);
                                               });
                         }),
                         nullptr);
    // View menu -> widget palette window (show/hide toggle).
    GObject* mi_pal = gtk_builder_get_object(builder, "mi_view_palette");
    if (mi_pal) {
        g_palette_menu_item = GTK_WIDGET(mi_pal);
        g_signal_connect(mi_pal, "toggled",
                         G_CALLBACK(on_view_palette_toggled), nullptr);
    }
    g_signal_connect(g_palette_window, "delete-event",
                     G_CALLBACK(on_palette_window_delete), nullptr);

    // Debug menu -> stepping debugger window / settings.
    GObject* mi_dbg = gtk_builder_get_object(builder, "mi_debug_open");
    if (mi_dbg)
        g_signal_connect(mi_dbg, "activate",
                         G_CALLBACK(+[](GtkMenuItem*, gpointer) {
                             debugger_window_open(g_window);
                         }),
                         nullptr);
    auto on_dbg_settings = +[](GtkMenuItem*, gpointer) {
        debugger_settings_open(g_window);
    };
    GObject* mi_dset = gtk_builder_get_object(builder, "mi_debug_settings");
    if (mi_dset)
        g_signal_connect(mi_dset, "activate", G_CALLBACK(on_dbg_settings),
                         nullptr);
    GObject* mi_dset2 = gtk_builder_get_object(builder, "mi_dbg_settings");
    if (mi_dset2)
        g_signal_connect(mi_dset2, "activate", G_CALLBACK(on_dbg_settings),
                         nullptr);
    // "Debugger" sub-menu toggles: flip the setting, save it, and apply
    // it to any live session (takes effect on the next Run).
    auto on_dbg_toggle = +[](GtkCheckMenuItem* item, gpointer which) {
        DebuggerSettings s = debugger_current_settings();
        bool on = gtk_check_menu_item_get_active(item);
        const char* w = (const char*)which;
        if (!std::strcmp(w, "main")) s.break_on_main = on;
        else if (!std::strcmp(w, "entry")) s.stop_on_entry = on;
        else if (!std::strcmp(w, "pretty")) s.pretty_print = on;
        s.save();
    };
    GObject* mi_bm = gtk_builder_get_object(builder, "mi_dbg_break_main");
    if (mi_bm) {
        gtk_check_menu_item_set_active(
            GTK_CHECK_MENU_ITEM(mi_bm),
            debugger_current_settings().break_on_main);
        g_signal_connect(mi_bm, "activate", G_CALLBACK(on_dbg_toggle),
                         (gpointer)"main");
    }
    GObject* mi_se = gtk_builder_get_object(builder, "mi_dbg_stop_entry");
    if (mi_se) {
        gtk_check_menu_item_set_active(
            GTK_CHECK_MENU_ITEM(mi_se),
            debugger_current_settings().stop_on_entry);
        g_signal_connect(mi_se, "activate", G_CALLBACK(on_dbg_toggle),
                         (gpointer)"entry");
    }
    GObject* mi_pr = gtk_builder_get_object(builder, "mi_dbg_pretty");
    if (mi_pr) {
        gtk_check_menu_item_set_active(
            GTK_CHECK_MENU_ITEM(mi_pr),
            debugger_current_settings().pretty_print);
        g_signal_connect(mi_pr, "activate", G_CALLBACK(on_dbg_toggle),
                         (gpointer)"pretty");
    }

    g_signal_connect(g_window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
    g_signal_connect(g_window, "key-press-event", G_CALLBACK(on_key_press),
                     nullptr);

    build_bulk_ui();
    build_colour_ui();
    refresh_props();  // window properties until something is selected
    status("Phase 2 — pick a widget from the palette to place it");
    gtk_widget_show_all(g_window);

    // Palette window: its own top-level window, opened together with the
    // designer and parked to the left of the design window (right if
    // there is no room on the left). Plain top-level, not transient or
    // modal, so it never blocks the canvas.
    if (g_palette_window) {
        // [palette-window] Tight Lazarus-style packing: small buttons,
        // minimal padding, no whitespace between rows. USER priority so
        // it wins over the application theme/font providers.
        GtkCssProvider* pal_css = gtk_css_provider_new();
        gtk_css_provider_load_from_data(
            pal_css,
            "#palette_window button { min-height: 20px; padding: 1px 4px; }"
            "#palette_window button label { padding: 0px; }"
            "#palette_window label { padding: 0px; }",
            -1, nullptr);
        gtk_style_context_add_provider(
            gtk_widget_get_style_context(g_palette_window),
            GTK_STYLE_PROVIDER(pal_css),
            GTK_STYLE_PROVIDER_PRIORITY_USER);
        gtk_widget_show_all(g_palette_window);
        while (gtk_events_pending()) gtk_main_iteration();
        gint mx = 0, my = 0, mw = 0;
        gtk_window_get_position(GTK_WINDOW(g_window), &mx, &my);
        gtk_window_get_size(GTK_WINDOW(g_window), &mw, nullptr);
        gint pw = 0;
        gtk_window_get_size(GTK_WINDOW(g_palette_window), &pw, nullptr);
        gint px = (mx >= pw + 12) ? mx - pw - 12 : mx + mw + 12;
        GdkRectangle mgeo = {0, 0, 1920, 1080};
        GdkMonitor* mon = gdk_display_get_primary_monitor(
            gdk_screen_get_display(gdk_screen_get_default()));
        if (mon) gdk_monitor_get_geometry(mon, &mgeo);
        gint sw = mgeo.width;
        if (px + pw > sw) px = sw - pw - 4;
        if (px < 0) px = 0;
        gtk_window_move(GTK_WINDOW(g_palette_window), px, my);
    }

    // The code window is a separate top-level window that opens
    // together with the designer (View -> Code View toggles it).
    // It opens AFTER the palette window so open_code_view() can park it
    // beside the palette instead of over the designer.
    open_code_view();
    // Keep the code window's paused line + inspection pane in step
    // with the shared debugger session (200 ms poll).
    g_timeout_add(200, on_codewin_poll, nullptr);
    if (std::getenv("FDESIGNER_OPEN_DEBUGGER") != nullptr)
        g_idle_add(+[](gpointer) -> gboolean {
                       debugger_window_open(g_window);
                       return G_SOURCE_REMOVE;
                   },
                   nullptr);
    // Test hook (also used by tests/debugger_tests.sh): open the debugger
    // window and its settings dialog at startup.
    if (std::getenv("FDESIGNER_OPEN_DEBUGGER_SETTINGS") != nullptr)
        g_idle_add(+[](gpointer) -> gboolean {
                       debugger_window_open(g_window);
                       debugger_settings_open(g_window);
                       return G_SOURCE_REMOVE;
                   },
                   nullptr);
    // [font-settings] Test hook (used by tests/font_tests.sh):
    // open the Preferences dialog at startup.
    if (std::getenv("FDESIGNER_OPEN_PREFERENCES") != nullptr)
        g_idle_add(+[](gpointer) -> gboolean {
                       preferences_open(g_window);
                       return G_SOURCE_REMOVE;
                   },
                   nullptr);

    // Test hook (used by tests/code_window_tests.sh): start a target
    // under the debugger at startup, staging a breakpoint first.
    // FDESIGNER_DEBUG_TARGET=<exe> FDESIGNER_DEBUG_BREAK=<file:line>
    const char* dbg_target = std::getenv("FDESIGNER_DEBUG_TARGET");
    if (dbg_target != nullptr) {
        std::string exe = dbg_target;
        const char* bp = std::getenv("FDESIGNER_DEBUG_BREAK");
        std::string loc = bp ? bp : "";
        g_idle_add(
            +[](gpointer data) -> gboolean {
                auto* pair = (std::pair<std::string, std::string>*)data;
                debugger_window_open(g_window);
                std::string err;
                if (!debugger_autorun(pair->first, pair->second, err))
                    status("Debug autorun failed: " + err);
                delete pair;
                return G_SOURCE_REMOVE;
            },
            new std::pair<std::string, std::string>(exe, loc));
    }
    // Test hooks (also used by tests/theme_tests.sh): open the code
    // view / theme picker at startup.
    if (std::getenv("FDESIGNER_OPEN_CODEVIEW") != nullptr)
        g_idle_add(+[](gpointer) -> gboolean {
                       open_code_view();
                       return G_SOURCE_REMOVE;
                   },
                   nullptr);
    if (std::getenv("FDESIGNER_OPEN_THEME") != nullptr)
        g_idle_add(+[](gpointer) -> gboolean {
                       theme_picker_open(g_window, nullptr);
                       return G_SOURCE_REMOVE;
                   },
                   nullptr);

    bool selftest = std::getenv("FDESIGNER_SELFTEST") != nullptr;
    if (selftest) g_idle_add(selftest_step, nullptr);

    gtk_main();
    return selftest ? g_selftest_rc : 0;
}

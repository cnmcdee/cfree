// FDesigner code generator — see generator.h. All-code output with one
// standalone class per widget, mirroring the Python generator's shape.

#include "generator.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "cmake_build.h"
#include "drawings_embedded.h"
#include "events.h"
#include "indicators_embedded.h"
#include "props.h"

namespace fs = std::filesystem;

// ---------------------------------------------------------------- helpers

// Escape a string for a C++ string literal.
static std::string esc_c(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\%03o", c);
                    out += buf;
                } else {
                    out += (char)c;
                }
        }
    }
    return out;
}

// GTK constant name for an enum property value, or "" if unmapped.
static std::string enum_const(const std::string& prop, int v) {
    static const std::map<std::string, std::vector<std::string>> kTable = {
        {"relief",
         {"GTK_RELIEF_NORMAL", "GTK_RELIEF_HALF", "GTK_RELIEF_NONE"}},
        {"shadow-type",
         {"GTK_SHADOW_NONE", "GTK_SHADOW_IN", "GTK_SHADOW_OUT",
          "GTK_SHADOW_ETCHED_IN", "GTK_SHADOW_ETCHED_OUT"}},
        {"selection-mode",
         {"GTK_SELECTION_NONE", "GTK_SELECTION_SINGLE", "GTK_SELECTION_BROWSE",
          "GTK_SELECTION_MULTIPLE"}},
        {"direction",
         {"GTK_ARROW_UP", "GTK_ARROW_DOWN", "GTK_ARROW_LEFT",
          "GTK_ARROW_RIGHT", "GTK_ARROW_NONE"}},
        {"wrap-mode",
         {"GTK_WRAP_NONE", "GTK_WRAP_CHAR", "GTK_WRAP_WORD",
          "GTK_WRAP_WORD_CHAR"}},
        {"orientation",
         {"GTK_ORIENTATION_HORIZONTAL", "GTK_ORIENTATION_VERTICAL"}},
        {"toolbar-style",
         {"GTK_TOOLBAR_ICONS", "GTK_TOOLBAR_TEXT", "GTK_TOOLBAR_BOTH",
          "GTK_TOOLBAR_BOTH_HORIZ"}},
    };
    auto it = kTable.find(prop);
    if (it == kTable.end() || v < 0 || v >= (int)it->second.size()) return "";
    return it->second[v];
}

// Format a stored double for a C++ literal (varargs need a real double:
// "0" would pass an int, so make sure a '.' or exponent is present).
static std::string fmt_double(const std::string& v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%g", std::atof(v.c_str()));
    std::string s = buf;
    if (s.find_first_of(".eEnN") == std::string::npos) s += ".0";
    return s;
}

static std::string prop_or(const Widget& w, const std::string& name,
                           const std::string& dflt) {
    auto it = w.props.find(name);
    return it == w.props.end() ? dflt : it->second;
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Split an indicators spec ("SMA(20), MACD(12,26,9), RSI") into its
// individual specs — commas inside parentheses don't split.
static std::vector<std::string> split_indicator_specs(
    const std::string& spec) {
    std::vector<std::string> out;
    int depth = 0;
    size_t start = 0;
    for (size_t i = 0; i <= spec.size(); ++i) {
        if (i == spec.size() || (spec[i] == ',' && depth == 0)) {
            std::string one = trim(spec.substr(start, i - start));
            if (!one.empty()) out.push_back(one);
            start = i + 1;
        } else if (spec[i] == '(') {
            ++depth;
        } else if (spec[i] == ')') {
            --depth;
        }
    }
    return out;
}

// Split a compare-files spec ("a.csv, b.csv") on commas.
static std::vector<std::string> split_compare_files(
    const std::string& spec) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= spec.size(); ++i) {
        if (i == spec.size() || spec[i] == ',') {
            std::string one = trim(spec.substr(start, i - start));
            if (!one.empty()) out.push_back(one);
            start = i + 1;
        }
    }
    return out;
}

// Split a drawings spec ("trend ...; hline ...") on semicolons.
static std::vector<std::string> split_drawings_specs(
    const std::string& spec) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= spec.size(); ++i) {
        if (i == spec.size() || spec[i] == ';') {
            std::string one = trim(spec.substr(start, i - start));
            if (!one.empty()) out.push_back(one);
            start = i + 1;
        }
    }
    return out;
}

static std::string sanitize_class(const std::string& raw) {
    std::string out;
    for (char c : raw)
        if (std::isalnum((unsigned char)c) || c == '_') out += c;
    if (out.empty()) out = "MyWindow";
    if (std::isdigit((unsigned char)out[0])) out = "W" + out;
    return out;
}

// ---------------------------------------------------------------- menus

struct MenuDef {
    std::string label;
    std::vector<std::string> items;
};

// Parse the designer's menu spec: one "Label: Item | Item | -" per line.
static std::vector<MenuDef> parse_menus(const std::string& spec) {
    std::vector<MenuDef> out;
    std::istringstream in(spec);
    std::string line;
    while (std::getline(in, line)) {
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        MenuDef m;
        m.label = trim(line.substr(0, colon));
        if (m.label.empty()) continue;
        std::istringstream items(line.substr(colon + 1));
        std::string item;
        while (std::getline(items, item, '|')) {
            item = trim(item);
            if (!item.empty()) m.items.push_back(item);
        }
        out.push_back(m);
    }
    return out;
}

// Identifier fragment from a menu label ("Save As" -> "save_as").
static std::string menu_id_part(const std::string& label) {
    std::string out;
    for (unsigned char c : label) {
        if (std::isalnum(c))
            out += (char)std::tolower(c);
        else if (!out.empty() && out.back() != '_')
            out += '_';
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out.empty() ? "item" : out;
}

static std::string menu_item_id(const MenuDef& m, const std::string& item) {
    return "menu_" + menu_id_part(m.label) + "_" + menu_id_part(item);
}

// ------------------------------------------------------- mouse stub sets
// The Python generator's nine handlers per widget, in its order. In a
// widget class the handlers are unprefixed (Button_001::buttonPress1);
// in the window class the root set keeps the Root_001_ prefix.

static const char* kPress[3] = {"buttonPress1", "buttonPress2",
                                "buttonPress3"};
static const char* kDbl[3] = {"dblbuttonPress1", "dblbuttonPress2",
                              "dblbuttonPress3"};
static const char* kRel[3] = {"buttonRelease1", "buttonRelease2",
                              "buttonRelease3"};

struct StubNames {
    std::string prefix;   // "Root_001_" or ""
    std::string press;    // dispatcher names
    std::string release;
};

static void stub_decls(std::ostringstream& o, const StubNames& n) {
    for (int i = 0; i < 3; ++i)
        o << "    void " << n.prefix << kPress[i]
          << "(GdkEventButton* event);\n";
    for (int i = 0; i < 3; ++i)
        o << "    void " << n.prefix << kDbl[i]
          << "(GdkEventButton* event);\n";
    for (int i = 0; i < 3; ++i)
        o << "    void " << n.prefix << kRel[i]
          << "(GdkEventButton* event);\n";
    o << "    static gboolean " << n.press
      << "(GtkWidget*, GdkEventButton*, gpointer self);\n";
    o << "    static gboolean " << n.release
      << "(GtkWidget*, GdkEventButton*, gpointer self);\n";
}

static void stub_defs(std::ostringstream& o, const std::string& cls,
                      const StubNames& n) {
    auto one = [&](const char* handler) {
        o << "\nvoid " << cls << "::" << n.prefix << handler
          << "(GdkEventButton*) {\n"
          << "    // Code block here\n"
          << "}\n";
    };
    for (int i = 0; i < 3; ++i) one(kPress[i]);
    for (int i = 0; i < 3; ++i) one(kDbl[i]);
    for (int i = 0; i < 3; ++i) one(kRel[i]);
    o << "\ngboolean " << cls << "::" << n.press
      << "(GtkWidget*, GdkEventButton* e, gpointer self) {\n"
      << "    auto* me = static_cast<" << cls << "*>(self);\n"
      << "    if (e->type == GDK_2BUTTON_PRESS) {\n"
      << "        if (e->button == 1) me->" << n.prefix << kDbl[0]
      << "(e);\n"
      << "        else if (e->button == 2) me->" << n.prefix << kDbl[1]
      << "(e);\n"
      << "        else if (e->button == 3) me->" << n.prefix << kDbl[2]
      << "(e);\n"
      << "    } else {\n"
      << "        if (e->button == 1) me->" << n.prefix << kPress[0]
      << "(e);\n"
      << "        else if (e->button == 2) me->" << n.prefix << kPress[1]
      << "(e);\n"
      << "        else if (e->button == 3) me->" << n.prefix << kPress[2]
      << "(e);\n"
      << "    }\n"
      << "    return FALSE;\n"
      << "}\n";
    o << "\ngboolean " << cls << "::" << n.release
      << "(GtkWidget*, GdkEventButton* e, gpointer self) {\n"
      << "    auto* me = static_cast<" << cls << "*>(self);\n"
      << "    if (e->button == 1) me->" << n.prefix << kRel[0] << "(e);\n"
      << "    else if (e->button == 2) me->" << n.prefix << kRel[1]
      << "(e);\n"
      << "    else if (e->button == 3) me->" << n.prefix << kRel[2]
      << "(e);\n"
      << "    return FALSE;\n"
      << "}\n";
}

// Connect a widget's press/release dispatchers (the .bind() equivalent).
static std::string connect_mouse(const std::string& cls,
                                 const std::string& target,
                                 const StubNames& n, int indent) {
    std::string p(indent, ' ');
    std::ostringstream o;
    o << p << "gtk_widget_add_events(" << target
      << ", GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);\n"
      << p << "g_signal_connect(" << target << ", \"button-press-event\",\n"
      << p << "                 G_CALLBACK(&" << cls << "::" << n.press
      << "), this);\n"
      << p << "g_signal_connect(" << target
      << ", \"button-release-event\",\n"
      << p << "                 G_CALLBACK(&" << cls << "::" << n.release
      << "), this);\n";
    return o.str();
}

// ------------------------------------------------------- event handlers
static std::vector<std::string> split_on(char sep,
                                         const std::string& spec);
// The connected signals of one widget (Widget.signals, validated against
// the event table, de-duplicated). Builtin events (Shape/Plot/StockChart
// draw) are already wired by the generator and are left out here so they
// are never connected twice.
static std::vector<const EventDef*> connected_events(const Widget& w) {
    std::vector<const EventDef*> out;
    std::set<std::string> seen;
    for (const std::string& s : w.signals) {
        const EventDef* d = find_event(w.type, s);
        if (!d || d->builtin) continue;
        if (!seen.insert(s).second) continue;
        out.push_back(d);
    }
    return out;
}

// Identifier fragments for per-item handlers (ToolBar tool buttons,
// MenuBar top-level items) from the widget's items/menus property.
static std::vector<std::string> event_item_ids(const Widget& w) {
    std::vector<std::string> out;
    std::string spec;
    if (w.type == "ToolBar")
        spec = prop_or(w, "items", "New|Open|Save");
    else if (w.type == "MenuBar")
        spec = prop_or(w, "menus", "File|Edit|Help");
    else
        return out;
    for (const std::string& name : split_on('|', spec))
        out.push_back(menu_id_part(name));
    return out;
}

static bool event_connected(const Widget& w, const char* signal) {
    for (const std::string& s : w.signals)
        if (s == signal) return true;
    return false;
}

// One g_signal_connect line (4-space indent) for a plain event.
static std::string event_connect_line(const Widget& w, const EventDef& d,
                                      const std::string& target) {
    std::ostringstream o;
    std::string cb = std::string(d.handler) + "_cb";
    switch (d.kind) {
        case EventKind::BufferChanged:
            o << "    g_signal_connect(gtk_text_view_get_buffer(GTK_TEXT_VIEW("
              << target << ")), \"" << d.signal << "\", G_CALLBACK(&"
              << w.name << "::" << cb << "), this);\n";
            break;
        case EventKind::Draw:
            o << "    g_signal_connect(" << target << ", \"" << d.signal
              << "\", G_CALLBACK(&" << w.name << "::" << d.handler
              << "), this);\n";
            break;
        default:
            o << "    g_signal_connect(" << target << ", \"" << d.signal
              << "\", G_CALLBACK(&" << w.name << "::" << cb << "), this);\n";
            break;
    }
    return o.str();
}

// All g_signal_connect lines for a widget's connected events (the
// per-item and per-cell kinds are emitted where the items/cells are
// created instead).
static std::string event_connect_lines(const Widget& w,
                                       const std::string& target) {
    std::ostringstream o;
    for (const EventDef* d : connected_events(w)) {
        if (d->kind == EventKind::ItemClicked ||
            d->kind == EventKind::ItemActivate ||
            d->kind == EventKind::CellChanged)
            continue;
        o << event_connect_line(w, *d, target);
    }
    return o.str();
}

// Handler declarations for the widget class header.
static void event_handler_decls(std::ostringstream& o, const Widget& w) {
    std::vector<const EventDef*> evs = connected_events(w);
    if (evs.empty()) return;
    o << "\n    // Event handlers (connected signals — see events.h)\n";
    for (const EventDef* d : evs) {
        switch (d->kind) {
            case EventKind::Simple:
            case EventKind::BufferChanged:
            case EventKind::CellChanged:
                o << "    void " << d->handler << "();\n"
                  << "    static void " << d->handler
                  << "_cb(GtkWidget*, gpointer);\n";
                break;
            case EventKind::SwitchPage:
                o << "    void " << d->handler << "(guint page);\n"
                  << "    static void " << d->handler
                  << "_cb(GtkNotebook*, GtkWidget*, guint, gpointer);\n";
                break;
            case EventKind::RowActivated:
                o << "    void " << d->handler << "(GtkTreePath* path);\n"
                  << "    static void " << d->handler
                  << "_cb(GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*, "
                     "gpointer);\n";
                break;
            case EventKind::RowSelected:
                o << "    void " << d->handler << "();\n"
                  << "    static void " << d->handler
                  << "_cb(GtkListBox*, GtkListBoxRow*, gpointer);\n";
                break;
            case EventKind::DeleteEvent:
                o << "    bool " << d->handler << "();\n"
                  << "    static gboolean " << d->handler
                  << "_cb(GtkWidget*, GdkEvent*, gpointer);\n";
                break;
            case EventKind::Draw:
                o << "    static gboolean " << d->handler
                  << "(GtkWidget*, cairo_t*, gpointer);\n";
                break;
            case EventKind::ItemClicked:
                for (const std::string& id : event_item_ids(w))
                    o << "    void " << d->handler << "_" << id << "();\n"
                      << "    static void " << d->handler << "_" << id
                      << "_cb(GtkToolButton*, gpointer);\n";
                break;
            case EventKind::ItemActivate:
                for (const std::string& id : event_item_ids(w))
                    o << "    void " << d->handler << "_" << id << "();\n"
                      << "    static void " << d->handler << "_" << id
                      << "_cb(GtkMenuItem*, gpointer);\n";
                break;
        }
    }
}

// The trace line a stub prints under FDESIGNER_EVENT_TRACE (compiled
// into generated apps only when the test harness defines the macro).
static std::string event_trace(const Widget& w, const EventDef& d,
                               const std::string& detail) {
    std::string line = "#ifdef FDESIGNER_EVENT_TRACE\n"
                       "    std::fprintf(stderr, \"EVENT " +
                       w.name + " " + d.signal + detail + "\\n\");\n#endif\n";
    return line;
}

// Handler definitions for the widget class .cpp: the member stub the
// user fills in, plus the static trampoline GTK calls.
static void event_handler_defs(std::ostringstream& o, const Widget& w) {
    for (const EventDef* d : connected_events(w)) {
        std::string cb = std::string(d->handler) + "_cb";
        switch (d->kind) {
            case EventKind::Simple:
            case EventKind::BufferChanged:
            case EventKind::CellChanged:
                o << "\nvoid " << w.name << "::" << d->handler << "() {\n"
                  << "    // Code block here\n"
                  << event_trace(w, *d, "") << "}\n"
                  << "\nvoid " << w.name << "::" << cb
                  << "(GtkWidget*, gpointer self) {\n"
                  << "    auto* me = static_cast<" << w.name << "*>(self);\n"
                  << "    me->" << d->handler << "();\n}\n";
                break;
            case EventKind::SwitchPage:
                o << "\nvoid " << w.name << "::" << d->handler
                  << "(guint page) {\n"
                  << "    // Code block here\n"
                  << event_trace(w, *d, "") << "    (void)page;\n}\n"
                  << "\nvoid " << w.name << "::" << cb
                  << "(GtkNotebook*, GtkWidget*, guint page, gpointer self) {\n"
                  << "    auto* me = static_cast<" << w.name << "*>(self);\n"
                  << "    me->" << d->handler << "(page);\n}\n";
                break;
            case EventKind::RowActivated:
                o << "\nvoid " << w.name << "::" << d->handler
                  << "(GtkTreePath* path) {\n"
                  << "    // Code block here\n"
                  << event_trace(w, *d, "") << "    (void)path;\n}\n"
                  << "\nvoid " << w.name << "::" << cb
                  << "(GtkTreeView*, GtkTreePath* path, GtkTreeViewColumn*, "
                     "gpointer self) {\n"
                  << "    auto* me = static_cast<" << w.name << "*>(self);\n"
                  << "    me->" << d->handler << "(path);\n}\n";
                break;
            case EventKind::RowSelected:
                o << "\nvoid " << w.name << "::" << d->handler << "() {\n"
                  << "    // Code block here\n"
                  << event_trace(w, *d, "") << "}\n"
                  << "\nvoid " << w.name << "::" << cb
                  << "(GtkListBox*, GtkListBoxRow*, gpointer self) {\n"
                  << "    auto* me = static_cast<" << w.name << "*>(self);\n"
                  << "    me->" << d->handler << "();\n}\n";
                break;
            case EventKind::DeleteEvent:
                o << "\nbool " << w.name << "::" << d->handler << "() {\n"
                  << "    // Code block here — return true to stop the "
                     "window closing.\n"
                  << event_trace(w, *d, "") << "    return false;\n}\n"
                  << "\n"
                     "gboolean "
                  << w.name << "::" << cb
                  << "(GtkWidget*, GdkEvent*, gpointer self) {\n"
                  << "    auto* me = static_cast<" << w.name << "*>(self);\n"
                  << "    return me->" << d->handler << "() ? TRUE : FALSE;\n}\n";
                break;
            case EventKind::Draw:
                o << "\ngboolean " << w.name << "::" << d->handler
                  << "(GtkWidget*, cairo_t*, gpointer) {\n"
                  << "    // Code block here — draw with cairo.\n"
                  << event_trace(w, *d, "") << "    return FALSE;\n}\n";
                break;
            case EventKind::ItemClicked:
                for (const std::string& id : event_item_ids(w)) {
                    std::string h = std::string(d->handler) + "_" + id;
                    o << "\nvoid " << w.name << "::" << h << "() {\n"
                      << "    // Code block here\n"
                      << event_trace(w, *d, " " + id) << "}\n"
                      << "\nvoid " << w.name << "::" << h
                      << "_cb(GtkToolButton*, gpointer self) {\n"
                      << "    auto* me = static_cast<" << w.name
                      << "*>(self);\n"
                      << "    me->" << h << "();\n}\n";
                }
                break;
            case EventKind::ItemActivate:
                for (const std::string& id : event_item_ids(w)) {
                    std::string h = std::string(d->handler) + "_" + id;
                    o << "\nvoid " << w.name << "::" << h << "() {\n"
                      << "    // Code block here\n"
                      << event_trace(w, *d, " " + id) << "}\n"
                      << "\nvoid " << w.name << "::" << h
                      << "_cb(GtkMenuItem*, gpointer self) {\n"
                      << "    auto* me = static_cast<" << w.name
                      << "*>(self);\n"
                      << "    me->" << h << "();\n}\n";
                }
                break;
        }
    }
}

// g_object_set lines for a widget's stored properties, one per property
// (one steppable line each). Skips colours (CSS) and per-type properties
// consumed at creation time.
static std::string prop_lines(const Widget& w, const std::string& target,
                              const std::set<std::string>& skip) {
    std::ostringstream o;
    for (const auto& [name, value] : w.props) {
        if (name == "background" || name == "foreground") continue;
        if (skip.count(name)) continue;
        const PropDef* def = find_prop(w.type, name);
        if (!def) continue;
        if ((w.type == "Scale" || w.type == "TrackBar") &&
            name == "value") {
            o << "    gtk_range_set_value(GTK_RANGE(" << target << "), "
              << fmt_double(value) << ");\n";
            continue;
        }
        std::string v;
        switch (def->kind) {
            case PropKind::Str: v = "\"" + esc_c(value) + "\""; break;
            case PropKind::Bool: v = value == "true" ? "TRUE" : "FALSE"; break;
            case PropKind::Int: v = value; break;
            case PropKind::Double: v = fmt_double(value); break;
            case PropKind::Enum: {
                std::string c = enum_const(name, std::atoi(value.c_str()));
                v = c.empty() ? value : c;
                break;
            }
            case PropKind::Color: continue;
        }
        o << "    g_object_set(" << target << ", \"" << name << "\", "
          << v << ", nullptr);\n";
    }
    return o.str();
}

// A widget's own colours as a small CSS block on its style context.
static std::string colour_lines(const Widget& w, const std::string& target) {
    auto bg = w.props.find("background");
    auto fg = w.props.find("foreground");
    if (bg == w.props.end() && fg == w.props.end()) return "";
    std::string body;
    if (bg != w.props.end()) body += "background: " + bg->second + "; ";
    if (fg != w.props.end()) body += "color: " + fg->second + "; ";
    std::ostringstream o;
    o << "    GtkCssProvider* css = gtk_css_provider_new();\n"
      << "    gtk_css_provider_load_from_data(css, \"* { " << body
      << "}\", -1, nullptr);\n"
      << "    gtk_style_context_add_provider(gtk_widget_get_style_"
         "context("
      << target << "),\n"
      << "                                       GTK_STYLE_PROVIDER(css), "
         "GTK_STYLE_PROVIDER_PRIORITY_USER);\n";
    return o.str();
}

// ------------------------------------------------------- widget classes

// Split a spec on one separator, trimming parts, dropping empties.
static std::vector<std::string> split_on(char sep, const std::string& spec) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= spec.size(); ++i) {
        if (i == spec.size() || spec[i] == sep) {
            std::string one = trim(spec.substr(start, i - start));
            if (!one.empty()) out.push_back(one);
            start = i + 1;
        }
    }
    return out;
}

// "YYYY-MM-DD" -> y/m/d, false on garbage.
static bool parse_ymd_gen(const std::string& s, int& y, int& m, int& d) {
    return std::sscanf(s.c_str(), "%d-%d-%d", &y, &m, &d) == 3 && y >= 1 &&
           m >= 1 && m <= 12 && d >= 1 && d <= 31;
}

// Append one notebook page per "Tab 1|Tab 2" entry (label body + tab label).
static std::string emit_notebook_pages(const Widget& w, const std::string& M) {
    std::ostringstream o;
    for (const std::string& page :
         split_on('|', prop_or(w, "pages", "Tab 1|Tab 2|Tab 3")))
        o << "    gtk_notebook_append_page(GTK_NOTEBOOK(" << M
          << "), gtk_label_new(\"" << esc_c(page)
          << "\"), gtk_label_new(\"" << esc_c(page) << "\"));\n";
    return o.str();
}

// Append one tool button per "New|Open|Save" entry. When the ToolBar's
// "clicked" event is connected, each button is kept in a local and wired
// to its own on_<item>_clicked handler.
static std::string emit_toolbar_items(const Widget& w, const std::string& M) {
    std::ostringstream o;
    bool connect = event_connected(w, "clicked");
    for (const std::string& name :
         split_on('|', prop_or(w, "items", "New|Open|Save"))) {
        if (connect) {
            std::string id = menu_id_part(name);
            o << "    GtkToolItem* tb_" << id
              << " = gtk_tool_button_new(nullptr, \"" << esc_c(name)
              << "\");\n"
              << "    gtk_toolbar_insert(GTK_TOOLBAR(" << M << "), tb_" << id
              << ", -1);\n"
              << "    g_signal_connect(tb_" << id
              << ", \"clicked\", G_CALLBACK(&" << w.name << "::on_clicked_"
              << id << "_cb), this);\n";
        } else {
            o << "    gtk_toolbar_insert(GTK_TOOLBAR(" << M
              << "), gtk_tool_button_new(nullptr, \"" << esc_c(name)
              << "\"), -1);\n";
        }
    }
    return o.str();
}

// Append one top-level item per "File|Edit|Help" entry. When the MenuBar's
// "activate" event is connected, each item is kept in a local and wired
// to its own on_<item>_activate handler.
static std::string emit_menubar_items(const Widget& w, const std::string& M) {
    std::ostringstream o;
    bool connect = event_connected(w, "activate");
    for (const std::string& name :
         split_on('|', prop_or(w, "menus", "File|Edit|Help"))) {
        if (connect) {
            std::string id = menu_id_part(name);
            o << "    GtkWidget* mb_" << id
              << " = gtk_menu_item_new_with_label(\"" << esc_c(name)
              << "\");\n"
              << "    gtk_menu_shell_append(GTK_MENU_SHELL(" << M << "), mb_"
              << id << ");\n"
              << "    g_signal_connect(mb_" << id
              << ", \"activate\", G_CALLBACK(&" << w.name
              << "::on_activate_" << id << "_cb), this);\n";
        } else {
            o << "    gtk_menu_shell_append(GTK_MENU_SHELL(" << M
              << "), gtk_menu_item_new_with_label(\"" << esc_c(name)
              << "\"));\n";
        }
    }
    return o.str();
}

// Build a ListView's ListStore + columns from its columns/items props.
// Columns: "Name|Value". Items: rows separated by ';', cells by ','.
static std::string emit_listview_setup(const Widget& w, const std::string& M) {
    std::vector<std::string> cols =
        split_on('|', prop_or(w, "columns", "Name|Value"));
    if (cols.empty()) cols.push_back("Name");
    std::vector<std::vector<std::string>> rows;
    for (const std::string& row : split_on(
             ';', prop_or(w, "items", "Row 1,Value 1;Row 2,Value 2")))
        rows.push_back(split_on(',', row));
    std::ostringstream o;
    o << "    GtkListStore* store = gtk_list_store_new(" << cols.size();
    for (size_t i = 0; i < cols.size(); ++i) o << ", G_TYPE_STRING";
    o << ");\n";
    for (const auto& row : rows) {
        o << "    {\n        GtkTreeIter iter;\n"
          << "        gtk_list_store_append(store, &iter);\n";
        for (size_t c = 0; c < cols.size() && c < row.size(); ++c)
            o << "        gtk_list_store_set(store, &iter, " << c << ", \""
              << esc_c(row[c]) << "\", -1);\n";
        o << "    }\n";
    }
    o << "    gtk_tree_view_set_model(GTK_TREE_VIEW(" << M
      << "), GTK_TREE_MODEL(store));\n";
    for (size_t c = 0; c < cols.size(); ++c)
        o << "    gtk_tree_view_append_column(GTK_TREE_VIEW(" << M
          << "), gtk_tree_view_column_new_with_attributes(\""
          << esc_c(cols[c])
          << "\", gtk_cell_renderer_text_new(), \"text\", " << c
          << ", nullptr));\n";
    o << "    g_object_unref(store);\n";
    return o.str();
}

// Build a TreeView's TreeStore from hierarchical "Root/Child;Other" items.
static std::string emit_treeview_setup(const Widget& w, const std::string& M) {
    std::vector<std::string> paths = split_on(
        ';', prop_or(w, "items", "Root/Child 1;Root/Child 2;Other"));
    std::ostringstream o;
    o << "    GtkTreeStore* store = gtk_tree_store_new(1, G_TYPE_STRING);\n";
    std::map<std::string, std::string> iters;  // path -> "&itN"
    int n = 0;
    for (const std::string& path : paths) {
        std::vector<std::string> parts = split_on('/', path);
        std::string prefix, parent = "nullptr";
        for (const std::string& part : parts) {
            if (!prefix.empty()) prefix += "/";
            prefix += part;
            auto found = iters.find(prefix);
            if (found != iters.end()) {
                parent = found->second;
                continue;
            }
            std::string var = "it" + std::to_string(n++);
            o << "    GtkTreeIter " << var << ";\n"
              << "    gtk_tree_store_append(store, &" << var << ", " << parent
              << ");\n"
              << "    gtk_tree_store_set(store, &" << var << ", 0, \""
              << esc_c(part) << "\", -1);\n";
            iters[prefix] = "&" + var;
            parent = "&" + var;
        }
    }
    o << "    gtk_tree_view_set_model(GTK_TREE_VIEW(" << M
      << "), GTK_TREE_MODEL(store));\n"
      << "    gtk_tree_view_append_column(GTK_TREE_VIEW(" << M
      << "), gtk_tree_view_column_new_with_attributes(\"Tree\", "
         "gtk_cell_renderer_text_new(), \"text\", 0, nullptr));\n"
      << "    g_object_unref(store);\n";
    return o.str();
}

// Build a StringGrid's ListStore: N text columns, R rows of "RrCc" cells.
static std::string emit_stringgrid_setup(const Widget& w,
                                         const std::string& M) {
    int rows = std::atoi(prop_or(w, "rows", "5").c_str());
    int cols = std::atoi(prop_or(w, "columns", "4").c_str());
    if (rows < 1) rows = 1;
    if (cols < 1) cols = 1;
    std::ostringstream o;
    o << "    GtkListStore* store = gtk_list_store_new(" << cols;
    for (int c = 0; c < cols; ++c) o << ", G_TYPE_STRING";
    o << ");\n"
      << "    for (int r = 0; r < " << rows << "; ++r) {\n"
      << "        GtkTreeIter iter;\n"
      << "        gtk_list_store_append(store, &iter);\n"
      << "        for (int c = 0; c < " << cols << "; ++c) {\n"
      << "            char cell[32];\n"
      << "            std::snprintf(cell, sizeof cell, \"R%dC%d\", r + 1, c + 1);\n"
      << "            gtk_list_store_set(store, &iter, c, cell, -1);\n"
      << "        }\n"
      << "    }\n"
      << "    gtk_tree_view_set_model(GTK_TREE_VIEW(" << M
      << "), GTK_TREE_MODEL(store));\n";
    for (int c = 0; c < cols; ++c)
        o << "    gtk_tree_view_append_column(GTK_TREE_VIEW(" << M
          << "), gtk_tree_view_column_new_with_attributes(\"Col " << c + 1
          << "\", gtk_cell_renderer_text_new(), \"text\", " << c
          << ", nullptr));\n";
    o << "    g_object_unref(store);\n";
    return o.str();
}

// Draw handlers for the Shape and Plot widgets (member state; the on_draw
// trampoline is a static member, like the StockChart's chart callbacks).
static std::string shape_plot_draw_methods(const Widget& w) {
    std::ostringstream o;
    o << "\ngboolean " << w.name << "::on_draw(GtkWidget* w, cairo_t* cr,\n"
      << "                              gpointer self) {\n"
      << "    auto* me = static_cast<" << w.name << "*>(self);\n"
      << "#ifdef FDESIGNER_EVENT_TRACE\n"
      << "    std::fprintf(stderr, \"EVENT " << w.name << " draw\\n\");\n"
      << "#endif\n"
      << "    GtkAllocation a;\n"
      << "    gtk_widget_get_allocation(w, &a);\n";
    if (w.type == "Shape") {
        o << "    cairo_set_source_rgba(cr, me->fill_.red, me->fill_.green,\n"
          << "                          me->fill_.blue, me->fill_.alpha);\n"
          << "    double x = 6, y = 6, ww = std::max(1.0, a.width - 12.0),\n"
          << "           hh = std::max(1.0, a.height - 12.0);\n"
          << "    switch (me->shape_) {\n"
          << "        case 1:\n"
          << "            cairo_save(cr);\n"
          << "            cairo_translate(cr, x + ww / 2, y + hh / 2);\n"
          << "            cairo_scale(cr, ww / 2, hh / 2);\n"
          << "            cairo_arc(cr, 0, 0, 1, 0, 2 * G_PI);\n"
          << "            cairo_restore(cr);\n"
          << "            cairo_fill(cr);\n"
          << "            break;\n"
          << "        case 2:\n"
          << "            cairo_set_line_width(cr, 3);\n"
          << "            cairo_move_to(cr, x, y + hh);\n"
          << "            cairo_line_to(cr, x + ww, y);\n"
          << "            cairo_stroke(cr);\n"
          << "            break;\n"
          << "        case 3:\n"
          << "            cairo_move_to(cr, x + ww / 2, y);\n"
          << "            cairo_line_to(cr, x + ww, y + hh);\n"
          << "            cairo_line_to(cr, x, y + hh);\n"
          << "            cairo_close_path(cr);\n"
          << "            cairo_fill(cr);\n"
          << "            break;\n"
          << "        default:\n"
          << "            cairo_rectangle(cr, x, y, ww, hh);\n"
          << "            cairo_fill(cr);\n"
          << "            break;\n"
          << "    }\n";
    } else {  // Plot
        o << "    cairo_set_source_rgb(cr, 1, 1, 1);\n"
          << "    cairo_paint(cr);\n"
          << "    cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);\n"
          << "    cairo_select_font_face(cr, \"Sans\", CAIRO_FONT_SLANT_NORMAL,\n"
          << "                           CAIRO_FONT_WEIGHT_BOLD);\n"
          << "    cairo_set_font_size(cr, 12);\n"
          << "    cairo_move_to(cr, 8, 16);\n"
          << "    cairo_show_text(cr, me->title_.c_str());\n"
          << "    cairo_set_source_rgba(cr, me->line_.red, me->line_.green,\n"
          << "                          me->line_.blue, me->line_.alpha);\n"
          << "    cairo_set_line_width(cr, 2);\n"
          << "    double mid = a.height / 2.0 + 8,\n"
          << "           amp = std::max(1.0, a.height / 2.0 - 24);\n"
          << "    for (int px = 0; px < a.width; ++px) {\n"
          << "        double t = (double)px / std::max(1, a.width) * 4 * G_PI;\n"
          << "        double y = mid - std::sin(t) * amp;\n"
          << "        if (px == 0) cairo_move_to(cr, px, y);\n"
          << "        else cairo_line_to(cr, px, y);\n"
          << "    }\n"
          << "    cairo_stroke(cr);\n";
    }
    o << "    return FALSE;\n}\n";
    return o.str();
}

static const StubNames kWidgetStubs{"", "on_press", "on_release"};
static const StubNames kRootStubs{"Root_001_", "on_Root_001_press",
                                  "on_Root_001_release"};

static std::string emit_widget_header(const Widget& w) {
    std::ostringstream o;
    o << "// Generated by FDesigner (C++/GTK) — standalone widget class.\n"
      << "#pragma once\n\n"
      << "#include <gtk/gtk.h>\n";
    if (w.type == "StockChart")
        o << "#include <string>\n#include <vector>\n"
          << "#include <memory>\n\n"
          << "#include \"drawings.h\"\n"
          << "#include \"indicators.h\"\n";
    if (w.type == "Shape" || w.type == "Plot")
        o << "#include <string>\n";
    o << "\n"
      << "class " << w.name << " {\n"
      << "public:\n";
    if (w.type == "Toplevel")
        o << "    " << w.name << "();\n";
    else
        o << "    explicit " << w.name << "(GtkWidget* fixed);\n";
    o << "    ~" << w.name << "();\n"
      << "    GtkWidget* widget() const { return widget_; }\n";
    if (w.type == "StockChart") {
        o << "\n"
          << "    // One OHLC bar (Massive aggregates format).\n"
          << "    struct Candle {\n"
          << "        double open = 0, high = 0, low = 0, close = 0, volume "
             "= 0;\n"
          << "        long long time = 0;  // window_start, ns\n"
          << "    };\n"
          << "    bool load_csv(const std::string& path);\n"
          << "    void set_candles(const std::vector<Candle>& rows);\n"
          << "    int candle_count() const { return (int)candles_.size(); "
             "}\n"
          << "    // Indicator submodules (indicators.h): attach by name,\n"
          << "    // e.g. add_indicator(\"SMA(20)\"). They draw on this\n"
          << "    // chart's canvas; overlays share the price scale, pane\n"
          << "    // indicators get their own strip below the candles.\n"
          << "    bool add_indicator(const std::string& spec);\n"
          << "    void clear_indicators();\n"
          << "    // Value scale: 0 = price, 1 = percent change from "
             "the\n"
          << "    // first visible bar's close (the axis relabels).\n"
          << "    void set_scale_mode(int mode);\n"
          << "    int scale_mode() const { return scale_mode_; }\n"
          << "    // Comparative series: another ticker's CSV, aligned "
             "by\n"
          << "    // timestamp and rebased onto this chart's first "
             "visible\n"
          << "    // close, drawn as a coloured line (see the legend).\n"
          << "    bool add_compare(const std::string& path);\n"
          << "    int compare_count() const { return "
             "(int)compares_.size(); }\n"
          << "    // Annotations (drawings.h): add by spec, e.g.\n"
          << "    // add_drawing(\"trend 10 655.0 200 672.5 red\"). "
             "Anchors\n"
          << "    // are data coordinates, so drawings survive "
             "resizes.\n"
          << "    bool add_drawing(const std::string& spec);\n"
          << "    void clear_drawings();\n"
          << "    int drawing_count() const { return "
             "(int)drawings_.size(); }\n"
          << "    // Mouse drawing tools: 0 none (drag pans), 1 trend, "
             "2 hline,\n"
          << "    // 3 vline, 4 rect, 5 ellipse, 6 arrow, 7 text, 8 "
             "fib,\n"
          << "    // 9 freehand, 10 measure, 11 erase, 12 select/move.\n"
          << "    // Keys T H V R L A X F D M E S pick a tool, Esc "
             "returns\n"
          << "    // to pan, Delete removes the selected drawing. The "
             "mouse\n"
          << "    // wheel zooms around the cursor in every mode.\n"
          << "    void set_draw_tool(int tool);\n"
          << "    int draw_tool() const { return tool_; }\n"
          << "    void zoom_at(double factor, double frac);\n"
          << "    void reset_view();\n"
          << "    size_t visible_begin() const;\n"
          << "    size_t visible_count() const;\n";
    }
    o << "\nprivate:\n"
      << "    GtkWidget* widget_ = nullptr;\n";
    if (w.type == "Shape") {
        o << "    int shape_ = 0;\n"
          << "    GdkRGBA fill_{0.21, 0.51, 0.89, 1.0};\n"
          << "    static gboolean on_draw(GtkWidget*, cairo_t*, gpointer);\n";
    }
    if (w.type == "Plot") {
        o << "    std::string title_;\n"
          << "    GdkRGBA line_{0.86, 0.0, 0.0, 1.0};\n"
          << "    static gboolean on_draw(GtkWidget*, cairo_t*, gpointer);\n";
    }
    if (w.type == "StockChart") {
        o << "    std::vector<Candle> candles_;\n"
          << "    std::vector<std::unique_ptr<fdi::Indicator>> "
             "indicators_;\n"
          << "    GdkRGBA up_{0.0, 0.78, 0.0, 1.0};\n"
          << "    GdkRGBA down_{0.86, 0.0, 0.0, 1.0};\n"
          << "    int max_candles_ = 120;\n"
          << "    struct Compare {\n"
          << "        std::string name;\n"
          << "        std::vector<Candle> rows;\n"
          << "        GdkRGBA color{0, 0, 0, 1};\n"
          << "    };\n"
          << "    std::vector<Compare> compares_;\n"
          << "    std::vector<fdw::Drawing> drawings_;\n"
          << "    std::string ticker_;\n"
          << "    int scale_mode_ = 0;\n"
          << "    int tool_ = 0;\n"
          << "    int selected_ = -1;\n"
          << "    double view_begin_ = -1, view_count_ = 0;\n"
          << "    bool panning_ = false;\n"
          << "    double pan_x_ = 0, pan_begin_ = 0;\n"
          << "    bool moving_ = false;\n"
          << "    double last_bar_ = 0, last_price_ = 0;\n"
          << "    bool sketching_ = false;\n"
          << "    fdw::Drawing sketch_;\n"
          << "    struct Layout {\n"
          << "        double plot_x = 0, plot_y = 0, plot_w = 0;\n"
          << "        double price_h = 0, pane_h = 0;\n"
          << "        double axis_w = 56, axis_h = 18, pad = 4;\n"
          << "        int panes = 0;\n"
          << "        size_t begin = 0, count = 0;\n"
          << "        double lo = 0, hi = 1, base = 0;\n"
          << "    };\n"
          << "    Layout layout(int width, int height) const;\n"
          << "    fdw::Map current_map(const Layout& L) const;\n"
          << "    double compare_close_at(const Compare& c, long long "
             "t) const;\n"
          << "    double compare_base(const Compare& c, size_t begin,\n"
          << "                        size_t count) const;\n"
          << "    static gboolean on_draw(GtkWidget*, cairo_t*, gpointer "
             "self);\n"
          << "    static gboolean on_chart_scroll(GtkWidget*, "
             "GdkEventScroll*, gpointer self);\n"
          << "    static gboolean on_chart_press(GtkWidget*, "
             "GdkEventButton*, gpointer self);\n"
          << "    static gboolean on_chart_motion(GtkWidget*, "
             "GdkEventMotion*, gpointer self);\n"
          << "    static gboolean on_chart_release(GtkWidget*, "
             "GdkEventButton*, gpointer self);\n"
          << "    static gboolean on_chart_key(GtkWidget*, GdkEventKey*, "
             "gpointer self);\n";
    }
    o << "    void util_error_print(const char* what);\n\n"
      << "    // Mouse handlers\n";
    stub_decls(o, kWidgetStubs);
    event_handler_decls(o, w);
    o << "};\n";
    return o.str();
}

// The StockChart implementation, emitted verbatim into every
// generated chart class (%CLASS% -> the widget class name).
static std::string chart_methods(const std::string& cls) {
    std::string s = R"FDCHART7(
// ------------------------------------------------------------------
// StockChart implementation: candles + indicators (indicators.h),
// annotations (drawings.h), comparative series, price/percent scale,
// time axis, wheel zoom and drag pan. Emitted verbatim by the
// generator; %CLASS% is the widget's class name.
// ------------------------------------------------------------------

// Parse a Massive-format aggregates CSV (columns matched by header
// name); also captures the ticker from the first data row.
static bool parse_massive_csv(const std::string& path,
                              std::vector<%CLASS%::Candle>& rows,
                              std::string& ticker) {
    std::ifstream in(path);
    if (!in) return false;
    auto split = [](const std::string& line) {
        std::vector<std::string> fields;
        std::stringstream ss(line);
        std::string field;
        while (std::getline(ss, field, ',')) fields.push_back(field);
        return fields;
    };
    std::string header;
    if (!std::getline(in, header)) return false;
    auto cols = split(header);
    auto index_of = [&](const char* name) {
        for (size_t i = 0; i < cols.size(); ++i)
            if (cols[i] == name) return (int)i;
        return -1;
    };
    int io = index_of("open"), ih = index_of("high"),
        il = index_of("low"), ic = index_of("close"),
        it = index_of("window_start"), iv = index_of("volume"),
        ik = index_of("ticker");
    if (io < 0 || ih < 0 || il < 0 || ic < 0) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        auto fields = split(line);
        int need = std::max(std::max(io, ih), std::max(il, ic));
        if ((int)fields.size() <= need) continue;
        %CLASS%::Candle c;
        c.open = std::atof(fields[io].c_str());
        c.high = std::atof(fields[ih].c_str());
        c.low = std::atof(fields[il].c_str());
        c.close = std::atof(fields[ic].c_str());
        if (iv >= 0 && (int)fields.size() > iv)
            c.volume = std::atof(fields[iv].c_str());
        if (it >= 0 && (int)fields.size() > it)
            c.time = std::atoll(fields[it].c_str());
        if (c.high == 0 && c.low == 0 && c.close == 0) continue;
        if (ticker.empty() && ik >= 0 && (int)fields.size() > ik)
            ticker = fields[ik];
        rows.push_back(c);
    }
    return !rows.empty();
}

// Bottom-axis time labels are UTC (Massive window_start is epoch ns).
static void fmt_axis_time(long long ns, bool with_date, char* buf,
                          size_t n) {
    std::time_t t = (std::time_t)(ns / 1000000000LL);
    std::tm tmv;
    gmtime_r(&t, &tmv);
    std::strftime(buf, n, with_date ? "%m-%d %H:%M" : "%H:%M", &tmv);
}

// Mouse-tool ids (see set_draw_tool) that sketch a drawing: which
// fdw::Kind they create. Tools 2/3 (hline/vline) commit on click,
// 7 (text) asks for words, 11 erases, 12 selects/moves, 0 pans.
static bool tool_sketch_kind(int tool, fdw::Kind& k) {
    switch (tool) {
        case 1: k = fdw::Kind::Trend; return true;
        case 4: k = fdw::Kind::Rect; return true;
        case 5: k = fdw::Kind::Ellipse; return true;
        case 6: k = fdw::Kind::Arrow; return true;
        case 8: k = fdw::Kind::Fib; return true;
        case 9: k = fdw::Kind::Freehand; return true;
        case 10: k = fdw::Kind::Measure; return true;
    }
    return false;
}

bool %CLASS%::load_csv(const std::string& path) {
    std::vector<Candle> rows;
    std::string tick;
    if (!parse_massive_csv(path, rows, tick)) return false;
    candles_ = rows;
    if (!tick.empty()) ticker_ = tick;
    view_begin_ = -1;
    view_count_ = 0;
    if (widget_) gtk_widget_queue_draw(widget_);
    return true;
}

void %CLASS%::set_candles(const std::vector<Candle>& rows) {
    candles_ = rows;
    view_begin_ = -1;
    view_count_ = 0;
    if (widget_) gtk_widget_queue_draw(widget_);
}

bool %CLASS%::add_indicator(const std::string& spec) {
    auto ind = fdi::make_indicator(spec);
    if (!ind) return false;
    indicators_.push_back(std::move(ind));
    if (widget_) gtk_widget_queue_draw(widget_);
    return true;
}

void %CLASS%::clear_indicators() {
    indicators_.clear();
    if (widget_) gtk_widget_queue_draw(widget_);
}

void %CLASS%::set_scale_mode(int mode) {
    scale_mode_ = mode ? 1 : 0;
    if (widget_) gtk_widget_queue_draw(widget_);
}

// Close of a comparison series at-or-before time t (NaN if the series
// has not started yet) — series are aligned by timestamp, not index.
double %CLASS%::compare_close_at(const Compare& c, long long t) const {
    const double kNaN = std::numeric_limits<double>::quiet_NaN();
    if (c.rows.empty() || t < c.rows.front().time) return kNaN;
    size_t lo = 0, hi = c.rows.size();
    while (lo + 1 < hi) {
        size_t mid = (lo + hi) / 2;
        if (c.rows[mid].time <= t)
            lo = mid;
        else
            hi = mid;
    }
    return c.rows[lo].close;
}

// The comparison close that anchors the rebase for a window: the first
// close available inside it.
double %CLASS%::compare_base(const Compare& c, size_t begin,
                             size_t count) const {
    for (size_t i = begin; i < begin + count && i < candles_.size(); ++i) {
        double v = compare_close_at(c, candles_[i].time);
        if (v == v && v != 0) return v;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

bool %CLASS%::add_compare(const std::string& path) {
    std::vector<Candle> rows;
    std::string tick;
    if (!parse_massive_csv(path, rows, tick)) return false;
    Compare c;
    c.name = tick.empty() ? path : tick;
    c.rows = rows;
    static const double kCols[][3] = {{0.90, 0.45, 0.05},
                                      {0.55, 0.20, 0.75},
                                      {0.00, 0.55, 0.60},
                                      {0.85, 0.20, 0.65},
                                      {0.35, 0.35, 0.35}};
    int i = (int)compares_.size() % 5;
    c.color = GdkRGBA{kCols[i][0], kCols[i][1], kCols[i][2], 1.0};
    compares_.push_back(c);
    if (widget_) gtk_widget_queue_draw(widget_);
    return true;
}

bool %CLASS%::add_drawing(const std::string& spec) {
    auto d = fdw::parse_spec(spec);
    if (!d) return false;
    drawings_.push_back(*d);
    if (widget_) gtk_widget_queue_draw(widget_);
    return true;
}

void %CLASS%::clear_drawings() {
    drawings_.clear();
    selected_ = -1;
    if (widget_) gtk_widget_queue_draw(widget_);
}

void %CLASS%::set_draw_tool(int tool) {
    tool_ = tool;
    if (widget_) gtk_widget_queue_draw(widget_);
}

size_t %CLASS%::visible_count() const {
    if (candles_.empty()) return 0;
    if (view_count_ > 0)
        return (size_t)std::min(view_count_, (double)candles_.size());
    return std::min((size_t)std::max(1, max_candles_), candles_.size());
}

size_t %CLASS%::visible_begin() const {
    if (candles_.empty()) return 0;
    if (view_count_ > 0) {
        double vb = view_begin_;
        if (vb < 0) vb = 0;
        if (vb + view_count_ > (double)candles_.size())
            vb = (double)candles_.size() - view_count_;
        return (size_t)vb;
    }
    return candles_.size() - visible_count();
}

// Zoom around a fractional position (0..1) across the plot: the bar
// under the cursor stays under the cursor.
void %CLASS%::zoom_at(double factor, double frac) {
    if (candles_.empty() || factor <= 0) return;
    if (view_count_ <= 0) {
        view_count_ = (double)visible_count();
        view_begin_ = (double)visible_begin();
    }
    frac = std::max(0.0, std::min(1.0, frac));
    double anchor = view_begin_ + frac * view_count_;
    double nc = std::round(std::min(std::max(view_count_ * factor, 5.0),
                                    (double)candles_.size()));
    double nb = std::round(anchor - frac * nc);
    if (nb < 0) nb = 0;
    if (nb + nc > (double)candles_.size())
        nb = (double)candles_.size() - nc;
    view_begin_ = nb;
    view_count_ = nc;
    if (widget_) gtk_widget_queue_draw(widget_);
}

void %CLASS%::reset_view() {
    view_begin_ = -1;
    view_count_ = 0;
    if (widget_) gtk_widget_queue_draw(widget_);
}

%CLASS%::Layout %CLASS%::layout(int width, int height) const {
    Layout L;
    for (auto& ind : indicators_)
        if (ind->kind() == fdi::PlotKind::Pane) ++L.panes;
    L.plot_w = std::max(10.0, width - L.axis_w);
    double total_h = std::max(10.0, height - L.axis_h);
    L.price_h = L.panes ? total_h * 0.66 : total_h;
    L.pane_h = L.panes ? (total_h - L.price_h) / L.panes : 0;
    if (candles_.empty()) return L;
    L.begin = visible_begin();
    L.count = visible_count();
    L.base = candles_[L.begin].close;
    if (L.base == 0) L.base = candles_[L.begin].open;
    L.lo = candles_[L.begin].low;
    L.hi = candles_[L.begin].high;
    for (size_t i = L.begin; i < L.begin + L.count; ++i) {
        L.lo = std::min(L.lo, candles_[i].low);
        L.hi = std::max(L.hi, candles_[i].high);
    }
    // Comparison series extend the range with their rebased values.
    for (const Compare& c : compares_) {
        double c0 = compare_base(c, L.begin, L.count);
        if (c0 != c0 || c0 == 0 || L.base == 0) continue;
        for (size_t i = L.begin; i < L.begin + L.count; ++i) {
            double v = compare_close_at(c, candles_[i].time);
            if (v == v) {
                double eq = L.base * v / c0;
                L.lo = std::min(L.lo, eq);
                L.hi = std::max(L.hi, eq);
            }
        }
    }
    if (L.hi - L.lo < 1e-9) L.hi = L.lo + 1;
    return L;
}

fdw::Map %CLASS%::current_map(const Layout& L) const {
    fdw::Map m;
    m.x = L.plot_x;
    m.y = L.plot_y + L.pad;
    m.w = L.plot_w;
    m.h = L.price_h - 2 * L.pad;
    m.begin = (double)L.begin;
    m.count = (double)L.count;
    m.lo = L.lo;
    m.hi = L.hi;
    return m;
}

gboolean %CLASS%::on_draw(GtkWidget* drawing, cairo_t* cr,
                          gpointer self) {
    auto* me = static_cast<%CLASS%*>(self);
#ifdef FDESIGNER_EVENT_TRACE
    std::fprintf(stderr, "EVENT %CLASS% draw\n");
#endif
    int width = gtk_widget_get_allocated_width(drawing);
    int height = gtk_widget_get_allocated_height(drawing);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    const std::vector<Candle>& all = me->candles_;
    if (all.empty()) {
        cairo_set_source_rgb(cr, 0.4, 0.4, 0.4);
        cairo_move_to(cr, 8, 20);
        cairo_show_text(cr, "StockChart - no data");
        return FALSE;
    }
    Layout L = me->layout(width, height);
    const size_t begin = L.begin, n = L.count;
    const double lo = L.lo, hi = L.hi;
    auto y = [&](double v) {
        return L.plot_y + L.pad +
               (hi - v) / (hi - lo) * (L.price_h - 2 * L.pad);
    };
    double slot = L.plot_w / (double)n;

    // Horizontal gridlines + the value axis: prices, or percent
    // change from the first visible close when scale_mode_ == 1.
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
            cairo_move_to(cr, L.plot_x, gy);
            cairo_line_to(cr, L.plot_x + L.plot_w, gy);
            cairo_stroke(cr);
            char buf[32];
            if (me->scale_mode_ == 1 && L.base > 0)
                std::snprintf(buf, sizeof buf, "%+.1f%%",
                              (v / L.base - 1) * 100);
            else
                std::snprintf(buf, sizeof buf, "%.2f", v);
            cairo_set_source_rgb(cr, 0.35, 0.35, 0.35);
            cairo_move_to(cr, L.plot_x + L.plot_w + 4, gy + 3);
            cairo_show_text(cr, buf);
        }
    }

    // Vertical gridlines + the bottom time axis (UTC; the date is
    // shown on the first tick and whenever the day changes).
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
            double gx = L.plot_x + slot * (double)(i - begin) + slot / 2;
            cairo_set_source_rgba(cr, 0, 0, 0, 0.06);
            cairo_set_line_width(cr, 1);
            cairo_move_to(cr, gx, L.plot_y);
            cairo_line_to(cr, gx, L.plot_y + L.price_h);
            cairo_stroke(cr);
            long long day = all[i].time / 86400000000000LL;
            char buf[24];
            fmt_axis_time(all[i].time, prev_day < 0 || day != prev_day,
                          buf, sizeof buf);
            prev_day = day;
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
        cairo_move_to(cr, L.plot_x, height - L.axis_h + 0.5);
        cairo_line_to(cr, L.plot_x + L.plot_w, height - L.axis_h + 0.5);
        cairo_stroke(cr);
    }

    // Candles.
    double body_w = std::max(1.0, slot * 0.7);
    for (size_t k = 0; k < n; ++k) {
        const Candle& c = all[begin + k];
        const GdkRGBA& col = c.close >= c.open ? me->up_ : me->down_;
        double cx = L.plot_x + slot * (double)k + slot / 2;
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

    // Comparative series: aligned by timestamp, rebased so they start
    // at this chart's first visible close, drawn as lines.
    for (const Compare& c : me->compares_) {
        double c0 = me->compare_base(c, begin, n);
        if (c0 != c0 || c0 == 0 || L.base == 0) continue;
        cairo_set_source_rgba(cr, c.color.red, c.color.green, c.color.blue,
                              1.0);
        cairo_set_line_width(cr, 1.5);
        bool started = false;
        for (size_t k = 0; k < n; ++k) {
            double v = me->compare_close_at(c, all[begin + k].time);
            if (v != v) {
                started = false;
                continue;
            }
            double eq = L.base * v / c0;
            double px = L.plot_x + slot * (double)k + slot / 2;
            if (!started) {
                cairo_move_to(cr, px, y(eq));
                started = true;
            } else {
                cairo_line_to(cr, px, y(eq));
            }
        }
        cairo_stroke(cr);
    }

    // Indicators (overlays share the price scale — in percent mode the
    // scale is affine in price, so the same lo/hi serves both).
    std::vector<fdi::Bar> bars(all.size());
    for (size_t i = 0; i < all.size(); ++i) {
        bars[i].open = all[i].open;
        bars[i].high = all[i].high;
        bars[i].low = all[i].low;
        bars[i].close = all[i].close;
        bars[i].volume = all[i].volume;
        bars[i].time = all[i].time;
    }
    int pane_idx = 0;
    for (auto& ind : me->indicators_) {
        fdi::PlotArea area;
        area.cr = cr;
        area.begin = begin;
        area.count = n;
        if (ind->kind() == fdi::PlotKind::Pane) {
            area.x = L.plot_x;
            area.y = L.price_h + pane_idx * L.pane_h + 10;
            area.w = L.plot_w;
            area.h = L.pane_h - 12;
            cairo_set_source_rgb(cr, 0.75, 0.75, 0.75);
            cairo_set_line_width(cr, 1);
            cairo_move_to(cr, L.plot_x, L.price_h + pane_idx * L.pane_h);
            cairo_line_to(cr, L.plot_x + L.plot_w,
                          L.price_h + pane_idx * L.pane_h);
            cairo_stroke(cr);
            cairo_set_source_rgb(cr, 0.35, 0.35, 0.35);
            cairo_move_to(cr, L.plot_x + 2,
                          L.price_h + pane_idx * L.pane_h + 9);
            cairo_show_text(cr, ind->name());
            ind->draw(area, bars);
            ++pane_idx;
        } else {
            area.x = L.plot_x;
            area.y = L.plot_y + L.pad;
            area.w = L.plot_w;
            area.h = L.price_h - 2 * L.pad;
            area.lo = lo;
            area.hi = hi;
            ind->draw(area, bars);
        }
    }

    // Annotations (data coordinates — resize/zoom safe).
    {
        fdw::Map map = me->current_map(L);
        for (size_t i = 0; i < me->drawings_.size(); ++i)
            fdw::draw(cr, map, me->drawings_[i],
                      (int)i == me->selected_);
        if (me->sketching_) fdw::draw(cr, map, me->sketch_);
    }

    // Legend: main ticker + last value, then comparison names.
    {
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 11);
        std::vector<std::pair<std::string, GdkRGBA>> entries;
        std::string main_name =
            me->ticker_.empty() ? std::string("MAIN") : me->ticker_;
        char lastbuf[48];
        double last = all[begin + n - 1].close;
        if (me->scale_mode_ == 1 && L.base > 0)
            std::snprintf(lastbuf, sizeof lastbuf, " %+.2f%%",
                          (last / L.base - 1) * 100);
        else
            std::snprintf(lastbuf, sizeof lastbuf, " %.2f", last);
        entries.push_back(
            {main_name + lastbuf, GdkRGBA{0.15, 0.15, 0.15, 1.0}});
        for (const Compare& c : me->compares_)
            entries.push_back({c.name, c.color});
        double tw = 0;
        for (auto& e : entries) {
            cairo_text_extents_t te;
            cairo_text_extents(cr, e.first.c_str(), &te);
            tw = std::max(tw, te.width);
        }
        cairo_set_source_rgba(cr, 1, 1, 1, 0.78);
        cairo_rectangle(cr, L.plot_x + 4, L.plot_y + 4, tw + 10,
                        (double)entries.size() * 14 + 6);
        cairo_fill(cr);
        double ly = L.plot_y + 15;
        for (auto& e : entries) {
            cairo_set_source_rgba(cr, e.second.red, e.second.green,
                                  e.second.blue, 1.0);
            cairo_move_to(cr, L.plot_x + 8, ly);
            cairo_show_text(cr, e.first.c_str());
            ly += 14;
        }
    }

    // Active-tool hint, right end of the time-axis strip.
    if (me->tool_ != 0) {
        static const char* kToolNames[] = {
            "", "trend", "hline", "vline", "rect", "ellipse", "arrow",
            "text", "fib", "freehand", "measure", "erase", "select"};
        char buf[64];
        std::snprintf(buf, sizeof buf, "tool: %s  (Esc = pan)",
                      (me->tool_ >= 1 && me->tool_ <= 12)
                          ? kToolNames[me->tool_]
                          : "?");
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 9.5);
        cairo_text_extents_t te;
        cairo_text_extents(cr, buf, &te);
        cairo_set_source_rgb(cr, 0.45, 0.45, 0.45);
        cairo_move_to(cr, std::max(0.0, (double)width - te.width - 4),
                      height - 5);
        cairo_show_text(cr, buf);
    }
    return FALSE;
}

gboolean %CLASS%::on_chart_scroll(GtkWidget* w, GdkEventScroll* ev,
                            gpointer self) {
    auto* me = static_cast<%CLASS%*>(self);
    if (me->candles_.empty()) return FALSE;
    Layout L = me->layout(gtk_widget_get_allocated_width(w),
                          gtk_widget_get_allocated_height(w));
    double frac = L.plot_w > 0 ? (ev->x - L.plot_x) / L.plot_w : 0.5;
    double factor;
    if (ev->direction == GDK_SCROLL_UP)
        factor = 0.8;
    else if (ev->direction == GDK_SCROLL_DOWN)
        factor = 1.25;
    else
        factor = ev->delta_y < 0 ? 0.8 : 1.25;  // smooth scrolling
    me->zoom_at(factor, frac);
    return TRUE;
}

gboolean %CLASS%::on_chart_press(GtkWidget* w, GdkEventButton* ev,
                           gpointer self) {
    auto* me = static_cast<%CLASS%*>(self);
    if (ev->button != 1 || me->candles_.empty()) return FALSE;
    gtk_widget_grab_focus(w);
    Layout L = me->layout(gtk_widget_get_allocated_width(w),
                          gtk_widget_get_allocated_height(w));
    fdw::Map map = me->current_map(L);
    double bar = map.bar_at(ev->x);
    double price = map.price_at(ev->y);
    fdw::Kind sk;
    if (me->tool_ == 0) {
        // No tool: drag pans the view.
        me->panning_ = true;
        me->pan_x_ = ev->x;
        if (me->view_count_ <= 0) {
            me->view_count_ = (double)me->visible_count();
            me->view_begin_ = (double)me->visible_begin();
        }
        me->pan_begin_ = me->view_begin_;
    } else if (me->tool_ == 2 || me->tool_ == 3) {
        fdw::Drawing d;
        d.kind = me->tool_ == 2 ? fdw::Kind::HLine : fdw::Kind::VLine;
        d.pts = {{bar, price}};
        me->drawings_.push_back(d);
        me->selected_ = (int)me->drawings_.size() - 1;
    } else if (me->tool_ == 7) {
        // Text label: ask for the words, then anchor them here.
        GtkWidget* top = gtk_widget_get_toplevel(w);
        GtkWidget* dlg = gtk_dialog_new_with_buttons(
            "Annotation text",
            GTK_IS_WINDOW(top) ? GTK_WINDOW(top) : nullptr,
            GTK_DIALOG_MODAL, "_OK", GTK_RESPONSE_OK, "_Cancel",
            GTK_RESPONSE_CANCEL, nullptr);
        GtkWidget* entry = gtk_entry_new();
        gtk_container_add(
            GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dlg))),
            entry);
        gtk_widget_show_all(dlg);
        if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK) {
            const char* txt = gtk_entry_get_text(GTK_ENTRY(entry));
            if (txt && *txt) {
                fdw::Drawing d;
                d.kind = fdw::Kind::Text;
                d.pts = {{bar, price}};
                d.text = txt;
                me->drawings_.push_back(d);
                me->selected_ = (int)me->drawings_.size() - 1;
            }
        }
        gtk_widget_destroy(dlg);
    } else if (me->tool_ == 11) {
        for (int i = (int)me->drawings_.size() - 1; i >= 0; --i)
            if (fdw::hit_test(map, me->drawings_[i], ev->x, ev->y)) {
                me->drawings_.erase(me->drawings_.begin() + i);
                me->selected_ = -1;
                break;
            }
    } else if (me->tool_ == 12) {
        me->selected_ = -1;
        for (int i = (int)me->drawings_.size() - 1; i >= 0; --i)
            if (fdw::hit_test(map, me->drawings_[i], ev->x, ev->y)) {
                me->selected_ = i;
                break;
            }
        if (me->selected_ >= 0) {
            me->moving_ = true;
            me->last_bar_ = bar;
            me->last_price_ = price;
        }
    } else if (tool_sketch_kind(me->tool_, sk)) {
        me->sketch_ = fdw::Drawing();
        me->sketch_.kind = sk;
        me->sketch_.pts = {{bar, price}};
        me->sketching_ = true;
    }
    gtk_widget_queue_draw(w);
    return TRUE;
}

gboolean %CLASS%::on_chart_motion(GtkWidget* w, GdkEventMotion* ev,
                            gpointer self) {
    auto* me = static_cast<%CLASS%*>(self);
    if (me->candles_.empty()) return FALSE;
    Layout L = me->layout(gtk_widget_get_allocated_width(w),
                          gtk_widget_get_allocated_height(w));
    fdw::Map map = me->current_map(L);
    if (me->panning_) {
        double slot = L.plot_w / std::max(1.0, me->view_count_);
        double nb = std::round(me->pan_begin_ - (ev->x - me->pan_x_) / slot);
        double maxb = (double)me->candles_.size() - me->view_count_;
        if (nb < 0) nb = 0;
        if (nb > maxb) nb = maxb;
        me->view_begin_ = nb;
        gtk_widget_queue_draw(w);
        return TRUE;
    }
    if (me->moving_ && me->selected_ >= 0 &&
        me->selected_ < (int)me->drawings_.size()) {
        double bar = map.bar_at(ev->x), price = map.price_at(ev->y);
        fdw::move(me->drawings_[me->selected_], bar - me->last_bar_,
                  price - me->last_price_);
        me->last_bar_ = bar;
        me->last_price_ = price;
        gtk_widget_queue_draw(w);
        return TRUE;
    }
    if (me->sketching_) {
        fdw::Anchor a{map.bar_at(ev->x), map.price_at(ev->y)};
        if (me->sketch_.kind == fdw::Kind::Freehand) {
            const fdw::Anchor& last = me->sketch_.pts.back();
            double dx = map.px(a.bar) - map.px(last.bar);
            double dy = map.py(a.price) - map.py(last.price);
            if (dx * dx + dy * dy >= 9) me->sketch_.pts.push_back(a);
        } else if (me->sketch_.pts.size() == 1) {
            me->sketch_.pts.push_back(a);
        } else {
            me->sketch_.pts[1] = a;
        }
        gtk_widget_queue_draw(w);
        return TRUE;
    }
    return FALSE;
}

gboolean %CLASS%::on_chart_release(GtkWidget* w, GdkEventButton* ev,
                             gpointer self) {
    auto* me = static_cast<%CLASS%*>(self);
    if (ev->button != 1) return FALSE;
    if (me->panning_) {
        me->panning_ = false;
        return TRUE;
    }
    if (me->moving_) {
        me->moving_ = false;
        return TRUE;
    }
    if (me->sketching_) {
        me->sketching_ = false;
        bool ok = me->sketch_.pts.size() >= 2 &&
                  (me->sketch_.kind == fdw::Kind::Freehand ||
                   me->sketch_.pts[0].bar != me->sketch_.pts[1].bar ||
                   me->sketch_.pts[0].price != me->sketch_.pts[1].price);
        if (ok) {
            me->drawings_.push_back(me->sketch_);
            me->selected_ = (int)me->drawings_.size() - 1;
        }
        gtk_widget_queue_draw(w);
        return TRUE;
    }
    return FALSE;
}

gboolean %CLASS%::on_chart_key(GtkWidget*, GdkEventKey* ev, gpointer self) {
    auto* me = static_cast<%CLASS%*>(self);
    int tool = -1;
    switch (ev->keyval) {
        case GDK_KEY_Escape:
            me->tool_ = 0;
            me->selected_ = -1;
            break;
        case GDK_KEY_t: case GDK_KEY_T: tool = 1; break;
        case GDK_KEY_h: case GDK_KEY_H: tool = 2; break;
        case GDK_KEY_v: case GDK_KEY_V: tool = 3; break;
        case GDK_KEY_r: case GDK_KEY_R: tool = 4; break;
        case GDK_KEY_l: case GDK_KEY_L: tool = 5; break;
        case GDK_KEY_a: case GDK_KEY_A: tool = 6; break;
        case GDK_KEY_x: case GDK_KEY_X: tool = 7; break;
        case GDK_KEY_f: case GDK_KEY_F: tool = 8; break;
        case GDK_KEY_d: case GDK_KEY_D: tool = 9; break;
        case GDK_KEY_m: case GDK_KEY_M: tool = 10; break;
        case GDK_KEY_e: case GDK_KEY_E: tool = 11; break;
        case GDK_KEY_s: case GDK_KEY_S: tool = 12; break;
        case GDK_KEY_Delete:
        case GDK_KEY_KP_Delete:
        case GDK_KEY_BackSpace:
            if (me->selected_ >= 0 &&
                me->selected_ < (int)me->drawings_.size()) {
                me->drawings_.erase(me->drawings_.begin() + me->selected_);
                me->selected_ = -1;
            }
            break;
        default:
            return FALSE;
    }
    if (tool >= 0) me->tool_ = tool;
    if (me->widget_) gtk_widget_queue_draw(me->widget_);
    return TRUE;
}
)FDCHART7";
    std::string out;
    size_t pos = 0;
    while (true) {
        size_t hit = s.find("%CLASS%", pos);
        if (hit == std::string::npos) {
            out += s.substr(pos);
            break;
        }
        out += s.substr(pos, hit - pos);
        out += cls;
        pos = hit + 7;
    }
    return out;
}

static std::string emit_widget_cpp(const Widget& w) {
    const std::string& t = w.type;
    const std::string M = "widget_";
    std::ostringstream o;
    o << "// Generated by FDesigner (C++/GTK) — standalone widget class.\n"
      << "#include \"" << w.name << ".h\"\n\n"
      << "#include <cstdio>\n";
    if (t == "StockChart")
        o << "#include <algorithm>\n#include <cmath>\n#include "
             "<cstdlib>\n#include <ctime>\n#include <fstream>\n"
          << "#include <limits>\n#include <sstream>\n#include "
             "<utility>\n#include <gdk/gdkkeysyms.h>\n";
    if (t == "Shape" || t == "Plot")
        o << "#include <algorithm>\n";
    if (t == "Plot") o << "#include <cmath>\n";
    o << "\n";

    if (t == "Toplevel") {
        std::string title = prop_or(w, "title", "");
        if (title.empty()) title = w.name;
        std::string resizable =
            prop_or(w, "resizable", "true") == "true" ? "TRUE" : "FALSE";
        o << w.name << "::" << w.name << "() {\n"
          << "    // #region " << w.name << "\n"
          << "    widget_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);\n"
          << "    if (!widget_) {\n"
          << "        util_error_print(\"" << w.name
          << " creation error\");\n"
          << "        return;\n"
          << "    }\n"
          << "    gtk_widget_set_name(widget_, \"" << w.name << "\");\n"
          << "    gtk_window_set_title(GTK_WINDOW(widget_), \""
          << esc_c(title) << "\");\n"
          << "    gtk_window_set_default_size(GTK_WINDOW(widget_), "
          << w.width << ", " << w.height << ");\n"
          << "    gtk_window_set_resizable(GTK_WINDOW(widget_), "
          << resizable << ");\n"
          << "    GtkWidget* inner_fixed = gtk_fixed_new();\n"
          << "    gtk_container_add(GTK_CONTAINER(widget_), "
             "inner_fixed);\n"
          << colour_lines(w, M)
          << connect_mouse(w.name, M, kWidgetStubs, 4)
          << event_connect_lines(w, M)
          << "    // #endregion\n"
          << "}\n";
    } else {
        std::string place_target = M;
        std::set<std::string> skip;
        std::string label = prop_or(w, "label", w.name);
        std::string creation;
        if (t == "Button") {
            creation = "    " + M + " = gtk_button_new_with_label(\"" +
                       esc_c(label) + "\");\n";
            skip = {"label"};
        } else if (t == "Label") {
            creation = "    " + M + " = gtk_label_new(\"" + esc_c(label) + "\");\n";
            skip = {"label"};
        } else if (t == "Entry") {
            creation = "    " + M + " = gtk_entry_new();\n";
        } else if (t == "Checkbutton") {
            creation = "    " + M + " = gtk_check_button_new_with_label(\"" +
                       esc_c(label) + "\");\n";
            skip = {"label"};
        } else if (t == "Radiobutton") {
            creation = "    " + M + " = gtk_radio_button_new_with_label(nullptr, \"" +
                       esc_c(label) + "\");\n";
            skip = {"label"};
        } else if (t == "Frame") {
            creation = "    " + M + " = gtk_frame_new(\"" + esc_c(label) + "\");\n";
            skip = {"label"};
        } else if (t == "Combobox") {
            creation = "    " + M + " = gtk_combo_box_text_new();\n";
            skip = {"active"};
        } else if (t == "Menubutton") {
            creation = "    " + M + " = gtk_menu_button_new();\n";
            skip = {"label"};
        } else if (t == "GroupBox") {
            creation = "    " + M + " = gtk_frame_new(\"" +
                       esc_c(label) + "\");\n";
            skip = {"label"};
        } else if (t == "Panel") {
            creation = "    " + M + " = gtk_frame_new(\"" +
                       esc_c(label) + "\");\n";
            skip = {"label"};
        } else if (t == "Image") {
            std::string file = prop_or(w, "file", "");
            creation = "    " + M +
                       " = gtk_image_new_from_icon_name(\"image-missing\", "
                       "GTK_ICON_SIZE_DIALOG);\n";
            if (!file.empty())
                creation +=
                    "    if (g_file_test(\"" + esc_c(file) +
                    "\", G_FILE_TEST_EXISTS))\n"
                    "        gtk_image_set_from_file(GTK_IMAGE(" + M +
                    "), \"" + esc_c(file) + "\");\n";
            skip = {"file"};
        } else if (t == "Notebook") {
            creation = "    " + M + " = gtk_notebook_new();\n";
            skip = {"pages"};
        } else if (t == "StatusBar") {
            creation = "    " + M + " = gtk_statusbar_new();\n";
            skip = {"text"};
        } else if (t == "ToolBar") {
            creation = "    " + M + " = gtk_toolbar_new();\n";
            skip = {"items"};
        } else if (t == "MenuBar") {
            creation = "    " + M + " = gtk_menu_bar_new();\n";
            skip = {"menus"};
        } else if (t == "SpinEdit") {
            creation = "    " + M +
                       " = gtk_spin_button_new_with_range(" +
                       prop_or(w, "minimum", "0") + ", " +
                       prop_or(w, "maximum", "100") + ", " +
                       prop_or(w, "step", "1") + ");\n";
            skip = {"minimum", "maximum", "step"};
        } else if (t == "TrackBar") {
            std::string orient =
                prop_or(w, "orientation", "0") == "1"
                    ? "GTK_ORIENTATION_VERTICAL"
                    : "GTK_ORIENTATION_HORIZONTAL";
            creation = "    " + M + " = gtk_scale_new_with_range(" + orient +
                       ", " + prop_or(w, "minimum", "0") + ", " +
                       prop_or(w, "maximum", "100") + ", " +
                       prop_or(w, "step", "1") + ");\n";
            skip = {"minimum", "maximum", "step"};
        } else if (t == "ProgressBar") {
            creation = "    " + M + " = gtk_progress_bar_new();\n";
        } else if (t == "ColorBox") {
            creation = "    " + M + " = gtk_color_button_new();\n";
            skip = {"color"};
        } else if (t == "Splitter") {
            std::string orient =
                prop_or(w, "orientation", "0") == "1"
                    ? "GTK_ORIENTATION_VERTICAL"
                    : "GTK_ORIENTATION_HORIZONTAL";
            creation = "    " + M + " = gtk_paned_new(" + orient + ");\n"
                       "    gtk_paned_pack1(GTK_PANED(" + M +
                       "), gtk_label_new(\"Pane 1\"), TRUE, FALSE);\n"
                       "    gtk_paned_pack2(GTK_PANED(" + M +
                       "), gtk_label_new(\"Pane 2\"), TRUE, FALSE);\n";
        } else if (t == "Scale") {
            creation = M +
                       " = gtk_scale_new_with_range("
                       "GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);\n";
        } else if (t == "Scrollbar") {
            creation = M +
                       " = gtk_scrollbar_new(GTK_ORIENTATION_HORIZONTAL,\n"
                       "                            gtk_adjustment_new(" +
                       fmt_double(prop_or(w, "value", "0")) +
                       ", 0, 100, 1, 10, 10));\n";
            skip = {"value"};
        } else if (t == "Shape" || t == "Plot") {
            creation =
                "    GtkWidget* frame = gtk_frame_new(nullptr);\n"
                "    gtk_frame_set_shadow_type(GTK_FRAME(frame), "
                "GTK_SHADOW_IN);\n"
                "    " + M + " = gtk_drawing_area_new();\n"
                "    gtk_container_add(GTK_CONTAINER(frame), " + M + ");\n";
            place_target = "frame";
            skip = {"shape", "fill-color", "title", "line-color"};
        } else if (t == "ScrollBox") {
            creation =
                "    GtkWidget* sw = gtk_scrolled_window_new(nullptr, "
                "nullptr);\n"
                "    gtk_scrolled_window_set_shadow_type("
                "GTK_SCROLLED_WINDOW(sw), GTK_SHADOW_IN);\n"
                "    " + M + " = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);\n"
                "    gtk_container_add(GTK_CONTAINER(sw), " + M + ");\n"
                "    GtkWidget* scroll_label = gtk_label_new(\"" +
                esc_c(label) + "\");\n"
                "    gtk_container_add(GTK_CONTAINER(" + M +
                "), scroll_label);\n";
            place_target = "sw";
            skip = {"label"};
        } else if (t == "DateTimePicker") {
            creation =
                "    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, "
                "0);\n"
                "    " + M + " = gtk_calendar_new();\n"
                "    gtk_container_add(GTK_CONTAINER(box), " + M + ");\n"
                "    GtkWidget* date_entry = gtk_entry_new();\n"
                "    gtk_entry_set_text(GTK_ENTRY(date_entry), \"" +
                esc_c(prop_or(w, "date", "")) + "\");\n"
                "    gtk_container_add(GTK_CONTAINER(box), date_entry);\n";
            place_target = "box";
            skip = {"date"};
        } else if (t == "Canvas" || t == "StockChart" || t == "Listbox" ||
                   t == "Text" || t == "Spreadsheet" || t == "Memo" ||
                   t == "ListView" || t == "TreeView" || t == "StringGrid") {
            // Wrapper types: an outer container is placed on the canvas,
            // the member widget lives inside it.
            std::string outer_decl;
            if (t == "Canvas" || t == "StockChart") {
                outer_decl =
                    "    GtkWidget* frame = gtk_frame_new(nullptr);\n"
                    "    gtk_frame_set_shadow_type(GTK_FRAME(frame), "
                    "GTK_SHADOW_IN);\n";
                place_target = "frame";
            } else {
                outer_decl =
                    "    GtkWidget* sw = gtk_scrolled_window_new(nullptr, "
                    "nullptr);\n"
                    "    gtk_scrolled_window_set_shadow_type("
                    "GTK_SCROLLED_WINDOW(sw), GTK_SHADOW_IN);\n";
                place_target = "sw";
            }
            std::string inner;
            if (t == "Canvas") inner = "gtk_drawing_area_new()";
            else if (t == "StockChart") {
                inner = "gtk_drawing_area_new()";
                skip = {"data-file", "up-color", "down-color",
                        "max-candles", "indicators", "scale-mode",
                        "compare-files", "drawings"};
            }
            else if (t == "Listbox") inner = "gtk_list_box_new()";
            else if (t == "Text") inner = "gtk_text_view_new()";
            else if (t == "Spreadsheet") {
                inner = "gtk_grid_new()";
                skip = {"rows", "columns"};
            } else if (t == "Memo") {
                inner = "gtk_text_view_new()";
                skip = {"text"};
            } else if (t == "ListView" || t == "TreeView" ||
                       t == "StringGrid") {
                inner = "gtk_tree_view_new()";
                if (t == "ListView") skip = {"columns", "items"};
                if (t == "TreeView") skip = {"items"};
                if (t == "StringGrid") skip = {"rows", "columns"};
            }
            creation = outer_decl + "    " + M + " = " + inner + ";\n" +
                       "    gtk_container_add(GTK_CONTAINER(" +
                       place_target + "), " + M + ");\n";
        } else {
            creation = "    " + M + " = gtk_label_new(\"" + esc_c(w.name) + "\");\n";
        }

        o << w.name << "::" << w.name << "(GtkWidget* fixed) {\n"
          << "    // #region " << w.name << "\n"
          << creation
          << "    if (!" << M << ") {\n"
          << "        util_error_print(\"" << w.name
          << " creation error\");\n"
          << "        return;\n"
          << "    }\n"
          << "    gtk_widget_set_name(" << M << ", \"" << w.name << "\");\n";

        // Creation-time extras that need the live widget.
        if (t == "Combobox") {
            for (int i = 1; i <= 3; ++i)
                o << "    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT("
                  << M << "), \"Item " << i << "\");\n";
            o << "    gtk_combo_box_set_active(GTK_COMBO_BOX(" << M << "), "
              << prop_or(w, "active", "0") << ");\n";
        } else if (t == "Menubutton") {
            o << "    gtk_button_set_label(GTK_BUTTON(" << M << "), \""
              << esc_c(label) << "\");\n";
        } else if (t == "Listbox") {
            for (int i = 1; i <= 3; ++i)
                o << "    gtk_list_box_insert(GTK_LIST_BOX(" << M
                  << "), gtk_label_new(\"Item " << i << "\"), -1);\n";
        } else if (t == "Text") {
            o << "    gtk_text_buffer_set_text(gtk_text_view_get_buffer("
                 "GTK_TEXT_VIEW("
              << M << ")), \"" << esc_c(w.name) << "\", -1);\n";
        } else if (t == "Spreadsheet") {
            int rows = std::atoi(prop_or(w, "rows", "5").c_str());
            int cols = std::atoi(prop_or(w, "columns", "4").c_str());
            if (rows < 1) rows = 1;
            if (cols < 1) cols = 1;
            o << "    gtk_grid_set_column_homogeneous(GTK_GRID(" << M
              << "), TRUE);\n"
              << "    for (int r = 0; r < " << rows << "; ++r)\n"
              << "        for (int c = 0; c < " << cols << "; ++c) {\n"
              << "            GtkWidget* cell = gtk_entry_new();\n"
              << "            gtk_entry_set_width_chars(GTK_ENTRY(cell), "
                 "8);\n"
              << "            gtk_grid_attach(GTK_GRID(" << M
              << "), cell, c, r, 1, 1);\n";
            if (event_connected(w, "changed"))
                o << "            g_signal_connect(cell, \"changed\", "
                     "G_CALLBACK(&"
                  << w.name << "::on_cell_changed_cb), this);\n";
            o << "        }\n";
        } else if (t == "Memo") {
            o << "    gtk_text_buffer_set_text(gtk_text_view_get_buffer("
                 "GTK_TEXT_VIEW("
              << M << ")), \"" << esc_c(prop_or(w, "text", "")) << "\", -1);\n";
        } else if (t == "Notebook") {
            o << emit_notebook_pages(w, M);
        } else if (t == "StatusBar") {
            o << "    gtk_statusbar_push(GTK_STATUSBAR(" << M
              << "), gtk_statusbar_get_context_id(GTK_STATUSBAR(" << M
              << "), \"design\"), \"" << esc_c(prop_or(w, "text", ""))
              << "\");\n";
        } else if (t == "ToolBar") {
            o << emit_toolbar_items(w, M);
        } else if (t == "MenuBar") {
            o << emit_menubar_items(w, M);
        } else if (t == "ListView") {
            o << emit_listview_setup(w, M);
        } else if (t == "TreeView") {
            o << emit_treeview_setup(w, M);
        } else if (t == "StringGrid") {
            o << emit_stringgrid_setup(w, M);
        } else if (t == "Shape") {
            o << "    shape_ = " << prop_or(w, "shape", "0") << ";\n"
              << "    gdk_rgba_parse(&fill_, \""
              << esc_c(prop_or(w, "fill-color", "rgb(53,132,228)"))
              << "\");\n"
              << "    g_signal_connect(" << M
              << ", \"draw\", G_CALLBACK(&" << w.name << "::on_draw), this);\n";
        } else if (t == "Plot") {
            o << "    title_ = \"" << esc_c(prop_or(w, "title", ""))
              << "\";\n"
              << "    gdk_rgba_parse(&line_, \""
              << esc_c(prop_or(w, "line-color", "rgb(220,0,0)"))
              << "\");\n"
              << "    g_signal_connect(" << M
              << ", \"draw\", G_CALLBACK(&" << w.name << "::on_draw), this);\n";
        } else if (t == "DateTimePicker") {
            int y = 0, m = 0, d = 0;
            if (parse_ymd_gen(prop_or(w, "date", ""), y, m, d))
                o << "    gtk_calendar_select_month(GTK_CALENDAR(" << M
                  << "), " << m - 1 << ", " << y << ");\n"
                  << "    gtk_calendar_select_day(GTK_CALENDAR(" << M
                  << "), " << d << ");\n";
        } else if (t == "ColorBox") {
            o << "    {\n"
              << "        GdkRGBA rgba;\n"
              << "        gdk_rgba_parse(&rgba, \""
              << esc_c(prop_or(w, "color", "rgb(53,132,228)")) << "\");\n"
              << "        gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(" << M
              << "), &rgba);\n"
              << "    }\n";
        } else if (t == "StockChart") {
            o << "    gdk_rgba_parse(&up_, \""
              << esc_c(prop_or(w, "up-color", "rgb(0,200,0)")) << "\");\n"
              << "    gdk_rgba_parse(&down_, \""
              << esc_c(prop_or(w, "down-color", "rgb(220,0,0)")) << "\");\n"
              << "    max_candles_ = "
              << prop_or(w, "max-candles", "120") << ";\n"
              << "    g_signal_connect(" << M
              << ", \"draw\", G_CALLBACK(&" << w.name << "::on_draw), "
                 "this);\n";
            std::string file = prop_or(w, "data-file", "");
            if (!file.empty())
                o << "    load_csv(\"" << esc_c(file) << "\");\n";
            for (const std::string& spec :
                 split_indicator_specs(prop_or(w, "indicators", "")))
                o << "    add_indicator(\"" << esc_c(spec) << "\");\n";
            for (const std::string& cf :
                 split_compare_files(prop_or(w, "compare-files", "")))
                o << "    add_compare(\"" << esc_c(cf) << "\");\n";
            for (const std::string& ds :
                 split_drawings_specs(prop_or(w, "drawings", "")))
                o << "    add_drawing(\"" << esc_c(ds) << "\");\n";
            int scale_mode =
                std::atoi(prop_or(w, "scale-mode", "0").c_str());
            if (scale_mode)
                o << "    set_scale_mode(" << scale_mode << ");\n";
            o << "    gtk_widget_add_events(" << M
              << ", GDK_SCROLL_MASK | GDK_BUTTON_PRESS_MASK |\n"
              << "                            GDK_BUTTON_RELEASE_MASK | "
                 "GDK_POINTER_MOTION_MASK |\n"
              << "                            GDK_KEY_PRESS_MASK);\n"
              << "    gtk_widget_set_can_focus(" << M << ", TRUE);\n"
              << "    g_signal_connect(" << M
              << ", \"scroll-event\", G_CALLBACK(&" << w.name
              << "::on_chart_scroll), this);\n"
              << "    g_signal_connect(" << M
              << ", \"button-press-event\", G_CALLBACK(&" << w.name
              << "::on_chart_press), this);\n"
              << "    g_signal_connect(" << M
              << ", \"motion-notify-event\", G_CALLBACK(&" << w.name
              << "::on_chart_motion), this);\n"
              << "    g_signal_connect(" << M
              << ", \"button-release-event\", G_CALLBACK(&" << w.name
              << "::on_chart_release), this);\n"
              << "    g_signal_connect(" << M
              << ", \"key-press-event\", G_CALLBACK(&" << w.name
              << "::on_chart_key), this);\n";
        }

        o << "    gtk_fixed_put(GTK_FIXED(fixed), " << place_target << ", "
          << w.x << ", " << w.y << ");\n"
          << "    gtk_widget_set_size_request(" << place_target << ", "
          << w.width << ", " << w.height << ");\n"
          << prop_lines(w, M, skip)
          << colour_lines(w, M)
          << connect_mouse(w.name, M, kWidgetStubs, 4)
          << event_connect_lines(w, M)
          << "    // #endregion\n"
          << "}\n";
    }

    if (w.type == "StockChart") {
        o << chart_methods(w.name);
    }
    if (w.type == "Shape" || w.type == "Plot") {
        o << shape_plot_draw_methods(w);
    }

    o << "\n"
      << w.name << "::~" << w.name << "() {}\n\n"
      << "void " << w.name << "::util_error_print(const char* what) {\n"
      << "    std::fprintf(stderr, \"%s\\n\", what);\n"
      << "}\n";
    stub_defs(o, w.name, kWidgetStubs);
    event_handler_defs(o, w);
    return o.str();
}

// ------------------------------------------------------- window class

static std::string emit_window_header(const std::string& cls,
                                      const std::vector<Widget>& widgets,
                                      const std::vector<MenuDef>& menus) {
    std::ostringstream o;
    o << "// Generated by FDesigner (C++/GTK) — all code, no .ui file.\n"
      << "// Every widget is a standalone class; this class instantiates\n"
      << "// them. Construction mirrors the Python fdesigner generator.\n"
      << "#pragma once\n\n"
      << "#include <gtk/gtk.h>\n"
      << "#include <string>\n\n";
    for (const Widget& w : widgets)
        o << "#include \"" << w.name << ".h\"\n";
    if (!widgets.empty()) o << "\n";
    o << "class " << cls << " {\n"
      << "public:\n"
      << "    " << cls << "();\n"
      << "    ~" << cls << "();\n"
      << "    void show();\n"
      << "    GtkWidget* window() const { return window_; }\n\n"
      << "private:\n"
      << "    GtkWidget* window_ = nullptr;\n"
      << "    GtkWidget* fixed_ = nullptr;\n";
    if (!menus.empty()) o << "    GtkWidget* menubar_ = nullptr;\n";
    for (const Widget& w : widgets)
        o << "    " << w.name << "* " << w.name << "_ = nullptr;\n";
    for (const MenuDef& m : menus)
        for (const std::string& item : m.items)
            if (item != "-")
                o << "    GtkWidget* " << menu_item_id(m, item)
                  << "_ = nullptr;\n";
    o << "\n    void util_error_print(const char* what);\n";
    o << "\n    // Mouse handlers — Root_001 (the main window)\n";
    stub_decls(o, kRootStubs);
    if (!menus.empty()) o << "\n    // Menu handlers\n";
    for (const MenuDef& m : menus)
        for (const std::string& item : m.items)
            if (item != "-")
                o << "    static void on_" << menu_item_id(m, item)
                  << "_activate(GtkMenuItem*, gpointer self);\n";
    o << "};\n";
    return o.str();
}

static std::string emit_menubar_code(const std::string& cls,
                                     const std::vector<MenuDef>& menus) {
    std::ostringstream o;
    o << "\n    // #region menu bar\n"
      << "    menubar_ = gtk_menu_bar_new();\n";
    for (const MenuDef& m : menus) {
        std::string top = "menu_" + menu_id_part(m.label);
        o << "    {\n"
          << "        GtkWidget* " << top
          << " = gtk_menu_item_new_with_label(\"" << esc_c(m.label)
          << "\");\n"
          << "        GtkWidget* " << top << "_submenu = gtk_menu_new();\n";
        for (const std::string& item : m.items) {
            if (item == "-") {
                o << "        gtk_menu_shell_append(GTK_MENU_SHELL(" << top
                  << "_submenu), gtk_separator_menu_item_new());\n";
            } else {
                std::string id = menu_item_id(m, item);
                o << "        " << id
                  << "_ = gtk_menu_item_new_with_label(\"" << esc_c(item)
                  << "\");\n"
                  << "        g_signal_connect(" << id
                  << "_, \"activate\", G_CALLBACK(&" << cls << "::on_" << id
                  << "_activate), this);\n"
                  << "        gtk_menu_shell_append(GTK_MENU_SHELL(" << top
                  << "_submenu), " << id << "_);\n";
            }
        }
        o << "        gtk_menu_item_set_submenu(GTK_MENU_ITEM(" << top
          << "), " << top << "_submenu);\n"
          << "        gtk_menu_shell_append(GTK_MENU_SHELL(menubar_), "
          << top << ");\n"
          << "    }\n";
    }
    o << "    gtk_box_pack_start(GTK_BOX(vbox), menubar_, FALSE, FALSE, 0);\n"
      << "    gtk_box_reorder_child(GTK_BOX(vbox), menubar_, 0);\n"
      << "    // #endregion\n";
    return o.str();
}

static std::string emit_window_cpp(const std::string& cls, const Project& p,
                                   const std::vector<Widget>& widgets,
                                   const std::vector<MenuDef>& menus) {
    std::ostringstream o;
    o << "// Generated by FDesigner (C++/GTK) — all code, no .ui file.\n"
      << "#include \"" << cls << ".h\"\n\n"
      << "#include <cstdio>\n\n"
      << cls << "::" << cls << "() {\n"
      << "    // #region root setup\n"
      << "    window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);\n"
      << "    gtk_widget_set_name(window_, \"main_window\");\n"
      << "    gtk_window_set_title(GTK_WINDOW(window_), \""
      << esc_c(p.title) << "\");\n"
      << "    gtk_window_set_default_size(GTK_WINDOW(window_), "
      << p.win_width << ", " << p.win_height << ");\n";
    if (!menus.empty())
        o << "    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, "
             "0);\n"
          << "    gtk_container_add(GTK_CONTAINER(window_), vbox);\n";
    o << "    fixed_ = gtk_fixed_new();\n";
    if (!menus.empty())
        o << "    gtk_box_pack_start(GTK_BOX(vbox), fixed_, TRUE, TRUE, 0);\n";
    else
        o << "    gtk_container_add(GTK_CONTAINER(window_), fixed_);\n";
    o << connect_mouse(cls, "window_", kRootStubs, 4)
      << "    g_signal_connect(window_, \"destroy\", "
         "G_CALLBACK(gtk_main_quit), nullptr);\n"
      << "    // #endregion\n";
    if (!menus.empty()) o << emit_menubar_code(cls, menus);
    for (const Widget& w : widgets) {
        o << "\n    // #region " << w.name << "\n"
          << "    " << w.name << "_ = new " << w.name << "(";
        if (w.type != "Toplevel") o << "fixed_";
        o << ");\n"
          << "    // #endregion\n";
    }
    if (!p.background.empty()) {
        o << "\n    // #region colours\n"
          << "    GtkCssProvider* css = gtk_css_provider_new();\n"
          << "    gtk_css_provider_load_from_data(css,\n"
          << "        \"#main_window { background: " << p.background
          << "; }\\n\"\n"
          << "        , -1, nullptr);\n"
          << "    gtk_style_context_add_provider_for_screen(\n"
          << "        gdk_screen_get_default(), GTK_STYLE_PROVIDER(css),\n"
          << "        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);\n"
          << "    // #endregion\n";
    }
    o << "}\n\n"
      << cls << "::~" << cls << "() {\n";
    for (auto it = widgets.rbegin(); it != widgets.rend(); ++it)
        o << "    delete " << it->name << "_;\n";
    o << "}\n\n"
      << "void " << cls << "::show() {\n"
      << "    if (window_) gtk_widget_show_all(window_);\n"
      << "}\n\n"
      << "void " << cls << "::util_error_print(const char* what) {\n"
      << "    std::fprintf(stderr, \"%s\\n\", what);\n"
      << "}\n";
    stub_defs(o, cls, kRootStubs);
    for (const MenuDef& m : menus)
        for (const std::string& item : m.items)
            if (item != "-")
                o << "\nvoid " << cls << "::on_" << menu_item_id(m, item)
                  << "_activate(GtkMenuItem*, gpointer self) {\n"
                  << "    auto* me = static_cast<" << cls << "*>(self);\n"
                  << "    (void)me;\n"
                  << "    // Code block here\n"
                  << "}\n";
    return o.str();
}

static std::string emit_main(const std::string& cls) {
    std::ostringstream o;
    o << "// Generated by FDesigner (C++/GTK).\n"
      << "#include <gtk/gtk.h>\n\n"
      << "#include \"" << cls << ".h\"\n\n"
      << "int main(int argc, char** argv) {\n"
      << "    gtk_init(&argc, &argv);\n"
      << "    " << cls << " win;  // a = my_class()\n"
      << "    win.show();\n"
      << "    gtk_main();  // root.mainloop()\n"
      << "    return 0;\n"
      << "}\n";
    return o.str();
}

// ---- CMake (cmake_build.h) ----
// CMakeLists.txt for the generated project: the same sources the Makefile
// builds, as an explicit list, with the executable named `app` to match.
static std::string emit_cmake_lists(
    const std::string& cls, const std::vector<Widget>& widgets) {
    std::vector<std::string> sources;
    sources.push_back("main.cpp");
    sources.push_back(cls + ".cpp");
    for (const Widget& w : widgets) sources.push_back(w.name + ".cpp");
    return cmake_lists_text(cls, sources);
}

static std::string emit_makefile(const std::string& cls) {
    std::ostringstream o;
    o << "# Generated by FDesigner (C++/GTK). One class per widget:\n"
      << "# SRCS picks up every .cpp, so hand-added classes build too.\n"
      << "CXX ?= g++\n"
      << "CXXFLAGS ?= -O2 -Wall -std=c++17\n"
      << "SRCS := $(wildcard *.cpp)\n\n"
      << "app: $(SRCS) " << cls << ".h\n"
      << "\t$(CXX) $(CXXFLAGS) -o $@ $(SRCS) $$(pkg-config --cflags "
         "--libs gtk+-3.0)\n\n"
      << "clean:\n"
      << "\trm -f app\n\n"
      << ".PHONY: clean\n";
    return o.str();
}

// ---------------------------------------------------------------- driver

static bool write_file(const fs::path& path, const std::string& content,
                       GenResult& r) {
    std::ofstream f(path);
    if (!f) {
        r.error = "cannot write " + path.string();
        return false;
    }
    f << content;
    if (!f) {
        r.error = "write failed for " + path.string();
        return false;
    }
    r.files.push_back(path.string());
    return true;
}

GenResult generate_project(const Project& project,
                           const std::vector<Widget>& widgets,
                           const std::string& out_dir) {
    GenResult r;
    std::error_code ec;
    fs::create_directories(out_dir, ec);
    if (ec) {
        r.error = "cannot create " + out_dir + ": " + ec.message();
        return r;
    }
    std::string cls = sanitize_class(project.class_name);
    std::vector<MenuDef> menus = parse_menus(project.menubar);

    fs::path dir(out_dir);
    if (!write_file(dir / (cls + ".h"),
                    emit_window_header(cls, widgets, menus), r))
        return r;
    if (!write_file(dir / (cls + ".cpp"),
                    emit_window_cpp(cls, project, widgets, menus), r))
        return r;
    for (const Widget& w : widgets) {
        if (!write_file(dir / (w.name + ".h"), emit_widget_header(w), r))
            return r;
        if (!write_file(dir / (w.name + ".cpp"), emit_widget_cpp(w), r))
            return r;
    }
    bool has_chart = false;
    for (const Widget& w : widgets)
        if (w.type == "StockChart") has_chart = true;
    if (has_chart &&
        !write_file(dir / "indicators.h", kIndicatorsHeader, r))
        return r;
    if (has_chart &&
        !write_file(dir / "drawings.h", kDrawingsHeader, r))
        return r;
    if (!write_file(dir / "main.cpp", emit_main(cls), r)) return r;
    if (!write_file(dir / "Makefile", emit_makefile(cls), r)) return r;
    if (!write_file(dir / "CMakeLists.txt", emit_cmake_lists(cls, widgets),
                    r))
        return r;
    r.ok = true;
    return r;
}

// Event handler tests: one project holding every widget type with every
// natural signal connected; generate it and check per-widget markers in
// the emitted code — a handler declaration in <Name>.h, a g_signal_connect
// for the signal in <Name>.cpp, and the stub definition (with its EVENT
// trace line). Types with no natural signal must emit no event stubs.
// The companion shell script (tests/event_handler_tests.sh) compiles the
// generated app and runs the runtime harness, which triggers each signal
// for real. Usage: event_handler_tests [out_dir]
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../src/events.h"
#include "../src/generator.h"
#include "../src/model.h"
#include "../src/props.h"

static const char* kEventTypes[] = {
    "Button", "Label", "Entry", "Checkbutton", "Radiobutton", "Frame",
    "Combobox", "Listbox", "Menubutton", "Canvas", "Scale", "Scrollbar",
    "Text", "Toplevel", "Spreadsheet", "StockChart",
    "Memo", "GroupBox", "Panel", "Image", "Shape", "Notebook",
    "ScrollBox", "Splitter", "StatusBar", "ToolBar", "MenuBar",
    "SpinEdit", "TrackBar", "ProgressBar", "DateTimePicker", "ColorBox",
    "ListView", "TreeView", "StringGrid", "Plot",
};

static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

// Count non-overlapping occurrences of needle in haystack.
static int count_of(const std::string& hay, const std::string& needle) {
    int n = 0;
    size_t pos = 0;
    while ((pos = hay.find(needle, pos)) != std::string::npos) {
        ++n;
        pos += needle.size();
    }
    return n;
}

// First item id of a per-item event ("New" -> "new"), "" if not per-item.
static std::string first_item_id(const Widget& w, const EventDef& d) {
    if (d.kind != EventKind::ItemClicked &&
        d.kind != EventKind::ItemActivate)
        return "";
    std::string spec = w.type == "ToolBar"
                           ? "New|Open|Save"
                           : "File|Edit|Help";
    auto bar = spec.find('|');
    std::string first = spec.substr(0, bar);
    std::string out;
    for (unsigned char c : first) {
        if (std::isalnum(c))
            out += (char)std::tolower(c);
        else if (!out.empty() && out.back() != '_')
            out += '_';
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out.empty() ? "item" : out;
}

int main(int argc, char** argv) {
    std::string out = argc > 1 ? argv[1] : "/tmp/fdesigner_events_gen";
    Project p;
    p.title = "Event Test";
    p.class_name = "MyWindow";
    int i = 0;
    for (const char* type : kEventTypes) {
        Widget w;
        w.name = std::string(type) + "_001";
        w.type = type;
        const TypeInfo* t = find_type(type);
        w.width = t ? t->def_width : 100;
        w.height = t ? t->def_height : 30;
        w.x = (i % 6) * 140;
        w.y = (i / 6) * 110;
        if (w.type == "ToolBar") w.props["items"] = "New|Open|Save";
        if (w.type == "MenuBar") w.props["menus"] = "File|Edit|Help";
        if (w.type == "DateTimePicker") w.props["date"] = "2026-11-25";
        // Connect every natural signal (builtin draw included: the
        // generator must wire it exactly once, not twice).
        for (const EventDef& d : events_for(w.type))
            w.signals.push_back(d.signal);
        p.widgets.push_back(w);
        ++i;
    }

    GenResult gr = generate_project(p, p.widgets, out);
    if (!gr.ok) {
        std::printf("FAIL generate: %s\n", gr.error.c_str());
        return 1;
    }

    int pass = 0, fail = 0;
    for (const Widget& w : p.widgets) {
        std::string cpp = read_file(out + "/" + w.name + ".cpp");
        std::string hdr = read_file(out + "/" + w.name + ".h");
        std::string why;
        if (cpp.empty()) why = "missing " + w.name + ".cpp";
        if (hdr.empty()) why = "missing " + w.name + ".h";
        const std::vector<EventDef>& evs = events_for(w.type);
        if (why.empty() && evs.empty()) {
            // No natural signal: no event stubs at all.
            if (cpp.find("EVENT ") != std::string::npos)
                why = "unexpected event stub in " + w.name + ".cpp";
        }
        for (const EventDef& d : evs) {
            if (!why.empty()) break;
            std::string handler = d.handler + first_item_id(w, d);
            if (d.kind == EventKind::ItemClicked ||
                d.kind == EventKind::ItemActivate)
                handler = std::string(d.handler) + "_" + first_item_id(w, d);
            std::string sig = "\"" + std::string(d.signal) + "\"";
            if (hdr.find(handler) == std::string::npos) {
                why = "no decl " + handler + " in " + w.name + ".h";
            } else if (cpp.find(sig) == std::string::npos) {
                why = "no signal " + sig + " in " + w.name + ".cpp";
            } else if (cpp.find("g_signal_connect") == std::string::npos) {
                why = "no g_signal_connect in " + w.name + ".cpp";
            } else if (cpp.find(w.name + "::" + handler) ==
                       std::string::npos) {
                why = "no stub " + w.name + "::" + handler;
            } else if (cpp.find("EVENT " + w.name + " " + d.signal) ==
                       std::string::npos) {
                why = std::string("no EVENT trace for ") + d.signal;
            }
            if (d.builtin && why.empty()) {
                // Builtin draw must be wired exactly once.
                int n = count_of(cpp, "g_signal_connect");
                int draws = count_of(cpp, "\"draw\"");
                if (draws != 1)
                    why = "draw connected " + std::to_string(draws) +
                          " times (want 1)";
                (void)n;
            }
        }
        if (why.empty()) {
            std::printf("PASS %-16s %-18s (%zu event%s)\n", w.type.c_str(),
                        w.name.c_str(), evs.size(),
                        evs.size() == 1 ? "" : "s");
            ++pass;
        } else {
            std::printf("FAIL %-16s %-18s: %s\n", w.type.c_str(),
                        w.name.c_str(), why.c_str());
            ++fail;
        }
    }
    std::printf("== event_handler_tests: %d passed, %d failed ==\n", pass,
                fail);
    return fail ? 1 : 0;
}

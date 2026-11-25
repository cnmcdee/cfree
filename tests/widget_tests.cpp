// Widget tests: one project holding every Lazarus-style widget with
// non-default properties; generate it, check per-widget markers in the
// emitted code, and report PASS/FAIL per widget. The companion shell
// script (tests/widget_tests.sh) compiles the generated app and runs it.
// Usage: widget_tests [out_dir]
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../src/generator.h"
#include "../src/project_io.h"
#include "../src/props.h"

struct Spec {
    const char* type;
    const char* prop;
    const char* value;
    const char* marker;  // must appear in <Name>.cpp
};

static const Spec kSpecs[] = {
    {"Memo", "text", "Hello memo", "gtk_text_buffer_set_text"},
    {"GroupBox", "label", "Options", "gtk_frame_new"},
    {"Panel", "label", "Panel A", "gtk_frame_new"},
    {"Image", "file", "chart_pro_1024x768.png", "gtk_image_set_from_file"},
    {"Shape", "shape", "1", "cairo_arc"},
    {"Notebook", "pages", "One|Two|Three", "gtk_notebook_append_page"},
    {"ScrollBox", "label", "Scroll me", "gtk_scrolled_window_new"},
    {"Splitter", "position", "150", "gtk_paned_new"},
    {"StatusBar", "text", "All good", "gtk_statusbar_push"},
    {"ToolBar", "items", "Cut|Copy|Paste", "gtk_tool_button_new"},
    {"MenuBar", "menus", "File|Tools", "gtk_menu_item_new_with_label"},
    {"SpinEdit", "value", "42", "gtk_spin_button_new_with_range"},
    {"TrackBar", "value", "70", "gtk_scale_new_with_range"},
    {"ProgressBar", "fraction", "0.5", "gtk_progress_bar_new"},
    {"DateTimePicker", "date", "2026-11-25", "gtk_calendar_select_day"},
    {"ColorBox", "color", "#00ff00", "gtk_color_chooser_set_rgba"},
    {"ListView", "items", "x,1;y,2", "gtk_list_store_new"},
    {"TreeView", "items", "Root/Leaf;Other", "gtk_tree_store_new"},
    {"StringGrid", "rows", "3", "gtk_list_store_new"},
    {"Plot", "title", "Sine", "sin("},
};

static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
    std::string out = argc > 1 ? argv[1] : "/tmp/fdesigner_widgets_gen";
    Project p;
    p.title = "Widget Test";
    p.class_name = "MyWindow";
    int i = 0;
    for (const Spec& s : kSpecs) {
        Widget w;
        w.name = std::string(s.type) + "_001";
        w.type = s.type;
        w.x = 10 + (i % 4) * 210;
        w.y = 10 + (i / 4) * 150;
        const TypeInfo* t = find_type(s.type);
        w.width = t ? t->def_width : 100;
        w.height = t ? t->def_height : 30;
        for (const PropDef& d : props_for(s.type))
            if (d.def && d.def[0]) w.props[d.name] = d.def;
        for (const PropDef& d : common_props())
            if (d.def && d.def[0]) w.props[d.name] = d.def;
        if (find_prop(s.type, "label")) w.props["label"] = w.name;
        w.props[s.prop] = s.value;
        p.widgets.push_back(w);
        ++i;
    }
    GenResult r = generate_project(p, p.widgets, out);
    if (!r.ok) {
        std::printf("FAIL generate: %s\n", r.error.c_str());
        return 1;
    }
    int pass = 0, fail = 0;
    for (const Spec& s : kSpecs) {
        std::string name = std::string(s.type) + "_001";
        std::string code = read_file(out + "/" + name + ".cpp");
        bool ok = !code.empty() &&
                  code.find(s.marker) != std::string::npos;
        std::printf("%s %-16s marker '%s' in %s.cpp\n",
                    ok ? "PASS" : "FAIL", s.type, s.marker, name.c_str());
        if (ok) ++pass; else ++fail;
    }
    // Every widget class file must exist, plus window + main + Makefile.
    for (const Spec& s : kSpecs) {
        std::string h = out + "/" + std::string(s.type) + "_001.h";
        if (!std::filesystem::exists(h)) {
            std::printf("FAIL %-16s missing %s\n", s.type, h.c_str());
            ++fail;
        }
    }
    // Save/load round-trip (.fdj) for every new widget type.
    {
        std::string err;
        if (!save_project_file("/tmp/fdesigner_widgets.fdj", p, err)) {
            std::printf("FAIL save %s\n", err.c_str());
            ++fail;
        } else {
            Project q;
            if (!load_project_file("/tmp/fdesigner_widgets.fdj", q, err)) {
                std::printf("FAIL load %s\n", err.c_str());
                ++fail;
            } else if (q.widgets.size() != p.widgets.size()) {
                std::printf("FAIL round-trip count %zu != %zu\n",
                            q.widgets.size(), p.widgets.size());
                ++fail;
            } else {
                for (size_t k = 0; k < q.widgets.size(); ++k) {
                    bool ok = q.widgets[k].type == p.widgets[k].type &&
                              q.widgets[k].name == p.widgets[k].name &&
                              q.widgets[k].props == p.widgets[k].props;
                    std::printf("%s %-16s .fdj round-trip\n",
                                ok ? "PASS" : "FAIL",
                                q.widgets[k].type.c_str());
                    if (ok) ++pass; else ++fail;
                }
            }
        }
    }

    std::printf("== widget_tests: %d passed, %d failed ==\n", pass, fail);
    return fail ? 1 : 0;
}

// Extensive generator test matrix: builds many designs through the real
// generator API and writes each as a project under tests/out/<case>/.
// run_matrix.sh then compiles every project (with -Wextra) and runs it.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../src/generator.h"
#include "../src/props.h"

static int g_case = 0;
static std::string g_base;

static Widget mk(const std::string& type, const std::string& name, int x,
                 int y) {
    Widget w;
    w.name = name;
    w.type = type;
    w.x = x;
    w.y = y;
    const TypeInfo* t = find_type(type);
    w.width = t ? t->def_width : 100;
    w.height = t ? t->def_height : 30;
    for (const PropDef& d : props_for(type))
        if (d.def && d.def[0]) w.props[d.name] = d.def;
    for (const PropDef& d : common_props())
        if (d.def && d.def[0]) w.props[d.name] = d.def;
    if (find_prop(type, "label")) w.props["label"] = name;
    return w;
}

static void emit(const std::string& case_name, Project p) {
    std::string dir = g_base + "/" + case_name;
    GenResult r = generate_project(p, p.widgets, dir);
    std::printf("case %-28s %s (%zu files)\n", case_name.c_str(),
                r.ok ? "generated" : ("FAILED: " + r.error).c_str(),
                r.files.size());
    if (!r.ok) ++g_case;  // count failures in exit code via marker
}

int main(int argc, char** argv) {
    g_base = argc > 1 ? argv[1] : "tests/out";

    // 1. Empty design.
    emit("01_empty", Project());

    // 2. One of every widget type, default properties.
    {
        Project p;
        int i = 0;
        for (const TypeInfo& t : kTypes) {
            std::string name = std::string(t.key) + "_001";
            p.widgets.push_back(mk(t.key, name, 10 + (i % 4) * 190,
                                   10 + (i / 4) * 130));
            ++i;
        }
        emit("02_all_types", p);
    }

    // 3. Every property of every type set to a non-default test value.
    {
        Project p;
        int i = 0;
        for (const TypeInfo& t : kTypes) {
            Widget w = mk(t.key, std::string(t.key) + "_001",
                          10 + (i % 4) * 190, 10 + (i / 4) * 130);
            std::vector<const PropDef*> defs;
            for (const PropDef& d : props_for(t.key)) defs.push_back(&d);
            for (const PropDef& d : common_props()) defs.push_back(&d);
            for (const PropDef* d : defs) {
                switch (d->kind) {
                    case PropKind::Str:
                        w.props[d->name] = "Test value";
                        break;
                    case PropKind::Bool:
                        w.props[d->name] =
                            std::string(d->def) == "true" ? "false" : "true";
                        break;
                    case PropKind::Int:
                        w.props[d->name] = d->def;
                        break;
                    case PropKind::Double: {
                        // 42.5, clamped into the property's valid range
                        // (the designer's set_prop rejects out-of-range).
                        double v = 42.5;
                        if (d->hi > d->lo) {
                            if (v < d->lo) v = d->lo;
                            if (v > d->hi) v = d->hi;
                        }
                        char buf[64];
                        std::snprintf(buf, sizeof buf, "%g", v);
                        w.props[d->name] = buf;
                        break;
                    }
                    case PropKind::Enum: {
                        int last = 0;
                        for (int k = 0; d->enum_values && d->enum_values[k];
                             ++k)
                            last = d->enum_values[k];
                        w.props[d->name] = std::to_string(last);
                        break;
                    }
                    case PropKind::Color:
                        w.props[d->name] = "#336699";
                        break;
                }
            }
            if (find_prop(t.key, "label")) w.props["label"] = "Test value";
            w.props["background"] = "#336699";
            w.props["foreground"] = "white";
            w.props["tooltip-text"] = "tip with \"quotes\" and \\ slash";
            p.widgets.push_back(w);
            ++i;
        }
        emit("03_all_props", p);
    }

    // 4. Every enum property at every value.
    {
        Project p;
        int x = 10;
        auto add = [&](const std::string& type, const std::string& name,
                       const std::string& prop, const std::string& val) {
            Widget w = mk(type, name, x, 10);
            w.props[prop] = val;
            p.widgets.push_back(w);
            x += 130;
        };
        for (int v = 0; v <= 2; ++v)
            add("Button", "Btn_relief_" + std::to_string(v), "relief",
                std::to_string(v));
        for (int v = 0; v <= 4; ++v)
            add("Frame", "Frm_shadow_" + std::to_string(v), "shadow-type",
                std::to_string(v));
        for (int v = 0; v <= 3; ++v)
            add("Listbox", "Lst_sel_" + std::to_string(v), "selection-mode",
                std::to_string(v));
        for (int v = 0; v <= 4; ++v)
            add("Menubutton", "Mnu_dir_" + std::to_string(v), "direction",
                std::to_string(v));
        for (int v = 0; v <= 3; ++v)
            add("Text", "Txt_wrap_" + std::to_string(v), "wrap-mode",
                std::to_string(v));
        emit("04_enum_values", p);
    }

    // 5. Menus: several menus, separators, spaces and symbols in labels,
    //    plus junk lines that must be ignored.
    {
        Project p;
        p.menubar =
            "File: New | Open | Save As | - | Quit\n"
            "Edit: Cut | Copy & Paste | Select All\n"
            "this line has no colon and is ignored\n"
            ": empty menu label ignored\n"
            "Help: About FDesigner\n";
        p.widgets.push_back(mk("Button", "Button_001", 10, 10));
        emit("05_menus", p);
    }

    // 6. Hostile strings: quotes, backslashes, newline, tab, UTF-8,
    //    format-specifier lookalikes.
    {
        Project p;
        p.title = "My \"Quoted\" \\ App\nSecond line";
        p.background = "#102030";
        Widget b = mk("Button", "Button_001", 10, 10);
        b.props["label"] = "Click \"me\" \\ 100% %s %d";
        p.widgets.push_back(b);
        Widget l = mk("Label", "Label_001", 10, 60);
        l.props["label"] = "café — ☕\t tabbed";
        p.widgets.push_back(l);
        Widget e = mk("Entry", "Entry_001", 10, 110);
        e.props["text"] = "a\"b\\c\nd";
        e.props["placeholder-text"] = "type \"here\"";
        p.widgets.push_back(e);
        emit("06_hostile_strings", p);
    }

    // 7. Class-name sanitization: spaces/punctuation, leading digit,
    //    empty (falls back to MyWindow).
    {
        Project p;
        p.class_name = "my cool app!";
        p.widgets.push_back(mk("Button", "Button_001", 10, 10));
        emit("07_class_spaces", p);
    }
    {
        Project p;
        p.class_name = "9lives";
        p.widgets.push_back(mk("Label", "Label_001", 10, 10));
        emit("08_class_digit", p);
    }
    {
        Project p;
        p.class_name = "";
        p.widgets.push_back(mk("Entry", "Entry_001", 10, 10));
        emit("09_class_empty", p);
    }

    // 8. Spreadsheet extremes + several toplevels.
    {
        Project p;
        Widget big = mk("Spreadsheet", "Spreadsheet_001", 10, 10);
        big.props["rows"] = "50";
        big.props["columns"] = "26";
        big.width = 900;
        big.height = 500;
        p.widgets.push_back(big);
        Widget tiny = mk("Spreadsheet", "Spreadsheet_002", 10, 520);
        tiny.props["rows"] = "1";
        tiny.props["columns"] = "1";
        p.widgets.push_back(tiny);
        for (int i = 1; i <= 3; ++i) {
            Widget t = mk("Toplevel", "Toplevel_00" + std::to_string(i),
                          100 * i, 100 * i);
            t.props["title"] = "Window " + std::to_string(i);
            t.props["resizable"] = i == 2 ? "false" : "true";
            p.widgets.push_back(t);
        }
        emit("10_spreadsheet_toplevels", p);
    }

    // 9. Forty mixed widgets.
    {
        Project p;
        const char* types[] = {"Button", "Label", "Entry", "Checkbutton",
                               "Frame", "Combobox", "Scale", "Text"};
        for (int i = 0; i < 40; ++i) {
            std::string type = types[i % 8];
            char buf[32];
            std::snprintf(buf, sizeof buf, "%s_%03d", type.c_str(), i + 1);
            p.widgets.push_back(
                mk(type, buf, 10 + (i % 5) * 170, 10 + (i / 5) * 70));
        }
        emit("11_forty_widgets", p);
    }

    // 10. Window variations: no background, huge size, tiny size.
    {
        Project p;
        p.win_width = 3840;
        p.win_height = 2160;
        p.widgets.push_back(mk("Label", "Label_001", 10, 10));
        emit("12_window_huge", p);
    }
    {
        Project p;
        p.win_width = 50;
        p.win_height = 50;
        p.background = "red";
        emit("13_window_tiny", p);
    }

    // 11. StockChart fed with the fake Massive-format CSV.
    {
        Project p;
        Widget chart = mk("StockChart", "StockChart_001", 10, 10);
        chart.width = 640;
        chart.height = 320;
        std::string csv = std::filesystem::absolute(
            g_base + "/14_stockchart/SPY_fake.csv").string();
        chart.props["data-file"] = csv;
        p.widgets.push_back(chart);
        p.widgets.push_back(mk("Label", "Label_001", 10, 340));
        emit("14_stockchart", p);
        std::ifstream src("tests/data/SPY_fake.csv", std::ios::binary);
        std::ofstream dst(g_base + "/14_stockchart/SPY_fake.csv",
                          std::ios::binary);
        dst << src.rdbuf();
    }

    // 12. StockChart with indicator submodules attached (overlay + pane
    // + markers), fed with the 3-day series.
    {
        Project p;
        Widget chart = mk("StockChart", "StockChart_001", 10, 10);
        chart.width = 900;
        chart.height = 500;
        std::string csv = std::filesystem::absolute(
            g_base + "/15_stockchart_indicators/SPY_fake_long.csv")
                              .string();
        chart.props["data-file"] = csv;
        chart.props["max-candles"] = "400";
        chart.props["indicators"] =
            "SMA(20), EMA(50), Bollinger Bands, RSI, MACD, Engulfing";
        p.widgets.push_back(chart);
        emit("15_stockchart_indicators", p);
        std::ifstream src("tests/data/SPY_fake_long.csv",
                          std::ios::binary);
        std::ofstream dst(g_base +
                              "/15_stockchart_indicators/SPY_fake_long.csv",
                          std::ios::binary);
        dst << src.rdbuf();
    }

    // 13. StockChart Pro: percent scale, QQQ comparison series, and a
    // full set of annotations, in a 1024x768 window.
    {
        Project p;
        p.win_width = 1024;
        p.win_height = 768;
        Widget chart = mk("StockChart", "StockChart_001", 10, 10);
        chart.width = 1004;
        chart.height = 748;
        std::string csv = std::filesystem::absolute(
            g_base + "/16_stockchart_pro/SPY_fake_long.csv").string();
        std::string qqq = std::filesystem::absolute(
            g_base + "/16_stockchart_pro/QQQ_fake_long.csv").string();
        chart.props["data-file"] = csv;
        chart.props["max-candles"] = "400";
        chart.props["indicators"] = "SMA(20), RSI";
        chart.props["scale-mode"] = "1";
        chart.props["compare-files"] = qqq;
        chart.props["drawings"] =
            "trend 790 676 1150 690 red; hline 688 blue; "
            "fib 800 674 1000 692 purple; "
            "text 830 691 orange Resistance zone; "
            "rect 860 678 960 684 teal; "
            "arrow 920 690 1090 680 magenta; "
            "measure 840 677 1010 689 black";
        p.widgets.push_back(chart);
        emit("16_stockchart_pro", p);
        std::ifstream src("tests/data/SPY_fake_long.csv",
                          std::ios::binary);
        std::ofstream dst(g_base + "/16_stockchart_pro/SPY_fake_long.csv",
                          std::ios::binary);
        dst << src.rdbuf();
        std::ifstream src2("tests/data/QQQ_fake_long.csv",
                           std::ios::binary);
        std::ofstream dst2(g_base + "/16_stockchart_pro/QQQ_fake_long.csv",
                           std::ios::binary);
        dst2 << src2.rdbuf();
    }

    return g_case == 0 ? 0 : 1;
}

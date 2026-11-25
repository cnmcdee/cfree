// FDesigner property tables — the GTK counterpart of the original's
// per-widget parameter mapping (the Entry_001..019 tables in fdesigner7.py).
// Each palette type lists the properties the designer can edit; values are
// stored in Widget.props as canonical strings and applied live with
// g_object_set (or, for the two pseudo-properties background/foreground,
// through a per-widget CSS provider — GTK's equivalent of Tk's bg/fg config).

#pragma once

#include <string>
#include <vector>

enum class PropKind { Str, Bool, Int, Double, Color, Enum };

struct PropDef {
    const char* name;          // GTK property name, or background/foreground
    PropKind kind;
    const char* def;           // default value (canonical string), "" = none
    double lo = 0, hi = 0;     // spin range for Int/Double
    const char* enum_labels = nullptr;  // Enum: "Label A|Label B|..."
    const int* enum_values = nullptr;   // Enum: value per label
};

// Enum value tables (GTK 3 constants, written out so this header stays
// independent of the GTK headers).
inline const int kReliefValues[] = {0, 1, 2};        // NORMAL, HALF, NONE
inline const int kShadowValues[] = {0, 1, 2, 3, 4};  // NONE, IN, OUT, ETCHED_IN, ETCHED_OUT
inline const int kSelModeValues[] = {0, 1, 2, 3};    // NONE, SINGLE, BROWSE, MULTIPLE
inline const int kArrowValues[] = {0, 1, 2, 3, 4};   // UP, DOWN, LEFT, RIGHT, NONE
inline const int kWrapValues[] = {0, 1, 2, 3};       // NONE, CHAR, WORD, WORD_CHAR
inline const int kScaleModeValues[] = {0, 1};        // PRICE, PERCENT

// Properties every widget gets (the original configures bg/fg on nearly
// every widget type).
inline const std::vector<PropDef>& common_props() {
    static const std::vector<PropDef> v = {
        {"background", PropKind::Color, ""},
        {"foreground", PropKind::Color, ""},
        {"tooltip-text", PropKind::Str, ""},
    };
    return v;
}

inline const std::vector<PropDef>& props_for(const std::string& type) {
    using K = PropKind;
    static const std::vector<PropDef> none;
    static const std::vector<PropDef> button = {
        {"label", K::Str, ""},
        {"relief", K::Enum, "0", 0, 0, "Normal|Half|None", kReliefValues},
        {"focus-on-click", K::Bool, "true"},
    };
    static const std::vector<PropDef> label = {
        {"label", K::Str, ""},
        {"xalign", K::Double, "0.5", 0.0, 1.0},
        {"selectable", K::Bool, "false"},
        {"wrap", K::Bool, "false"},
    };
    static const std::vector<PropDef> entry = {
        {"text", K::Str, ""},
        {"placeholder-text", K::Str, ""},
        {"max-length", K::Int, "0", 0, 100000},
        {"width-chars", K::Int, "-1", -1, 1000},
        {"editable", K::Bool, "true"},
        {"visibility", K::Bool, "true"},
    };
    static const std::vector<PropDef> checkbutton = {
        {"label", K::Str, ""},
        {"active", K::Bool, "false"},
        {"inconsistent", K::Bool, "false"},
    };
    static const std::vector<PropDef> radiobutton = {
        {"label", K::Str, ""},
        {"active", K::Bool, "false"},
    };
    static const std::vector<PropDef> frame = {
        {"label", K::Str, ""},
        {"shadow-type", K::Enum, "3", 0, 0, "None|In|Out|Etched in|Etched out",
         kShadowValues},
    };
    static const std::vector<PropDef> combobox = {
        {"active", K::Int, "0", -1, 1000},
    };
    static const std::vector<PropDef> listbox = {
        {"selection-mode", K::Enum, "1", 0, 0, "None|Single|Browse|Multiple",
         kSelModeValues},
    };
    static const std::vector<PropDef> menubutton = {
        {"label", K::Str, ""},
        {"direction", K::Enum, "1", 0, 0, "Up|Down|Left|Right|None",
         kArrowValues},
    };
    static const std::vector<PropDef> scale = {
        {"value", K::Double, "0", 0, 100},
        {"digits", K::Int, "1", -1, 10},
        {"draw-value", K::Bool, "true"},
        {"inverted", K::Bool, "false"},
    };
    static const std::vector<PropDef> scrollbar = {
        {"value", K::Double, "0", 0, 100},
        {"inverted", K::Bool, "false"},
    };
    static const std::vector<PropDef> text = {
        {"editable", K::Bool, "true"},
        {"cursor-visible", K::Bool, "true"},
        {"wrap-mode", K::Enum, "0", 0, 0, "None|Char|Word|Word-char",
         kWrapValues},
    };
    if (type == "Button") return button;
    if (type == "Label") return label;
    if (type == "Entry") return entry;
    if (type == "Checkbutton") return checkbutton;
    if (type == "Radiobutton") return radiobutton;
    if (type == "Frame") return frame;
    if (type == "Combobox") return combobox;
    if (type == "Listbox") return listbox;
    if (type == "Menubutton") return menubutton;
    if (type == "Scale") return scale;
    if (type == "Scrollbar") return scrollbar;
    if (type == "Text") return text;
    static const std::vector<PropDef> toplevel = {
        {"title", K::Str, ""},
        {"resizable", K::Bool, "true"},
    };
    static const std::vector<PropDef> spreadsheet = {
        {"rows", K::Int, "5", 1, 50},
        {"columns", K::Int, "4", 1, 26},
    };
    if (type == "Toplevel") return toplevel;
    if (type == "Spreadsheet") return spreadsheet;
    static const std::vector<PropDef> stockchart = {
        {"data-file", K::Str, ""},
        {"up-color", K::Color, "rgb(0,200,0)"},
        {"down-color", K::Color, "rgb(220,0,0)"},
        {"max-candles", K::Int, "120", 1, 5000},
        {"indicators", K::Str, ""},
        {"scale-mode", K::Enum, "0", 0, 0, "Price|Percent",
         kScaleModeValues},
        {"compare-files", K::Str, ""},
        {"drawings", K::Str, ""},
    };
    if (type == "StockChart") return stockchart;
    return none;  // Canvas: common props only
}

// Find a property definition for a widget type (type table, then common).
inline const PropDef* find_prop(const std::string& type,
                                const std::string& name) {
    for (const PropDef& d : props_for(type))
        if (name == d.name) return &d;
    for (const PropDef& d : common_props())
        if (name == d.name) return &d;
    return nullptr;
}

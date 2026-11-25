// FDesigner model — the designed window as data.
// Mirrors the Python original's widget dict: every placed widget has a name,
// a type, absolute geometry (Tk .place() style), and a property map.

#pragma once

#include <map>
#include <string>
#include <vector>

struct Widget {
    std::string name;   // e.g. "Button_001"
    std::string type;   // palette type key, e.g. "Button"
    int x = 0, y = 0;
    int width = 100, height = 30;
    std::map<std::string, std::string> props;  // GTK properties, as strings
    std::vector<std::string> signals;          // signals to stub in generated code
};

struct Project {
    std::string title = "Main Window";
    std::string class_name = "MyWindow";
    int win_width = 800;
    int win_height = 600;
    std::string background;  // window background colour, "" = default
    // Menu bar spec, one menu per line: "File: New | Open | - | Quit"
    // ("-" is a separator). Empty = no menu bar.
    std::string menubar;
    std::vector<Widget> widgets;
};

// One row of the palette: type key, button label in the .ui, default size,
// and whether this phase implements it (Toplevel/Spreadsheet arrive later).
struct TypeInfo {
    const char* key;
    const char* palette_id;  // GtkBuilder id of the palette button
    int def_width;
    int def_height;
    bool phase1;
};

inline const TypeInfo kTypes[] = {
    {"Button",      "pal_button",      100, 30,  true },
    {"Label",       "pal_label",       100, 24,  true },
    {"Entry",       "pal_entry",       160, 28,  true },
    {"Checkbutton", "pal_checkbutton", 120, 26,  true },
    {"Radiobutton", "pal_radiobutton", 120, 26,  true },
    {"Frame",       "pal_frame",       200, 120, true },
    {"Combobox",    "pal_combobox",    160, 28,  true },
    {"Listbox",     "pal_listbox",     180, 110, true },
    {"Menubutton",  "pal_menubutton",  110, 30,  true },
    {"Canvas",      "pal_canvas",      200, 140, true },
    {"Scale",       "pal_scale",       160, 28,  true },
    {"Scrollbar",   "pal_scrollbar",   160, 18,  true },
    {"Text",        "pal_text",        220, 120, true },
    {"Toplevel",    "pal_toplevel",    300, 200, true },
    {"Spreadsheet", "pal_spreadsheet", 320, 180, true },
    {"StockChart",  "pal_stockchart",  320, 200, true },
    // Lazarus-style widgets (Lazarus palette -> GTK 3 equivalent).
    {"Memo",          "pal_memo",          220, 120, true },
    {"GroupBox",      "pal_groupbox",      200, 120, true },
    {"Panel",         "pal_panel",         200, 120, true },
    {"Image",         "pal_image",         160, 120, true },
    {"Shape",         "pal_shape",         160, 120, true },
    {"Notebook",      "pal_notebook",      320, 200, true },
    {"ScrollBox",     "pal_scrollbox",     240, 160, true },
    {"Splitter",      "pal_splitter",      280, 160, true },
    {"StatusBar",     "pal_statusbar",     320,  24, true },
    {"ToolBar",       "pal_toolbar",       320,  36, true },
    {"MenuBar",       "pal_menubar",       320,  28, true },
    {"SpinEdit",      "pal_spinedit",      120,  28, true },
    {"TrackBar",      "pal_trackbar",      160,  28, true },
    {"ProgressBar",   "pal_progressbar",   200,  24, true },
    {"DateTimePicker","pal_datetimepicker",240, 200, true },
    {"ColorBox",      "pal_colorbox",      120,  28, true },
    {"ListView",      "pal_listview",      240, 140, true },
    {"TreeView",      "pal_treeview",      240, 140, true },
    {"StringGrid",    "pal_stringgrid",    280, 160, true },
    {"Plot",          "pal_plot",          280, 180, true },
};

inline const TypeInfo* find_type(const std::string& key) {
    for (const TypeInfo& t : kTypes)
        if (key == t.key) return &t;
    return nullptr;
}

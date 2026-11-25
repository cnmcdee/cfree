// FDesigner event table — the natural GTK signal(s) for every widget type.
//
// Every palette type is listed here exactly once: either with the signals
// a user can connect (each gets a generated stub handler, connected via
// g_signal_connect in the generated widget class) or with an explicit
// "no events" note saying why it has none. NONE is left unexamined.
// The table drives three things: the designer's Events pane (main.cpp),
// the code generator (generator.cpp), and the event tests.
//
// Signal kinds (how the generator wires each one):
//   Simple        — void on_x(); trampoline (GtkWidget*, gpointer)
//   SwitchPage    — void on_x(guint page); notebook "switch-page"
//   RowActivated  — void on_x(GtkTreePath* path); tree "row-activated"
//   RowSelected   — void on_x(); trampoline (GtkListBox*, GtkListBoxRow*, gpointer)
//   BufferChanged — void on_x(); connected to the TextView's GtkTextBuffer
//   DeleteEvent   — bool on_x(); trampoline (GtkWidget*, GdkEvent*, gpointer)
//   Draw          — static gboolean on_draw(GtkWidget*, cairo_t*, gpointer)
//   ItemClicked   — ToolBar: one void on_<item>_clicked() per tool button
//   ItemActivate  — MenuBar: one void on_<item>_activate() per menu item
//   CellChanged   — Spreadsheet: void on_x() on every cell entry
// A builtin event is already wired by the generator for other reasons
// (Shape/Plot/StockChart draw); the table documents it and the generator
// must not connect it a second time.

#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "model.h"

enum class EventKind {
    Simple,
    SwitchPage,
    RowActivated,
    RowSelected,
    BufferChanged,
    DeleteEvent,
    Draw,
    ItemClicked,
    ItemActivate,
    CellChanged,
};

struct EventDef {
    const char* signal;   // GTK signal name, e.g. "clicked"
    const char* handler;  // member handler name, e.g. "on_clicked"
    const char* label;    // short human label for the Events pane
    EventKind kind;
    bool builtin = false;  // already wired; document only, do not reconnect
};

inline const std::vector<EventDef>& events_for(const std::string& type) {
    using K = EventKind;
    static const std::vector<EventDef> none;
    static const std::vector<EventDef> button = {
        {"clicked", "on_clicked", "Clicked", K::Simple},
    };
    static const std::vector<EventDef> entry = {
        {"changed", "on_changed", "Text changed", K::Simple},
        {"activate", "on_activate", "Enter pressed (activate)", K::Simple},
    };
    static const std::vector<EventDef> checkbutton = {
        {"toggled", "on_toggled", "Toggled", K::Simple},
    };
    static const std::vector<EventDef> radiobutton = {
        {"toggled", "on_toggled", "Toggled", K::Simple},
    };
    static const std::vector<EventDef> combobox = {
        {"changed", "on_changed", "Selection changed", K::Simple},
    };
    static const std::vector<EventDef> listbox = {
        {"row-selected", "on_row_selected", "Row selected", K::RowSelected},
    };
    static const std::vector<EventDef> menubutton = {
        {"clicked", "on_clicked", "Clicked", K::Simple},
    };
    static const std::vector<EventDef> canvas = {
        {"draw", "on_draw", "Draw (redraw)", K::Draw},
    };
    static const std::vector<EventDef> scale = {
        {"value-changed", "on_value_changed", "Value changed", K::Simple},
    };
    static const std::vector<EventDef> scrollbar = {
        {"value-changed", "on_value_changed", "Value changed", K::Simple},
    };
    static const std::vector<EventDef> text = {
        {"changed", "on_changed", "Text changed", K::BufferChanged},
    };
    static const std::vector<EventDef> toplevel = {
        {"delete-event", "on_delete_event", "Window close (delete-event)",
         K::DeleteEvent},
    };
    static const std::vector<EventDef> spreadsheet = {
        {"changed", "on_cell_changed", "Cell text changed", K::CellChanged},
    };
    static const std::vector<EventDef> stockchart = {
        // Draw, wheel zoom, drag pan and the drawing tools are already
        // wired by the generator (on_draw, on_chart_scroll, on_chart_press,
        // on_chart_motion, on_chart_release, on_chart_key).
        {"draw", "on_draw", "Draw (redraw)", K::Draw, true},
    };
    static const std::vector<EventDef> memo = {
        {"changed", "on_changed", "Text changed", K::BufferChanged},
    };
    static const std::vector<EventDef> shape = {
        {"draw", "on_draw", "Draw (redraw)", K::Draw, true},
    };
    static const std::vector<EventDef> notebook = {
        {"switch-page", "on_switch_page", "Page switched", K::SwitchPage},
    };
    static const std::vector<EventDef> toolbar = {
        {"clicked", "on_clicked", "Tool button clicked", K::ItemClicked},
    };
    static const std::vector<EventDef> menubar = {
        {"activate", "on_activate", "Menu item activated", K::ItemActivate},
    };
    static const std::vector<EventDef> spinedit = {
        {"value-changed", "on_value_changed", "Value changed", K::Simple},
    };
    static const std::vector<EventDef> trackbar = {
        {"value-changed", "on_value_changed", "Value changed", K::Simple},
    };
    static const std::vector<EventDef> datetimepicker = {
        {"day-selected", "on_day_selected", "Day selected", K::Simple},
        {"month-changed", "on_month_changed", "Month changed", K::Simple},
    };
    static const std::vector<EventDef> colorbox = {
        {"color-set", "on_color_set", "Colour chosen", K::Simple},
    };
    static const std::vector<EventDef> listview = {
        {"row-activated", "on_row_activated", "Row activated (double-click)",
         K::RowActivated},
        {"cursor-changed", "on_cursor_changed", "Cursor moved", K::Simple},
    };
    static const std::vector<EventDef> treeview = {
        {"row-activated", "on_row_activated", "Row activated (double-click)",
         K::RowActivated},
        {"cursor-changed", "on_cursor_changed", "Cursor moved", K::Simple},
    };
    static const std::vector<EventDef> stringgrid = {
        {"row-activated", "on_row_activated", "Row activated (double-click)",
         K::RowActivated},
        {"cursor-changed", "on_cursor_changed", "Cursor moved", K::Simple},
    };
    static const std::vector<EventDef> plot = {
        {"draw", "on_draw", "Draw (redraw)", K::Draw, true},
    };
    if (type == "Button") return button;
    if (type == "Entry") return entry;
    if (type == "Checkbutton") return checkbutton;
    if (type == "Radiobutton") return radiobutton;
    if (type == "Combobox") return combobox;
    if (type == "Listbox") return listbox;
    if (type == "Menubutton") return menubutton;
    if (type == "Canvas") return canvas;
    if (type == "Scale") return scale;
    if (type == "Scrollbar") return scrollbar;
    if (type == "Text") return text;
    if (type == "Toplevel") return toplevel;
    if (type == "Spreadsheet") return spreadsheet;
    if (type == "StockChart") return stockchart;
    if (type == "Memo") return memo;
    if (type == "Shape") return shape;
    if (type == "Notebook") return notebook;
    if (type == "ToolBar") return toolbar;
    if (type == "MenuBar") return menubar;
    if (type == "SpinEdit") return spinedit;
    if (type == "TrackBar") return trackbar;
    if (type == "DateTimePicker") return datetimepicker;
    if (type == "ColorBox") return colorbox;
    if (type == "ListView") return listview;
    if (type == "TreeView") return treeview;
    if (type == "StringGrid") return stringgrid;
    if (type == "Plot") return plot;
    return none;  // Label, Frame, GroupBox, Panel, Image, ScrollBox,
                  // Splitter, StatusBar, ProgressBar — see no_events_note()
}

// Why a type with no events has none (shown in the Events pane so the
// absence is explicit, not an omission).
inline std::string no_events_note(const std::string& type) {
    if (type == "Label")
        return "Display-only: no widget-specific signal "
               "(mouse handlers still apply).";
    if (type == "Frame" || type == "GroupBox" || type == "Panel")
        return "Container: no widget-specific signal — connect the "
               "signals of the widgets placed inside it.";
    if (type == "Image")
        return "Display-only: no widget-specific signal "
               "(mouse handlers still apply).";
    if (type == "ScrollBox")
        return "Container: no widget-specific signal — connect the "
               "signals of the widgets placed inside it.";
    if (type == "Splitter")
        return "GTK 3 GtkPaned exposes no position signal: no "
               "widget-specific signal.";
    if (type == "StatusBar")
        return "Display-only: no widget-specific signal.";
    if (type == "ProgressBar")
        return "Display-only: no widget-specific signal — set its "
               "fraction from another widget's handler.";
    return "No widget-specific signal.";
}

// Find one event definition for a type by GTK signal name.
inline const EventDef* find_event(const std::string& type,
                                  const std::string& signal) {
    for (const EventDef& d : events_for(type))
        if (signal == d.signal) return &d;
    return nullptr;
}

// Connect/disconnect one signal on a widget model; false if the signal
// is not one of the type's events.
inline bool set_widget_signal(Widget& w, const std::string& signal,
                              bool connect) {
    if (!find_event(w.type, signal)) return false;
    auto it = std::find(w.signals.begin(), w.signals.end(), signal);
    if (connect) {
        if (it == w.signals.end()) w.signals.push_back(signal);
    } else if (it != w.signals.end()) {
        w.signals.erase(it);
    }
    return true;
}

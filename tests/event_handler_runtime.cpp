// Runtime event harness: instantiate every generated widget class,
// trigger each connected signal through the GTK API, and check the EVENT
// trace lines the stubs print to stderr. The shell script runs this under
// Xvfb with stderr redirected to argv[1]; the harness reads that file
// back and prints a per-widget pass/fail table to stdout.
// Usage: event_handler_runtime <event-log-path>
// (Compile with -DFDESIGNER_EVENT_TRACE and -I <generated dir>, linking
// the generated widget .cpp files except main.cpp.)
#include <gtk/gtk.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "Button_001.h"
#include "Label_001.h"
#include "Entry_001.h"
#include "Checkbutton_001.h"
#include "Radiobutton_001.h"
#include "Frame_001.h"
#include "Combobox_001.h"
#include "Listbox_001.h"
#include "Menubutton_001.h"
#include "Canvas_001.h"
#include "Scale_001.h"
#include "Scrollbar_001.h"
#include "Text_001.h"
#include "Toplevel_001.h"
#include "Spreadsheet_001.h"
#include "StockChart_001.h"
#include "Memo_001.h"
#include "GroupBox_001.h"
#include "Panel_001.h"
#include "Image_001.h"
#include "Shape_001.h"
#include "Notebook_001.h"
#include "ScrollBox_001.h"
#include "Splitter_001.h"
#include "StatusBar_001.h"
#include "ToolBar_001.h"
#include "MenuBar_001.h"
#include "SpinEdit_001.h"
#include "TrackBar_001.h"
#include "ProgressBar_001.h"
#include "DateTimePicker_001.h"
#include "ColorBox_001.h"
#include "ListView_001.h"
#include "TreeView_001.h"
#include "StringGrid_001.h"
#include "Plot_001.h"

static void pump() {
    while (gtk_events_pending()) gtk_main_iteration();
}

struct Expect {
    const char* type;
    const char* signal;  // "" = no-events type (pass by design)
    const char* line;    // substring to find in the trace log
};

static const Expect kExpect[] = {
    {"Button", "clicked", "EVENT Button_001 clicked"},
    {"Entry", "changed", "EVENT Entry_001 changed"},
    {"Entry", "activate", "EVENT Entry_001 activate"},
    {"Checkbutton", "toggled", "EVENT Checkbutton_001 toggled"},
    {"Radiobutton", "toggled", "EVENT Radiobutton_001 toggled"},
    {"Combobox", "changed", "EVENT Combobox_001 changed"},
    {"Listbox", "row-selected", "EVENT Listbox_001 row-selected"},
    {"Menubutton", "clicked", "EVENT Menubutton_001 clicked"},
    {"Canvas", "draw", "EVENT Canvas_001 draw"},
    {"Scale", "value-changed", "EVENT Scale_001 value-changed"},
    {"Scrollbar", "value-changed", "EVENT Scrollbar_001 value-changed"},
    {"Text", "changed", "EVENT Text_001 changed"},
    {"Toplevel", "delete-event", "EVENT Toplevel_001 delete-event"},
    {"Spreadsheet", "changed", "EVENT Spreadsheet_001 changed"},
    {"StockChart", "draw", "EVENT StockChart_001 draw"},
    {"Memo", "changed", "EVENT Memo_001 changed"},
    {"Shape", "draw", "EVENT Shape_001 draw"},
    {"Notebook", "switch-page", "EVENT Notebook_001 switch-page"},
    {"ToolBar", "clicked", "EVENT ToolBar_001 clicked"},
    {"MenuBar", "activate", "EVENT MenuBar_001 activate"},
    {"SpinEdit", "value-changed", "EVENT SpinEdit_001 value-changed"},
    {"TrackBar", "value-changed", "EVENT TrackBar_001 value-changed"},
    {"DateTimePicker", "day-selected",
     "EVENT DateTimePicker_001 day-selected"},
    {"DateTimePicker", "month-changed",
     "EVENT DateTimePicker_001 month-changed"},
    {"ColorBox", "color-set", "EVENT ColorBox_001 color-set"},
    {"ListView", "row-activated", "EVENT ListView_001 row-activated"},
    {"ListView", "cursor-changed", "EVENT ListView_001 cursor-changed"},
    {"TreeView", "row-activated", "EVENT TreeView_001 row-activated"},
    {"TreeView", "cursor-changed", "EVENT TreeView_001 cursor-changed"},
    {"StringGrid", "row-activated", "EVENT StringGrid_001 row-activated"},
    {"StringGrid", "cursor-changed", "EVENT StringGrid_001 cursor-changed"},
    {"Plot", "draw", "EVENT Plot_001 draw"},
    // No natural signal — pass by design, with the explicit note.
    {"Label", "", ""},
    {"Frame", "", ""},
    {"GroupBox", "", ""},
    {"Panel", "", ""},
    {"Image", "", ""},
    {"ScrollBox", "", ""},
    {"Splitter", "", ""},
    {"StatusBar", "", ""},
    {"ProgressBar", "", ""},
};

int main(int argc, char** argv) {
    gtk_init(&argc, &argv);
    std::string log_path = argc > 1 ? argv[1] : "/tmp/event_trace.log";

    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWidget* fixed = gtk_fixed_new();
    gtk_container_add(GTK_CONTAINER(window), fixed);

    Button_001 btn(fixed);
    Label_001 lbl(fixed);
    Entry_001 ent(fixed);
    Checkbutton_001 chk(fixed);
    Radiobutton_001 radio(fixed);
    Frame_001 frm(fixed);
    Combobox_001 combo(fixed);
    Listbox_001 lbox(fixed);
    Menubutton_001 mbtn(fixed);
    Canvas_001 canvas(fixed);
    Scale_001 scale(fixed);
    Scrollbar_001 sbar(fixed);
    Text_001 text(fixed);
    Toplevel_001 top;  // own window; not shown
    Spreadsheet_001 sheet(fixed);
    StockChart_001 chart(fixed);
    Memo_001 memo(fixed);
    GroupBox_001 gbox(fixed);
    Panel_001 panel(fixed);
    Image_001 img(fixed);
    Shape_001 shape(fixed);
    Notebook_001 nb(fixed);
    ScrollBox_001 sbox(fixed);
    Splitter_001 split(fixed);
    StatusBar_001 status(fixed);
    ToolBar_001 tbar(fixed);
    MenuBar_001 mbar(fixed);
    SpinEdit_001 spin(fixed);
    TrackBar_001 track(fixed);
    ProgressBar_001 prog(fixed);
    DateTimePicker_001 dtp(fixed);
    ColorBox_001 cbox(fixed);
    ListView_001 lview(fixed);
    TreeView_001 tview(fixed);
    StringGrid_001 grid(fixed);
    Plot_001 plot(fixed);

    gtk_widget_show_all(window);
    pump();

    // No-events widgets are constructed (they must build + run) but have
    // no signal to trigger; keep the build warning-free.
    (void)lbl.widget();
    (void)frm.widget();
    (void)gbox.widget();
    (void)panel.widget();
    (void)img.widget();
    (void)sbox.widget();
    (void)split.widget();
    (void)status.widget();
    (void)prog.widget();

    // --- trigger every signal through the GTK API ---
    gtk_button_clicked(GTK_BUTTON(btn.widget()));
    gtk_entry_set_text(GTK_ENTRY(ent.widget()), "hello");
    g_signal_emit_by_name(ent.widget(), "activate");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk.widget()), TRUE);
    // A lone radio button can never leave its group active: GTK keeps
    // the sole member on (set_active(FALSE) and clicking are both
    // no-ops). Toggle it by activating a second button in its group.
    {
        GtkWidget* radio2 = gtk_radio_button_new_with_label_from_widget(
            GTK_RADIO_BUTTON(radio.widget()), "Radiobutton_002");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio2), TRUE);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo.widget()), 1);
    {
        GtkListBoxRow* row = gtk_list_box_get_row_at_index(
            GTK_LIST_BOX(lbox.widget()), 0);
        gtk_list_box_select_row(GTK_LIST_BOX(lbox.widget()), row);
    }
    gtk_button_clicked(GTK_BUTTON(mbtn.widget()));
    gtk_range_set_value(GTK_RANGE(scale.widget()), 42.0);
    gtk_range_set_value(GTK_RANGE(sbar.widget()), 55.0);
    gtk_text_buffer_set_text(
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(text.widget())), "hi", -1);
    {
        GdkEvent* ev = gdk_event_new(GDK_DELETE);
        gboolean ret = FALSE;
        g_signal_emit_by_name(top.widget(), "delete-event", ev, &ret);
        gdk_event_free(ev);
    }
    {
        GList* kids =
            gtk_container_get_children(GTK_CONTAINER(sheet.widget()));
        if (kids) {
            gtk_entry_set_text(GTK_ENTRY(kids->data), "9");
            g_list_free(kids);
        }
    }
    gtk_text_buffer_set_text(
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(memo.widget())), "memo", -1);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(nb.widget()), 1);
    {
        GList* kids =
            gtk_container_get_children(GTK_CONTAINER(tbar.widget()));
        if (kids) {
            g_signal_emit_by_name(kids->data, "clicked");
            g_list_free(kids);
        }
    }
    {
        GList* kids =
            gtk_container_get_children(GTK_CONTAINER(mbar.widget()));
        if (kids) {
            g_signal_emit_by_name(kids->data, "activate");
            g_list_free(kids);
        }
    }
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin.widget()), 7.0);
    gtk_range_set_value(GTK_RANGE(track.widget()), 33.0);
    gtk_calendar_select_day(GTK_CALENDAR(dtp.widget()), 15);
    gtk_calendar_select_month(GTK_CALENDAR(dtp.widget()), 5, 2026);
    g_signal_emit_by_name(cbox.widget(), "color-set");
    auto trigger_tree = [](GtkWidget* tv) {
        GtkTreePath* path = gtk_tree_path_new_from_string("0");
        gtk_tree_view_set_cursor(GTK_TREE_VIEW(tv), path, nullptr, FALSE);
        GtkTreeViewColumn* col = gtk_tree_view_get_column(
            GTK_TREE_VIEW(tv), 0);
        g_signal_emit_by_name(tv, "row-activated", path, col);
        gtk_tree_path_free(path);
    };
    trigger_tree(lview.widget());
    trigger_tree(tview.widget());
    trigger_tree(grid.widget());
    // Draw events need a main-loop pass after the expose is queued.
    gtk_widget_queue_draw(canvas.widget());
    gtk_widget_queue_draw(chart.widget());
    gtk_widget_queue_draw(shape.widget());
    gtk_widget_queue_draw(plot.widget());
    pump();

    // --- read the trace log back and score it ---
    std::ifstream in(log_path);
    std::string log((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
    int pass = 0, fail = 0;
    for (const Expect& e : kExpect) {
        if (e.signal[0] == '\0') {
            std::printf("PASS %-16s (no events — by design)\n", e.type);
            ++pass;
            continue;
        }
        if (log.find(e.line) != std::string::npos) {
            std::printf("PASS %-16s %s\n", e.type, e.signal);
            ++pass;
        } else {
            std::printf("FAIL %-16s %s: no \"%s\" in trace log\n", e.type,
                        e.signal, e.line);
            ++fail;
        }
    }
    std::printf("== event_handler_runtime: %d passed, %d failed ==\n", pass,
                fail);
    return fail ? 1 : 0;
}

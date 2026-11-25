// FDesigner stepping debugger — GTK window implementation.
// Layout (all built in code, no extra .ui file — the window is dynamic):
//   toolbar: Run | Continue | Step Over | Step Into | Step Out | Stop | Restart | Settings
//   target row: executable entry + Browse + args entry + status label
//   middle: breakpoints list (id, location, enabled toggle, hits, type,
//           condition, ignore count, log message) |
//           locals list (name, type, value) + watch entry/add + watch list
//   stack frames list + current source line label
//   bottom: target stdout/stderr text view
// A g_timeout_add(150ms) poll drives the session: while running it drains
// MI output; on a stop it refreshes locals/frames/watches and the UI.
#include "debugger_ui.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static DebuggerSettings g_dbg_settings;
static bool g_dbg_settings_loaded = false;
static GtkWidget* g_dbg_window = nullptr;
static GdbSession* g_session = nullptr;

// Widgets (rebuilt never; created once with the window)
static GtkWidget *g_target_entry = nullptr, *g_args_entry = nullptr;
static GtkWidget *g_status_label = nullptr, *g_source_label = nullptr;
static GtkWidget *g_bp_entry = nullptr, *g_watch_entry = nullptr;
static GtkWidget *g_output_view = nullptr;
static GtkListStore *g_bp_store = nullptr, *g_locals_store = nullptr,
                    *g_watch_store = nullptr, *g_frames_store = nullptr;
static GtkWidget *g_btn_run = nullptr, *g_btn_cont = nullptr,
                 *g_btn_over = nullptr, *g_btn_into = nullptr,
                 *g_btn_out = nullptr, *g_btn_stop = nullptr,
                 *g_btn_restart = nullptr;
static std::string g_output_seen;  // target output already shown
static std::string g_log_seen;
static bool g_was_running = false;

static DebuggerSettings load_settings_once() {
    if (!g_dbg_settings_loaded) {
        g_dbg_settings.load();  // keeps defaults when no file exists
        g_dbg_settings_loaded = true;
    }
    return g_dbg_settings;
}

DebuggerSettings debugger_current_settings() {
    return load_settings_once();
}

bool debugger_window_is_open() { return g_dbg_window != nullptr; }

static void show_error(GtkWidget* parent, const std::string& msg) {
    GtkWidget* d = gtk_message_dialog_new(
        GTK_WINDOW(parent ? parent : g_dbg_window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", msg.c_str());
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}

static void set_status(const std::string& s) {
    if (g_status_label)
        gtk_label_set_text(GTK_LABEL(g_status_label), s.c_str());
}

static void refresh_buttons() {
    if (!g_session) return;
    DbgState st = g_session->state();
    bool running = st == DbgState::Running;
    bool stopped = st == DbgState::Stopped;
    bool idle = st == DbgState::Idle || st == DbgState::Exited ||
                st == DbgState::Error;
    gtk_widget_set_sensitive(g_btn_run, idle || stopped);
    gtk_widget_set_sensitive(g_btn_cont, stopped);
    gtk_widget_set_sensitive(g_btn_over, stopped);
    gtk_widget_set_sensitive(g_btn_into, stopped);
    gtk_widget_set_sensitive(g_btn_out, stopped);
    gtk_widget_set_sensitive(g_btn_stop, running || stopped);
    gtk_widget_set_sensitive(g_btn_restart, !idle);
}

// --- list refresh -------------------------------------------------------

static void refresh_bp_list() {
    if (!g_bp_store || !g_session) return;
    gtk_list_store_clear(g_bp_store);
    for (const auto& b : g_session->breakpoints()) {
        GtkTreeIter it;
        gtk_list_store_append(g_bp_store, &it);
        std::string loc = b.location;
        if (!b.file.empty() && b.line > 0 &&
            loc.find(b.file) == std::string::npos)
            loc += "  (" + b.file + ":" + std::to_string(b.line) + ")";
        // Message column: the logpoint template, with the group appended
        // when one is set (plain/conditional breakpoints show the group
        // alone, so grouped breakpoints stay identifiable in the list).
        std::string msg = b.log_message;
        if (!b.group.empty())
            msg += (msg.empty() ? "[" : "  [") + b.group + "]";
        gtk_list_store_set(g_bp_store, &it, 0, b.id, 1, loc.c_str(), 2,
                           b.enabled ? TRUE : FALSE, 3, b.hits, 4,
                           bp_kind_name(b.kind), 5, b.condition.c_str(), 6,
                           b.ignore_count, 7, msg.c_str(), -1);
    }
}

static void refresh_var_list(GtkListStore* store,
                             const std::vector<Variable>& vars) {
    if (!store) return;
    gtk_list_store_clear(store);
    for (const auto& v : vars) {
        GtkTreeIter it;
        gtk_list_store_append(store, &it);
        gtk_list_store_set(store, &it, 0, v.name.c_str(), 1, v.type.c_str(),
                           2, v.value.c_str(), -1);
    }
}

static void refresh_frames_list() {
    if (!g_frames_store || !g_session) return;
    gtk_list_store_clear(g_frames_store);
    for (const auto& f : g_session->frames()) {
        GtkTreeIter it;
        gtk_list_store_append(g_frames_store, &it);
        std::string where = f.function;
        if (!f.file.empty())
            where += " at " + f.file + ":" + std::to_string(f.line);
        gtk_list_store_set(g_frames_store, &it, 0, f.level, 1, where.c_str(),
                           -1);
    }
}

static void refresh_output() {
    if (!g_output_view || !g_session) return;
    const std::string& out = g_session->target_output();
    const std::string& log = g_session->gdb_log();
    GtkTextBuffer* buf =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_output_view));
    if (out.size() > g_output_seen.size() || log.size() > g_log_seen.size()) {
        std::string add;
        if (out.size() > g_output_seen.size()) {
            add += out.substr(g_output_seen.size());
            g_output_seen = out;
        }
        if (log.size() > g_log_seen.size()) {
            std::string lg = log.substr(g_log_seen.size());
            g_log_seen = log;
            if (!lg.empty()) add += "[gdb] " + lg;
        }
        if (!add.empty()) {
            GtkTextIter end;
            gtk_text_buffer_get_end_iter(buf, &end);
            gtk_text_buffer_insert(buf, &end, add.c_str(), -1);
            GtkTextIter e2;
            gtk_text_buffer_get_end_iter(buf, &e2);
            gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(g_output_view), &e2,
                                         0.0, FALSE, 0, 0);
        }
    }
}

// Called on every poll tick and after every user action.
static void sync_ui_from_session() {
    if (!g_session) return;
    refresh_bp_list();
    refresh_var_list(g_locals_store, g_session->locals());
    refresh_var_list(g_watch_store, g_session->watches());
    refresh_frames_list();
    refresh_output();
    refresh_buttons();
    DbgState st = g_session->state();
    const StopInfo& stop = g_session->last_stop();
    std::string s;
    switch (st) {
        case DbgState::Idle: s = "Idle — enter a target and press Run"; break;
        case DbgState::Starting: s = "Starting gdb…"; break;
        case DbgState::Running: s = "Running…"; break;
        case DbgState::Stopped:
            s = "Stopped";
            if (!stop.reason.empty()) s += " (" + stop.reason + ")";
            if (!stop.frame.function.empty())
                s += " — " + stop.frame.function + " at " +
                     stop.frame.file + ":" +
                     std::to_string(stop.frame.line);
            break;
        case DbgState::Exited:
            s = "Target exited";
            if (stop.exit_code >= 0)
                s += " (code " + std::to_string(stop.exit_code) + ")";
            break;
        case DbgState::Error: s = "Error: " + g_session->last_error(); break;
    }
    set_status(s);
    if (st == DbgState::Stopped && !stop.frame.file.empty()) {
        std::string src = stop.frame.file + ":" +
                          std::to_string(stop.frame.line) + " — " +
                          stop.frame.function + "()";
        gtk_label_set_text(GTK_LABEL(g_source_label), src.c_str());
    } else if (st == DbgState::Exited) {
        gtk_label_set_text(GTK_LABEL(g_source_label), "Target exited.");
    }
}

// Poll tick: drain gdb, detect a fresh stop, refresh inspection.
static gboolean on_dbg_poll(gpointer) {
    if (!g_session || !g_dbg_window) return G_SOURCE_REMOVE;
    DbgState before = g_session->state();
    bool was_running = before == DbgState::Running;
    g_session->poll_events();  // non-blocking drain
    DbgState after = g_session->state();
    if (after == DbgState::Stopped && before != DbgState::Stopped) {
        g_session->refresh_on_stop();
    } else if (was_running && after == DbgState::Exited) {
        g_session->refresh_on_stop();
    }
    sync_ui_from_session();
    g_was_running = after == DbgState::Running;
    return G_SOURCE_CONTINUE;
}

// --- actions ------------------------------------------------------------

static std::string entry_text(GtkWidget* e) {
    const gchar* t = gtk_entry_get_text(GTK_ENTRY(e));
    return t ? t : "";
}

static bool ensure_session(std::string& err) {
    (void)err;
    if (!g_session) {
        load_settings_once();
        g_session = new GdbSession(g_dbg_settings);
        // Restore breakpoints saved by an earlier session.
        g_session->load_breakpoints();
        // Apply them once a target is loaded (start() re-inserts them).
    } else {
        g_session->set_settings(g_dbg_settings);
    }
    return true;
}

static void on_run_clicked(GtkButton*, gpointer) {
    std::string err;
    if (!ensure_session(err)) return;
    std::string exe = entry_text(g_target_entry);
    std::string args = entry_text(g_args_entry);
    if (exe.empty()) {
        show_error(g_dbg_window, "Enter a target executable path first.");
        return;
    }
    if (g_session->state() == DbgState::Stopped ||
        g_session->state() == DbgState::Exited) {
        // Already loaded: restart from the beginning.
        if (!g_session->restart(err)) show_error(g_dbg_window, err);
    } else {
        if (!g_session->start(exe, args, err)) show_error(g_dbg_window, err);
    }
    sync_ui_from_session();
}

static void on_step_clicked(GtkButton*, gpointer which) {
    if (!g_session) return;
    std::string err;
    const char* w = (const char*)which;
    bool ok = false;
    if (!std::strcmp(w, "cont")) ok = g_session->cont(err);
    else if (!std::strcmp(w, "over")) ok = g_session->step_over(err);
    else if (!std::strcmp(w, "into")) ok = g_session->step_into(err);
    else if (!std::strcmp(w, "out")) ok = g_session->step_out(err);
    if (!ok) show_error(g_dbg_window, err);
    sync_ui_from_session();
}

static void on_stop_clicked(GtkButton*, gpointer) {
    if (!g_session) return;
    std::string err;
    if (g_session->target_running()) {
        if (!g_session->interrupt(err)) show_error(g_dbg_window, err);
        g_session->wait_for_stop(2000);
    } else if (!g_session->kill_target(err)) {
        show_error(g_dbg_window, err);
    }
    sync_ui_from_session();
}

static void on_restart_clicked(GtkButton*, gpointer) {
    if (!g_session) return;
    std::string err;
    if (!g_session->restart(err)) show_error(g_dbg_window, err);
    sync_ui_from_session();
}

static void on_browse_clicked(GtkButton*, gpointer) {
    GtkWidget* fc = gtk_file_chooser_dialog_new(
        "Choose target executable", GTK_WINDOW(g_dbg_window),
        GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT, nullptr);
    if (gtk_dialog_run(GTK_DIALOG(fc)) == GTK_RESPONSE_ACCEPT) {
        gchar* path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc));
        if (path) {
            gtk_entry_set_text(GTK_ENTRY(g_target_entry), path);
            g_free(path);
        }
    }
    gtk_widget_destroy(fc);
}

static void on_bp_add_clicked(GtkButton*, gpointer) {
    std::string err;
    if (!ensure_session(err)) return;
    std::string loc = entry_text(g_bp_entry);
    if (loc.empty()) return;
    // If gdb isn't up yet, remember the breakpoint; start() inserts it.
    if (!g_session->is_alive()) {
        if (!g_session->stage_breakpoint(loc)) {
            show_error(g_dbg_window,
                       "not a breakpoint location (need file:line or "
                       "function): " + loc);
            return;
        }
        g_session->save_breakpoints();
        gtk_entry_set_text(GTK_ENTRY(g_bp_entry), "");
        refresh_bp_list();
        return;
    }
    Breakpoint b;
    if (!g_session->add_breakpoint(loc, b, err)) {
        show_error(g_dbg_window, err);
        return;
    }
    g_session->save_breakpoints();
    gtk_entry_set_text(GTK_ENTRY(g_bp_entry), "");
    sync_ui_from_session();
}

static void on_bp_remove_clicked(GtkButton*, gpointer) {
    if (!g_session) return;
    GtkTreeSelection* sel = gtk_tree_view_get_selection(
        GTK_TREE_VIEW(g_object_get_data(G_OBJECT(g_dbg_window), "bp_view")));
    GtkTreeModel* model = nullptr;
    GtkTreeIter it;
    if (!gtk_tree_selection_get_selected(sel, &model, &it)) return;
    int id = 0;
    gtk_tree_model_get(model, &it, 0, &id, -1);
    std::string err;
    if (id > 0) {
        if (!g_session->remove_breakpoint(id, err))
            show_error(g_dbg_window, err);
    } else {
        // Staged breakpoint (not in gdb yet): remove by list position —
        // staged entries all share id 0, so the id alone cannot tell
        // them apart.
        GtkTreePath* tp = gtk_tree_model_get_path(model, &it);
        int idx = tp ? gtk_tree_path_get_indices(tp)[0] : -1;
        if (tp) gtk_tree_path_free(tp);
        auto& bps = const_cast<std::vector<Breakpoint>&>(
            g_session->breakpoints());
        if (idx >= 0 && idx < (int)bps.size()) bps.erase(bps.begin() + idx);
    }
    g_session->save_breakpoints();
    sync_ui_from_session();
}

// --- breakpoint add/edit dialog -----------------------------------------
//
// One dialog for the full handler set: kind (plain / conditional /
// logpoint), condition, ignore count, log message, group. For a new
// breakpoint the location is editable; for an existing one it is shown
// read-only (the backend's update keeps the location — delete the
// breakpoint and add a new one to move it).

static void bp_dialog_set_sensitive(GtkComboBox* kind_combo,
                                    GtkWidget* cond_entry,
                                    GtkWidget* log_entry) {
    int active = gtk_combo_box_get_active(kind_combo);
    gtk_widget_set_sensitive(cond_entry, active == 1);
    gtk_widget_set_sensitive(log_entry, active == 2);
}

static void on_bp_kind_changed(GtkComboBox* combo, gpointer data) {
    GtkWidget** widgets = (GtkWidget**)data;  // [cond_entry, log_entry]
    bp_dialog_set_sensitive(combo, widgets[0], widgets[1]);
}

// Edit `bp` in place; returns false when the user cancelled or the
// values do not validate (an error is shown in that case).
static bool breakpoint_edit_dialog(GtkWidget* parent, const char* title,
                                   Breakpoint& bp, bool is_new) {
    GtkWidget* d = gtk_dialog_new_with_buttons(
        title, GTK_WINDOW(parent), GTK_DIALOG_MODAL, "_Cancel",
        GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, nullptr);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(d));
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
    gtk_container_add(GTK_CONTAINER(content), grid);

    GtkWidget* loc_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(loc_entry), bp.location.c_str());
    gtk_widget_set_hexpand(loc_entry, TRUE);
    gtk_widget_set_sensitive(loc_entry, is_new);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Location:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), loc_entry, 1, 0, 1, 1);

    GtkWidget* kind_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(kind_combo), "Plain");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(kind_combo),
                                   "Conditional");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(kind_combo),
                                   "Logpoint");
    gtk_combo_box_set_active(GTK_COMBO_BOX(kind_combo),
                             bp.kind == BpKind::Conditional
                                 ? 1
                                 : (bp.kind == BpKind::Logpoint ? 2 : 0));
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Type:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), kind_combo, 1, 1, 1, 1);

    GtkWidget* cond_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(cond_entry), bp.condition.c_str());
    gtk_entry_set_placeholder_text(
        GTK_ENTRY(cond_entry), "e.g. i == 5  (only for Conditional)");
    gtk_widget_set_hexpand(cond_entry, TRUE);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Condition:"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), cond_entry, 1, 2, 1, 1);

    GtkWidget* ignore_spin = gtk_spin_button_new_with_range(0, 1000000, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ignore_spin),
                              bp.ignore_count);
    gtk_grid_attach(GTK_GRID(grid),
                    gtk_label_new("Ignore first N hits:"), 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), ignore_spin, 1, 3, 1, 1);

    GtkWidget* log_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(log_entry), bp.log_message.c_str());
    gtk_entry_set_placeholder_text(
        GTK_ENTRY(log_entry), "e.g. i={i} sum={sum}  (only for Logpoint)");
    gtk_widget_set_hexpand(log_entry, TRUE);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Log message:"), 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), log_entry, 1, 4, 1, 1);

    GtkWidget* group_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(group_entry), bp.group.c_str());
    gtk_entry_set_placeholder_text(GTK_ENTRY(group_entry),
                                   "optional group label");
    gtk_widget_set_hexpand(group_entry, TRUE);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Group:"), 0, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), group_entry, 1, 5, 1, 1);

    GtkWidget* hint = gtk_label_new(
        "Conditional: stops only when the condition is true.\n"
        "Logpoint: prints the message (never stops) — {expr} prints as an\n"
        "integer, {s:name} as a string, {f:x} as a float, {x:v} as hex.\n"
        "Ignore: the first N hits are skipped, then it stops as normal.\n"
        "Logpoint output appears in the target-output pane as [gdb] text.");
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0);
    gtk_grid_attach(GTK_GRID(grid), hint, 0, 6, 2, 1);

    GtkWidget* sens_widgets[2] = {cond_entry, log_entry};
    g_signal_connect(kind_combo, "changed", G_CALLBACK(on_bp_kind_changed),
                     sens_widgets);
    bp_dialog_set_sensitive(GTK_COMBO_BOX(kind_combo), cond_entry, log_entry);

    gtk_widget_show_all(d);
    if (gtk_dialog_run(GTK_DIALOG(d)) != GTK_RESPONSE_ACCEPT) {
        gtk_widget_destroy(d);
        return false;  // cancelled
    }
    std::string loc = entry_text(loc_entry);
    int kind_active = gtk_combo_box_get_active(GTK_COMBO_BOX(kind_combo));
    std::string cond = entry_text(cond_entry);
    int ignore = gtk_spin_button_get_value_as_int(
        GTK_SPIN_BUTTON(ignore_spin));
    std::string logmsg = entry_text(log_entry);
    std::string group = entry_text(group_entry);
    gtk_widget_destroy(d);

    std::string file;
    int line = 0;
    std::string func;
    if (!parse_breakpoint_location(loc, file, line, func)) {
        show_error(parent,
                   "not a breakpoint location (need file:line or "
                   "function): " +
                       loc);
        return false;
    }
    if (kind_active == 1 && cond.empty()) {
        show_error(parent,
                   "a conditional breakpoint needs a condition "
                   "(e.g. i == 5).");
        return false;
    }
    if (kind_active == 2 && logmsg.empty()) {
        show_error(parent,
                   "a logpoint needs a log message (e.g. i={i}).");
        return false;
    }
    bp.location = loc;
    bp.file = file;
    bp.line = line;
    bp.function = func;
    bp.kind = kind_active == 1
                  ? BpKind::Conditional
                  : (kind_active == 2 ? BpKind::Logpoint : BpKind::Plain);
    bp.condition = cond;
    bp.ignore_count = ignore;
    bp.log_message = logmsg;
    bp.group = group;
    return true;
}

static void on_bp_add_full_clicked(GtkButton*, gpointer) {
    std::string err;
    if (!ensure_session(err)) return;
    Breakpoint bp;
    bp.location = entry_text(g_bp_entry);
    if (!breakpoint_edit_dialog(g_dbg_window, "Add Breakpoint", bp, true))
        return;
    if (!g_session->is_alive()) {
        if (!g_session->stage_breakpoint_opts(bp.location, bp)) {
            show_error(g_dbg_window,
                       "not a breakpoint location (need file:line or "
                       "function): " +
                           bp.location);
            return;
        }
    } else {
        Breakpoint out;
        if (!g_session->add_breakpoint_opts(bp.location, bp, out, err)) {
            show_error(g_dbg_window, err);
            return;
        }
    }
    g_session->save_breakpoints();
    gtk_entry_set_text(GTK_ENTRY(g_bp_entry), "");
    sync_ui_from_session();
}

static void on_bp_edit_clicked(GtkButton*, gpointer) {
    if (!g_session) return;
    GtkTreeSelection* sel = gtk_tree_view_get_selection(
        GTK_TREE_VIEW(g_object_get_data(G_OBJECT(g_dbg_window), "bp_view")));
    GtkTreeModel* model = nullptr;
    GtkTreeIter it;
    if (!gtk_tree_selection_get_selected(sel, &model, &it)) return;
    int id = 0;
    gtk_tree_model_get(model, &it, 0, &id, -1);
    // Find the breakpoint: by gdb id when live, by list position when
    // staged (staged entries all share id 0).
    Breakpoint* target = nullptr;
    auto& bps = const_cast<std::vector<Breakpoint>&>(
        g_session->breakpoints());
    if (id > 0) {
        for (auto& b : bps)
            if (b.id == id) target = &b;
    } else {
        GtkTreePath* tp = gtk_tree_model_get_path(model, &it);
        int idx = tp ? gtk_tree_path_get_indices(tp)[0] : -1;
        if (tp) gtk_tree_path_free(tp);
        if (idx >= 0 && idx < (int)bps.size()) target = &bps[idx];
    }
    if (!target) return;
    Breakpoint edited = *target;
    if (!breakpoint_edit_dialog(g_dbg_window, "Edit Breakpoint", edited,
                                false))
        return;
    std::string err;
    Breakpoint out;
    if (!g_session->update_breakpoint(target->id, edited, out, err)) {
        show_error(g_dbg_window, err);
        return;
    }
    g_session->save_breakpoints();
    sync_ui_from_session();
}

static void on_bp_toggled(GtkCellRendererToggle*, gchar* path, gpointer) {
    if (!g_session) return;
    GtkTreeIter it;
    if (!gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(g_bp_store), &it,
                                             path))
        return;
    int id = 0;
    gboolean enabled = FALSE;
    gtk_tree_model_get(GTK_TREE_MODEL(g_bp_store), &it, 0, &id, 2, &enabled,
                       -1);
    std::string err;
    if (g_session->is_alive() && id > 0) {
        if (!g_session->enable_breakpoint(id, !enabled, err)) {
            show_error(g_dbg_window, err);
            return;
        }
    } else {
        // gdb down, or a staged breakpoint (id 0): toggle by list
        // position — staged entries all share id 0, so the id alone
        // cannot tell them apart.
        int idx = std::atoi(path);
        auto& bps = const_cast<std::vector<Breakpoint>&>(
            g_session->breakpoints());
        if (idx >= 0 && idx < (int)bps.size())
            bps[idx].enabled = !enabled;
        else if (id > 0)
            for (auto& b : bps)
                if (b.id == id) b.enabled = !enabled;
    }
    g_session->save_breakpoints();
    sync_ui_from_session();
}

static void on_watch_add_clicked(GtkButton*, gpointer) {
    std::string err;
    if (!ensure_session(err)) return;
    std::string expr = entry_text(g_watch_entry);
    if (expr.empty()) return;
    if (!g_session->add_watch(expr, err)) show_error(g_dbg_window, err);
    gtk_entry_set_text(GTK_ENTRY(g_watch_entry), "");
    sync_ui_from_session();
}

static void on_watch_remove_clicked(GtkButton*, gpointer) {
    if (!g_session) return;
    GtkTreeSelection* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(
        g_object_get_data(G_OBJECT(g_dbg_window), "watch_view")));
    GtkTreeModel* model = nullptr;
    GtkTreeIter it;
    if (!gtk_tree_selection_get_selected(sel, &model, &it)) return;
    gchar* name = nullptr;
    gtk_tree_model_get(model, &it, 0, &name, -1);
    if (name) {
        g_session->remove_watch(name);
        g_free(name);
    }
    sync_ui_from_session();
}

// --- settings dialog ----------------------------------------------------

static bool settings_dialog(GtkWidget* parent, DebuggerSettings& s) {
    GtkWidget* d = gtk_dialog_new_with_buttons(
        "Debugger Settings", GTK_WINDOW(parent), GTK_DIALOG_MODAL, "_Cancel",
        GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, nullptr);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(d));
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
    gtk_container_add(GTK_CONTAINER(content), grid);

    GtkWidget* gdb_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(gdb_entry), s.gdb_path.c_str());
    gtk_widget_set_hexpand(gdb_entry, TRUE);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("gdb executable:"), 0, 0, 1,
                    1);
    gtk_grid_attach(GTK_GRID(grid), gdb_entry, 1, 0, 1, 1);

    GtkWidget* args_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(args_entry), s.default_args.c_str());
    gtk_widget_set_hexpand(args_entry, TRUE);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Default target args:"), 0,
                    1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), args_entry, 1, 1, 1, 1);

    GtkWidget* cb_main =
        gtk_check_button_new_with_label("Break on main (stop at main())");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_main),
                                 s.break_on_main);
    gtk_grid_attach(GTK_GRID(grid), cb_main, 0, 2, 2, 1);

    GtkWidget* cb_entry = gtk_check_button_new_with_label(
        "Stop on entry (stop at program entry, before main)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_entry),
                                 s.stop_on_entry);
    gtk_grid_attach(GTK_GRID(grid), cb_entry, 0, 3, 2, 1);

    GtkWidget* cb_pretty = gtk_check_button_new_with_label(
        "Pretty-print values (STL containers, structs)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_pretty),
                                 s.pretty_print);
    gtk_grid_attach(GTK_GRID(grid), cb_pretty, 0, 4, 2, 1);

    GtkWidget* cb_locals = gtk_check_button_new_with_label(
        "Refresh locals on every stop");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb_locals),
                                 s.print_all_locals);
    gtk_grid_attach(GTK_GRID(grid), cb_locals, 0, 5, 2, 1);

    GtkWidget* note = gtk_label_new(
        "Settings are saved to ~/.config/fdesigner/debugger.conf\n"
        "and apply to the next debug session (pretty-print and the\n"
        "break-on-main / stop-on-entry options take effect on Run).");
    gtk_label_set_xalign(GTK_LABEL(note), 0.0);
    gtk_grid_attach(GTK_GRID(grid), note, 0, 6, 2, 1);

    gtk_widget_show_all(d);
    if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT) {
        s.gdb_path = gtk_entry_get_text(GTK_ENTRY(gdb_entry));
        s.default_args = gtk_entry_get_text(GTK_ENTRY(args_entry));
        s.break_on_main =
            gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb_main));
        s.stop_on_entry =
            gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb_entry));
        s.pretty_print =
            gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb_pretty));
        s.print_all_locals =
            gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb_locals));
        s.save();
        gtk_widget_destroy(d);
        return true;  // accepted
    }
    gtk_widget_destroy(d);
    return false;  // cancelled
}

void debugger_settings_open(GtkWidget* parent) {
    load_settings_once();
    DebuggerSettings s = g_dbg_settings;
    if (settings_dialog(parent ? parent : g_dbg_window, s)) {
        g_dbg_settings = s;
        g_dbg_settings_loaded = true;
        if (g_session) g_session->set_settings(g_dbg_settings);
        if (g_dbg_window && g_args_entry && s.default_args.size() &&
            entry_text(g_args_entry).empty())
            gtk_entry_set_text(GTK_ENTRY(g_args_entry),
                               s.default_args.c_str());
    }
}

// --- window construction ------------------------------------------------

static GtkWidget* make_tree_view(GtkListStore* store,
                                 const char* cols[], int ncols,
                                 bool toggle_col) {
    GtkWidget* view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    for (int i = 0; i < ncols; ++i) {
        GtkCellRenderer* rend = nullptr;
        if (toggle_col && i == 2) {
            rend = gtk_cell_renderer_toggle_new();
            g_signal_connect(rend, "toggled", G_CALLBACK(on_bp_toggled),
                             nullptr);
            GtkTreeViewColumn* col =
                gtk_tree_view_column_new_with_attributes(
                    cols[i], rend, "active", i, nullptr);
            gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);
        } else {
            rend = gtk_cell_renderer_text_new();
            GtkTreeViewColumn* col =
                gtk_tree_view_column_new_with_attributes(
                    cols[i], rend, "text", i, nullptr);
            gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);
        }
    }
    return view;
}

void debugger_window_open(GtkWidget* parent) {
    load_settings_once();
    if (g_dbg_window) {
        gtk_window_present(GTK_WINDOW(g_dbg_window));
        return;
    }
    {
        std::string err;
        ensure_session(err);
    }
    GtkWidget* win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), "FDesigner Debugger");
    gtk_window_set_default_size(GTK_WINDOW(win), 960, 640);
    if (parent) gtk_window_set_transient_for(GTK_WINDOW(win),
                                             GTK_WINDOW(parent));
    g_dbg_window = win;
    // Keyboard shortcuts matching the toolbar tooltips: F5 Continue,
    // F10 Step Over, F11 Step Into. (Also lets scripted tests drive
    // stepping with xdotool key.)
    g_signal_connect(win, "key-press-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventKey* e,
                                    gpointer) -> gboolean {
                         if (!g_session) return FALSE;
                         std::string err;
                         const char* w = nullptr;
                         if (e->keyval == GDK_KEY_F5) w = "cont";
                         else if (e->keyval == GDK_KEY_F10) w = "over";
                         else if (e->keyval == GDK_KEY_F11) w = "into";
                         else return FALSE;
                         bool ok = false;
                         if (!std::strcmp(w, "cont"))
                             ok = g_session->cont(err);
                         else if (!std::strcmp(w, "over"))
                             ok = g_session->step_over(err);
                         else if (!std::strcmp(w, "into"))
                             ok = g_session->step_into(err);
                         if (ok) sync_ui_from_session();
                         else show_error(g_dbg_window, err);
                         return TRUE;
                     }),
                     nullptr);
    g_signal_connect(win, "destroy", G_CALLBACK(+[](GtkWidget*) {
                         g_dbg_window = nullptr;
                         // Keep the session alive for the menu toggles;
                         // the backend is cheap and holds breakpoints.
                     }),
                     nullptr);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 6);
    gtk_container_add(GTK_CONTAINER(win), vbox);

    // Toolbar
    GtkWidget* toolbar = gtk_toolbar_new();
    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);
    auto tool_btn = [&](const char* label, const char* tip,
                        GCallback cb, gpointer data) {
        GtkToolItem* b = gtk_tool_button_new(nullptr, label);
        gtk_tool_item_set_tooltip_text(b, tip);
        gtk_toolbar_insert(GTK_TOOLBAR(toolbar), b, -1);
        g_signal_connect(b, "clicked", cb, data);
        return GTK_WIDGET(b);
    };
    g_btn_run = tool_btn("Run", "Start the target under gdb", G_CALLBACK(on_run_clicked), nullptr);
    g_btn_cont = tool_btn("Continue", "Continue execution (F5)", G_CALLBACK(on_step_clicked), (gpointer)"cont");
    g_btn_over = tool_btn("Step Over", "Step over the current line (F10)", G_CALLBACK(on_step_clicked), (gpointer)"over");
    g_btn_into = tool_btn("Step Into", "Step into the called function (F11)", G_CALLBACK(on_step_clicked), (gpointer)"into");
    g_btn_out = tool_btn("Step Out", "Run until the current function returns", G_CALLBACK(on_step_clicked), (gpointer)"out");
    g_btn_stop = tool_btn("Stop", "Interrupt / kill the running target", G_CALLBACK(on_stop_clicked), nullptr);
    g_btn_restart = tool_btn("Restart", "Kill and run the target again", G_CALLBACK(on_restart_clicked), nullptr);
    GtkToolItem* sep = gtk_separator_tool_item_new();
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), sep, -1);
    GtkToolItem* settings_btn = gtk_tool_button_new(nullptr, "Settings");
    gtk_tool_item_set_tooltip_text(settings_btn, "Debugger settings (gdb path, break on main, pretty-print…) — also under Debug ▸ Debugger");
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), settings_btn, -1);
    g_signal_connect(settings_btn, "clicked",
                     G_CALLBACK(+[](GtkToolItem*, gpointer) {
                         debugger_settings_open(g_dbg_window);
                     }),
                     nullptr);

    // Target row
    GtkWidget* target_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(vbox), target_row, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(target_row), gtk_label_new("Target:"), FALSE,
                       FALSE, 0);
    g_target_entry = gtk_entry_new();
    gtk_widget_set_hexpand(g_target_entry, TRUE);
    gtk_box_pack_start(GTK_BOX(target_row), g_target_entry, TRUE, TRUE, 0);
    GtkWidget* browse = gtk_button_new_with_label("Browse…");
    g_signal_connect(browse, "clicked", G_CALLBACK(on_browse_clicked),
                     nullptr);
    gtk_box_pack_start(GTK_BOX(target_row), browse, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(target_row), gtk_label_new("Args:"), FALSE,
                       FALSE, 0);
    g_args_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(g_args_entry),
                       g_dbg_settings.default_args.c_str());
    gtk_widget_set_hexpand(g_args_entry, TRUE);
    gtk_box_pack_start(GTK_BOX(target_row), g_args_entry, TRUE, TRUE, 0);

    // Status + current source line
    g_status_label = gtk_label_new("Idle — enter a target and press Run");
    gtk_label_set_xalign(GTK_LABEL(g_status_label), 0.0);
    gtk_box_pack_start(GTK_BOX(vbox), g_status_label, FALSE, FALSE, 0);
    g_source_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(g_source_label), 0.0);
    gtk_box_pack_start(GTK_BOX(vbox), g_source_label, FALSE, FALSE, 0);

    // Middle: breakpoints | locals+watches | frames
    GtkWidget* mid = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), mid, TRUE, TRUE, 0);

    // Breakpoints pane
    {
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_paned_add1(GTK_PANED(mid), box);
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
        g_bp_entry = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(g_bp_entry),
                                       "file.cpp:123 or function");
        gtk_widget_set_hexpand(g_bp_entry, TRUE);
        gtk_box_pack_start(GTK_BOX(row), g_bp_entry, TRUE, TRUE, 0);
        GtkWidget* add = gtk_button_new_with_label("Add BP");
        g_signal_connect(add, "clicked", G_CALLBACK(on_bp_add_clicked),
                         nullptr);
        gtk_box_pack_start(GTK_BOX(row), add, FALSE, FALSE, 0);
        GtkWidget* add_full = gtk_button_new_with_label("Add…");
        gtk_widget_set_tooltip_text(
            add_full, "Add a breakpoint with handler options: conditional, "
                      "ignore count, logpoint, group");
        g_signal_connect(add_full, "clicked",
                         G_CALLBACK(on_bp_add_full_clicked), nullptr);
        gtk_box_pack_start(GTK_BOX(row), add_full, FALSE, FALSE, 0);
        GtkWidget* edit = gtk_button_new_with_label("Edit…");
        gtk_widget_set_tooltip_text(
            edit, "Edit the selected breakpoint's type, condition, "
                  "ignore count, log message or group");
        g_signal_connect(edit, "clicked", G_CALLBACK(on_bp_edit_clicked),
                         nullptr);
        gtk_box_pack_start(GTK_BOX(row), edit, FALSE, FALSE, 0);
        GtkWidget* del = gtk_button_new_with_label("Remove");
        g_signal_connect(del, "clicked", G_CALLBACK(on_bp_remove_clicked),
                         nullptr);
        gtk_box_pack_start(GTK_BOX(row), del, FALSE, FALSE, 0);
        g_bp_store = gtk_list_store_new(
            8, G_TYPE_INT, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_INT,
            G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_STRING);
        const char* cols[] = {"#",     "Location", "Enabled", "Hits",
                              "Type",  "Condition", "Ignore",  "Message"};
        GtkWidget* view = make_tree_view(g_bp_store, cols, 8, true);
        g_object_set_data(G_OBJECT(win), "bp_view", view);
        GtkWidget* sc = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_container_add(GTK_CONTAINER(sc), view);
        gtk_box_pack_start(GTK_BOX(box), sc, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(box),
                           gtk_label_new("Breakpoints (toggle Enabled to "
                                         "enable/disable; Add… / Edit… for "
                                         "conditional, ignore-count and "
                                         "logpoint handlers)"),
                           FALSE, FALSE, 0);
    }

    // Locals + watches pane
    GtkWidget* right = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_paned_add2(GTK_PANED(mid), right);
    {
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_paned_add1(GTK_PANED(right), box);
        gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Locals"), FALSE,
                           FALSE, 0);
        g_locals_store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING,
                                            G_TYPE_STRING);
        const char* cols[] = {"Name", "Type", "Value"};
        GtkWidget* view = make_tree_view(g_locals_store, cols, 3, false);
        GtkWidget* sc = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_container_add(GTK_CONTAINER(sc), view);
        gtk_box_pack_start(GTK_BOX(box), sc, TRUE, TRUE, 0);
        // Watch row
        GtkWidget* wrow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_box_pack_start(GTK_BOX(box), wrow, FALSE, FALSE, 0);
        g_watch_entry = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(g_watch_entry),
                                       "watch expression, e.g. sum + 1");
        gtk_widget_set_hexpand(g_watch_entry, TRUE);
        gtk_box_pack_start(GTK_BOX(wrow), g_watch_entry, TRUE, TRUE, 0);
        GtkWidget* wadd = gtk_button_new_with_label("Add Watch");
        g_signal_connect(wadd, "clicked", G_CALLBACK(on_watch_add_clicked),
                         nullptr);
        gtk_box_pack_start(GTK_BOX(wrow), wadd, FALSE, FALSE, 0);
        GtkWidget* wdel = gtk_button_new_with_label("Remove");
        g_signal_connect(wdel, "clicked", G_CALLBACK(on_watch_remove_clicked),
                         nullptr);
        gtk_box_pack_start(GTK_BOX(wrow), wdel, FALSE, FALSE, 0);
        g_watch_store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING,
                                           G_TYPE_STRING);
        GtkWidget* wview = make_tree_view(g_watch_store, cols, 3, false);
        g_object_set_data(G_OBJECT(win), "watch_view", wview);
        GtkWidget* wsc = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_container_add(GTK_CONTAINER(wsc), wview);
        gtk_box_pack_start(GTK_BOX(box), wsc, TRUE, TRUE, 0);
    }
    // Frames pane
    {
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_paned_add2(GTK_PANED(right), box);
        gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Stack"), FALSE,
                           FALSE, 0);
        g_frames_store = gtk_list_store_new(2, G_TYPE_INT, G_TYPE_STRING);
        const char* cols[] = {"#", "Frame"};
        GtkWidget* view = make_tree_view(g_frames_store, cols, 2, false);
        GtkWidget* sc = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_container_add(GTK_CONTAINER(sc), view);
        gtk_box_pack_start(GTK_BOX(box), sc, TRUE, TRUE, 0);
    }

    // Output pane
    gtk_box_pack_start(GTK_BOX(vbox),
                       gtk_label_new("Target output (stdout/stderr)"), FALSE,
                       FALSE, 0);
    g_output_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(g_output_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(g_output_view), TRUE);
    GtkWidget* osc = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(osc), 110);
    gtk_container_add(GTK_CONTAINER(osc), g_output_view);
    gtk_box_pack_start(GTK_BOX(vbox), osc, FALSE, TRUE, 0);

    gtk_widget_show_all(win);
    gtk_paned_set_position(GTK_PANED(mid), 380);
    g_timeout_add(150, on_dbg_poll, nullptr);
    // Restore saved breakpoints into the list (they apply on Run).
    if (g_session) {
        g_session->load_breakpoints();
        refresh_bp_list();
    }
    refresh_buttons();
}

// ------------------------------------------------------- gutter integration
// Code-view gutter toggles a file:line breakpoint in the shared session.
// See debugger_ui.h. The session is created on demand (restoring saved
// breakpoints) so a gutter click works before the debugger window opens.

namespace {
const Breakpoint* find_bp(const GdbSession& sess, const std::string& file,
                          int line) {
    std::string loc = file + ":" + std::to_string(line);
    for (const auto& b : sess.breakpoints()) {
        if (b.location == loc) return &b;
        if (!b.file.empty() && b.file == file && b.line == line) return &b;
    }
    return nullptr;
}
}  // namespace

bool debugger_toggle_breakpoint(const std::string& file, int line) {
    std::string err;
    if (!ensure_session(err)) return false;
    std::string loc = file + ":" + std::to_string(line);
    const Breakpoint* hit = find_bp(*g_session, file, line);
    if (hit) {
        if (hit->id == 0) {
            g_session->remove_staged_breakpoint(loc);
        } else if (g_session->is_alive()) {
            if (!g_session->remove_breakpoint(hit->id, err)) return true;
        } else {
            g_session->remove_staged_breakpoint(loc);
        }
        g_session->save_breakpoints();
        if (g_dbg_window) sync_ui_from_session();
        return false;
    }
    bool ok;
    if (g_session->is_alive()) {
        Breakpoint b;
        ok = g_session->add_breakpoint(loc, b, err);
    } else {
        ok = g_session->stage_breakpoint(loc);
    }
    if (ok) {
        g_session->save_breakpoints();
        if (g_dbg_window) sync_ui_from_session();
    }
    return ok;
}

bool debugger_has_breakpoint(const std::string& file, int line) {
    std::string err;
    if (!ensure_session(err)) return false;
    return find_bp(*g_session, file, line) != nullptr;
}

std::vector<int> debugger_breakpoint_lines(const std::string& file) {
    std::vector<int> out;
    std::string err;
    if (!ensure_session(err)) return out;
    for (const auto& b : g_session->breakpoints())
        if (!b.file.empty() && b.file == file && b.line > 0)
            out.push_back(b.line);
    return out;
}

// --- code-window integration: the code window reads the SAME session ---
// (never a second gdb) through these accessors, on its own poll tick.
void debugger_poll_events() {
    if (g_session) g_session->poll_events();
}

DbgState debugger_session_state() {
    return g_session ? g_session->state() : DbgState::Idle;
}

unsigned long debugger_stop_seq() {
    return g_session ? g_session->stop_seq() : 0;
}

StopInfo debugger_last_stop() {
    return g_session ? g_session->last_stop() : StopInfo();
}

std::vector<Variable> debugger_locals() {
    return g_session ? g_session->locals() : std::vector<Variable>();
}

void debugger_refresh_on_stop() {
    if (g_session) g_session->refresh_on_stop();
}

bool debugger_autorun(const std::string& exe, const std::string& bp_loc,
                      std::string& err) {
    if (!ensure_session(err)) return false;
    if (g_target_entry)
        gtk_entry_set_text(GTK_ENTRY(g_target_entry), exe.c_str());
    if (!bp_loc.empty()) {
        if (g_session->is_alive()) {
            Breakpoint b;
            if (!g_session->add_breakpoint(bp_loc, b, err)) return false;
        } else if (!g_session->stage_breakpoint(bp_loc)) {
            err = "not a breakpoint location: " + bp_loc;
            return false;
        }
        g_session->save_breakpoints();
    }
    if (g_session->state() == DbgState::Stopped ||
        g_session->state() == DbgState::Exited) {
        if (!g_session->restart(err)) return false;
    } else {
        if (!g_session->start(exe, "", err)) return false;
    }
    if (g_dbg_window) sync_ui_from_session();
    return true;
}

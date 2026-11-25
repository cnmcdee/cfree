// FDesigner stepping debugger — GTK window (separate from the main window).
// Open with debugger_window_open(parent). The window owns a GdbSession and
// polls it on a GTK timeout; all gdb traffic stays in the backend.
#pragma once
#include <gtk/gtk.h>

#include <string>
#include <vector>

// Show the debugger window (creates it on first call, raises it after).
void debugger_window_open(GtkWidget* parent);
// Open the debugger settings dialog (also reachable from the Debug menu's
// "Debugger" sub-menu and the Settings button in the debugger window).
void debugger_settings_open(GtkWidget* parent);
// Code-view gutter integration: toggle a file:line breakpoint in the
// shared debugger session (creates the session and restores persisted
// breakpoints first if needed, persists the change). Returns true when
// the breakpoint is now set. With gdb running the breakpoint is inserted
// / deleted live; otherwise it is staged for the next Run.
bool debugger_toggle_breakpoint(const std::string& file, int line);
bool debugger_has_breakpoint(const std::string& file, int line);
// 1-based lines in `file` that currently have breakpoints.
std::vector<int> debugger_breakpoint_lines(const std::string& file);
// True once the debugger window has been created (used by the GUI smoke test).
bool debugger_window_is_open();
// For tests: the settings currently in effect (loaded from disk at open).
#include "debugger.h"
DebuggerSettings debugger_current_settings();

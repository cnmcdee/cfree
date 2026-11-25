// FDesigner stepping debugger — GTK window (separate from the main window).
// Open with debugger_window_open(parent). The window owns a GdbSession and
// polls it on a GTK timeout; all gdb traffic stays in the backend.
#pragma once
#include <gtk/gtk.h>

#include <string>
#include <vector>

#include "debugger.h"  // DbgState, StopInfo, Variable, DebuggerSettings

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
// --- code-window integration (same shared session, no second gdb) ---
// Drain pending gdb events without needing the debugger window open.
// Safe to call from another UI poll; the debugger window's own poll
// drains the same queue.
void debugger_poll_events();
// Session state as seen by the code window's poll.
DbgState debugger_session_state();
// Monotonic stop counter; changes on every fresh *stopped event.
unsigned long debugger_stop_seq();
// Copy of the last stop record (frame, reason, exit code).
StopInfo debugger_last_stop();
// Locals of the current frame (refreshed by debugger_refresh_on_stop).
std::vector<Variable> debugger_locals();
// Refresh locals/frames/watches after a stop (best effort, no-op
// unless stopped). The debugger window's poll does this too; calling
// twice is harmless.
void debugger_refresh_on_stop();
// Test hook: start `exe` under the debugger, staging a breakpoint at
// `bp_loc` ("file:line", may be empty) first. Opens the debugger
// window if needed. Returns false + err when gdb refuses to start.
bool debugger_autorun(const std::string& exe, const std::string& bp_loc,
                      std::string& err);
// True once the debugger window has been created (used by the GUI smoke test).
bool debugger_window_is_open();
// For tests: the settings currently in effect (loaded from disk at open).
DebuggerSettings debugger_current_settings();

// FDesigner code view — GtkTextView showing generated code, with a
// gutter (line numbers, red-dot breakpoints, fold toggles) and syntax
// highlighting from a CodeTheme (src/theme.h).
//
// Folding covers every natural foldable unit the scanner can find:
//   * #region / #endregion blocks (nested)
//   * every matched-brace span: functions, if/else, loops, classes,
//     structs, namespaces, initializer blocks, any other {} block
//   * multi-line /* */ comments
//   * preprocessor #if / #ifdef / #ifndef ... #endif blocks
// Braces inside strings, character literals, raw strings and comments
// are ignored, so a '{' in a string never opens a fold. Unbalanced
// input fails gracefully: an unmatched '}' is ignored and an unclosed
// '{' / #region / #if at end of file simply produces no fold — never a
// crash and never a wrong (over-long) fold.
//
// Foldable regions are found by the pure function
// codeview_find_regions(); the widget hides folded lines with an
// invisible text tag (proven to collapse them to zero height), so file
// line numbers stay exact and breakpoint lines keep their meaning.
// The gutter is a GtkDrawingArea beside the text view inside one
// GtkScrolledWindow, so it scrolls in lockstep. Clicking the fold
// marker column toggles the fold; clicking anywhere else in the gutter
// toggles a breakpoint at that line via the callback installed with
// codeview_set_breakpoint_callback() — main wires it to
// debugger_toggle_breakpoint(), i.e. the real GdbSession machinery.
#pragma once

#include <gtk/gtk.h>

#include <functional>
#include <set>
#include <string>
#include <vector>

#include "theme.h"

// One foldable span. Lines are 0-based, inclusive at both ends; only
// spans longer than one line are reported (a single-line {} has
// nothing to hide). kind is "region", "brace", "comment" or "preproc".
// depth is the nesting depth (0 = outermost). label is a short human
// hint (region name, "#if ...", or the trimmed first line).
struct FoldRegion {
    int start_line = 0;
    int end_line = 0;
    std::string kind;
    int depth = 0;
    std::string label;
};

// Pure fold logic (no GTK) — unit-testable.
std::vector<std::string> codeview_split_lines(const std::string& text);
std::vector<FoldRegion> codeview_find_regions(
    const std::vector<std::string>& lines);
// True when 0-based `line` is strictly inside any region whose start
// line is in `folded_starts` (start/end lines themselves stay visible).
bool codeview_line_hidden(int line, const std::vector<FoldRegion>& regions,
                          const std::set<int>& folded_starts);
// 0-based lines that stay visible given the folded start lines.
std::vector<int> codeview_visible_lines(
    const std::vector<std::string>& lines,
    const std::set<int>& folded_starts);

// Widget: a GtkBox (gutter + scrolled text view). All line numbers in
// the widget API are 1-based file lines.
GtkWidget* codeview_new();
GtkWidget* codeview_text_view(GtkWidget* cv);  // inner GtkTextView
void codeview_set_text(GtkWidget* cv, const std::string& text,
                       const std::string& file_path);
std::string codeview_file_path(GtkWidget* cv);
int codeview_line_count(GtkWidget* cv);
void codeview_set_theme(GtkWidget* cv, const CodeTheme& theme);
CodeTheme codeview_theme(GtkWidget* cv);
// Highlight this file line as the current execution line (0 = none).
void codeview_set_current_line(GtkWidget* cv, int line_1based);
int codeview_current_line(GtkWidget* cv);
// Breakpoint dots (1-based file lines). The widget does not own the
// debugger; toggling goes through the callback, which returns true
// when the breakpoint is now set. With no callback, toggling just
// flips the local dot (test mode).
void codeview_set_breakpoint_callback(
    GtkWidget* cv,
    const std::function<bool(const std::string&, int)>& cb);
void codeview_set_breakpoint_lines(GtkWidget* cv,
                                   const std::set<int>& lines_1based);
std::set<int> codeview_breakpoint_lines(GtkWidget* cv);
// Fold state (0-based start lines in the pure API; 1-based here).
std::vector<FoldRegion> codeview_regions(GtkWidget* cv);
bool codeview_toggle_fold_at(GtkWidget* cv, int line_1based);
bool codeview_is_folded(GtkWidget* cv, int line_1based);
// Test helpers: simulate a gutter click at a file line. `in_fold_zone`
// mimics clicking the [-]/[+] marker column.
void codeview_gutter_click(GtkWidget* cv, int line_1based, bool in_fold_zone);
void codeview_toggle_breakpoint_at(GtkWidget* cv, int line_1based);

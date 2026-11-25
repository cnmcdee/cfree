// FDesigner code view — implementation. See codeview.h.
#include "codeview.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>

namespace {

// ------------------------------------------------------------ pure logic

std::string trim2(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

}  // namespace

std::vector<std::string> codeview_split_lines(const std::string& text) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find('\n', start);
        if (nl == std::string::npos) {
            if (start < text.size()) out.push_back(text.substr(start));
            break;
        }
        std::string ln = text.substr(start, nl - start);
        if (!ln.empty() && ln.back() == '\r') ln.pop_back();
        out.push_back(ln);
        start = nl + 1;
        if (start == text.size()) break;  // trailing \n: no phantom line
    }
    return out;
}

std::vector<FoldRegion> codeview_find_regions(
    const std::vector<std::string>& lines) {
    // Rejoin for a single character stream so strings/comments/braces
    // are tracked across line boundaries with exact line numbers.
    std::string text;
    for (const auto& l : lines) { text += l; text += '\n'; }
    std::vector<FoldRegion> out;
    std::vector<int> brace_stack;
    std::vector<std::pair<int, std::string>> region_stack;  // start, label
    std::vector<int> if_stack;
    auto open_count = [&]() {
        return (int)(brace_stack.size() + region_stack.size() +
                     if_stack.size());
    };
    size_t n = text.size(), i = 0;
    int line = 0;  // 0-based line of text[i]
    bool line_start = true;
    bool in_block_comment = false, in_line_comment = false;
    bool in_string = false, in_char = false;
    int comment_start = -1;
    while (i < n) {
        char c = text[i];
        if (c == '\n') {
            in_line_comment = false;
            in_string = false;  // unterminated string ends at EOL
            in_char = false;
            ++line;
            line_start = true;
            ++i;
            continue;
        }
        if (in_line_comment) { ++i; continue; }
        if (in_block_comment) {
            if (c == '*' && i + 1 < n && text[i + 1] == '/') {
                in_block_comment = false;
                if (line > comment_start)
                    out.push_back({comment_start, line, "comment",
                                   open_count(), "/* ... */"});
                i += 2;
                line_start = false;
                continue;
            }
            ++i;
            continue;
        }
        if (in_string) {
            if (c == '\\' && i + 1 < n) { i += 2; continue; }
            if (c == '"') in_string = false;
            ++i;
            continue;
        }
        if (in_char) {
            if (c == '\\' && i + 1 < n) { i += 2; continue; }
            if (c == '\'') in_char = false;
            ++i;
            continue;
        }
        if (line_start && (c == ' ' || c == '\t' || c == '\r')) { ++i; continue; }
        if (line_start && c == '#') {
            // Preprocessor directive: parse the word, handle the
            // foldable ones, then skip the whole line (no braces counted
            // inside a directive line).
            size_t j = i + 1;
            while (j < n && (text[j] == ' ' || text[j] == '\t')) ++j;
            size_t ws = j;
            while (j < n && std::isalpha((unsigned char)text[j])) ++j;
            std::string word = text.substr(ws, j - ws);
            std::string rest;
            if (word == "region" || word == "if" || word == "ifdef" ||
                word == "ifndef") {
                size_t eol = text.find('\n', j);
                rest = trim2(text.substr(
                    j, (eol == std::string::npos ? n : eol) - j));
                if (rest.size() > 48) rest = rest.substr(0, 48);
            }
            if (word == "region") {
                region_stack.push_back(
                    {line, rest.empty() ? std::string("#region")
                                        : "#region " + rest});
            } else if (word == "endregion") {
                if (!region_stack.empty()) {
                    auto st = region_stack.back();
                    region_stack.pop_back();
                    if (line > st.first)
                        out.push_back({st.first, line, "region",
                                       (int)(brace_stack.size() +
                                             region_stack.size() +
                                             if_stack.size()),
                                       st.second});
                }
            } else if (word == "if" || word == "ifdef" || word == "ifndef") {
                if_stack.push_back(line);
            } else if (word == "endif") {
                if (!if_stack.empty()) {
                    int st = if_stack.back();
                    if_stack.pop_back();
                    if (line > st)
                        out.push_back({st, line, "preproc",
                                       (int)(brace_stack.size() +
                                             region_stack.size() +
                                             if_stack.size()),
                                       "#" + word + " " + rest});
                }
            }
            // Skip to end of line.
            while (i < n && text[i] != '\n') ++i;
            continue;
        }
        line_start = false;
        if (c == '/' && i + 1 < n && text[i + 1] == '/') {
            in_line_comment = true;
            i += 2;
            continue;
        }
        if (c == '/' && i + 1 < n && text[i + 1] == '*') {
            in_block_comment = true;
            comment_start = line;
            i += 2;
            continue;
        }
        if (c == '"') {
            // Raw string? R"delim( ... )delim" — also u8R/uR/UR/LR.
            bool raw = false;
            std::string delim;
            if (i >= 1 && text[i - 1] == 'R' &&
                (i < 2 || !std::isalnum((unsigned char)text[i - 2]) ||
                 text[i - 2] == 'u' || text[i - 2] == 'U' ||
                 text[i - 2] == 'L')) {
                size_t p = i + 1;
                while (p < n && text[p] != '(' && text[p] != '\n' &&
                       text[p] != ' ' && text[p] != '\t')
                    ++p;
                if (p < n && text[p] == '(') {
                    raw = true;
                    delim = text.substr(i + 1, p - (i + 1));
                }
            }
            if (raw) {
                std::string close = ")" + delim + "\"";
                size_t f = text.find(close, i + 1);
                if (f == std::string::npos) {
                    i = n;  // unterminated raw string: rest is string
                    break;
                }
                for (size_t k = i; k < f + close.size(); ++k)
                    if (text[k] == '\n') ++line;
                i = f + close.size();
                continue;
            }
            in_string = true;
            ++i;
            continue;
        }
        if (c == '\'') {
            // Digit separator (1'000) is not a character literal.
            bool sep = i > 0 && (std::isalnum((unsigned char)text[i - 1]) ||
                                 text[i - 1] == '_');
            if (!sep) in_char = true;
            ++i;
            continue;
        }
        if (c == '{') {
            brace_stack.push_back(line);
            ++i;
            continue;
        }
        if (c == '}') {
            if (!brace_stack.empty()) {
                int st = brace_stack.back();
                brace_stack.pop_back();
                if (line > st) {
                    std::string label = trim2(lines[(size_t)st]);
                    if (label.size() > 48) label = label.substr(0, 48);
                    out.push_back({st, line, "brace",
                                   (int)(brace_stack.size() +
                                         region_stack.size() +
                                         if_stack.size()),
                                   label});
                }
            }
            // Unmatched '}': ignored — graceful, no fold, no crash.
            ++i;
            continue;
        }
        ++i;
    }
    // Unclosed '{', #region, #if, /* at EOF: dropped, no wrong fold.
    // Depths: recompute properly — a region's depth is the number of
    // other regions strictly containing it.
    for (auto& r : out) {
        int d = 0;
        for (const auto& o : out)
            if (&o != &r && o.start_line < r.start_line &&
                o.end_line > r.end_line)
                ++d;
        r.depth = d;
    }
    std::sort(out.begin(), out.end(), [](const FoldRegion& a,
                                         const FoldRegion& b) {
        if (a.start_line != b.start_line) return a.start_line < b.start_line;
        return a.end_line > b.end_line;  // outermost first at same start
    });
    return out;
}

bool codeview_line_hidden(int line, const std::vector<FoldRegion>& regions,
                          const std::set<int>& folded_starts) {
    for (const auto& r : regions)
        if (folded_starts.count(r.start_line) && line > r.start_line &&
            line < r.end_line)
            return true;
    return false;
}

std::vector<int> codeview_visible_lines(
    const std::vector<std::string>& lines,
    const std::set<int>& folded_starts) {
    std::vector<FoldRegion> regions = codeview_find_regions(lines);
    std::vector<int> out;
    for (int l = 0; l < (int)lines.size(); ++l)
        if (!codeview_line_hidden(l, regions, folded_starts)) out.push_back(l);
    return out;
}

namespace {

// ------------------------------------------------------------ widget

struct CodeViewData {
    GtkWidget* box = nullptr;
    GtkWidget* gutter = nullptr;
    GtkWidget* scroll = nullptr;
    GtkWidget* view = nullptr;
    std::string file_path;
    std::vector<std::string> lines;
    std::vector<FoldRegion> regions;
    std::set<int> folded;       // 0-based region start lines
    std::set<int> breakpoints;  // 1-based file lines
    int current_line = 0;       // 1-based, 0 = none
    CodeTheme theme;
    std::function<bool(const std::string&, int)> bp_cb;
    GtkTextTag* fold_tag = nullptr;
    GtkTextTag* cur_tag = nullptr;
};

CodeViewData* data_of(GtkWidget* cv) {
    return (CodeViewData*)g_object_get_data(G_OBJECT(cv), "codeview-data");
}

void free_data(gpointer p) { delete (CodeViewData*)p; }

bool parse_hex(const std::string& s, double& r, double& g, double& b) {
    if (s.size() != 7 || s[0] != '#') return false;
    auto hv = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    int v[6];
    for (int k = 0; k < 6; ++k) {
        v[k] = hv(s[1 + k]);
        if (v[k] < 0) return false;
    }
    r = (v[0] * 16 + v[1]) / 255.0;
    g = (v[2] * 16 + v[3]) / 255.0;
    b = (v[4] * 16 + v[5]) / 255.0;
    return true;
}

bool is_dark_theme(const CodeTheme& t) {
    double r = 1, g = 1, b = 1;
    if (!parse_hex(t.background, r, g, b)) return false;
    return (0.299 * r + 0.587 * g + 0.114 * b) < 0.5;
}

void apply_folds(CodeViewData* d) {
    GtkTextBuffer* buf =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(d->view));
    if (!d->fold_tag) return;
    GtkTextIter a, b;
    gtk_text_buffer_get_bounds(buf, &a, &b);
    gtk_text_buffer_remove_tag(buf, d->fold_tag, &a, &b);
    for (int start : d->folded) {
        for (const auto& r : d->regions) {
            if (r.start_line != start) continue;
            if (r.end_line <= r.start_line + 1) continue;  // nothing to hide
            GtkTextIter sa, sb;
            gtk_text_buffer_get_iter_at_line(buf, &sa, r.start_line + 1);
            gtk_text_buffer_get_iter_at_line(buf, &sb, r.end_line);
            gtk_text_buffer_apply_tag(buf, d->fold_tag, &sa, &sb);
        }
    }
}

void apply_current_line(CodeViewData* d) {
    GtkTextBuffer* buf =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(d->view));
    if (!d->cur_tag) return;
    GtkTextIter a, b;
    gtk_text_buffer_get_bounds(buf, &a, &b);
    gtk_text_buffer_remove_tag(buf, d->cur_tag, &a, &b);
    if (d->current_line >= 1 &&
        d->current_line <= gtk_text_buffer_get_line_count(buf)) {
        GtkTextIter sa, sb;
        gtk_text_buffer_get_iter_at_line(buf, &sa, d->current_line - 1);
        if (d->current_line < gtk_text_buffer_get_line_count(buf))
            gtk_text_buffer_get_iter_at_line(buf, &sb, d->current_line);
        else
            gtk_text_buffer_get_end_iter(buf, &sb);
        gtk_text_buffer_apply_tag(buf, d->cur_tag, &sa, &sb);
    }
}

void queue_redraw(CodeViewData* d) {
    gtk_widget_queue_draw(d->gutter);
    gtk_widget_queue_draw(d->view);
}

bool region_starts_at(CodeViewData* d, int line0) {
    for (const auto& r : d->regions)
        if (r.start_line == line0) return true;
    return false;
}

// Buffer line (0-based) at a gutter y in widget coordinates; -1 if none.
int gutter_line_at_y(CodeViewData* d, double y) {
    GtkTextBuffer* buf =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(d->view));
    int count = gtk_text_buffer_get_line_count(buf);
    for (int ln = 0; ln < count; ++ln) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_line(buf, &it, ln);
        gint ly, lh;
        gtk_text_view_get_line_yrange(GTK_TEXT_VIEW(d->view), &it, &ly, &lh);
        if (lh > 0 && y >= ly && y < ly + lh) return ln;
    }
    return -1;
}

void toggle_fold_line(CodeViewData* d, int line0) {
    if (!region_starts_at(d, line0)) return;
    if (d->folded.count(line0)) d->folded.erase(line0);
    else d->folded.insert(line0);
    apply_folds(d);
    queue_redraw(d);
}

void toggle_bp_line(CodeViewData* d, int line1) {
    bool now_set;
    if (d->bp_cb) now_set = d->bp_cb(d->file_path, line1);
    else now_set = !d->breakpoints.count(line1);
    if (now_set) d->breakpoints.insert(line1);
    else d->breakpoints.erase(line1);
    queue_redraw(d);
}

gboolean on_gutter_draw(GtkWidget* gutter, cairo_t* cr, gpointer user) {
    CodeViewData* d = (CodeViewData*)user;
    GtkAllocation alloc;
    gtk_widget_get_allocation(gutter, &alloc);
    bool dark = is_dark_theme(d->theme);
    if (dark) cairo_set_source_rgb(cr, 0.17, 0.17, 0.17);
    else cairo_set_source_rgb(cr, 0.93, 0.93, 0.93);
    cairo_rectangle(cr, 0, 0, alloc.width, alloc.height);
    cairo_fill(cr);
    // Divider between gutter and code.
    cairo_set_source_rgb(cr, dark ? 0.35 : 0.75, dark ? 0.35 : 0.75,
                         dark ? 0.35 : 0.75);
    cairo_rectangle(cr, alloc.width - 1, 0, 1, alloc.height);
    cairo_fill(cr);

    double fr, fg, fb;
    if (!parse_hex(d->theme.foreground, fr, fg, fb)) { fr = fg = fb = 0.2; }
    double cr_, cg, cb;
    if (!parse_hex(d->theme.current_line, cr_, cg, cb)) { cr_ = cg = cb = 0.9; }

    GtkTextBuffer* buf =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(d->view));
    int count = gtk_text_buffer_get_line_count(buf);
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 11);
    for (int ln = 0; ln < count; ++ln) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_line(buf, &it, ln);
        gint y, h;
        gtk_text_view_get_line_yrange(GTK_TEXT_VIEW(d->view), &it, &y, &h);
        if (h <= 0) continue;  // folded away
        if (d->current_line == ln + 1) {
            cairo_set_source_rgb(cr, cr_, cg, cb);
            cairo_rectangle(cr, 0, y, alloc.width, h);
            cairo_fill(cr);
        }
        bool is_start = region_starts_at(d, ln);
        if (is_start) {
            bool folded = d->folded.count(ln) != 0;
            cairo_set_source_rgb(cr, dark ? 0.75 : 0.4, dark ? 0.75 : 0.4,
                                 dark ? 0.75 : 0.4);
            cairo_rectangle(cr, 3, y + (h - 12) / 2, 12, 12);
            cairo_stroke(cr);
            cairo_move_to(cr, 6, y + (h - 12) / 2 + 9);
            cairo_show_text(cr, folded ? "+" : "-");
        }
        if (d->breakpoints.count(ln + 1)) {
            cairo_set_source_rgb(cr, 0.88, 0.11, 0.14);
            cairo_arc(cr, 26, y + h / 2.0, 5, 0, 2 * 3.14159265);
            cairo_fill(cr);
        }
        char num[16];
        std::snprintf(num, sizeof num, "%d", ln + 1);
        cairo_text_extents_t ext;
        cairo_text_extents(cr, num, &ext);
        cairo_set_source_rgba(cr, fr, fg, fb, 0.65);
        cairo_move_to(cr, alloc.width - 6 - ext.width, y + h - 4);
        cairo_show_text(cr, num);
    }
    return FALSE;
}

gboolean on_gutter_press(GtkWidget*, GdkEventButton* ev, gpointer user) {
    CodeViewData* d = (CodeViewData*)user;
    if (ev->button != 1) return FALSE;
    int ln = gutter_line_at_y(d, ev->y);
    if (ln < 0) return FALSE;
    if (ev->x < 18) {
        if (region_starts_at(d, ln)) toggle_fold_line(d, ln);
    } else {
        toggle_bp_line(d, ln + 1);
    }
    return TRUE;
}

void on_view_size_allocate(GtkWidget*, GtkAllocation* alloc,
                           gpointer user) {
    CodeViewData* d = (CodeViewData*)user;
    gtk_widget_set_size_request(d->gutter, 76, alloc->height);
}

void on_scroll_changed(GtkAdjustment*, gpointer user) {
    CodeViewData* d = (CodeViewData*)user;
    gtk_widget_queue_draw(d->gutter);
}

}  // namespace

// ------------------------------------------------------------ public API

GtkWidget* codeview_new() {
    CodeViewData* d = new CodeViewData();
    d->theme = theme_current();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    d->box = box;
    d->gutter = gtk_drawing_area_new();
    gtk_widget_set_size_request(d->gutter, 76, 100);
    gtk_widget_add_events(d->gutter, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(d->gutter, "draw", G_CALLBACK(on_gutter_draw), d);
    g_signal_connect(d->gutter, "button-press-event",
                     G_CALLBACK(on_gutter_press), d);
    d->view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(d->view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(d->view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(d->view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(d->view), GTK_WRAP_NONE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(d->view), 8);
    d->scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(d->scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    GtkWidget* inner = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(inner), d->gutter, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(inner), d->view, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(d->scroll), inner);
    gtk_box_pack_start(GTK_BOX(box), d->scroll, TRUE, TRUE, 0);
    g_signal_connect(d->view, "size-allocate",
                     G_CALLBACK(on_view_size_allocate), d);
    GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(
        GTK_SCROLLED_WINDOW(d->scroll));
    g_signal_connect(vadj, "value-changed", G_CALLBACK(on_scroll_changed), d);
    g_object_set_data_full(G_OBJECT(box), "codeview-data", d, free_data);
    return box;
}

GtkWidget* codeview_text_view(GtkWidget* cv) {
    CodeViewData* d = data_of(cv);
    return d ? d->view : nullptr;
}

void codeview_set_text(GtkWidget* cv, const std::string& text,
                       const std::string& file_path) {
    CodeViewData* d = data_of(cv);
    if (!d) return;
    d->file_path = file_path;
    d->lines = codeview_split_lines(text);
    d->regions = codeview_find_regions(d->lines);
    d->folded.clear();
    d->breakpoints.clear();
    d->current_line = 0;
    GtkTextBuffer* buf =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(d->view));
    gtk_text_buffer_set_text(buf, text.c_str(), -1);
    GtkTextTagTable* table = gtk_text_buffer_get_tag_table(buf);
    d->fold_tag = gtk_text_tag_table_lookup(table, "theme-fold-hidden");
    if (!d->fold_tag)
        d->fold_tag = gtk_text_buffer_create_tag(buf, "theme-fold-hidden",
                                                 "invisible", TRUE, NULL);
    d->cur_tag = gtk_text_tag_table_lookup(table, "theme-current-line");
    if (!d->cur_tag)
        d->cur_tag = gtk_text_buffer_create_tag(
            buf, "theme-current-line", "background",
            d->theme.current_line.c_str(), NULL);
    else
        g_object_set(d->cur_tag, "background", d->theme.current_line.c_str(),
                     NULL);
    theme_highlight_buffer(buf, d->theme);
    theme_apply_to_view(d->view, d->theme);
    apply_folds(d);
    queue_redraw(d);
}

std::string codeview_file_path(GtkWidget* cv) {
    CodeViewData* d = data_of(cv);
    return d ? d->file_path : "";
}

int codeview_line_count(GtkWidget* cv) {
    CodeViewData* d = data_of(cv);
    if (!d) return 0;
    // File lines, not the buffer count: a trailing newline makes the
    // buffer report one extra empty line.
    return (int)d->lines.size();
}

void codeview_set_theme(GtkWidget* cv, const CodeTheme& theme) {
    CodeViewData* d = data_of(cv);
    if (!d) return;
    d->theme = theme;
    GtkTextBuffer* buf =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(d->view));
    if (d->cur_tag)
        g_object_set(d->cur_tag, "background", theme.current_line.c_str(),
                     NULL);
    theme_highlight_buffer(buf, theme);
    theme_apply_to_view(d->view, theme);
    queue_redraw(d);
}

CodeTheme codeview_theme(GtkWidget* cv) {
    CodeViewData* d = data_of(cv);
    return d ? d->theme : CodeTheme();
}

void codeview_set_current_line(GtkWidget* cv, int line_1based) {
    CodeViewData* d = data_of(cv);
    if (!d) return;
    d->current_line = line_1based;
    apply_current_line(d);
    queue_redraw(d);
}

int codeview_current_line(GtkWidget* cv) {
    CodeViewData* d = data_of(cv);
    return d ? d->current_line : 0;
}

void codeview_set_breakpoint_callback(
    GtkWidget* cv,
    const std::function<bool(const std::string&, int)>& cb) {
    CodeViewData* d = data_of(cv);
    if (d) d->bp_cb = cb;
}

void codeview_set_breakpoint_lines(GtkWidget* cv,
                                   const std::set<int>& lines_1based) {
    CodeViewData* d = data_of(cv);
    if (!d) return;
    d->breakpoints = lines_1based;
    queue_redraw(d);
}

std::set<int> codeview_breakpoint_lines(GtkWidget* cv) {
    CodeViewData* d = data_of(cv);
    return d ? d->breakpoints : std::set<int>();
}

std::vector<FoldRegion> codeview_regions(GtkWidget* cv) {
    CodeViewData* d = data_of(cv);
    return d ? d->regions : std::vector<FoldRegion>();
}

bool codeview_toggle_fold_at(GtkWidget* cv, int line_1based) {
    CodeViewData* d = data_of(cv);
    if (!d) return false;
    int line0 = line_1based - 1;
    if (!region_starts_at(d, line0)) return false;
    toggle_fold_line(d, line0);
    return true;
}

bool codeview_is_folded(GtkWidget* cv, int line_1based) {
    CodeViewData* d = data_of(cv);
    return d && d->folded.count(line_1based - 1) != 0;
}

void codeview_gutter_click(GtkWidget* cv, int line_1based, bool in_fold_zone) {
    CodeViewData* d = data_of(cv);
    if (!d) return;
    int line0 = line_1based - 1;
    if (in_fold_zone && region_starts_at(d, line0)) toggle_fold_line(d, line0);
    else if (!in_fold_zone) toggle_bp_line(d, line_1based);
}

void codeview_toggle_breakpoint_at(GtkWidget* cv, int line_1based) {
    CodeViewData* d = data_of(cv);
    if (d) toggle_bp_line(d, line_1based);
}

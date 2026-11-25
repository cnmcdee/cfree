// FDesigner code-view color themes — implementation. See theme.h.
#include "theme.h"

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>

namespace {

std::string home_dir() {
    const char* h = std::getenv("HOME");
    return h ? h : "";
}

bool ensure_parent_dir(const std::string& path) {
    size_t slash = path.rfind('/');
    if (slash == std::string::npos) return true;
    std::string dir = path.substr(0, slash);
    std::string cur;
    for (size_t i = 1; i <= dir.size(); ++i) {
        if (i == dir.size() || dir[i] == '/') {
            cur = dir.substr(0, i);
            if (!cur.empty()) ::mkdir(cur.c_str(), 0755);
        }
    }
    return true;
}

std::string trim_s(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// C++ type words get the "type" colour; the rest of the reserved words
// get "keyword". Words in neither set stay plain foreground.
const std::set<std::string>& type_words() {
    static const std::set<std::string> t = {
        "bool", "char", "char8_t", "char16_t", "char32_t", "double", "float",
        "int", "long", "short", "signed", "unsigned", "void", "wchar_t",
        "size_t", "ssize_t", "string", "vector", "map", "set", "list",
        "array", "pair", "tuple", "optional", "uint8_t", "uint16_t",
        "uint32_t", "uint64_t", "int8_t", "int16_t", "int32_t", "int64_t",
        "nullptr_t", "GtkWidget", "GtkTextBuffer", "GtkTextIter",
    };
    return t;
}

const std::set<std::string>& keyword_words() {
    static const std::set<std::string> k = {
        "alignas", "alignof", "and", "and_eq", "asm", "auto", "break",
        "case", "catch", "class", "concept", "const", "consteval",
        "constexpr", "constinit", "continue", "decltype", "default",
        "delete", "do", "else", "enum", "explicit", "export", "extern",
        "false", "for", "friend", "goto", "if", "inline", "mutable",
        "namespace", "new", "noexcept", "not", "not_eq", "operator", "or",
        "or_eq", "private", "protected", "public", "register",
        "reinterpret_cast", "requires", "return", "sizeof", "static",
        "static_assert", "static_cast", "struct", "switch", "template",
        "this", "thread_local", "throw", "true", "try", "typedef",
        "typeid", "typename", "union", "using", "virtual", "volatile",
        "while", "xor", "xor_eq", "override", "final",
    };
    return k;
}

bool ident_start(char c) {
    return std::isalpha((unsigned char)c) || c == '_';
}
bool ident_char(char c) {
    return std::isalnum((unsigned char)c) || c == '_';
}
bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_hex(char c) {
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

CodeTheme g_current;
bool g_current_loaded = false;

}  // namespace

bool CodeTheme::valid_color(const std::string& s) {
    if (s.size() != 7 || s[0] != '#') return false;
    for (size_t i = 1; i < 7; ++i)
        if (!is_hex(s[i])) return false;
    return true;
}

std::vector<std::string> CodeTheme::builtin_names() {
    return {"Light", "Dark", "Monokai", "Solarized Dark"};
}

CodeTheme CodeTheme::builtin(const std::string& name) {
    CodeTheme t;
    if (name == "Dark") {
        t.name = "Dark";
        t.background = "#1e1e1e"; t.foreground = "#d4d4d4";
        t.keyword = "#569cd6"; t.type = "#4ec9b0"; t.string = "#ce9178";
        t.comment = "#6a9955"; t.number = "#b5cea8";
        t.preprocessor = "#c586c0"; t.current_line = "#2a2d2e";
    } else if (name == "Monokai") {
        t.name = "Monokai";
        t.background = "#272822"; t.foreground = "#f8f8f2";
        t.keyword = "#f92672"; t.type = "#66d9ef"; t.string = "#e6db74";
        t.comment = "#75715e"; t.number = "#ae81ff";
        t.preprocessor = "#fd971f"; t.current_line = "#3e3d32";
    } else if (name == "Solarized Dark") {
        t.name = "Solarized Dark";
        t.background = "#002b36"; t.foreground = "#839496";
        t.keyword = "#859900"; t.type = "#2aa198"; t.string = "#b58900";
        t.comment = "#586e75"; t.number = "#d33682";
        t.preprocessor = "#cb4b16"; t.current_line = "#073642";
    } else {
        t.name = "Light";  // default, incl. unknown names
    }
    return t;
}

bool CodeTheme::is_builtin(const std::string& name) {
    for (const auto& n : builtin_names())
        if (n == name) return true;
    return false;
}

std::string CodeTheme::default_path() {
    const char* e = std::getenv("FDESIGNER_THEME_CONF");
    if (e && *e) return e;
    return home_dir() + "/.config/fdesigner/theme.conf";
}

bool CodeTheme::save(const std::string& path) const {
    std::string p = path.empty() ? default_path() : path;
    ensure_parent_dir(p);
    std::ofstream f(p);
    if (!f) return false;
    f << "theme=" << name << "\n"
      << "background=" << background << "\n"
      << "foreground=" << foreground << "\n"
      << "keyword=" << keyword << "\n"
      << "type=" << type << "\n"
      << "string=" << string << "\n"
      << "comment=" << comment << "\n"
      << "number=" << number << "\n"
      << "preprocessor=" << preprocessor << "\n"
      << "current_line=" << current_line << "\n";
    return (bool)f;
}

bool CodeTheme::load(const std::string& path) {
    std::string p = path.empty() ? default_path() : path;
    std::ifstream f(p);
    if (!f) return false;
    std::map<std::string, std::string> kv;
    std::string line;
    while (std::getline(f, line)) {
        std::string t = trim_s(line);
        if (t.empty() || t[0] == '#') continue;
        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        kv[trim_s(t.substr(0, eq))] = trim_s(t.substr(eq + 1));
    }
    std::string nm = kv.count("theme") ? kv["theme"] : "Light";
    CodeTheme base = (nm == "Custom") ? builtin("Dark") : builtin(nm);
    base.name = nm;  // keep the stored name, even if customised
    auto take = [&](const char* key, std::string& field) {
        auto it = kv.find(key);
        if (it != kv.end() && valid_color(it->second)) field = it->second;
    };
    take("background", base.background);
    take("foreground", base.foreground);
    take("keyword", base.keyword);
    take("type", base.type);
    take("string", base.string);
    take("comment", base.comment);
    take("number", base.number);
    take("preprocessor", base.preprocessor);
    take("current_line", base.current_line);
    *this = base;
    return true;
}

std::string CodeTheme::css() const {
    // GTK 3.20+ draws the text area from the "text" subnode of the
    // textview: setting background/color on "textview" alone leaves
    // the text area at the default white. Set both nodes.
    return "textview text { background-color: " + background +
           "; color: " + foreground + "; }"
           " textview { background-color: " + background +
           "; color: " + foreground + "; }";
}

CodeTheme theme_current() {
    if (!g_current_loaded) {
        g_current = CodeTheme();
        g_current.load();  // missing file: defaults stand
        g_current_loaded = true;
    }
    return g_current;
}

void theme_set_current(const CodeTheme& t) {
    g_current = t;
    g_current_loaded = true;
    g_current.save();
}

// ------------------------------------------------------------ tokeniser

std::vector<HlSpan> theme_tokenize(const std::string& code) {
    std::vector<HlSpan> out;
    size_t n = code.size(), i = 0;
    bool line_start = true;  // only whitespace seen so far on this line
    while (i < n) {
        char c = code[i];
        if (c == '\n') { line_start = true; ++i; continue; }
        if (line_start && (c == ' ' || c == '\t' || c == '\r')) { ++i; continue; }
        if (line_start && c == '#') {
            size_t s = i;
            while (i < n && code[i] != '\n') ++i;
            out.push_back({s, i, "preprocessor"});
            continue;
        }
        line_start = false;
        if (c == '/' && i + 1 < n && code[i + 1] == '/') {
            size_t s = i;
            while (i < n && code[i] != '\n') ++i;
            out.push_back({s, i, "comment"});
            continue;
        }
        if (c == '/' && i + 1 < n && code[i + 1] == '*') {
            size_t s = i;
            i += 2;
            while (i + 1 < n && !(code[i] == '*' && code[i + 1] == '/')) ++i;
            if (i + 1 < n) i += 2;
            else i = n;
            out.push_back({s, i, "comment"});
            continue;
        }
        if (c == '"') {
            size_t s = i++;
            while (i < n && code[i] != '"' && code[i] != '\n') {
                if (code[i] == '\\' && i + 1 < n) ++i;
                ++i;
            }
            if (i < n && code[i] == '"') ++i;
            out.push_back({s, i, "string"});
            continue;
        }
        if (c == '\'') {
            // A ' between identifier/digit chars is a C++14 digit
            // separator (1'000), not a character literal.
            bool sep = i > 0 && (ident_char(code[i - 1]));
            if (!sep) {
                size_t s = i++;
                while (i < n && code[i] != '\'' && code[i] != '\n') {
                    if (code[i] == '\\' && i + 1 < n) ++i;
                    ++i;
                }
                if (i < n && code[i] == '\'') ++i;
                out.push_back({s, i, "string"});
                continue;
            }
            ++i;
            continue;
        }
        if (is_digit(c) || (c == '.' && i + 1 < n && is_digit(code[i + 1]))) {
            size_t s = i;
            if (c == '0' && i + 1 < n &&
                (code[i + 1] == 'x' || code[i + 1] == 'X')) {
                i += 2;
                while (i < n && (is_hex(code[i]) || code[i] == '\'')) ++i;
            } else {
                while (i < n &&
                       (is_digit(code[i]) || code[i] == '.' ||
                        code[i] == '_' || code[i] == '\'' ||
                        code[i] == 'e' || code[i] == 'E' ||
                        ((code[i] == '+' || code[i] == '-') &&
                         i > s && (code[i - 1] == 'e' || code[i - 1] == 'E'))))
                    ++i;
                while (i < n && std::isalpha((unsigned char)code[i])) ++i;
            }
            out.push_back({s, i, "number"});
            continue;
        }
        if (ident_start(c)) {
            size_t s = i;
            while (i < n && ident_char(code[i])) ++i;
            std::string w = code.substr(s, i - s);
            if (type_words().count(w))
                out.push_back({s, i, "type"});
            else if (keyword_words().count(w))
                out.push_back({s, i, "keyword"});
            continue;
        }
        ++i;
    }
    return out;
}

// ------------------------------------------------------------ GTK apply

namespace {
GtkTextTag* ensure_tag(GtkTextBuffer* buf, const char* name,
                       const std::string& color) {
    GtkTextTagTable* table = gtk_text_buffer_get_tag_table(buf);
    GtkTextTag* tag = gtk_text_tag_table_lookup(table, name);
    if (!tag) {
        tag = gtk_text_buffer_create_tag(buf, name, "foreground", color.c_str(),
                                         NULL);
    } else {
        g_object_set(tag, "foreground", color.c_str(), NULL);
    }
    return tag;
}
}  // namespace

void theme_highlight_buffer(GtkTextBuffer* buf, const CodeTheme& theme) {
    if (!buf) return;
    GtkTextIter a, b;
    gtk_text_buffer_get_bounds(buf, &a, &b);
    gchar* text = gtk_text_buffer_get_text(buf, &a, &b, FALSE);
    std::string code = text ? text : "";
    g_free(text);
    static const char* kinds[] = {"keyword", "type", "string", "comment",
                                  "number", "preprocessor"};
    std::map<std::string, GtkTextTag*> tags;
    for (const char* k : kinds) {
        std::string color;
        if (!std::strcmp(k, "keyword")) color = theme.keyword;
        else if (!std::strcmp(k, "type")) color = theme.type;
        else if (!std::strcmp(k, "string")) color = theme.string;
        else if (!std::strcmp(k, "comment")) color = theme.comment;
        else if (!std::strcmp(k, "number")) color = theme.number;
        else color = theme.preprocessor;
        std::string tagname = std::string("theme-") + k;
        tags[k] = ensure_tag(buf, tagname.c_str(), color);
        gtk_text_buffer_remove_tag(buf, tags[k], &a, &b);
    }
    for (const HlSpan& s : theme_tokenize(code)) {
        auto it = tags.find(s.kind);
        if (it == tags.end()) continue;
        GtkTextIter sa, sb;
        gtk_text_buffer_get_iter_at_offset(buf, &sa, (gint)s.start);
        gtk_text_buffer_get_iter_at_offset(buf, &sb, (gint)s.end);
        gtk_text_buffer_apply_tag(buf, it->second, &sa, &sb);
    }
}

void theme_apply_to_view(GtkWidget* view, const CodeTheme& theme) {
    if (!view) return;
    GtkCssProvider* provider = (GtkCssProvider*)g_object_get_data(
        G_OBJECT(view), "theme-css-provider");
    if (!provider) {
        provider = gtk_css_provider_new();
        gtk_style_context_add_provider(
            gtk_widget_get_style_context(view), GTK_STYLE_PROVIDER(provider),
            GTK_STYLE_PROVIDER_PRIORITY_USER);
        g_object_set_data_full(G_OBJECT(view), "theme-css-provider", provider,
                               (GDestroyNotify)g_object_unref);
    }
    GError* error = nullptr;
    gtk_css_provider_load_from_data(provider, theme.css().c_str(), -1, &error);
    if (error) {
        std::fprintf(stderr, "fdesigner: theme CSS failed: %s\n",
                     error->message);
        g_error_free(error);
    }
}

// ------------------------------------------------------------ picker dialog

namespace {

struct PickerCtx {
    CodeTheme edited;
    GtkWidget* combo = nullptr;
    std::map<std::string, GtkWidget*> buttons;  // role -> GtkColorButton
    std::map<std::string, std::string*> fields;  // role -> CodeTheme field
};

std::string color_of_button(GtkWidget* b) {
    GdkRGBA rgba;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(b), &rgba);
    char buf[16];
    std::snprintf(buf, sizeof buf, "#%02x%02x%02x",
                  (int)(rgba.red * 255 + 0.5), (int)(rgba.green * 255 + 0.5),
                  (int)(rgba.blue * 255 + 0.5));
    return buf;
}

void set_button_color(GtkWidget* b, const std::string& hex) {
    GdkRGBA rgba;
    if (!gdk_rgba_parse(&rgba, hex.c_str()))
        gdk_rgba_parse(&rgba, "#000000");
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(b), &rgba);
}

void picker_refresh_from_combo(PickerCtx* ctx) {
    gchar* name = gtk_combo_box_text_get_active_text(
        GTK_COMBO_BOX_TEXT(ctx->combo));
    if (!name) return;
    CodeTheme base = CodeTheme::builtin(name);
    g_free(name);
    // Switching the built-in resets the colours to that theme; the
    // user then customises from there.
    ctx->edited = base;
    for (const auto& kv : ctx->fields)
        set_button_color(ctx->buttons[kv.first], *kv.second);
}

}  // namespace

bool theme_picker_open(GtkWidget* parent,
                       const std::function<void(const CodeTheme&)>& on_apply) {
    PickerCtx ctx;
    ctx.edited = theme_current();
    ctx.fields = {
        {"background", &ctx.edited.background},
        {"foreground", &ctx.edited.foreground},
        {"keyword", &ctx.edited.keyword},
        {"type", &ctx.edited.type},
        {"string", &ctx.edited.string},
        {"comment", &ctx.edited.comment},
        {"number", &ctx.edited.number},
        {"preprocessor", &ctx.edited.preprocessor},
        {"current_line", &ctx.edited.current_line},
    };
    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "Color Theme", GTK_WINDOW(parent), GTK_DIALOG_MODAL, "_Cancel",
        GTK_RESPONSE_CANCEL, "_Apply", GTK_RESPONSE_APPLY, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 420, 480);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 12);
    gtk_container_add(GTK_CONTAINER(content), grid);

    GtkWidget* name_lbl = gtk_label_new("Theme");
    gtk_widget_set_halign(name_lbl, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), name_lbl, 0, 0, 1, 1);
    ctx.combo = gtk_combo_box_text_new();
    for (const auto& n : CodeTheme::builtin_names())
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ctx.combo),
                                       n.c_str());
    {
        // Select by index: a text combo has no per-row ids.
        int active = 0;
        const std::vector<std::string> names = CodeTheme::builtin_names();
        for (size_t k = 0; k < names.size(); ++k)
            if (names[k] == ctx.edited.name) active = (int)k;
        gtk_combo_box_set_active(GTK_COMBO_BOX(ctx.combo), active);
    }
    gtk_grid_attach(GTK_GRID(grid), ctx.combo, 1, 0, 1, 1);

    static const char* roles[][2] = {
        {"background", "Background"}, {"foreground", "Foreground"},
        {"keyword", "Keyword"},       {"type", "Type"},
        {"string", "String"},         {"comment", "Comment"},
        {"number", "Number"},         {"preprocessor", "Preprocessor"},
        {"current_line", "Current line"},
    };
    int row = 1;
    for (const auto& r : roles) {
        GtkWidget* lbl = gtk_label_new(r[1]);
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 1, 1);
        GtkWidget* btn = gtk_color_button_new();
        gtk_color_button_set_title(GTK_COLOR_BUTTON(btn), r[1]);
        set_button_color(btn, *ctx.fields[r[0]]);
        ctx.buttons[r[0]] = btn;
        gtk_grid_attach(GTK_GRID(grid), btn, 1, row, 1, 1);
        ++row;
    }
    GtkWidget* note = gtk_label_new(
        "Pick a built-in theme, then tweak any colour.\n"
        "Apply saves to ~/.config/fdesigner/theme.conf.");
    gtk_label_set_line_wrap(GTK_LABEL(note), TRUE);
    gtk_widget_set_halign(note, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), note, 0, row, 2, 1);

    g_signal_connect(ctx.combo, "changed",
                     G_CALLBACK(+[](GtkComboBox*, gpointer d) {
                         picker_refresh_from_combo((PickerCtx*)d);
                     }),
                     &ctx);
    gtk_widget_show_all(dlg);
    gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
    bool applied = false;
    if (resp == GTK_RESPONSE_APPLY) {
        for (const auto& kv : ctx.fields) *kv.second = color_of_button(ctx.buttons[kv.first]);
        // If colours were tweaked away from the named built-in, the
        // stored name still records the base theme.
        theme_set_current(ctx.edited);
        if (on_apply) on_apply(ctx.edited);
        applied = true;
    }
    gtk_widget_destroy(dlg);
    return applied;
}

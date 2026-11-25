// FDesigner global font settings — implementation. See preferences.h.
#include "preferences.h"

#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace {

// Same persistence pattern as DebuggerSettings in debugger.cpp.
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

// Escape a font family for a quoted CSS string value.
std::string css_quote(const std::string& family) {
    std::string out;
    for (char c : family) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return "\"" + out + "\"";
}

void load_css(GtkCssProvider* provider, const std::string& css) {
    GError* error = nullptr;
    gtk_css_provider_load_from_data(provider, css.c_str(), -1, &error);
    if (error) {
        std::fprintf(stderr, "fdesigner: font CSS failed: %s\n",
                     error->message);
        g_error_free(error);
    }
}

FontSettings g_current;
bool g_current_loaded = false;

}  // namespace

std::string FontSettings::default_path() {
    const char* e = std::getenv("FDESIGNER_SETTINGS_CONF");
    if (e && *e) return e;
    return home_dir() + "/.config/fdesigner/settings.conf";
}

std::string FontSettings::sanitized(const std::string& desc,
                                    const std::string& fallback) {
    PangoFontDescription* d =
        pango_font_description_from_string(desc.c_str());
    const char* family =
        d ? pango_font_description_get_family(d) : nullptr;
    if (!d || !family || !*family) {
        if (d) pango_font_description_free(d);
        if (desc == fallback) return "Sans 10";  // fallback itself is bad
        return sanitized(fallback, fallback);
    }
    int size = pango_font_description_get_size(d);  // Pango units, 0 = unset
    int fallback_size = size_of(fallback);
    if (size <= 0)
        size = (fallback_size > 0 ? fallback_size : 10) * PANGO_SCALE;
    else if (size < 6 * PANGO_SCALE)
        size = 6 * PANGO_SCALE;
    else if (size > 48 * PANGO_SCALE)
        size = 48 * PANGO_SCALE;
    pango_font_description_set_size(d, size);
    char* s = pango_font_description_to_string(d);
    std::string out = s ? s : fallback;
    g_free(s);
    pango_font_description_free(d);
    return out;
}

std::string FontSettings::family_of(const std::string& desc) {
    PangoFontDescription* d =
        pango_font_description_from_string(desc.c_str());
    if (!d) return "";
    const char* family = pango_font_description_get_family(d);
    std::string out = family ? family : "";
    pango_font_description_free(d);
    return out;
}

int FontSettings::size_of(const std::string& desc) {
    PangoFontDescription* d =
        pango_font_description_from_string(desc.c_str());
    if (!d) return 0;
    int size = pango_font_description_get_size(d);
    pango_font_description_free(d);
    return size > 0 ? size / PANGO_SCALE : 0;
}

std::string FontSettings::ui_css() const {
    return "* { font-family: " + css_quote(family_of(font)) +
           "; font-size: " + std::to_string(size_of(font)) + "pt; }";
}

std::string FontSettings::code_css() const {
    return "textview { font-family: " + css_quote(family_of(code_font)) +
           "; font-size: " + std::to_string(size_of(code_font)) + "pt; }";
}

bool FontSettings::save(const std::string& path) const {
    std::string p = path.empty() ? default_path() : path;
    ensure_parent_dir(p);
    std::ofstream f(p);
    if (!f) return false;
    f << "# FDesigner settings\n";
    f << "font=" << sanitized(font, "Sans 10") << "\n";
    f << "code_font=" << sanitized(code_font, "Monospace 11") << "\n";
    return (bool)f;
}

bool FontSettings::load(const std::string& path) {
    std::string p = path.empty() ? default_path() : path;
    std::ifstream f(p);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        std::string t = trim_s(line);
        if (t.empty() || t[0] == '#') continue;
        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim_s(t.substr(0, eq));
        std::string value = trim_s(t.substr(eq + 1));
        if (key == "font")
            font = sanitized(value, font);
        else if (key == "code_font")
            code_font = sanitized(value, code_font);
    }
    return true;
}

FontSettings preferences_current() {
    if (!g_current_loaded) {
        g_current.load();  // keeps defaults when no file exists
        g_current_loaded = true;
    }
    return g_current;
}

void preferences_apply(const FontSettings& s) {
    static GtkCssProvider* ui_provider = nullptr;
    static GtkCssProvider* code_provider = nullptr;
    static bool providers_added = false;
    if (!ui_provider) ui_provider = gtk_css_provider_new();
    if (!code_provider) code_provider = gtk_css_provider_new();
    load_css(ui_provider, s.ui_css());
    load_css(code_provider, s.code_css());
    if (!providers_added) {
        GdkScreen* screen = gdk_screen_get_default();
        if (screen) {
            gtk_style_context_add_provider_for_screen(
                screen, GTK_STYLE_PROVIDER(ui_provider),
                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
            gtk_style_context_add_provider_for_screen(
                screen, GTK_STYLE_PROVIDER(code_provider),
                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
            providers_added = true;
        }
    }
}

void preferences_apply_to_text_view(GtkWidget* view,
                                    const FontSettings& s) {
    if (!view) return;
    GtkCssProvider* provider = gtk_css_provider_new();
    load_css(provider,
             "* { font-family: " + css_quote(FontSettings::family_of(
                                                s.code_font)) +
                 "; font-size: " +
                 std::to_string(FontSettings::size_of(s.code_font)) +
                 "pt; }");
    gtk_style_context_add_provider(
        gtk_widget_get_style_context(view), GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(provider);  // style context holds its own reference
}

std::string preferences_applied_font(GtkWidget* widget) {
    if (!widget) return "";
    PangoContext* ctx = gtk_widget_get_pango_context(widget);
    if (!ctx) return "";
    const PangoFontDescription* d = pango_context_get_font_description(ctx);
    if (!d) return "";
    char* s = pango_font_description_to_string(d);
    std::string out = s ? s : "";
    g_free(s);
    return out;
}

namespace {
// Response id for the dialog's "Reset to Defaults" button.
const int kResetResponse = 100;
}  // namespace

bool preferences_open(GtkWidget* parent) {
    FontSettings s = preferences_current();
    GtkWidget* d = gtk_dialog_new_with_buttons(
        "Preferences", GTK_WINDOW(parent), GTK_DIALOG_MODAL, "_Cancel",
        GTK_RESPONSE_CANCEL, "_Apply", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_dialog_add_button(GTK_DIALOG(d), "_Reset to Defaults",
                          kResetResponse);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(d));
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
    gtk_container_add(GTK_CONTAINER(content), grid);

    GtkWidget* ui_btn = gtk_font_button_new_with_font(s.font.c_str());
    gtk_font_button_set_use_font(GTK_FONT_BUTTON(ui_btn), TRUE);
    gtk_widget_set_hexpand(ui_btn, TRUE);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Interface font:"), 0, 0,
                    1, 1);
    gtk_grid_attach(GTK_GRID(grid), ui_btn, 1, 0, 1, 1);

    GtkWidget* code_btn =
        gtk_font_button_new_with_font(s.code_font.c_str());
    gtk_font_button_set_use_font(GTK_FONT_BUTTON(code_btn), TRUE);
    gtk_widget_set_hexpand(code_btn, TRUE);
    gtk_grid_attach(GTK_GRID(grid),
                    gtk_label_new("Code / text-view font:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), code_btn, 1, 1, 1, 1);

    std::string note_text =
        "Fonts apply immediately to the designer interface and to\n"
        "code / text views, and are saved to " +
        FontSettings::default_path() + ".";
    GtkWidget* note = gtk_label_new(note_text.c_str());
    gtk_label_set_xalign(GTK_LABEL(note), 0.0);
    gtk_grid_attach(GTK_GRID(grid), note, 0, 2, 2, 1);

    gtk_widget_show_all(d);
    int resp;
    while ((resp = gtk_dialog_run(GTK_DIALOG(d))) == kResetResponse) {
        gtk_font_chooser_set_font(GTK_FONT_CHOOSER(ui_btn), "Sans 10");
        gtk_font_chooser_set_font(GTK_FONT_CHOOSER(code_btn),
                                  "Monospace 11");
    }
    if (resp == GTK_RESPONSE_ACCEPT) {
        gchar* ui_font = gtk_font_chooser_get_font(GTK_FONT_CHOOSER(ui_btn));
        gchar* code_font =
            gtk_font_chooser_get_font(GTK_FONT_CHOOSER(code_btn));
        s.font = FontSettings::sanitized(ui_font ? ui_font : "",
                                         s.font);
        s.code_font = FontSettings::sanitized(code_font ? code_font : "",
                                              s.code_font);
        g_free(ui_font);
        g_free(code_font);
        s.save();
        g_current = s;
        g_current_loaded = true;
        preferences_apply(s);
        gtk_widget_destroy(d);
        return true;
    }
    gtk_widget_destroy(d);
    return false;
}

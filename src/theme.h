// FDesigner code-view color themes — syntax-highlighting themes for the
// code view (src/codeview.h). Four built-ins: Light, Dark, Monokai,
// Solarized Dark. Each theme colours keywords, types, strings, comments,
// numbers, preprocessor lines, plus the view background/foreground and
// the current-execution-line background.
//
// Persistence: ~/.config/fdesigner/theme.conf (key=value, same pattern
// as the debugger settings); FDESIGNER_THEME_CONF overrides the path
// (used by tests). The file stores the theme name plus every colour, so
// a hand-edited or picker-customised colour survives a restart: on load
// the named built-in is taken as the base and any colour keys present
// override it. A theme whose colours differ from its named built-in is
// a custom variant of that base; name "Custom" starts from Dark.
//
// NOTE (merge): the font-settings branch owns Edit -> Preferences and
// ~/.config/fdesigner/settings.conf. The theme picker lives under
// View -> Color Theme... and its own theme.conf, deliberately, so the
// two branches merge without touching each other; a later pass can move
// the picker into Preferences.
#pragma once

#include <gtk/gtk.h>

#include <functional>
#include <string>
#include <vector>

struct CodeTheme {
    std::string name = "Light";
    std::string background = "#ffffff";
    std::string foreground = "#1a1a1a";
    std::string keyword = "#0000ff";
    std::string type = "#008080";
    std::string string = "#a31515";
    std::string comment = "#008000";
    std::string number = "#098658";
    std::string preprocessor = "#af00db";
    std::string current_line = "#e8f2ff";

    static std::vector<std::string> builtin_names();  // Light, Dark, ...
    static CodeTheme builtin(const std::string& name);  // unknown -> Light
    static bool is_builtin(const std::string& name);
    // Strict "#rrggbb" (case-insensitive hex). Exposed for tests.
    static bool valid_color(const std::string& s);

    static std::string default_path();  // ~/.config/fdesigner/theme.conf
    bool save(const std::string& path = "") const;
    bool load(const std::string& path = "");
    // CSS for the view (sets both "textview" and "textview text":
    // the text area background lives on the "text" subnode).
    std::string css() const;
};

// One highlighted span of C++ source: [start, end) byte offsets, kind
// is one of "keyword", "type", "string", "comment", "number",
// "preprocessor". Pure function of the text (no GTK) — unit-testable.
struct HlSpan {
    size_t start = 0, end = 0;
    std::string kind;
};
std::vector<HlSpan> theme_tokenize(const std::string& code);

// Apply the theme's token colours to a buffer: creates/updates tags
// named "theme-keyword" etc., clears them over the whole buffer, then
// applies theme_tokenize() spans. The buffer text is unchanged.
void theme_highlight_buffer(GtkTextBuffer* buf, const CodeTheme& theme);
// Install the theme background/foreground on one text view (per-view
// CSS provider, re-appliable).
void theme_apply_to_view(GtkWidget* view, const CodeTheme& theme);

// Theme currently in effect (loaded from disk on first call).
CodeTheme theme_current();
// Make `t` current and persist it.
void theme_set_current(const CodeTheme& t);
// Open the theme picker dialog (theme combo + per-role colour buttons).
// On Apply/OK the choice is saved, made current, and on_apply (if set)
// is called so the caller can repaint open code views. Returns true on
// Apply/OK, false on Cancel.
bool theme_picker_open(GtkWidget* parent,
                       const std::function<void(const CodeTheme&)>& on_apply);

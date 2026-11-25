// FDesigner global font settings — Edit -> Preferences...
//
// One interface font and one code/text-view font, chosen with GtkFontButton
// widgets. Persisted in ~/.config/fdesigner/settings.conf (key=value, the
// same pattern as the debugger's debugger.conf); FDESIGNER_SETTINGS_CONF
// overrides the path (used by tests).
//
// Application is live and visible: the interface font is installed as a
// screen-wide CSS provider ("* { font-family...; font-size... }") and the
// code font as a "textview" CSS rule, so Text/Memo views (and any other
// GtkTextView) pick it up, as does the rest of the designer UI. Sizes are
// clamped to 6-48 pt; an empty or unparseable description falls back to
// the default instead of breaking the UI.
#pragma once

#include <gtk/gtk.h>

#include <string>

struct FontSettings {
    std::string font = "Sans 10";        // interface font (Pango description)
    std::string code_font = "Monospace 11";  // code / text-view font

    static std::string default_path();  // ~/.config/fdesigner/settings.conf
    bool save(const std::string& path = "") const;
    bool load(const std::string& path = "");

    // CSS snippets that apply each font (used by preferences_apply()).
    std::string ui_css() const;    // "* { font-family...; font-size... }"
    std::string code_css() const;  // "textview { font-family...; ... }"

    // Helpers, exposed for tests.
    // Parse desc as a Pango font description; fall back to `fallback`
    // when it is empty/unparseable, and clamp the size to 6-48 pt.
    static std::string sanitized(const std::string& desc,
                                 const std::string& fallback);
    static std::string family_of(const std::string& desc);  // "" if none
    static int size_of(const std::string& desc);            // pt, 0 if none
};

// Settings currently in effect (loaded from disk on first call).
FontSettings preferences_current();
// Install the fonts as screen-wide CSS providers (re-appliable).
void preferences_apply(const FontSettings& s);
// Apply the code font to one text view (e.g. a code display).
void preferences_apply_to_text_view(GtkWidget* view, const FontSettings& s);
// Pango description of the font a widget is currently showing
// ("" if it cannot be determined). Test helper.
std::string preferences_applied_font(GtkWidget* widget);
// Open the Preferences dialog; on Accept saves the choice, applies it
// live, and returns true. On Cancel returns false.
bool preferences_open(GtkWidget* parent);

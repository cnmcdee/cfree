// Font settings test harness — exercises src/preferences.cpp directly
// (no main window needed). Run under Xvfb: xvfb-run -a ./font_tests
// Prints one PASS/FAIL line per check; exit 0 only if all pass.
#include <gtk/gtk.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "preferences.h"

static int g_pass = 0, g_fail = 0;

static void check(bool cond, const char* name) {
    if (cond) {
        ++g_pass;
        std::printf("PASS %s\n", name);
    } else {
        ++g_fail;
        std::printf("FAIL %s\n", name);
    }
}

int main(int argc, char** argv) {
    gtk_init(&argc, &argv);

    // 1. Defaults.
    FontSettings def;
    check(def.font == "Sans 10", "defaults-ui-font");
    check(def.code_font == "Monospace 11", "defaults-code-font");

    // 2. Save / load round-trip at an explicit path.
    FontSettings fs;
    fs.font = "DejaVu Sans 14";
    fs.code_font = "DejaVu Sans Mono 13";
    const char* path = "/tmp/fdesigner_font_harness.conf";
    check(fs.save(path), "save-roundtrip");
    FontSettings fs2;
    check(fs2.load(path), "load-roundtrip");
    check(fs2.font == "DejaVu Sans 14" &&
              fs2.code_font == "DejaVu Sans Mono 13",
          "roundtrip-values");

    // 3. Missing file: load fails, defaults kept.
    FontSettings fs3;
    check(!fs3.load("/tmp/fdesigner_font_no_such_file.conf"),
          "load-missing-fails");
    check(fs3.font == "Sans 10", "load-missing-keeps-defaults");

    // 4. Garbage in the file does not break loading.
    {
        FILE* f = std::fopen("/tmp/fdesigner_font_garbage.conf", "w");
        std::fprintf(f, "# comment\nnot-a-kv-line\nfont=\ncode_font=\n");
        std::fclose(f);
    }
    FontSettings fs4;
    check(fs4.load("/tmp/fdesigner_font_garbage.conf"),
          "load-garbage-file");
    check(fs4.font == "Sans 10" && fs4.code_font == "Monospace 11",
          "garbage-falls-back-to-defaults");

    // 5. Sanitising: empty -> fallback, sizes clamped to 6-48 pt.
    check(FontSettings::sanitized("", "Sans 10") == "Sans 10",
          "sanitize-empty");
    check(FontSettings::size_of(FontSettings::sanitized("Sans 99",
                                                         "Sans 10")) == 48,
          "sanitize-clamp-high");
    check(FontSettings::size_of(FontSettings::sanitized("Sans 2",
                                                         "Sans 10")) == 6,
          "sanitize-clamp-low");
    check(FontSettings::family_of(
              FontSettings::sanitized("DejaVu Sans Mono 13",
                                      "Sans 10")) == "DejaVu Sans Mono",
          "sanitize-keeps-family");

    // 6. Env override for the default path (mirrors debugger.conf).
    ::setenv("FDESIGNER_SETTINGS_CONF", "/tmp/fdesigner_font_env.conf", 1);
    check(FontSettings::default_path() ==
              "/tmp/fdesigner_font_env.conf",
          "env-override-path");
    ::unsetenv("FDESIGNER_SETTINGS_CONF");

    // 7. CSS generation.
    check(fs.ui_css().find("DejaVu Sans") != std::string::npos &&
              fs.ui_css().find("14pt") != std::string::npos,
          "ui-css-content");
    check(fs.code_css().find("textview") != std::string::npos &&
              fs.code_css().find("DejaVu Sans Mono") != std::string::npos &&
              fs.code_css().find("13pt") != std::string::npos,
          "code-css-content");

    // 8. Live apply to a text view: the widget really shows the font.
    GtkWidget* tv = gtk_text_view_new();
    gtk_widget_show(tv);
    preferences_apply_to_text_view(tv, fs);
    while (gtk_events_pending()) gtk_main_iteration();
    std::string applied = preferences_applied_font(tv);
    check(FontSettings::family_of(applied) == "DejaVu Sans Mono" &&
              FontSettings::size_of(applied) == 13,
          "apply-to-text-view");
    gtk_widget_destroy(tv);

    // 9. Screen-wide apply: a fresh text view and a label pick it up.
    preferences_apply(fs);
    GtkWidget* tv2 = gtk_text_view_new();
    GtkWidget* lbl = gtk_label_new("probe");
    gtk_widget_show(tv2);
    gtk_widget_show(lbl);
    while (gtk_events_pending()) gtk_main_iteration();
    std::string applied2 = preferences_applied_font(tv2);
    check(FontSettings::family_of(applied2) == "DejaVu Sans Mono" &&
              FontSettings::size_of(applied2) == 13,
          "screen-apply-text-view");
    std::string applied_lbl = preferences_applied_font(lbl);
    check(FontSettings::family_of(applied_lbl) == "DejaVu Sans" &&
              FontSettings::size_of(applied_lbl) == 14,
          "screen-apply-label");
    gtk_widget_destroy(tv2);
    gtk_widget_destroy(lbl);

    // 10. Re-applying defaults restores the defaults (no crash, no leak
    //     of the old providers).
    preferences_apply(FontSettings());
    GtkWidget* tv3 = gtk_text_view_new();
    gtk_widget_show(tv3);
    while (gtk_events_pending()) gtk_main_iteration();
    std::string applied3 = preferences_applied_font(tv3);
    check(FontSettings::family_of(applied3) == "Monospace" &&
              FontSettings::size_of(applied3) == 11,
          "reapply-defaults");
    gtk_widget_destroy(tv3);

    std::printf("FONT TESTS %s pass=%d fail=%d\n",
                g_fail == 0 ? "PASS" : "FAIL", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}

// Theme + code-view gutter/folding test harness — exercises
// src/theme.cpp, src/codeview.cpp and the debugger gutter toggle
// (src/debugger_ui.cpp) directly. Run under Xvfb:
//   xvfb-run -a ./theme_tests
// Prints one PASS/FAIL line per check; exit 0 only if all pass.
// HOME is redirected to a scratch dir so the debugger toggle test
// never touches the real ~/.config/fdesigner/debugger_bps.conf.
#include <gtk/gtk.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "codeview.h"
#include "debugger_ui.h"
#include "theme.h"

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

static bool file_contains(const std::string& path, const std::string& needle) {
    std::ifstream f(path);
    if (!f) return false;
    std::string all((std::istreambuf_iterator<char>(f)),
                    std::istreambuf_iterator<char>());
    return all.find(needle) != std::string::npos;
}

// The fold sample used by both the pure-logic and widget checks.
//   0 #region Outer     9 };
//   1 class Foo {      10 #endregion
//   2   void bar() {   11 #if DEBUG
//   3     if (x) {     12   int d = 1;
//   4       work();    13 #endif
//   5     }            14 }            <- extra close, ignored
//   6     /* multi     15 int leftover() {  <- never closed, no fold
//   7        line */    16   return 1;
//   8   }
static std::vector<std::string> fold_lines() {
    return {"#region Outer",
            "class Foo {",
            "  void bar() {",
            "    if (x) {",
            "      work();",
            "    }",
            "    /* multi",
            "       line */",
            "  }",
            "};",
            "#endregion",
            "#if DEBUG",
            "int d = 1;",
            "#endif",
            "}",
            "int leftover() {",
            "  return 1;"};
}

static std::string join_lines(const std::vector<std::string>& lines) {
    std::string out;
    for (const auto& l : lines) out += l + "\n";
    return out;
}

int main(int argc, char** argv) {
    // Isolate all HOME-based config (debugger bps file, theme file).
    ::setenv("HOME", "/tmp/fdesigner_theme_home", 1);
    ::setenv("FDESIGNER_THEME_CONF", "", 1);
    gtk_init(&argc, &argv);

    // 1. Built-in themes.
    std::vector<std::string> names = CodeTheme::builtin_names();
    check(names.size() == 4, "builtins-count-4");
    check(names[0] == "Light" && names[1] == "Dark" &&
              names[2] == "Monokai" && names[3] == "Solarized Dark",
          "builtins-names");
    CodeTheme light = CodeTheme::builtin("Light");
    CodeTheme dark = CodeTheme::builtin("Dark");
    CodeTheme mono = CodeTheme::builtin("Monokai");
    CodeTheme solar = CodeTheme::builtin("Solarized Dark");
    check(light.keyword != dark.keyword &&
              dark.keyword != mono.keyword &&
              mono.keyword != solar.keyword,
          "builtins-distinct-keywords");
    check(light.background != dark.background &&
              dark.background != mono.background,
          "builtins-distinct-backgrounds");
    check(CodeTheme::builtin("Nope").name == "Light", "builtin-unknown-light");
    check(CodeTheme::is_builtin("Dark") && !CodeTheme::is_builtin("Custom"),
          "is-builtin");

    // 2. Colour validation.
    check(CodeTheme::valid_color("#1a2b3c") &&
              CodeTheme::valid_color("#FFFFFF") &&
              !CodeTheme::valid_color("red") &&
              !CodeTheme::valid_color("#12345") &&
              !CodeTheme::valid_color("#gggggg") &&
              !CodeTheme::valid_color(""),
          "valid-color");

    // 3. Save / load round-trip at an explicit path, incl. custom colour.
    CodeTheme custom = dark;
    custom.keyword = "#ff0000";
    const char* tpath = "/tmp/fdesigner_theme_harness.conf";
    check(custom.save(tpath), "save-roundtrip");
    CodeTheme reloaded;
    check(reloaded.load(tpath), "load-roundtrip");
    check(reloaded.name == "Dark" && reloaded.keyword == "#ff0000" &&
              reloaded.background == dark.background,
          "roundtrip-values");
    // Missing file: load fails, defaults kept.
    CodeTheme miss;
    check(!miss.load("/tmp/fdesigner_theme_no_such.conf"),
          "load-missing-fails");
    check(miss.name == "Light", "load-missing-keeps-defaults");
    // Garbage in the file does not break loading; bad colours ignored.
    {
        FILE* f = std::fopen("/tmp/fdesigner_theme_garbage.conf", "w");
        std::fprintf(f, "# comment\nnot-a-kv-line\ntheme=Dark\nkeyword=red\n"
                        "background=#1e1e1e\n");
        std::fclose(f);
    }
    CodeTheme garb;
    check(garb.load("/tmp/fdesigner_theme_garbage.conf"),
          "load-garbage-file");
    check(garb.name == "Dark" && garb.keyword == dark.keyword &&
              garb.background == "#1e1e1e",
          "garbage-bad-colour-ignored");

    // 4. Default-path persistence (HOME is redirected above).
    CodeTheme dp = CodeTheme::builtin("Monokai");
    theme_set_current(dp);
    check(file_contains(
              "/tmp/fdesigner_theme_home/.config/fdesigner/theme.conf",
              "theme=Monokai"),
          "default-path-persists");
    CodeTheme cur = theme_current();
    check(cur.name == "Monokai" && cur.keyword == mono.keyword,
          "theme-current-roundtrip");

    // 5. Tokeniser spans.
    std::string sample =
        "#include <gtk/gtk.h>\n// hi\nint main() { const char* s = \"x\"; "
        "return 42; }\n";
    std::vector<HlSpan> spans = theme_tokenize(sample);
    auto has_span = [&](const std::string& kind, const std::string& word) {
        for (const auto& sp : spans)
            if (sp.kind == kind &&
                sample.substr(sp.start, sp.end - sp.start) == word)
                return true;
        return false;
    };
    check(has_span("preprocessor", "#include <gtk/gtk.h>"),
          "token-preprocessor");
    check(has_span("comment", "// hi"), "token-line-comment");
    check(has_span("type", "int"), "token-type");
    check(has_span("keyword", "return"), "token-keyword");
    check(has_span("string", "\"x\""), "token-string");
    check(has_span("number", "42"), "token-number");
    // Block comment, char literal, digit separator, string with brace.
    std::string sample2 =
        "/* { not a fold */\nchar c = 'x';\nint n = 1'000;\n"
        "const char* s = \"a{b\";\nfloat f = 1.5e-3;\n";
    std::vector<HlSpan> spans2 = theme_tokenize(sample2);
    bool block_ok = false, char_ok = false, num_ok = false;
    for (const auto& sp : spans2) {
        std::string w = sample2.substr(sp.start, sp.end - sp.start);
        if (sp.kind == "comment" && w == "/* { not a fold */") block_ok = true;
        if (sp.kind == "string" && w == "'x'") char_ok = true;
        if (sp.kind == "number" && w == "1'000") num_ok = true;
    }
    check(block_ok, "token-block-comment");
    check(char_ok, "token-char-literal");
    check(num_ok, "token-digit-separator");

    // 6. Fold regions: braces, region, comment, #if, nesting, unbalanced.
    std::vector<std::string> flines = fold_lines();
    std::vector<FoldRegion> regs = codeview_find_regions(flines);
    check(regs.size() == 6, "regions-count-6");
    auto has_region = [&](int st, int en, const std::string& kind) {
        for (const auto& r : regs)
            if (r.start_line == st && r.end_line == en && r.kind == kind)
                return true;
        return false;
    };
    check(has_region(0, 10, "region"), "region-outer");
    check(has_region(1, 9, "brace"), "brace-class");
    check(has_region(2, 8, "brace"), "brace-method");
    check(has_region(3, 5, "brace"), "brace-if");
    check(has_region(6, 7, "comment"), "region-comment");
    check(has_region(11, 13, "preproc"), "region-if");
    int depth_of[17];
    for (int k = 0; k < 17; ++k) depth_of[k] = -1;
    for (const auto& r : regs) depth_of[r.start_line] = r.depth;
    check(depth_of[0] == 0 && depth_of[1] == 1 && depth_of[2] == 2 &&
              depth_of[3] == 3 && depth_of[6] == 3 && depth_of[11] == 0,
          "regions-depths-nested");
    // No region starts at the extra close (14) or the unclosed brace (15).
    bool bad_start = false;
    for (const auto& r : regs)
        if (r.start_line == 14 || r.start_line == 15) bad_start = true;
    check(!bad_start, "unbalanced-no-bogus-region");
    // A brace inside a string / comment opens no fold.
    std::vector<std::string> trick = {
        "const char* s = \"{\";",  // 0: brace in string
        "/* { */",                 // 1: brace in comment
        "int ok() {",              // 2: real brace
        "  return 0;",             // 3
        "}",                       // 4
    };
    std::vector<FoldRegion> tregs = codeview_find_regions(trick);
    check(tregs.size() == 1 && tregs[0].start_line == 2 &&
              tregs[0].end_line == 4,
          "braces-in-string-comment-ignored");
    // Single-line block: nothing to fold.
    std::vector<FoldRegion> sregs =
        codeview_find_regions({"int f() { return 0; }"});
    check(sregs.empty(), "single-line-block-no-fold");
    // Comment-style region markers (the form the generator emits:
    // `// #region Name` / `// #endregion`) fold like bare #region.
    // An unclosed one and one inside a string open no fold.
    std::vector<std::string> clines = {
        "    // #region Widget",   // 0
        "    int x = 1;",          // 1
        "    // #endregion",       // 2
        "    // #region Open",     // 3: unclosed, no fold
        "    int y = 2;",          // 4
        "const char* s = \"// #region nope\";",  // 5: in a string
    };
    std::vector<FoldRegion> cregs = codeview_find_regions(clines);
    bool cok = false;
    for (const auto& r : cregs)
        if (r.start_line == 0 && r.end_line == 2 && r.kind == "region")
            cok = true;
    check(cok, "comment-region-folds");
    check(cregs.size() == 1, "comment-region-count-1");

    // 7. Visible lines under folds.
    std::vector<int> vis = codeview_visible_lines(flines, {0});
    check(vis.size() == 8 && vis[0] == 0 && vis[1] == 10,
          "visible-fold-region");
    vis = codeview_visible_lines(flines, {2});
    check(vis.size() == 12, "visible-fold-method");
    vis = codeview_visible_lines(flines, {0, 2});
    check(vis.size() == 8, "visible-nested-fold-same-as-outer");
    vis = codeview_visible_lines(flines, {3});
    check(vis.size() == 16 && vis[3] == 3 && vis[4] == 5,
          "visible-fold-if-only");

    // 8. Widget level: regions, fold toggle, gutter clicks, current
    // line, theme switch changing the tag colour, real fold collapse.
    GtkWidget* win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWidget* cv = codeview_new();
    gtk_container_add(GTK_CONTAINER(win), cv);
    gtk_widget_show_all(win);
    while (gtk_events_pending()) gtk_main_iteration();
    codeview_set_text(cv, join_lines(flines),
                      "/tmp/fdesigner_codeview_test.cpp");
    while (gtk_events_pending()) gtk_main_iteration();
    check(codeview_line_count(cv) == 17, "widget-line-count");
    check(codeview_regions(cv).size() == 6, "widget-regions");
    check(codeview_toggle_fold_at(cv, 3), "widget-fold-toggle-ok");
    check(codeview_is_folded(cv, 3), "widget-folded");
    check(!codeview_toggle_fold_at(cv, 5), "widget-fold-no-region-start");
    codeview_gutter_click(cv, 3, true);
    check(!codeview_is_folded(cv, 3), "widget-gutter-fold-click-unfolds");
    codeview_gutter_click(cv, 5, false);
    check(codeview_breakpoint_lines(cv).count(5) == 1,
          "widget-gutter-bp-click-sets");
    codeview_gutter_click(cv, 5, false);
    check(codeview_breakpoint_lines(cv).empty(),
          "widget-gutter-bp-click-clears");
    codeview_set_current_line(cv, 4);
    check(codeview_current_line(cv) == 4, "widget-current-line");
    // Theme switch changes the keyword tag colour. "foreground" is
    // write-only and "foreground-rgba" does not read back on this GTK;
    // "foreground-gdk" (16-bit, 8-bit values as v*257) is the readable
    // form. Dark keyword #569cd6.
    codeview_set_theme(cv, dark);
    GtkTextBuffer* cbuf = gtk_text_view_get_buffer(
        GTK_TEXT_VIEW(codeview_text_view(cv)));
    GtkTextTag* kw = gtk_text_tag_table_lookup(
        gtk_text_buffer_get_tag_table(cbuf), "theme-keyword");
    GdkColor* col = nullptr;
    if (kw) g_object_get(kw, "foreground-gdk", &col, NULL);
    check(kw && col && col->red / 257 == 0x56 &&
              col->green / 257 == 0x9c && col->blue / 257 == 0xd6,
          "widget-theme-switch-changes-tag");
    if (col) g_boxed_free(g_type_from_name("GdkColor"), col);
    // Real collapse: fold bar() (lines 3..8, 1-based) and check the
    // hidden buffer lines report zero height.
    codeview_toggle_fold_at(cv, 3);
    while (gtk_events_pending()) gtk_main_iteration();
    {
        GtkTextView* tv = GTK_TEXT_VIEW(codeview_text_view(cv));
        gint y4, h4, y7, h7;
        GtkTextIter it4, it7;
        gtk_text_buffer_get_iter_at_line(cbuf, &it4, 3);  // file line 4
        gtk_text_view_get_line_yrange(tv, &it4, &y4, &h4);
        gtk_text_buffer_get_iter_at_line(cbuf, &it7, 6);  // file line 7
        gtk_text_view_get_line_yrange(tv, &it7, &y7, &h7);
        check(h4 == 0 && h7 == 0, "widget-fold-collapses-lines");
    }
    gtk_widget_destroy(win);

    // 9. Gutter -> debugger backend toggle (HOME redirected: the bps
    // file lands in the scratch dir, never the real config).
    const std::string bpfile =
        "/tmp/fdesigner_theme_home/.config/fdesigner/debugger_bps.conf";
    ::remove(bpfile.c_str());
    check(debugger_toggle_breakpoint("/tmp/fdesigner_codeview_test.cpp",
                                     42),
          "dbg-toggle-sets");
    check(debugger_has_breakpoint("/tmp/fdesigner_codeview_test.cpp", 42),
          "dbg-has-after-set");
    check(file_contains(bpfile, "/tmp/fdesigner_codeview_test.cpp:42"),
          "dbg-toggle-persists");
    check(!debugger_toggle_breakpoint("/tmp/fdesigner_codeview_test.cpp",
                                      42),
          "dbg-toggle-clears");
    check(!debugger_has_breakpoint("/tmp/fdesigner_codeview_test.cpp",
                                   42),
          "dbg-has-after-clear");
    check(!file_contains(bpfile, "/tmp/fdesigner_codeview_test.cpp:42"),
          "dbg-clear-persists");

    std::printf("THEME TESTS: %d pass, %d fail\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}

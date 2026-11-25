// CMake test probe — drives src/cmake_build.h + src/generator.h headlessly
// (no GUI): generates a project, checks the CMakeLists.txt, configures +
// builds it with cmake, runs the built GTK app under Xvfb, builds a
// hand-written console CMake project and a hand-written GTK one, and
// checks settings persistence and failure reporting.
// Usage: cmake_probe <workdir> ; exit 0 = all pass. Prints PASS/FAIL lines.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/wait.h>

#include "cmake_build.h"
#include "generator.h"
#include "model.h"

namespace fs = std::filesystem;

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const char* name) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (ok) ++g_pass; else ++g_fail;
}

static std::string read_file(const std::string& p) {
    std::ifstream f(p);
    if (!f) return "";
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

static void write_file(const std::string& p, const std::string& c) {
    std::ofstream f(p);
    f << c;
}

static int run(const std::string& cmd) {
    // std::system returns the wait status, not the exit code.
    int rc = std::system(cmd.c_str());
    if (rc == -1) return -1;
    if (WIFEXITED(rc)) return WEXITSTATUS(rc);
    return -1;
}

int main(int argc, char** argv) {
    std::string base = argc > 1 ? argv[1] : "/tmp/fdesigner_cmake_tests";
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);

    // 1. Generate a project (Button + Label + Entry) and inspect its
    // CMakeLists.txt.
    Project p;
    p.class_name = "MyWindow";
    Widget b;
    b.name = "Button_001"; b.type = "Button";
    b.props["label"] = "Click me";
    Widget l;
    l.name = "Label_001"; l.type = "Label";
    l.props["label"] = "Hello";
    Widget e;
    e.name = "Entry_001"; e.type = "Entry";
    p.widgets = {b, l, e};
    std::string gen_dir = base + "/generated";
    GenResult r = generate_project(p, p.widgets, gen_dir);
    check(r.ok, "generate-project");
    check(r.files.size() == 5 + 2 * p.widgets.size(),
          "generate-file-count-includes-cmakelists");
    std::string cm = read_file(gen_dir + "/CMakeLists.txt");
    check(!cm.empty(), "cmakelists-exists");
    check(cm.find("cmake_minimum_required(VERSION 3.10)") !=
              std::string::npos,
          "cmakelists-minimum-required");
    check(cm.find("project(MyWindow CXX)") != std::string::npos,
          "cmakelists-project");
    check(cm.find("find_package(PkgConfig REQUIRED)") != std::string::npos,
          "cmakelists-find-pkgconfig");
    check(cm.find("pkg_check_modules(GTK3 REQUIRED gtk+-3.0)") !=
              std::string::npos,
          "cmakelists-pkg-check-gtk3");
    check(cm.find("add_executable(app") != std::string::npos,
          "cmakelists-add-executable");
    check(cm.find("main.cpp") != std::string::npos &&
              cm.find("MyWindow.cpp") != std::string::npos &&
              cm.find("Button_001.cpp") != std::string::npos &&
              cm.find("Label_001.cpp") != std::string::npos &&
              cm.find("Entry_001.cpp") != std::string::npos,
          "cmakelists-lists-all-sources");
    check(cm.find("target_include_directories(app PRIVATE "
                  "${GTK3_INCLUDE_DIRS})") != std::string::npos,
          "cmakelists-include-dirs");
    check(cm.find("target_link_libraries(app PRIVATE ${GTK3_LIBRARIES})") !=
              std::string::npos,
          "cmakelists-link-libs");
    check(cm.find("CMAKE_CXX_STANDARD 17") != std::string::npos,
          "cmakelists-cxx17");
    check(cmake_project_exists(gen_dir), "cmake-project-detected");
    check(!cmake_project_exists(base), "cmake-project-absent-elsewhere");

    // 2. Configure + build the generated project with cmake, then run
    // the built GTK app briefly under Xvfb (timeout kills it: exit 124).
    CMakeSettings def;
    CMakeBuildResult br = cmake_build_project(gen_dir, def);
    check(br.ok && br.configured, "cmake-configure-build-generated");
    check(br.exe_path == gen_dir + "/build/app", "cmake-exe-path");
    check(fs::is_regular_file(br.exe_path), "cmake-exe-exists");
    check(run("timeout 3 xvfb-run -a '" + br.exe_path +
              "' >/dev/null 2>&1") == 124,
          "cmake-built-gtk-app-runs");
    // Rebuild is a no-op success (idempotent configure+build).
    CMakeBuildResult br2 = cmake_build_project(gen_dir, def);
    check(br2.ok, "cmake-rebuild-idempotent");

    // 3. Hand-written console CMake project (existing project, not
    // generated): configure + build + run, output must be exact.
    std::string console_dir = base + "/console";
    fs::create_directories(console_dir, ec);
    write_file(console_dir + "/CMakeLists.txt",
               "cmake_minimum_required(VERSION 3.10)\n"
               "project(console_demo CXX)\n"
               "add_executable(app main.cpp)\n");
    write_file(console_dir + "/main.cpp",
               "#include <cstdio>\n"
               "int main() { std::printf(\"cmake-console-ok\\n\"); "
               "return 0; }\n");
    CMakeBuildResult cr = cmake_build_project(console_dir, def);
    check(cr.ok, "cmake-build-handwritten-console");
    check(run("'" + console_dir + "/build/app' > '" + console_dir +
              "/run.log' 2>&1") == 0 &&
              read_file(console_dir + "/run.log") == "cmake-console-ok\n",
          "cmake-console-app-output");

    // 4. Hand-written GTK CMake project (uses PkgConfig like generated).
    std::string gtk_dir = base + "/gtk";
    fs::create_directories(gtk_dir, ec);
    write_file(gtk_dir + "/CMakeLists.txt",
               "cmake_minimum_required(VERSION 3.10)\n"
               "project(gtk_demo CXX)\n"
               "find_package(PkgConfig REQUIRED)\n"
               "pkg_check_modules(GTK3 REQUIRED gtk+-3.0)\n"
               "add_executable(app main.cpp)\n"
               "target_include_directories(app PRIVATE "
               "${GTK3_INCLUDE_DIRS})\n"
               "target_link_libraries(app PRIVATE ${GTK3_LIBRARIES})\n");
    write_file(gtk_dir + "/main.cpp",
               "#include <gtk/gtk.h>\n"
               "int main(int argc, char** argv) {\n"
               "    gtk_init(&argc, &argv);\n"
               "    GtkWidget* w = gtk_window_new(GTK_WINDOW_TOPLEVEL);\n"
               "    gtk_widget_show_all(w);\n"
               "    gtk_main();\n"
               "    return 0;\n"
               "}\n");
    CMakeBuildResult gr = cmake_build_project(gtk_dir, def);
    check(gr.ok, "cmake-build-handwritten-gtk");
    check(run("timeout 3 xvfb-run -a '" + gtk_dir + "/build/app' "
              "> /dev/null 2>&1") == 124,
          "cmake-handwritten-gtk-app-runs");

    // 5. Settings round-trip (custom path, so ~/.config is untouched).
    CMakeSettings s;
    s.cmake_path = "cmake";
    s.generator = "Unix Makefiles";
    s.build_type = "Debug";
    s.build_dir = "out";
    check(s.save(base + "/cmake.conf"), "cmake-settings-save");
    CMakeSettings s2;
    check(s2.load(base + "/cmake.conf") &&
              s2.generator == "Unix Makefiles" &&
              s2.build_type == "Debug" && s2.build_dir == "out" &&
              s2.cmake_path == "cmake",
          "cmake-settings-load");
    // Custom build dir + build type are honoured by a real build.
    CMakeBuildResult dbg = cmake_build_project(console_dir, s);
    check(dbg.ok && dbg.build_dir == console_dir + "/out" &&
              fs::is_regular_file(console_dir + "/out/app"),
          "cmake-settings-custom-build-dir");
    // Defaults load: unknown keys ignored, empty file loads as defaults.
    write_file(base + "/cmake2.conf", "# comment\nunknown_key=1\n");
    CMakeSettings s3;
    check(s3.load(base + "/cmake2.conf") &&
              s3.build_type == "Release" && s3.build_dir == "build",
          "cmake-settings-unknown-keys-ignored");

    // 6. Failure reporting: missing CMakeLists.txt, broken CMakeLists.txt,
    // and a project whose sources fail to compile must all report !ok.
    CMakeBuildResult nofile = cmake_build_project(base, def);
    check(!nofile.ok && !nofile.configured,
          "cmake-missing-cmakelists-fails");
    std::string broken_dir = base + "/broken";
    fs::create_directories(broken_dir, ec);
    write_file(broken_dir + "/CMakeLists.txt", "this is not valid cmake (\n");
    CMakeBuildResult bad = cmake_build_project(broken_dir, def);
    check(!bad.ok && !bad.configured, "cmake-broken-cmakelists-fails");
    std::string badsrc_dir = base + "/badsrc";
    fs::create_directories(badsrc_dir, ec);
    write_file(badsrc_dir + "/CMakeLists.txt",
               "cmake_minimum_required(VERSION 3.10)\n"
               "project(bad CXX)\n"
               "add_executable(app main.cpp)\n");
    write_file(badsrc_dir + "/main.cpp", "int main( { syntax error\n");
    CMakeBuildResult badsrc = cmake_build_project(badsrc_dir, def);
    check(!badsrc.ok && badsrc.configured,
          "cmake-compile-error-fails-after-configure");

    // 7. Shell quoting: a project dir with a space in its name builds.
    std::string spaced = base + "/dir with space";
    fs::create_directories(spaced, ec);
    write_file(spaced + "/CMakeLists.txt",
               "cmake_minimum_required(VERSION 3.10)\n"
               "project(spaced CXX)\n"
               "add_executable(app main.cpp)\n");
    write_file(spaced + "/main.cpp", "int main() { return 0; }\n");
    CMakeBuildResult sp = cmake_build_project(spaced, def);
    check(sp.ok, "cmake-dir-with-space-builds");

    std::printf("== cmake probe: %d passed, %d failed ==\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}

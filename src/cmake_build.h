// FDesigner CMake handling — no GTK dependency.
// Emits CMakeLists.txt text for generated projects, builds any directory
// containing one (configure + build), and persists the CMake settings
// (generator / build type / build directory) in
// ~/.config/fdesigner/cmake.conf, following the debugger.conf pattern
// (key=value; FDESIGNER_CMAKE_CONF overrides the path, used by tests).
#pragma once

#include <string>
#include <vector>

struct CMakeSettings {
    std::string cmake_path = "cmake";  // cmake binary (PATH lookup or path)
    std::string generator;             // "" = CMake default generator
    std::string build_type = "Release";
    std::string build_dir = "build";   // relative to the project directory

    static std::string default_path();  // ~/.config/fdesigner/cmake.conf
    bool save(const std::string& path = "") const;
    bool load(const std::string& path = "");
};

// CMakeLists.txt text for a generated project: project <name>, GTK 3 via
// PkgConfig, C++17, and add_executable(app <sources>). `sources` are the
// generated .cpp file names (e.g. main.cpp, MyWindow.cpp, Button_001.cpp).
std::string cmake_lists_text(const std::string& project_name,
                             const std::vector<std::string>& sources);

// True when <dir>/CMakeLists.txt exists.
bool cmake_project_exists(const std::string& dir);

// Shell-quote one argument for /bin/sh.
std::string cmake_shell_quote(const std::string& s);

struct CMakeBuildResult {
    bool ok = false;         // configure AND build both succeeded
    bool configured = false; // configure step succeeded
    std::string output;      // combined stdout+stderr of both steps
    std::string build_dir;   // absolute build directory used
    std::string exe_path;    // <build_dir>/app when it exists after build
};

// Configure + build the CMake project in `dir` (which must contain a
// CMakeLists.txt). The executable is <dir>/<build_dir>/app, matching the
// generated Makefile's `app` target. Blocking; captures all output.
CMakeBuildResult cmake_build_project(const std::string& dir,
                                     const CMakeSettings& settings);

// Absolute path of the executable a successful cmake_build_project leaves
// behind for `dir` under `settings`.
std::string cmake_exe_path(const std::string& dir,
                           const CMakeSettings& settings);

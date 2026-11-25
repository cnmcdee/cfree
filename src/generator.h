// FDesigner code generator.
// Turns the designed window (Project + widgets) into a compilable C++/GTK
// project. The output is ALL CODE — no .ui file — and every widget is a
// STANDALONE CLASS in its own .h/.cpp pair, mirroring the Python
// generator's boilerplate: the class creates and configures its widget in
// its constructor and carries the same nine mouse-event stub handlers
// (buttonPress1-3, dblbuttonPress1-3, buttonRelease1-3) plus a
// util_error_print helper. The generated window class just instantiates
// one widget class per designed widget. Because construction is ordinary
// sequential code in per-widget classes, a stepping debugger can walk it
// line by line, and adding a widget by hand means copying one class.

#pragma once

#include <string>
#include <vector>

#include "model.h"

struct GenResult {
    bool ok = false;
    std::string error;
    std::vector<std::string> files;  // paths written, in order
};

// Writes the project into out_dir (created if needed).
GenResult generate_project(const Project& project,
                           const std::vector<Widget>& widgets,
                           const std::string& out_dir);

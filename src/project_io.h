// FDesigner project files (.fdj) — JSON save/load, replacing the Python
// original's pickle files. Hand-rolled JSON (the model is small; no new
// dependencies). Format version 1:
//
//   { "format": "fdesigner", "version": 1,
//     "window": { "title", "class_name", "width", "height", "background" },
//     "widgets": [ { "name", "type", "x", "y", "width", "height",
//                    "props": { name: value, ... } } ] }
//
// Loading is tolerant of unknown keys and missing fields, but strict about
// the format tag. Property values are NOT validated here — the designer
// re-validates every property through set_prop() when it rebuilds the
// canvas, so a hand-edited file can't smuggle in bad values.

#pragma once

#include <string>

#include "model.h"

bool save_project_file(const std::string& path, const Project& project,
                       std::string& err);
bool load_project_file(const std::string& path, Project& project_out,
                       std::string& err);

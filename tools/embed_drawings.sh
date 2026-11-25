#!/bin/sh
# Wrap src/drawings.h as a C++ raw-string header so the generator can
# emit it into generated projects. Usage: embed_drawings.sh IN OUT
set -e
delim="FDDRAWINGS9b2c"
if grep -q "$delim" "$1"; then
    echo "embed_drawings: delimiter collision in $1" >&2
    exit 1
fi
{
    echo "// Generated from src/drawings.h by tools/embed_drawings.sh."
    echo "// Do not edit; run 'make' to regenerate."
    echo "#pragma once"
    printf 'static const char kDrawingsHeader[] = R"%s(\n' "$delim"
    cat "$1"
    printf ')%s";\n' "$delim"
} > "$2"

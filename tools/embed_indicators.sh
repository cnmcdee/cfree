#!/bin/sh
# Wrap src/indicators.h as a C++ raw-string header so the generator can
# emit it into generated projects. Usage: embed_indicators.sh IN OUT
set -e
delim="FDINDICATORS7f3a"
if grep -q "$delim" "$1"; then
    echo "embed_indicators: delimiter collision in $1" >&2
    exit 1
fi
{
    echo "// Generated from src/indicators.h by tools/embed_indicators.sh."
    echo "// Do not edit; run 'make' to regenerate."
    echo "#pragma once"
    printf 'static const char kIndicatorsHeader[] = R"%s(\n' "$delim"
    cat "$1"
    printf ')%s";\n' "$delim"
} > "$2"

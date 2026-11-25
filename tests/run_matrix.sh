#!/usr/bin/env bash
# Extensive generator compile test: generate the matrix of designs, then
# compile EVERY generated project with -Wall -Wextra and run it briefly
# under a virtual display. A generated app that starts and stays up
# (killed by timeout, exit 124) counts as a run pass.
set -u
cd "$(dirname "$0")/.."

echo "== building harness =="
g++ -O2 -Wall -std=c++17 -o tests/gen_matrix tests/gen_matrix.cpp src/generator.cpp src/cmake_build.cpp || exit 1

rm -rf tests/out
echo "== generating =="
tests/gen_matrix tests/out || exit 1

pass=0; fail=0; failed=""
for d in tests/out/*/; do
    name=$(basename "$d")
    if ! (cd "$d" && make CXXFLAGS="-O2 -Wall -Wextra -std=c++17" >build.log 2>&1); then
        echo "FAIL $name (compile) — see $d/build.log"
        fail=$((fail+1)); failed="$failed $name(compile)"; continue
    fi
    if grep -q "warning" "$d/build.log"; then
        echo "FAIL $name (warnings) — see $d/build.log"
        fail=$((fail+1)); failed="$failed $name(warnings)"; continue
    fi
    timeout 3 xvfb-run -a "$d/app" >"$d/run.log" 2>&1
    rc=$?
    if [ $rc -ne 124 ]; then
        echo "FAIL $name (run exit $rc) — see $d/run.log"
        fail=$((fail+1)); failed="$failed $name(run:$rc)"; continue
    fi
    # The Adwaita pixbuf-loader warning is VM environment noise (missing
    # mime database under Xvfb), not a defect in generated code; any other
    # CRITICAL/WARNING fails the case.
    bad=$(grep -E "CRITICAL|WARNING \*\*" "$d/run.log" | grep -v "Could not load a pixbuf" || true)
    if [ -n "$bad" ]; then
        echo "FAIL $name (runtime warnings) — see $d/run.log"
        fail=$((fail+1)); failed="$failed $name(runtime)"; continue
    fi
    echo "PASS $name"
    pass=$((pass+1))
done
echo "== candle render test =="
if g++ -O2 -Wall -std=c++17 -o tests/out/14_stockchart/render_test \
       tests/candle_render_test.cpp tests/out/14_stockchart/StockChart_001.cpp \
       -Itests/out/14_stockchart $(pkg-config --cflags --libs gtk+-3.0) \
       2>tests/out/14_stockchart/render_build.log; then
    if timeout 10 xvfb-run -a tests/out/14_stockchart/render_test; then
        echo "PASS candle-render"; pass=$((pass+1))
    else
        echo "FAIL candle-render"; fail=$((fail+1))
    fi
else
    echo "FAIL candle-render (build) — see tests/out/14_stockchart/render_build.log"
    fail=$((fail+1))
fi

echo "== indicator render test =="
if g++ -O2 -Wall -std=c++17 -o tests/out/15_stockchart_indicators/render_test \
       tests/indicator_render_test.cpp tests/out/15_stockchart_indicators/StockChart_001.cpp \
       -Itests/out/15_stockchart_indicators $(pkg-config --cflags --libs gtk+-3.0) \
       2>tests/out/15_stockchart_indicators/render_build.log; then
    if timeout 10 xvfb-run -a tests/out/15_stockchart_indicators/render_test; then
        echo "PASS indicator-render"; pass=$((pass+1))
    else
        echo "FAIL indicator-render"; fail=$((fail+1))
    fi
else
    echo "FAIL indicator-render (build) — see tests/out/15_stockchart_indicators/render_build.log"
    fail=$((fail+1))
fi

echo "== drawings module test =="
if g++ -O2 -Wall -Wextra -std=c++17 -o tests/out/drawings_test \
       tests/drawings_test.cpp -I src \
       $(pkg-config --cflags --libs cairo) \
       2>tests/out/drawings_build.log; then
    if tests/out/drawings_test >tests/out/drawings_test.log 2>&1; then
        echo "PASS drawings"; pass=$((pass+1))
    else
        echo "FAIL drawings — see tests/out/drawings_test.log"
        fail=$((fail+1))
    fi
else
    echo "FAIL drawings (build) — see tests/out/drawings_build.log"
    fail=$((fail+1))
fi

echo "== chart pro render test =="
if g++ -O2 -Wall -std=c++17 -o tests/out/16_stockchart_pro/render_test \
       tests/chart_pro_render_test.cpp tests/out/16_stockchart_pro/StockChart_001.cpp \
       -Itests/out/16_stockchart_pro $(pkg-config --cflags --libs gtk+-3.0) \
       2>tests/out/16_stockchart_pro/render_build.log; then
    if timeout 15 xvfb-run -a tests/out/16_stockchart_pro/render_test; then
        echo "PASS chart-pro-render"; pass=$((pass+1))
    else
        echo "FAIL chart-pro-render"; fail=$((fail+1))
    fi
else
    echo "FAIL chart-pro-render (build) — see tests/out/16_stockchart_pro/render_build.log"
    fail=$((fail+1))
fi

echo "== chart pro screenshot (1024x768) =="
if g++ -O2 -Wall -std=c++17 -o tests/out/16_stockchart_pro/screenshot \
       tests/chart_screenshot.cpp tests/out/16_stockchart_pro/MyWindow.cpp \
       tests/out/16_stockchart_pro/StockChart_001.cpp \
       -Itests/out/16_stockchart_pro $(pkg-config --cflags --libs gtk+-3.0) \
       2>tests/out/16_stockchart_pro/shot_build.log; then
    if timeout 20 xvfb-run -a tests/out/16_stockchart_pro/screenshot \
           tests/out/chart_pro_1024x768.png; then
        echo "PASS chart-pro-screenshot"; pass=$((pass+1))
    else
        echo "FAIL chart-pro-screenshot"; fail=$((fail+1))
    fi
else
    echo "FAIL chart-pro-screenshot (build) — see tests/out/16_stockchart_pro/shot_build.log"
    fail=$((fail+1))
fi

echo "== matrix: $pass passed, $fail failed =="
[ $fail -eq 0 ]

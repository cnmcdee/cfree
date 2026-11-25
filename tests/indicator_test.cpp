// indicator_test.cpp — visual + numeric test of all 250 StockChart
// indicator submodules in src/indicators.h.
//
// For every registered indicator:
//   1. compute() over the 3-day fake Massive series must produce finite
//      values (line/bar indicators) — patterns may legitimately not fire;
//   2. the indicator is rendered (with candles) onto its own cairo
//      surface and we count pixels painted in the indicator's palette
//      colours — proof it actually draws;
//   3. a labelled contact sheet of all 250 renders is written to
//      tests/out/indicators_contact_sheet.png for eyeballing.
// Plus crafted-bar detection checks for famous patterns and range
// checks for bounded oscillators.
//
// Build: g++ -O2 -std=c++17 -I src tests/indicator_test.cpp -o /tmp/itest
//          $(pkg-config --cflags --libs gtk+-3.0)
// Run:   /tmp/itest tests/data/SPY_fake_long.csv tests/out

#include <cairo.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "indicators.h"

using fdi::Bar;

static std::vector<Bar> load_csv(const std::string& path) {
    std::ifstream in(path);
    std::vector<Bar> out;
    std::string line;
    if (!std::getline(in, line)) return out;  // header
    while (std::getline(in, line)) {
        std::stringstream ss(line);
        std::string f[8];
        for (int i = 0; i < 8; ++i)
            if (!std::getline(ss, f[i], ',')) f[i] = "0";
        Bar b;
        b.volume = std::atof(f[1].c_str());
        b.open = std::atof(f[2].c_str());
        b.close = std::atof(f[3].c_str());
        b.high = std::atof(f[4].c_str());
        b.low = std::atof(f[5].c_str());
        b.time = std::atoll(f[6].c_str());
        out.push_back(b);
    }
    return out;
}

// Draw light-grey candles for bars [begin, begin+count) into an area.
static void draw_candles(cairo_t* cr, const std::vector<Bar>& bars,
                         size_t begin, size_t count, double x, double y,
                         double w, double h, double& lo, double& hi) {
    lo = 1e300;
    hi = -1e300;
    for (size_t i = begin; i < begin + count && i < bars.size(); ++i) {
        lo = std::min(lo, bars[i].low);
        hi = std::max(hi, bars[i].high);
    }
    if (hi - lo < 1e-9) hi = lo + 1;
    double slot = w / count;
    for (size_t k = 0; k < count && begin + k < bars.size(); ++k) {
        const Bar& b = bars[begin + k];
        double cx = x + slot * k + slot / 2;
        auto Y = [&](double v) { return y + (hi - v) / (hi - lo) * h; };
        cairo_set_source_rgb(cr, 0.78, 0.78, 0.78);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, cx, Y(b.high));
        cairo_line_to(cr, cx, Y(b.low));
        cairo_stroke(cr);
        double y1 = Y(std::max(b.open, b.close));
        double y2 = Y(std::min(b.open, b.close));
        if (y2 - y1 < 1) y2 = y1 + 1;
        cairo_rectangle(cr, cx - std::max(0.5, slot * 0.35), y1,
                        std::max(1.0, slot * 0.7), y2 - y1);
        cairo_fill(cr);
    }
}

// Count pixels close to any palette colour (indicator ink).
static long count_ink(cairo_surface_t* surf) {
    int w = cairo_image_surface_get_width(surf);
    int h = cairo_image_surface_get_height(surf);
    int stride = cairo_image_surface_get_stride(surf);
    unsigned char* data = cairo_image_surface_get_data(surf);
    long ink = 0;
    for (int yy = 0; yy < h; ++yy) {
        for (int xx = 0; xx < w; ++xx) {
            unsigned char* px = data + yy * stride + xx * 4;
            double r = px[2] / 255.0, g = px[1] / 255.0, b = px[0] / 255.0;
            for (int c = 0; c < fdi::kPaletteSize; ++c) {
                double dr = r - fdi::kPalette[c][0];
                double dg = g - fdi::kPalette[c][1];
                double db = b - fdi::kPalette[c][2];
                if (dr * dr + dg * dg + db * db < 0.09) {
                    ++ink;
                    break;
                }
            }
        }
    }
    return ink;
}

// A pattern-rich synthetic series: overnight gaps, varied bodies and
// occasional dojis — minute bars from a continuous feed rarely gap, so
// gap patterns (Kicking, Abandoned Baby, stars...) can never fire on
// the plain random walk. Patterns are tested on both series.
static std::vector<Bar> make_gapped_bars() {
    std::vector<Bar> out;
    unsigned long long s = 88172645463325252ULL;
    auto rnd = [&]() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return (s >> 11) / 9007199254740992.0;
    };
    double price = 100;
    for (int i = 0; i < 3000; ++i) {
        Bar b;
        double gap = (rnd() - 0.5) * 1.6;
        b.open = price + gap;
        double body = (rnd() - 0.48) * 3.0;
        if (i % 37 == 0) body = (rnd() - 0.5) * 0.06;  // forced doji
        if (i % 53 == 0) body = (rnd() > 0.5 ? 1 : -1) * (2.5 + rnd());
        b.close = b.open + body;
        b.high = std::max(b.open, b.close) + rnd() * 0.9;
        b.low = std::min(b.open, b.close) - rnd() * 0.9;
        b.volume = 1000 + rnd() * 40000;
        b.time = (long long)i * 60000000000LL;
        out.push_back(b);
        price = b.close;
        if (price < 20) price = 100;
    }
    return out;
}

int main(int argc, char** argv) {
    std::string csv = argc > 1 ? argv[1] : "tests/data/SPY_fake_long.csv";
    std::string outdir = argc > 2 ? argv[2] : "tests/out";
    std::vector<Bar> bars = load_csv(csv);
    if (bars.size() < 200) {
        std::printf("FAIL: only %zu bars loaded from %s\n", bars.size(),
                    csv.c_str());
        return 1;
    }
    std::printf("loaded %zu bars\n", bars.size());
    std::vector<Bar> gapped = make_gapped_bars();
    std::printf("plus %zu gapped pattern-test bars\n", gapped.size());
    const size_t VISIBLE = 400;
    size_t begin = bars.size() - VISIBLE;

    int passed = 0, failed = 0, nofire = 0;
    std::vector<std::string> fail_names, nofire_names;

    // Contact sheet geometry: 5 columns x 50 rows.
    const int COLS = 5, CELL_W = 380, CELL_H = 112;
    int rows = (int)((fdi::all_defs().size() + COLS - 1) / COLS);
    cairo_surface_t* sheet = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, COLS * CELL_W, rows * CELL_H);
    cairo_t* scr = cairo_create(sheet);
    cairo_set_source_rgb(scr, 1, 1, 1);
    cairo_paint(scr);

    size_t idx = 0;
    for (const fdi::Def& def : fdi::all_defs()) {
        auto ind = fdi::make_indicator(def.name);
        if (!ind) {
            std::printf("FAIL %-28s cannot construct\n", def.name);
            ++failed;
            fail_names.push_back(def.name);
            ++idx;
            continue;
        }
        // 1. Compute. Patterns also run on the gapped series.
        fdi::Lines lines = ind->compute(bars);
        long finite = 0, markers_fired = 0;
        for (const auto& s : lines)
            for (double v : s) {
                if (std::isfinite(v)) ++finite;
                if (def.kind == fdi::PlotKind::Markers && v != 0 &&
                    std::isfinite(v))
                    ++markers_fired;
            }
        // Render dataset: patterns render on the gapped series, windowed
        // to end at the last firing so a triangle is in view.
        const std::vector<Bar>* rbars = &bars;
        size_t rbegin = begin;
        if (def.kind == fdi::PlotKind::Markers) {
            fdi::Lines gl = ind->compute(gapped);
            long gfired = 0;
            size_t last_fire = 0;
            if (!gl.empty())
                for (size_t i = 0; i < gl[0].size(); ++i)
                    if (gl[0][i] != 0 && std::isfinite(gl[0][i])) {
                        ++gfired;
                        last_fire = i;
                    }
            markers_fired += gfired;
            if (gfired > 0) {
                rbars = &gapped;
                rbegin = last_fire >= VISIBLE - 1 ? last_fire - VISIBLE + 1
                                                  : 0;
            }
        }
        // 2. Render on its own surface and count ink.
        cairo_surface_t* surf =
            cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 480, 300);
        cairo_t* cr = cairo_create(surf);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_paint(cr);
        double lo, hi;
        bool is_pane = def.kind == fdi::PlotKind::Pane;
        draw_candles(cr, *rbars, rbegin, VISIBLE, 4, 4, 472,
                     is_pane ? 190 : 292, lo, hi);
        fdi::PlotArea area;
        area.cr = cr;
        area.begin = rbegin;
        area.count = VISIBLE;
        if (is_pane) {
            area.x = 4;
            area.y = 206;
            area.w = 472;
            area.h = 90;
        } else {
            area.x = 4;
            area.y = 4;
            area.w = 472;
            area.h = 292;
            area.lo = lo;
            area.hi = hi;
        }
        ind->draw(area, *rbars);
        cairo_surface_flush(surf);
        long ink = count_ink(surf);
        cairo_destroy(cr);
        cairo_surface_destroy(surf);

        bool ok = true;
        const char* note = "";
        if (def.kind == fdi::PlotKind::Markers) {
            if (markers_fired == 0) {
                ok = false;
                note = "pattern did not fire on this data";
                ++nofire;
                nofire_names.push_back(def.name);
            } else if (ink == 0) {
                ok = false;
                note = "fired but drew nothing";
            }
        } else {
            if (finite == 0) {
                ok = false;
                note = "no finite values";
            } else if (ink == 0) {
                ok = false;
                note = "computed but drew nothing";
            }
        }
        if (ok) {
            ++passed;
        } else if (def.kind != fdi::PlotKind::Markers ||
                   markers_fired > 0) {
            ++failed;
            fail_names.push_back(std::string(def.name) + " (" + note + ")");
        }
        std::printf("%-4s %-28s finite=%-7ld fired=%-4ld ink=%-6ld %s\n",
                    ok ? "PASS" : (def.kind == fdi::PlotKind::Markers &&
                                    markers_fired == 0
                                        ? "NOFIRE"
                                        : "FAIL"),
                    def.name, finite, markers_fired, ink, note);

        // 3. Contact sheet cell.
        int cx = (int)(idx % COLS) * CELL_W, cy = (int)(idx / COLS) * CELL_H;
        cairo_set_source_rgb(scr, 1, 1, 1);
        cairo_rectangle(scr, cx, cy, CELL_W, CELL_H);
        cairo_fill(scr);
        cairo_set_source_rgb(scr, 0.6, 0.6, 0.6);
        cairo_set_line_width(scr, 0.5);
        cairo_rectangle(scr, cx + 0.5, cy + 0.5, CELL_W - 1, CELL_H - 1);
        cairo_stroke(scr);
        cairo_set_source_rgb(scr, 0.15, 0.15, 0.15);
        cairo_select_font_face(scr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(scr, 10);
        cairo_move_to(scr, cx + 5, cy + 13);
        cairo_show_text(scr, def.name);
        size_t sbegin = bars.size() - 300;
        const std::vector<Bar>* cbars = &bars;
        if (def.kind == fdi::PlotKind::Markers && rbars == &gapped) {
            cbars = &gapped;
            sbegin = rbegin + 100;
        }
        double slo, shi;
        draw_candles(scr, *cbars, sbegin, 300, cx + 4, cy + 18, CELL_W - 8,
                     is_pane ? 52 : CELL_H - 24, slo, shi);
        fdi::PlotArea cell;
        cell.cr = scr;
        cell.begin = sbegin;
        cell.count = 300;
        if (is_pane) {
            cell.x = cx + 4;
            cell.y = cy + 74;
            cell.w = CELL_W - 8;
            cell.h = CELL_H - 78;
        } else {
            cell.x = cx + 4;
            cell.y = cy + 18;
            cell.w = CELL_W - 8;
            cell.h = CELL_H - 24;
            cell.lo = slo;
            cell.hi = shi;
        }
        ind->draw(cell, *cbars);
        ++idx;
    }
    std::string sheet_path = outdir + "/indicators_contact_sheet.png";
    cairo_surface_write_to_png(sheet, sheet_path.c_str());
    cairo_destroy(scr);
    cairo_surface_destroy(sheet);
    std::printf("contact sheet: %s\n", sheet_path.c_str());

    // 4. Crafted-bar pattern detection checks.
    auto mk = [](double o, double h, double l, double c) {
        Bar b;
        b.open = o;
        b.high = h;
        b.low = l;
        b.close = c;
        b.volume = 1000;
        return b;
    };
    int crafted_fail = 0;
    auto expect = [&](const char* name, const std::vector<Bar>& seq,
                      size_t at, double sign) {
        auto ind = fdi::make_indicator(name);
        fdi::Lines lines = ind->compute(seq);
        double got = lines.empty() || at >= lines[0].size() ? 0 : lines[0][at];
        bool ok = (sign > 0 && got > 0) || (sign < 0 && got < 0);
        std::printf("crafted %-16s at %zu: got %+.0f %s\n", name, at, got,
                    ok ? "OK" : "MISMATCH");
        if (!ok) ++crafted_fail;
    };
    expect("Doji", {mk(100, 101, 99, 100.05)}, 0, +1);
    expect("Engulfing",
           {mk(102, 102.5, 99.5, 100), mk(99.5, 103, 99, 102.5)}, 1, +1);
    expect("Hammer",
           {mk(100, 100.5, 96.5, 97), mk(100, 100.5, 96.8, 100.4)}, 1, +1);
    expect("Morning Star",
           {mk(110, 110.5, 99.5, 100), mk(98, 98.8, 97.5, 98.5),
            mk(99, 107.5, 98.5, 107)},
           2, +1);
    expect("Shooting Star",
           {mk(100, 101, 99, 100.8), mk(102, 105, 101.9, 102.15)}, 1,
           -1);
    expect("Takuri",
           {mk(100, 101.3, 99.7, 101), mk(101, 102.3, 100.7, 102),
            mk(102, 103.3, 101.7, 103), mk(100, 100.4, 97.5, 100.3)},
           3, +1);
    expect("Thrusting",
           {mk(100, 110.4, 99.6, 110), mk(111, 111.3, 106.5, 107)}, 1,
           -1);
    expect("Two Crows",
           {mk(100, 110.4, 99.6, 110), mk(112, 112.3, 107.6, 108),
            mk(111, 111.3, 104.6, 105)},
           2, -1);
    expect("Concealing Baby Swallow",
           {mk(110, 110.2, 103.8, 104), mk(103, 103.4, 98.6, 99),
            mk(100.5, 101, 96.6, 97), mk(102, 102.3, 94.7, 95)},
           3, +1);
    expect("Kicking By Length",
           {mk(103, 103.2, 99.8, 100), mk(106, 112.2, 105.8, 112)}, 1,
           +1);
    expect("Gap Side Side White",
           {mk(100, 101.3, 99.7, 101), mk(103, 105.4, 102.5, 105),
            mk(103.1, 106.3, 102.9, 106)},
           2, +1);
    expect("Piercing Line",
           {mk(110, 110.5, 99.5, 100), mk(99, 106.5, 98.8, 106)}, 1, +1);
    expect("Dark Cloud Cover",
           {mk(100, 110.5, 99.5, 110), mk(111, 111.5, 103.5, 104)}, 1,
           -1);
    expect("Three White Soldiers",
           {mk(100, 104.5, 99.5, 104), mk(103, 107.5, 102.5, 107),
            mk(106, 110.5, 105.5, 110)},
           2, +1);
    expect("Abandoned Baby",
           {mk(110, 110.5, 99.5, 100), mk(97, 97.3, 96.7, 97.05),
            mk(99, 106.5, 98.5, 106)},
           2, +1);
    expect("Kicking",
           {mk(105, 105.2, 99.8, 100), mk(107, 112.2, 106.8, 112)}, 1,
           +1);
    expect("Tristar",
           {mk(100, 100.6, 99.4, 100), mk(102, 102.6, 101.4, 102),
            mk(100, 100.6, 99.4, 100)},
           2, -1);

    // 5. Bounded-oscillator range checks.
    auto range_check = [&](const char* name, double lo, double hi) {
        auto ind = fdi::make_indicator(name);
        fdi::Lines lines = ind->compute(bars);
        bool ok = true, any = false;
        for (const auto& s : lines)
            for (double v : s)
                if (std::isfinite(v)) {
                    any = true;
                    if (v < lo - 1e-6 || v > hi + 1e-6) ok = false;
                }
        std::printf("range %-16s in [%g, %g]: %s\n", name, lo, hi,
                    ok && any ? "OK" : "MISMATCH");
        if (!ok || !any) ++crafted_fail;
    };
    range_check("RSI", 0, 100);
    range_check("Stochastic Fast", 0, 100);
    range_check("Williams %R", -100, 0);
    range_check("MFI", 0, 100);
    range_check("Stochastic RSI", 0, 100);

    std::printf("\nSUMMARY: %d passed, %d failed, %d patterns not fired, "
                "%d crafted/range mismatches\n",
                passed, failed, nofire, crafted_fail);
    for (auto& f : fail_names) std::printf("  FAIL: %s\n", f.c_str());
    for (auto& f : nofire_names) std::printf("  NOFIRE: %s\n", f.c_str());
    return (failed || crafted_fail) ? 1 : 0;
}

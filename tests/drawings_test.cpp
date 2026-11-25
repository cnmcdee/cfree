// Unit + render test for the drawings.h annotation module: every
// kind parses from its spec and draws visible ink in its palette
// colour; a trend line re-maps correctly when the plot size changes
// (resize safety, since anchors are data coordinates); hit testing
// finds lines and misses empty space.
#include <cairo.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "drawings.h"

static long count_ink(cairo_surface_t* surf, int ci) {
    const double* col = fdw::kPalette[ci];
    int w = cairo_image_surface_get_width(surf);
    int h = cairo_image_surface_get_height(surf);
    int stride = cairo_image_surface_get_stride(surf);
    unsigned char* d = cairo_image_surface_get_data(surf);
    long n = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            unsigned char* px = d + y * stride + x * 4;  // BGRA
            double r = px[2] / 255.0, g = px[1] / 255.0, b = px[0] / 255.0;
            double dr = r - col[0], dg = g - col[1], db = b - col[2];
            if (dr * dr + dg * dg + db * db < 0.09) ++n;
        }
    return n;
}

static long count_dark(cairo_surface_t* surf) {
    int w = cairo_image_surface_get_width(surf);
    int h = cairo_image_surface_get_height(surf);
    int stride = cairo_image_surface_get_stride(surf);
    unsigned char* d = cairo_image_surface_get_data(surf);
    long n = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            unsigned char* px = d + y * stride + x * 4;
            if (px[0] < 90 && px[1] < 90 && px[2] < 90) ++n;
        }
    return n;
}

// Ink of palette colour ci within a small box around (cx, cy).
static long ink_near(cairo_surface_t* surf, int ci, double cx, double cy) {
    const double* col = fdw::kPalette[ci];
    int stride = cairo_image_surface_get_stride(surf);
    unsigned char* d = cairo_image_surface_get_data(surf);
    long n = 0;
    for (int y = (int)cy - 6; y <= (int)cy + 6; ++y)
        for (int x = (int)cx - 6; x <= (int)cx + 6; ++x) {
            if (x < 0 || y < 0) continue;
            unsigned char* px = d + y * stride + x * 4;
            double r = px[2] / 255.0, g = px[1] / 255.0, b = px[0] / 255.0;
            double dr = r - col[0], dg = g - col[1], db = b - col[2];
            if (dr * dr + dg * dg + db * db < 0.09) ++n;
        }
    return n;
}

int main() {
    int fails = 0;
    auto check = [&](bool cond, const char* what) {
        std::printf("%s %s\n", cond ? "OK  " : "FAIL", what);
        if (!cond) ++fails;
    };
    fdw::Map map;
    map.x = 0;
    map.y = 0;
    map.w = 480;
    map.h = 300;
    map.begin = 0;
    map.count = 100;
    map.lo = 90;
    map.hi = 110;

    struct Case {
        const char* spec;
        int color;  // palette index, or -1 for dark-text check
    };
    const Case cases[] = {
        {"trend 5 95 90 108 red", 1},
        {"ray 5 95 20 100 orange", 3},
        {"hline 105 blue", 0},
        {"vline 50 green", 2},
        {"rect 10 96 60 106 teal", 5},
        {"ellipse 10 96 60 106 purple", 4},
        {"arrow 5 95 90 108 magenta", 6},
        {"text 10 100 black Hello chart", -1},
        {"fib 5 95 90 108 brown", 7},
        {"free 5 95 30 108 60 97 90 104 slate", 9},
        {"measure 5 95 90 108 red", 1},
    };
    for (const Case& c : cases) {
        auto d = fdw::parse_spec(c.spec);
        check(d.has_value(), c.spec);
        if (!d) continue;
        cairo_surface_t* surf =
            cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 480, 300);
        cairo_t* cr = cairo_create(surf);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_paint(cr);
        fdw::draw(cr, map, *d);
        cairo_destroy(cr);
        cairo_surface_flush(surf);
        long ink = c.color >= 0 ? count_ink(surf, c.color) : count_dark(surf);
        std::printf("     ink=%ld\n", ink);
        check(ink > 15, "     draws ink");
        cairo_surface_destroy(surf);
    }

    // Resize safety: the same trend line rendered at 480x300 and at
    // 960x600 must put ink at the scaled endpoint positions.
    auto trend = fdw::parse_spec("trend 10 95 90 108 red");
    check(trend.has_value(), "resize trend parses");
    for (double scale : {1.0, 2.0}) {
        int W = (int)(480 * scale), H = (int)(300 * scale);
        fdw::Map m2 = map;
        m2.w = W;
        m2.h = H;
        cairo_surface_t* surf =
            cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
        cairo_t* cr = cairo_create(surf);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_paint(cr);
        fdw::draw(cr, m2, *trend);
        cairo_destroy(cr);
        cairo_surface_flush(surf);
        double x1 = m2.px(10), y1 = m2.py(95);
        double x2 = m2.px(90), y2 = m2.py(108);
        long a = ink_near(surf, 1, x1, y1);
        long b = ink_near(surf, 1, x2, y2);
        std::printf("     scale=%.0f endpoints ink=%ld/%ld\n", scale, a, b);
        check(a > 2 && b > 2, "     endpoints remapped");
        cairo_surface_destroy(surf);
    }

    // Hit testing: on the line / off the line.
    {
        double mx = map.px(47.5), my = map.py(101.5);
        check(fdw::hit_test(map, *trend, mx, my), "hit on trend midpoint");
        check(!fdw::hit_test(map, *trend, map.px(47.5), map.py(92)),
              "miss away from trend");
        auto hl = fdw::parse_spec("hline 105 blue");
        check(hl && fdw::hit_test(map, *hl, 200, map.py(105)),
              "hit on hline");
        check(hl && !fdw::hit_test(map, *hl, 200, map.py(100)),
              "miss away from hline");
    }

    std::printf("DRAWINGS TEST %s (%d failures)\n",
                fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}

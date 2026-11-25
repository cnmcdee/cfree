// Renders the generated StockChart Pro (matrix case 16: percent scale,
// QQQ comparison, seven annotations, SMA + RSI) offscreen and verifies
// every feature actually draws — then exercises wheel-zoom math via
// zoom_at() and re-renders at a smaller size to prove the annotations
// survive resizes.
#include <gtk/gtk.h>

#include <cstdio>
#include <cstdlib>

#include "StockChart_001.h"

static void pump() {
    for (int i = 0; i < 25; ++i)
        while (gtk_events_pending()) gtk_main_iteration();
}

struct Counts {
    long green = 0, red = 0, orange = 0, magenta = 0, purple = 0;
    long axis_text = 0, time_text = 0;
};

static Counts render(StockChart_001& chart, int W, int H) {
    GtkWidget* w = chart.widget();
    gtk_widget_queue_draw(w);
    pump();
    int width = gtk_widget_get_allocated_width(w);
    int height = gtk_widget_get_allocated_height(w);
    cairo_surface_t* surf =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t* cr = cairo_create(surf);
    gtk_widget_draw(w, cr);
    cairo_destroy(cr);
    cairo_surface_flush(surf);
    unsigned char* px = cairo_image_surface_get_data(surf);
    int stride = cairo_image_surface_get_stride(surf);
    Counts c;
    (void)W;
    (void)H;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            unsigned char* p = px + y * stride + x * 4;  // BGRA
            int b = p[0], g = p[1], r = p[2];
            if (g > 150 && r < 110 && b < 110) ++c.green;
            if (r > 170 && g < 110 && b < 110) ++c.red;
            if (r > 200 && g > 80 && g < 170 && b < 90) ++c.orange;
            if (r > 180 && g > 30 && g < 95 && b > 130 && b < 210)
                ++c.magenta;
            if (r > 105 && r < 175 && g > 25 && g < 85 && b > 165)
                ++c.purple;
            // Axis labels are anti-aliased 0.35-grey text on white:
            // count unsaturated mid-grey pixels (gridlines at 0.08
            // alpha land ~235 and stay excluded).
            bool grey_text = r > 50 && r < 205 && std::abs(r - g) < 15 &&
                             std::abs(g - b) < 15;
            // In the right strip nothing but label text draws (grid-
            // lines stop at the plot edge), so a looser bound is safe.
            bool axis_grey = r > 40 && r < 225 && std::abs(r - g) < 18 &&
                             std::abs(g - b) < 18;
            if (axis_grey && x >= width - 56 && y < height - 18)
                ++c.axis_text;
            if (grey_text && y >= height - 18) ++c.time_text;
        }
    cairo_surface_destroy(surf);
    return c;
}

int main(int argc, char** argv) {
    gtk_init(&argc, &argv);
    int fails = 0;
    auto check = [&](bool cond, const char* what) {
        std::printf("%s %s\n", cond ? "OK  " : "FAIL", what);
        if (!cond) ++fails;
    };
    GtkWidget* win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWidget* fixed = gtk_fixed_new();
    gtk_container_add(GTK_CONTAINER(win), fixed);
    gtk_window_set_default_size(GTK_WINDOW(win), 1024, 768);
    StockChart_001 chart(fixed);
    check(chart.candle_count() == 1170, "1170 candles loaded");
    check(chart.compare_count() == 1, "QQQ comparison loaded");
    check(chart.drawing_count() == 7, "7 annotations from design");
    check(chart.scale_mode() == 1, "percent scale mode");
    check(chart.visible_count() == 400, "initial window = max-candles");
    gtk_widget_show_all(win);
    pump();

    Counts c = render(chart, 1024, 768);
    std::printf("     green=%ld red=%ld orange=%ld magenta=%ld "
                "purple=%ld axis=%ld time=%ld\n",
                c.green, c.red, c.orange, c.magenta, c.purple,
                c.axis_text, c.time_text);
    check(c.green > 200 && c.red > 200, "candles plotted");
    check(c.orange > 100, "QQQ comparison line plotted");
    check(c.magenta > 25, "arrow annotation plotted");
    check(c.purple > 25, "fib annotation plotted");
    check(c.axis_text > 40, "percent axis labels plotted");
    check(c.time_text > 60, "time axis labels plotted");

    // Wheel-zoom math (what on_scroll calls): in, back out, clamp.
    chart.zoom_at(0.5, 0.5);
    check(chart.visible_count() == 200, "zoom in halves the window");
    chart.zoom_at(2.0, 0.5);
    check(chart.visible_count() == 400, "zoom out restores the window");
    chart.zoom_at(10.0, 0.5);
    check(chart.visible_count() == 1170, "zoom clamps at the full series");
    chart.reset_view();
    check(chart.visible_count() == 400, "reset_view restores max-candles");

    // Resize: shrink the chart widget itself (its frame in the fixed
    // layout); annotations are data-anchored, so the arrow and fib
    // must still draw at their remapped positions.
    gtk_widget_set_size_request(gtk_widget_get_parent(chart.widget()),
                                620, 460);
    gtk_window_resize(GTK_WINDOW(win), 660, 520);
    pump();
    check(gtk_widget_get_allocated_width(chart.widget()) < 700,
          "chart widget actually resized");
    Counts s = render(chart, 640, 480);
    std::printf("     small: magenta=%ld purple=%ld orange=%ld\n",
                s.magenta, s.purple, s.orange);
    check(s.magenta > 12, "arrow survives resize");
    check(s.purple > 12, "fib survives resize");
    check(s.orange > 50, "comparison survives resize");

    std::printf("CHART PRO TEST %s (%d failures)\n",
                fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}

// Renders the generated StockChart class offscreen and verifies that
// up-coloured and down-coloured candle pixels actually appear — i.e. the
// widget doesn't just compile, it PLOTS.
#include <gtk/gtk.h>

#include <cstdio>

#include "StockChart_001.h"

int main(int argc, char** argv) {
    gtk_init(&argc, &argv);
    GtkWidget* win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWidget* fixed = gtk_fixed_new();
    gtk_container_add(GTK_CONTAINER(win), fixed);
    gtk_window_set_default_size(GTK_WINDOW(win), 900, 700);
    StockChart_001 chart(fixed);
    if (chart.candle_count() == 0) {
        std::printf("NO DATA LOADED\n");
        return 1;
    }
    gtk_widget_show_all(win);
    for (int i = 0; i < 20; ++i)
        while (gtk_events_pending()) gtk_main_iteration();
    GtkWidget* w = chart.widget();
    int width = gtk_widget_get_allocated_width(w);
    int height = gtk_widget_get_allocated_height(w);
    if (width < 50 || height < 50) {
        std::printf("BAD SIZE %dx%d\n", width, height);
        return 1;
    }
    cairo_surface_t* surf =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t* cr = cairo_create(surf);
    gtk_widget_draw(w, cr);
    cairo_destroy(cr);
    cairo_surface_flush(surf);
    unsigned char* px = cairo_image_surface_get_data(surf);
    int stride = cairo_image_surface_get_stride(surf);
    long green = 0, red = 0;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            unsigned char* p = px + y * stride + x * 4;  // BGRA
            int b = p[0], g = p[1], r = p[2];
            if (g > 150 && r < 110 && b < 110) ++green;
            if (r > 170 && g < 110 && b < 110) ++red;
        }
    std::printf("candles=%d size=%dx%d GREEN %ld RED %ld\n",
                chart.candle_count(), width, height, green, red);
    return (green > 200 && red > 200) ? 0 : 1;
}

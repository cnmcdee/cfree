// Renders the generated StockChart (matrix case 15, with indicator
// submodules attached by design) offscreen and verifies the indicators
// actually PLOT: the SMA's palette-blue line must appear in the price
// pane, and palette-coloured pixels must appear in the oscillator pane
// strip at the bottom (RSI/MACD live there).
#include <gtk/gtk.h>

#include <cstdio>

#include "StockChart_001.h"

int main(int argc, char** argv) {
    gtk_init(&argc, &argv);
    GtkWidget* win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWidget* fixed = gtk_fixed_new();
    gtk_container_add(GTK_CONTAINER(win), fixed);
    gtk_window_set_default_size(GTK_WINDOW(win), 1000, 700);
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
    // Palette blue (SMA and RSI line colour): b high, r low, g mid.
    long blue_top = 0, blue_bottom = 0;
    int split = (int)(height * 0.66);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            unsigned char* p = px + y * stride + x * 4;  // BGRA
            int b = p[0], g = p[1], r = p[2];
            if (b > 150 && r < 110 && g > 40 && g < 170) {
                if (y < split)
                    ++blue_top;
                else
                    ++blue_bottom;
            }
        }
    std::printf("candles=%d size=%dx%d BLUE price-pane %ld pane %ld\n",
                chart.candle_count(), width, height, blue_top,
                blue_bottom);
    return (blue_top > 100 && blue_bottom > 50) ? 0 : 1;
}

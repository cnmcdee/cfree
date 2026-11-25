// Captures the generated case-16 application window (MyWindow with
// the StockChart Pro) at exactly 1024x768 to a PNG, for a visual
// check of the whole feature set working together.
#include <gtk/gtk.h>

#include <cstdio>

#include "MyWindow.h"

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : "chart_pro_1024x768.png";
    gtk_init(&argc, &argv);
    MyWindow app;
    app.show();
    GtkWidget* win = app.window();
    gtk_window_resize(GTK_WINDOW(win), 1024, 768);
    // Let layout + first draws settle.
    for (int i = 0; i < 60; ++i) {
        while (gtk_events_pending()) gtk_main_iteration();
        g_usleep(10000);
    }
    GdkWindow* gw = gtk_widget_get_window(win);
    int w = gdk_window_get_width(gw);
    int h = gdk_window_get_height(gw);
    GdkPixbuf* pb = gdk_pixbuf_get_from_window(gw, 0, 0, w, h);
    if (!pb) {
        std::printf("SCREENSHOT FAIL: no pixbuf\n");
        return 1;
    }
    GError* err = nullptr;
    if (!gdk_pixbuf_save(pb, out, "png", &err, nullptr)) {
        std::printf("SCREENSHOT FAIL: %s\n", err ? err->message : "?");
        return 1;
    }
    std::printf("SCREENSHOT OK %s (%dx%d)\n", out, w, h);
    return (w == 1024 && h == 768) ? 0 : 1;
}

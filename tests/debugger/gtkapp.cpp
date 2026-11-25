// Debugger test target: small GTK app with a button callback.
// The button auto-clicks itself 800 ms after startup (so the test can
// drive it headlessly under Xvfb), and the app quits 4 s after startup.
// Compile with: g++ -g -O0 -o gtkapp gtkapp.cpp $(pkg-config --cflags --libs gtk+-3.0)
#include <gtk/gtk.h>
#include <cstdio>

static int counter = 0;
static GtkWidget* label = nullptr;
static GtkWidget* button = nullptr;

static void on_clicked(GtkButton*, gpointer) {
    int before = counter;   // MARK:CB
    counter = before + 1;   // MARK:CB_NEXT
    char buf[64];
    std::snprintf(buf, sizeof buf, "Clicks: %d", counter);
    gtk_label_set_text(GTK_LABEL(label), buf);
}

static gboolean auto_click(gpointer) {
    gtk_button_clicked(GTK_BUTTON(button));
    return G_SOURCE_REMOVE;
}

static gboolean quit_app(gpointer) {
    gtk_main_quit();
    return G_SOURCE_REMOVE;
}

int main(int argc, char** argv) {
    gtk_init(&argc, &argv);
    GtkWidget* win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), "Debugger GTK Test");
    gtk_window_set_default_size(GTK_WINDOW(win), 300, 120);
    g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_add(GTK_CONTAINER(win), box);
    label = gtk_label_new("Clicks: 0");
    gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 0);
    button = gtk_button_new_with_label("Click me");
    g_signal_connect(button, "clicked", G_CALLBACK(on_clicked), nullptr);
    gtk_box_pack_start(GTK_BOX(box), button, TRUE, TRUE, 0);
    gtk_widget_show_all(win);
    g_timeout_add(800, auto_click, nullptr);
    g_timeout_add(4000, quit_app, nullptr);
    gtk_main();
    std::printf("CLICKS=%d\n", counter);
    std::fflush(stdout);
    return 0;
}

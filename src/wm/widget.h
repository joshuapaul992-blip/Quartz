/* widget.h - minimal retained widget toolkit for Quartz windows.
 *
 * Widgets live in a window's client area (coordinates relative to it). The
 * window manager paints them and routes mouse clicks to their callbacks. */
#ifndef QUARTZ_WIDGET_H
#define QUARTZ_WIDGET_H

#include "../gfx/gfx.h"

typedef struct qz_window qz_window;
typedef struct qz_widget qz_widget;
typedef void (*qz_widget_cb)(qz_widget *w);

enum {
    QZ_WDG_LABEL,
    QZ_WDG_BUTTON,
    QZ_WDG_CHECKBOX,
    QZ_WDG_PANEL
};

struct qz_widget {
    int           type;
    int           x, y, w, h;      /* relative to client area */
    char          text[64];
    int           state;           /* button: pressed; checkbox: checked */
    int           enabled;
    qz_widget_cb  on_click;
    void         *user;
    qz_window    *owner;           /* set when added to a window */
    qz_widget    *next;
};

qz_widget *qz_label   (int x, int y, const char *text);
qz_widget *qz_button  (int x, int y, int w, int h, const char *text, qz_widget_cb cb);
qz_widget *qz_checkbox(int x, int y, const char *text, int checked, qz_widget_cb cb);
qz_widget *qz_panel   (int x, int y, int w, int h);

/* internal: used by the window manager */
void       qz__widget_paint(qz_widget *wd, qz_surface *surf, int ox, int oy);
qz_widget *qz__widget_at(qz_window *win, int lx, int ly);  /* client-local coords */

#endif

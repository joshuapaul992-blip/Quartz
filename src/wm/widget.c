/* widget.c - widget constructors, painting, and hit-testing. */
#include "wm.h"
#include <stdlib.h>
#include <string.h>

static qz_widget *alloc_widget(int type, int x, int y, int w, int h,
                               const char *text, qz_widget_cb cb)
{
    qz_widget *wd = (qz_widget *)malloc(sizeof *wd);
    if (!wd) return 0;
    memset(wd, 0, sizeof *wd);
    wd->type = type;
    wd->x = x; wd->y = y; wd->w = w; wd->h = h;
    wd->enabled = 1;
    wd->on_click = cb;
    if (text) { strncpy(wd->text, text, sizeof wd->text - 1); }
    return wd;
}

qz_widget *qz_label(int x, int y, const char *text)
{
    return alloc_widget(QZ_WDG_LABEL, x, y, qz_text_w(text), QZ_FONT_H, text, 0);
}

qz_widget *qz_button(int x, int y, int w, int h, const char *text, qz_widget_cb cb)
{
    return alloc_widget(QZ_WDG_BUTTON, x, y, w, h, text, cb);
}

qz_widget *qz_checkbox(int x, int y, const char *text, int checked, qz_widget_cb cb)
{
    qz_widget *wd = alloc_widget(QZ_WDG_CHECKBOX, x, y,
                                 16 + 6 + qz_text_w(text), 16, text, cb);
    if (wd) wd->state = checked ? 1 : 0;
    return wd;
}

qz_widget *qz_panel(int x, int y, int w, int h)
{
    return alloc_widget(QZ_WDG_PANEL, x, y, w, h, 0, 0);
}

void qz__widget_paint(qz_widget *wd, qz_surface *s, int ox, int oy)
{
    int x = ox + wd->x, y = oy + wd->y;
    switch (wd->type) {
    case QZ_WDG_LABEL:
        qz_text(s, x, y, wd->text, wd->enabled ? qz_th.text : qz_th.disabled);
        break;
    case QZ_WDG_PANEL:
        qz_bevel(s, x, y, wd->w, wd->h, qz_th.face, qz_th.light, qz_th.dark, 1);
        break;
    case QZ_WDG_BUTTON: {
        int sunk = wd->state ? 1 : 0;
        int tx = x + (wd->w - qz_text_w(wd->text)) / 2 + (sunk ? 1 : 0);
        int ty = y + (wd->h - QZ_FONT_H) / 2 + (sunk ? 1 : 0);
        qz_bevel(s, x, y, wd->w, wd->h, qz_th.face, qz_th.light, qz_th.dark, sunk);
        qz_frame_rect(s, x, y, wd->w, wd->h, qz_th.dark);
        qz_text(s, tx, ty, wd->text, wd->enabled ? qz_th.text : qz_th.disabled);
        break;
    }
    case QZ_WDG_CHECKBOX: {
        qz_bevel(s, x, y, 16, 16, qz_th.white, qz_th.dark, qz_th.light, 1);
        if (wd->state) {
            /* draw a check mark */
            qz_color c = qz_th.black;
            int i;
            for (i = 0; i < 4; i++) { qz_put(s, x + 3 + i, y + 7 + i, c);
                                      qz_put(s, x + 3 + i, y + 8 + i, c); }
            for (i = 0; i < 6; i++) { qz_put(s, x + 7 + i, y + 10 - i, c);
                                      qz_put(s, x + 7 + i, y + 11 - i, c); }
        }
        qz_text(s, x + 16 + 6, y, wd->text, wd->enabled ? qz_th.text : qz_th.disabled);
        break;
    }
    }
}

qz_widget *qz__widget_at(qz_window *win, int lx, int ly)
{
    qz_widget *wd;
    for (wd = win->widgets; wd; wd = wd->next) {
        if (wd->type == QZ_WDG_LABEL || wd->type == QZ_WDG_PANEL) continue;
        if (!wd->enabled) continue;
        if (lx >= wd->x && lx < wd->x + wd->w &&
            ly >= wd->y && ly < wd->y + wd->h)
            return wd;
    }
    return 0;
}

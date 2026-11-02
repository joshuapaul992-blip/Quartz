/* wm.h - Quartz window manager and compositor.
 *
 * The WM keeps a z-ordered list of windows, each backed by its own 32-bpp
 * surface. A dirty-rectangle compositor draws the wallpaper, menu bar, windows
 * and taskbar into an off-screen "scene" surface; the main loop then overlays
 * the mouse cursor and presents only the changed region to the framebuffer. */
#ifndef QUARTZ_WM_H
#define QUARTZ_WM_H

#include "../gfx/gfx.h"
#include "widget.h"

#define QZ_WF_VISIBLE   0x01
#define QZ_WF_MOVABLE   0x02
#define QZ_WF_CLOSEBOX  0x04
#define QZ_WF_TITLEBAR  0x08
#define QZ_WF_DEFAULT   (QZ_WF_VISIBLE|QZ_WF_MOVABLE|QZ_WF_CLOSEBOX|QZ_WF_TITLEBAR)

#define QZ_MENUBAR_H 24
#define QZ_TASKBAR_H 28
#define QZ_TB_H      22   /* window title bar */
#define QZ_WB        4    /* window border */
#define QZ_MAXWIN    32

typedef struct qz_window qz_window;
typedef void (*qz_paint_fn)(qz_window *w);
typedef void (*qz_close_fn)(qz_window *w);

struct qz_window {
    int          id;
    int          x, y, w, h;
    char         title[64];
    unsigned     flags;
    qz_surface  *surf;        /* full-window bitmap (w x h) */
    int          dirty;       /* chrome/client need re-render into surf */
    qz_widget   *widgets;     /* linked list, coords relative to client */
    qz_paint_fn  on_paint;    /* optional custom client painter */
    qz_close_fn  on_close;    /* optional close hook */
    void        *user;
};

/* Theme colors (filled by qz_wm_init, after the video mode is known). */
typedef struct {
    qz_color desk_top, desk_bot;
    qz_color face, light, dark, shadow, black, white, text, disabled;
    qz_color title_a0, title_a1;   /* active title gradient */
    qz_color title_i0, title_i1;   /* inactive */
    qz_color accent, bar;
} qz_theme;
extern qz_theme qz_th;

/* lifecycle */
int   qz_wm_init(void);
void  qz_wm_shutdown(void);

/* windows */
qz_window *qz_window_new(int x, int y, int w, int h, const char *title, unsigned flags);
void  qz_window_close(qz_window *win);
void  qz_window_raise(qz_window *win);
void  qz_window_invalidate(qz_window *win);   /* mark for re-render + redraw */
void  qz_window_move(qz_window *win, int x, int y);
/* client-area geometry within the window surface */
void  qz_window_client(qz_window *win, int *cx, int *cy, int *cw, int *ch);

/* widgets */
qz_widget *qz_window_add(qz_window *win, qz_widget *w);

/* event pump: process input, composite, present. Returns 0 when asked to quit
 * (ESC or last window closed with quit-on-empty set). Call in a loop. */
int   qz_wm_run_once(void);
void  qz_wm_quit(void);
void  qz_wm_set_quit_on_empty(int on);

/* force a full repaint (e.g. after wallpaper change) */
void  qz_wm_invalidate_all(void);

#endif

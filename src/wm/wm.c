/* wm.c - Quartz window manager + dirty-rectangle compositor. */
#include "wm.h"
#include "../input/input.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <dpmi.h>

qz_theme qz_th;

/* ---- cursor (12x19: 0 transparent, 1 black, 2 white) ---- */
static const char CURSOR[19][12] = {
    "1           ","11          ","121         ","1221        ",
    "12221       ","122221      ","1222221     ","12222221    ",
    "122222221   ","1222222221  ","12222222221 ","122222221111",
    "122212221   ","12211222    ","1211 1222   ","111  1222   ",
    "1     1222  ","      1222  ","       111  ",
};
#define CUR_W 12
#define CUR_H 19

/* ---- state ---- */
static qz_window *g_win[QZ_MAXWIN];
static int        g_nwin;
static qz_window *g_focus;
static qz_surface *g_scene;
static int        g_quit, g_quit_on_empty;
static int        g_next_id = 1;

/* accumulated dirty bbox (scene coords) */
static int g_dminx, g_dminy, g_dmaxx, g_dmaxy, g_dirty;

/* interaction */
static qz_window *g_drag; static int g_drag_ox, g_drag_oy;
static qz_widget *g_arm;  static qz_window *g_arm_win;
static int g_cx, g_cy, g_pcx, g_pcy, g_prev_btn;

static void set_focus(qz_window *win);   /* fwd */

/* ---------- dirty tracking ---------- */
static void dirty_add(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) return;
    if (!g_dirty) { g_dminx = x; g_dminy = y; g_dmaxx = x + w; g_dmaxy = y + h; g_dirty = 1; }
    else {
        if (x < g_dminx) g_dminx = x;
        if (y < g_dminy) g_dminy = y;
        if (x + w > g_dmaxx) g_dmaxx = x + w;
        if (y + h > g_dmaxy) g_dmaxy = y + h;
    }
}
void qz_wm_invalidate_all(void) { dirty_add(0, 0, qz_screen->w, qz_screen->h); }

/* ---------- theme ---------- */
int qz_wm_init(void)
{
    g_scene = qz_surface_create(qz_vmode.w, qz_vmode.h);
    if (!g_scene) return -1;
    qz_th.desk_top = qz_rgb(0x2e,0x54,0x7a); qz_th.desk_bot = qz_rgb(0x14,0x24,0x38);
    qz_th.face  = qz_rgb(0xc8,0xc8,0xc8); qz_th.light = qz_rgb(0xff,0xff,0xff);
    qz_th.dark  = qz_rgb(0x80,0x80,0x80); qz_th.shadow= qz_rgb(0x38,0x38,0x38);
    qz_th.black = qz_rgb(0,0,0);          qz_th.white = qz_rgb(0xff,0xff,0xff);
    qz_th.text  = qz_rgb(0x10,0x10,0x10); qz_th.disabled = qz_rgb(0x90,0x90,0x90);
    qz_th.title_a0 = qz_rgb(0x1f,0x4e,0x9b); qz_th.title_a1 = qz_rgb(0x3b,0x7a,0xd4);
    qz_th.title_i0 = qz_rgb(0x70,0x78,0x80); qz_th.title_i1 = qz_rgb(0x98,0xa0,0xa8);
    qz_th.accent = qz_rgb(0xe0,0x50,0x30);   qz_th.bar = qz_rgb(0xc0,0xc0,0xc0);
    g_cx = qz_vmode.w / 2; g_cy = qz_vmode.h / 2; g_pcx = g_cx; g_pcy = g_cy;
    qz_mouse_init(qz_vmode.w - 1, qz_vmode.h - 1);
    qz_wm_invalidate_all();
    return 0;
}

void qz_wm_shutdown(void)
{
    int i;
    for (i = 0; i < g_nwin; i++) {
        qz_widget *wd = g_win[i]->widgets, *nx;
        while (wd) { nx = wd->next; free(wd); wd = nx; }
        qz_surface_free(g_win[i]->surf);
        free(g_win[i]);
    }
    g_nwin = 0;
    if (g_scene) { qz_surface_free(g_scene); g_scene = 0; }
}

void qz_wm_quit(void) { g_quit = 1; }
void qz_wm_set_quit_on_empty(int on) { g_quit_on_empty = on; }

/* ---------- windows ---------- */
void qz_window_client(qz_window *win, int *cx, int *cy, int *cw, int *ch)
{
    if (cx) *cx = QZ_WB;
    if (cy) *cy = QZ_TB_H;
    if (cw) *cw = win->w - 2 * QZ_WB;
    if (ch) *ch = win->h - QZ_TB_H - QZ_WB;
}

qz_window *qz_window_new(int x, int y, int w, int h, const char *title, unsigned flags)
{
    qz_window *win;
    if (g_nwin >= QZ_MAXWIN) return 0;
    win = (qz_window *)malloc(sizeof *win);
    if (!win) return 0;
    memset(win, 0, sizeof *win);
    win->id = g_next_id++;
    win->x = x; win->y = y; win->w = w; win->h = h;
    win->flags = flags;
    if (title) strncpy(win->title, title, sizeof win->title - 1);
    win->surf = qz_surface_create(w, h);
    if (!win->surf) { free(win); return 0; }
    win->dirty = 1;
    g_win[g_nwin++] = win;
    set_focus(win);                    /* new window gets focus (topmost) */
    dirty_add(x, y, w, h);
    return win;
}

static int win_index(qz_window *win)
{
    int i; for (i = 0; i < g_nwin; i++) if (g_win[i] == win) return i; return -1;
}

/* Change focus, re-rendering both the window that loses focus and the one
 * that gains it (their title bars change color). */
static void set_focus(qz_window *win)
{
    if (g_focus == win) return;
    if (g_focus) {
        g_focus->dirty = 1;
        dirty_add(g_focus->x, g_focus->y, g_focus->w, g_focus->h);
    }
    g_focus = win;
    if (win) {
        win->dirty = 1;
        dirty_add(win->x, win->y, win->w, win->h);
    }
}

void qz_window_raise(qz_window *win)
{
    int i = win_index(win);
    if (i < 0) return;
    if (i != g_nwin - 1) {
        memmove(&g_win[i], &g_win[i + 1], (g_nwin - i - 1) * sizeof(qz_window *));
        g_win[g_nwin - 1] = win;
        dirty_add(win->x, win->y, win->w, win->h);
    }
    set_focus(win);
}

void qz_window_close(qz_window *win)
{
    int i = win_index(win);
    qz_widget *wd, *nx;
    if (i < 0) return;
    if (win->on_close) win->on_close(win);
    dirty_add(win->x, win->y, win->w, win->h);
    wd = win->widgets; while (wd) { nx = wd->next; free(wd); wd = nx; }
    qz_surface_free(win->surf);
    free(win);
    memmove(&g_win[i], &g_win[i + 1], (g_nwin - i - 1) * sizeof(qz_window *));
    g_nwin--;
    g_focus = 0;                       /* old pointer is freed */
    set_focus(g_nwin ? g_win[g_nwin - 1] : 0);  /* new top becomes active */
}

void qz_window_invalidate(qz_window *win)
{
    win->dirty = 1;
    dirty_add(win->x, win->y, win->w, win->h);
}

void qz_window_move(qz_window *win, int x, int y)
{
    dirty_add(win->x, win->y, win->w, win->h);  /* old */
    win->x = x; win->y = y;
    dirty_add(win->x, win->y, win->w, win->h);  /* new */
}

qz_widget *qz_window_add(qz_window *win, qz_widget *w)
{
    if (!w) return 0;
    w->owner = win;
    w->next = win->widgets;
    win->widgets = w;
    win->dirty = 1;
    return w;
}

/* ---------- rendering a window into its own surface ---------- */
static void grad_fill(qz_surface *s, int x, int y, int w, int h,
                      qz_color c0, qz_color c1)
{
    /* vertical gradient between two packed colors (decoded via theme fields) */
    int i;
    int r0=(c0>>qz_vmode.rpos)&((1<<qz_vmode.rsz)-1);
    int g0=(c0>>qz_vmode.gpos)&((1<<qz_vmode.gsz)-1);
    int b0=(c0>>qz_vmode.bpos)&((1<<qz_vmode.bsz)-1);
    int r1=(c1>>qz_vmode.rpos)&((1<<qz_vmode.rsz)-1);
    int g1=(c1>>qz_vmode.gpos)&((1<<qz_vmode.gsz)-1);
    int b1=(c1>>qz_vmode.bpos)&((1<<qz_vmode.bsz)-1);
    int sh_r=8-qz_vmode.rsz, sh_g=8-qz_vmode.gsz, sh_b=8-qz_vmode.bsz;
    for (i = 0; i < h; i++) {
        int t = h > 1 ? (i * 256) / h : 0;
        qz_color c = qz_rgb(((r0+(r1-r0)*t/256)<<sh_r),
                            ((g0+(g1-g0)*t/256)<<sh_g),
                            ((b0+(b1-b0)*t/256)<<sh_b));
        qz_hline(s, x, y + i, w, c);
    }
}

static void render_window(qz_window *win)
{
    qz_surface *s = win->surf;
    int active = (win == g_focus);
    int cx, cy, cw, ch;
    qz_widget *wd;

    qz_bevel(s, 0, 0, win->w, win->h, qz_th.face, qz_th.light, qz_th.dark, 0);
    qz_frame_rect(s, 0, 0, win->w, win->h, qz_th.black);

    if (win->flags & QZ_WF_TITLEBAR) {
        grad_fill(s, 3, 3, win->w - 6, QZ_TB_H - 3,
                  active ? qz_th.title_a0 : qz_th.title_i0,
                  active ? qz_th.title_a1 : qz_th.title_i1);
        qz_text(s, 9, 6, win->title, qz_th.white);
        if (win->flags & QZ_WF_CLOSEBOX) {
            qz_bevel(s, win->w - 22, 5, 16, 16, qz_th.face, qz_th.light, qz_th.dark, 0);
            qz_text(s, win->w - 18, 5, "x", qz_th.black);
        }
    }
    qz_window_client(win, &cx, &cy, &cw, &ch);
    qz_fill_rect(s, cx, cy, cw, ch, qz_th.face);
    for (wd = win->widgets; wd; wd = wd->next)
        qz__widget_paint(wd, s, cx, cy);
    if (win->on_paint) win->on_paint(win);
    win->dirty = 0;
}

/* ---------- compositor ---------- */
static void wallpaper(int x, int y, int w, int h)
{
    int r0=0x2e,g0=0x54,b0=0x7a, r1=0x14,g1=0x24,b1=0x38, row;
    for (row = 0; row < h; row++) {
        int yy = y + row;
        int t = (yy * 256) / g_scene->h;
        qz_color c = qz_rgb(r0+(r1-r0)*t/256, g0+(g1-g0)*t/256, b0+(b1-b0)*t/256);
        qz_hline(g_scene, x, yy, w, c);
    }
}

/* blit src into g_scene at (dx,dy), clipped to [clx,cly,clw,clh] */
static void blit_clip(qz_surface *src, int dx, int dy,
                      int clx, int cly, int clw, int clh, long ckey)
{
    int x0 = dx, y0 = dy, x1 = dx + src->w, y1 = dy + src->h, row;
    if (x0 < clx) x0 = clx; if (y0 < cly) y0 = cly;
    if (x1 > clx + clw) x1 = clx + clw; if (y1 > cly + clh) y1 = cly + clh;
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 > g_scene->w) x1 = g_scene->w; if (y1 > g_scene->h) y1 = g_scene->h;
    for (row = y0; row < y1; row++) {
        qz_color *sp = src->pixels + (long)(row - dy) * src->pitch + (x0 - dx);
        qz_color *dp = g_scene->pixels + (long)row * g_scene->pitch + x0;
        int i, n = x1 - x0;
        if (ckey < 0) for (i = 0; i < n; i++) dp[i] = sp[i];
        else for (i = 0; i < n; i++) if (sp[i] != (qz_color)ckey) dp[i] = sp[i];
    }
}

static void clock_str(char *b)
{
    time_t t = time(0); struct tm *lt = localtime(&t);
    if (lt) sprintf(b, "%02d:%02d", lt->tm_hour, lt->tm_min); else strcpy(b, "--:--");
}

static void draw_menubar(void)
{
    char clk[8]; int cw;
    qz_bevel(g_scene, 0, 0, g_scene->w, QZ_MENUBAR_H, qz_th.face, qz_th.light, qz_th.dark, 0);
    qz_text(g_scene, 10, 4, "Quartz", qz_th.title_a0);
    qz_text(g_scene, 84, 4, "File", qz_th.text);
    qz_text(g_scene, 136, 4, "View", qz_th.text);
    qz_text(g_scene, 188, 4, "Help", qz_th.text);
    clock_str(clk); cw = qz_text_w(clk);
    qz_bevel(g_scene, g_scene->w - cw - 20, 3, cw + 14, QZ_MENUBAR_H - 6,
             qz_th.face, qz_th.dark, qz_th.light, 1);
    qz_text(g_scene, g_scene->w - cw - 13, 4, clk, qz_th.text);
}

#define TASK_BTN_W 150
static void taskbar_btn_rect(int i, int *x, int *y, int *w, int *h)
{
    *y = g_scene->h - QZ_TASKBAR_H + 4;
    *h = QZ_TASKBAR_H - 8;
    *x = 8 + i * (TASK_BTN_W + 4);
    *w = TASK_BTN_W;
}

static void draw_taskbar(void)
{
    int by = g_scene->h - QZ_TASKBAR_H, i;
    qz_bevel(g_scene, 0, by, g_scene->w, QZ_TASKBAR_H, qz_th.face, qz_th.light, qz_th.dark, 0);
    for (i = 0; i < g_nwin; i++) {
        int x, y, w, h, active = (g_win[i] == g_focus);
        taskbar_btn_rect(i, &x, &y, &w, &h);
        qz_bevel(g_scene, x, y, w, h, active ? qz_th.white : qz_th.face,
                 qz_th.light, qz_th.dark, active ? 1 : 0);
        qz_text(g_scene, x + 8, y + (h - QZ_FONT_H) / 2, g_win[i]->title, qz_th.text);
    }
}

static void compose(int x, int y, int w, int h)
{
    int i;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > g_scene->w) w = g_scene->w - x;
    if (y + h > g_scene->h) h = g_scene->h - y;
    if (w <= 0 || h <= 0) return;

    wallpaper(x, y, w, h);
    for (i = 0; i < g_nwin; i++) {
        qz_window *win = g_win[i];
        if (!(win->flags & QZ_WF_VISIBLE)) continue;
        if (win->dirty) render_window(win);
        blit_clip(win->surf, win->x, win->y, x, y, w, h, -1);
    }
    if (y < QZ_MENUBAR_H) draw_menubar();
    if (y + h > g_scene->h - QZ_TASKBAR_H) draw_taskbar();
}

/* ---------- present (scene -> framebuffer with cursor overlay) ---------- */
static void copy_scene_to_screen(int x, int y, int w, int h)
{
    int row;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > qz_screen->w) w = qz_screen->w - x;
    if (y + h > qz_screen->h) h = qz_screen->h - y;
    if (w <= 0 || h <= 0) return;
    for (row = 0; row < h; row++)
        memcpy(qz_screen->pixels + (long)(y + row) * qz_screen->pitch + x,
               g_scene->pixels   + (long)(y + row) * g_scene->pitch + x,
               (size_t)w * sizeof(qz_color));
}

static void draw_cursor(int cx, int cy)
{
    int r, c;
    for (r = 0; r < CUR_H; r++)
        for (c = 0; c < CUR_W; c++) {
            char v = CURSOR[r][c];
            if (v == '1') qz_put(qz_screen, cx + c, cy + r, qz_th.black);
            else if (v == '2') qz_put(qz_screen, cx + c, cy + r, qz_th.white);
        }
}

/* ---------- input handling ---------- */
static qz_window *window_at(int x, int y)
{
    int i;
    for (i = g_nwin - 1; i >= 0; i--) {
        qz_window *w = g_win[i];
        if (!(w->flags & QZ_WF_VISIBLE)) continue;
        if (x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h) return w;
    }
    return 0;
}

static void handle_press(int x, int y)
{
    int i;
    /* taskbar */
    if (y >= g_scene->h - QZ_TASKBAR_H) {
        for (i = 0; i < g_nwin; i++) {
            int bx, by, bw, bh;
            taskbar_btn_rect(i, &bx, &by, &bw, &bh);
            if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
                qz_window_raise(g_win[i]);
                return;
            }
        }
        return;
    }
    if (y < QZ_MENUBAR_H) return;   /* menu: no actions yet */

    {
        qz_window *win = window_at(x, y);
        if (!win) return;
        if (win != g_focus) qz_window_raise(win);
        /* local coords */
        {
            int lx = x - win->x, ly = y - win->y;
            /* close box */
            if ((win->flags & QZ_WF_CLOSEBOX) &&
                lx >= win->w - 22 && lx < win->w - 6 && ly >= 5 && ly < 21) {
                qz_window_close(win);
                return;
            }
            /* title bar -> drag */
            if ((win->flags & QZ_WF_MOVABLE) && ly < QZ_TB_H) {
                g_drag = win; g_drag_ox = lx; g_drag_oy = ly;
                return;
            }
            /* client -> widgets */
            {
                int cx, cy, cw, ch;
                qz_window_client(win, &cx, &cy, &cw, &ch);
                if (lx >= cx && ly >= cy) {
                    qz_widget *wd = qz__widget_at(win, lx - cx, ly - cy);
                    if (wd) {
                        g_arm = wd; g_arm_win = win;
                        if (wd->type == QZ_WDG_BUTTON) { wd->state = 1; qz_window_invalidate(win); }
                    }
                }
            }
        }
    }
}

static void handle_release(int x, int y)
{
    if (g_drag) { g_drag = 0; }
    if (g_arm) {
        qz_widget *wd = g_arm; qz_window *win = g_arm_win;
        int cx, cy, cw, ch, lx, ly, over = 0;
        qz_window_client(win, &cx, &cy, &cw, &ch);
        lx = x - win->x - cx; ly = y - win->y - cy;
        over = (lx >= wd->x && lx < wd->x + wd->w && ly >= wd->y && ly < wd->y + wd->h);
        if (wd->type == QZ_WDG_BUTTON) wd->state = 0;
        if (over) {
            if (wd->type == QZ_WDG_CHECKBOX) wd->state = !wd->state;
            if (wd->on_click) wd->on_click(wd);
        }
        qz_window_invalidate(win);
        g_arm = 0; g_arm_win = 0;
    }
}

/* ---------- main pump ---------- */
int qz_wm_run_once(void)
{
    qz_mouse_state m;
    int k;
    static char lastclk[8] = "";
    char clk[8];

    while ((k = qz_key_get()) != 0)
        if (QZ_SCAN(k) == QZ_KEY_ESC) g_quit = 1;

    qz_mouse_read(&m);
    g_cx = m.x; g_cy = m.y;

    /* button edges (left) */
    if ((m.buttons & QZ_BTN_LEFT) && !(g_prev_btn & QZ_BTN_LEFT)) handle_press(g_cx, g_cy);
    if (!(m.buttons & QZ_BTN_LEFT) && (g_prev_btn & QZ_BTN_LEFT)) handle_release(g_cx, g_cy);
    g_prev_btn = m.buttons;

    /* dragging */
    if (g_drag && (m.buttons & QZ_BTN_LEFT)) {
        int nx = g_cx - g_drag_ox, ny = g_cy - g_drag_oy;
        if (ny < QZ_MENUBAR_H) ny = QZ_MENUBAR_H;
        qz_window_move(g_drag, nx, ny);
    }

    /* clock tick */
    clock_str(clk);
    if (strcmp(clk, lastclk) != 0) { strcpy(lastclk, clk); dirty_add(0, 0, g_scene->w, QZ_MENUBAR_H); }

    /* recompose dirty scene region */
    if (g_dirty) {
        compose(g_dminx, g_dminy, g_dmaxx - g_dminx, g_dmaxy - g_dminy);
    }

    /* present: union of scene-dirty and cursor movement */
    {
        int px0, py0, px1, py1;
        int moved = (g_cx != g_pcx || g_cy != g_pcy);
        if (g_dirty || moved) {
            /* start from dirty bbox (if any) */
            if (g_dirty) { px0 = g_dminx; py0 = g_dminy; px1 = g_dmaxx; py1 = g_dmaxy; }
            else { px0 = py0 = 1 << 29; px1 = py1 = -(1 << 29); }
            /* include old + new cursor boxes */
            if (g_pcx < px0) px0 = g_pcx; if (g_pcy < py0) py0 = g_pcy;
            if (g_pcx + CUR_W > px1) px1 = g_pcx + CUR_W;
            if (g_pcy + CUR_H > py1) py1 = g_pcy + CUR_H;
            if (g_cx < px0) px0 = g_cx; if (g_cy < py0) py0 = g_cy;
            if (g_cx + CUR_W > px1) px1 = g_cx + CUR_W;
            if (g_cy + CUR_H > py1) py1 = g_cy + CUR_H;
            copy_scene_to_screen(px0, py0, px1 - px0, py1 - py0);
            draw_cursor(g_cx, g_cy);
            qz_present_rect(px0, py0, px1 - px0, py1 - py0);
            g_pcx = g_cx; g_pcy = g_cy;
        }
        g_dirty = 0;
    }

    __dpmi_yield();

    if (g_quit) return 0;
    if (g_quit_on_empty && g_nwin == 0) return 0;
    return 1;
}

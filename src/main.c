/* main.c - Quartz desktop shell (Phase 1).
 *
 * Brings up a VESA linear-framebuffer mode, composes a desktop (wallpaper,
 * menu bar with clock, a demo window) into an off-screen buffer, and runs an
 * event loop that moves a mouse cursor using dirty-rectangle present. Press
 * ESC to quit back to DOS. */
#include "gfx/gfx.h"
#include "input/input.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <dpmi.h>

#define MENUBAR_H 24

/* Quartz palette (filled after the video mode is known). */
static qz_color C_DESK, C_DESK2, C_FACE, C_LIGHT, C_DARK, C_SHADOW;
static qz_color C_TITLE, C_TITLE2, C_WHITE, C_BLACK, C_TEXT, C_ACCENT;

static qz_surface *desk;   /* fully composed desktop, same size as screen */

/* ---- 12x19 arrow cursor: 0=transparent 1=black 2=white ---- */
static const char CURSOR[19][12] = {
    "1           ",
    "11          ",
    "121         ",
    "1221        ",
    "12221       ",
    "122221      ",
    "1222221     ",
    "12222221    ",
    "122222221   ",
    "1222222221  ",
    "12222222221 ",
    "122222221111",
    "122212221   ",
    "12211222    ",
    "1211 1222   ",
    "111  1222   ",
    "1     1222  ",
    "      1222  ",
    "       111  ",
};
#define CUR_W 12
#define CUR_H 19

static void palette_init(void)
{
    C_DESK   = qz_rgb(0x2e, 0x54, 0x7a);   /* teal-blue wallpaper top */
    C_DESK2  = qz_rgb(0x1b, 0x33, 0x4d);   /* darker bottom */
    C_FACE   = qz_rgb(0xc8, 0xc8, 0xc8);   /* 3D face gray */
    C_LIGHT  = qz_rgb(0xff, 0xff, 0xff);
    C_DARK   = qz_rgb(0x80, 0x80, 0x80);
    C_SHADOW = qz_rgb(0x40, 0x40, 0x40);
    C_TITLE  = qz_rgb(0x1f, 0x4e, 0x9b);   /* active title bar */
    C_TITLE2 = qz_rgb(0x3b, 0x7a, 0xd4);
    C_WHITE  = qz_rgb(0xff, 0xff, 0xff);
    C_BLACK  = qz_rgb(0x00, 0x00, 0x00);
    C_TEXT   = qz_rgb(0x10, 0x10, 0x10);
    C_ACCENT = qz_rgb(0xe0, 0x50, 0x30);
}

/* Vertical gradient wallpaper. */
static void draw_wallpaper(qz_surface *s)
{
    int y;
    int r0 = 0x2e, g0 = 0x54, b0 = 0x7a;
    int r1 = 0x14, g1 = 0x24, b1 = 0x38;
    for (y = 0; y < s->h; y++) {
        int t = (y * 256) / s->h;
        qz_color c = qz_rgb(r0 + (r1 - r0) * t / 256,
                            g0 + (g1 - g0) * t / 256,
                            b0 + (b1 - b0) * t / 256);
        qz_hline(s, 0, y, s->w, c);
    }
}

static void draw_menubar(qz_surface *s, const char *clock)
{
    qz_bevel(s, 0, 0, s->w, MENUBAR_H, C_FACE, C_LIGHT, C_DARK, 0);
    qz_text(s, 10, 4, "Quartz", C_TITLE);
    qz_text(s, 10 + qz_text_w("Quartz") + 24, 4, "File", C_TEXT);
    qz_text(s, 10 + qz_text_w("Quartz") + 24 + qz_text_w("File  ") + 16, 4, "View", C_TEXT);
    /* clock, right-aligned */
    {
        int cw = qz_text_w(clock);
        qz_bevel(s, s->w - cw - 20, 3, cw + 14, MENUBAR_H - 6,
                 C_FACE, C_DARK, C_LIGHT, 1);
        qz_text(s, s->w - cw - 13, 4, clock, C_TEXT);
    }
}

static void draw_window(qz_surface *s, int x, int y, int w, int h,
                        const char *title, const char *body)
{
    int tb = 22;
    /* drop shadow */
    qz_fill_rect(s, x + 5, y + 5, w, h, C_SHADOW);
    /* frame */
    qz_bevel(s, x, y, w, h, C_FACE, C_LIGHT, C_DARK, 0);
    qz_frame_rect(s, x, y, w, h, C_BLACK);
    /* title bar gradient */
    {
        int i;
        for (i = 0; i < tb; i++) {
            int t = (i * 256) / tb;
            qz_color c = qz_rgb(0x1f + (0x3b - 0x1f) * t / 256,
                                0x4e + (0x7a - 0x4e) * t / 256,
                                0x9b + (0xd4 - 0x9b) * t / 256);
            qz_hline(s, x + 3, y + 3 + i, w - 6, c);
        }
        qz_text(s, x + 9, y + 6, title, C_WHITE);
        /* close box */
        qz_bevel(s, x + w - 22, y + 5, 16, 16, C_FACE, C_LIGHT, C_DARK, 0);
        qz_text(s, x + w - 19, y + 5, "x", C_BLACK);
    }
    /* client area */
    qz_fill_rect(s, x + 4, y + 3 + tb, w - 8, h - tb - 7, C_WHITE);
    /* body text */
    {
        int ty = y + tb + 12, tx = x + 12;
        const char *p = body;
        char line[128];
        int li = 0;
        for (;; p++) {
            if (*p == '\n' || *p == '\0') {
                line[li] = 0;
                qz_text(s, tx, ty, line, C_TEXT);
                ty += QZ_FONT_H + 2;
                li = 0;
                if (*p == '\0') break;
            } else if (li < 127) {
                line[li++] = *p;
            }
        }
    }
}

static void compose_desktop(const char *clock)
{
    char info[192];
    draw_wallpaper(desk);
    sprintf(info,
            "Welcome to Quartz.\n"
            "\n"
            "A from-scratch GUI for FreeDOS.\n"
            "\n"
            "Video : %dx%d  %d-bpp VESA LFB\n"
            "Mode  : 0x%03X   FB @ 0x%08lX\n"
            "\n"
            "Move the mouse. Press ESC to exit.",
            qz_vmode.w, qz_vmode.h, qz_vmode.bpp,
            qz_vmode.vbe_mode, (unsigned long)qz_vmode.phys_base);
    draw_window(desk, desk->w / 2 - 230, desk->h / 2 - 140, 460, 230,
                "Quartz  -  About", info);
    /* a second, smaller window to show layering */
    draw_window(desk, 60, desk->h - 180, 300, 120, "Notes",
                "Phase 1: graphics core\n"
                "  * VESA LFB mode select\n"
                "  * off-screen compositing\n"
                "  * ROM font text\n"
                "  * INT 33h mouse cursor");
    draw_menubar(desk, clock);
}

/* copy a rectangle from desk -> screen back buffer */
static void restore_rect(int x, int y, int w, int h)
{
    int row;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > qz_screen->w) w = qz_screen->w - x;
    if (y + h > qz_screen->h) h = qz_screen->h - y;
    if (w <= 0 || h <= 0) return;
    for (row = 0; row < h; row++) {
        memcpy(qz_screen->pixels + (long)(y + row) * qz_screen->pitch + x,
               desk->pixels      + (long)(y + row) * desk->pitch + x,
               (size_t)w * sizeof(qz_color));
    }
}

static void draw_cursor(int cx, int cy)
{
    int r, c;
    for (r = 0; r < CUR_H; r++)
        for (c = 0; c < CUR_W; c++) {
            char v = CURSOR[r][c];
            if (v == '1') qz_put(qz_screen, cx + c, cy + r, C_BLACK);
            else if (v == '2') qz_put(qz_screen, cx + c, cy + r, C_WHITE);
        }
}

static void clock_str(char *buf)
{
    time_t t = time(0);
    struct tm *lt = localtime(&t);
    if (lt) sprintf(buf, "%02d:%02d", lt->tm_hour, lt->tm_min);
    else    strcpy(buf, "--:--");
}

int main(void)
{
    qz_mouse_state m;
    int rc, ox, oy, running = 1;
    char clk[8], lastclk[8];

    rc = qz_gfx_init(1024, 768);
    if (rc != 0) {
        printf("Quartz: no suitable VESA LFB mode (err %d)\n", rc);
        return 1;
    }
    palette_init();
    qz_font_init();
    qz_mouse_init(qz_vmode.w - 1, qz_vmode.h - 1);

    desk = qz_surface_create(qz_vmode.w, qz_vmode.h);
    if (!desk) { qz_gfx_shutdown(); printf("Quartz: out of memory\n"); return 1; }

    clock_str(clk);
    strcpy(lastclk, clk);
    compose_desktop(clk);

    /* initialise cursor and paint the first full frame */
    qz_mouse_read(&m);
    ox = m.x; oy = m.y;
    restore_rect(0, 0, qz_screen->w, qz_screen->h); /* copy desk -> screen */
    draw_cursor(ox, oy);
    qz_present_all();

    while (running) {
        int k;
        /* keyboard */
        while ((k = qz_key_get()) != 0) {
            if (QZ_SCAN(k) == QZ_KEY_ESC) { running = 0; break; }
        }
        /* mouse */
        qz_mouse_read(&m);
        if (m.x != ox || m.y != oy) {
            int ux = ox < m.x ? ox : m.x;
            int uy = oy < m.y ? oy : m.y;
            int uw = (ox > m.x ? ox : m.x) - ux + CUR_W;
            int uh = (oy > m.y ? oy : m.y) - uy + CUR_H;
            restore_rect(ux, uy, uw, uh);
            draw_cursor(m.x, m.y);
            qz_present_rect(ux, uy, uw, uh);
            ox = m.x; oy = m.y;
        }
        /* clock */
        clock_str(clk);
        if (strcmp(clk, lastclk) != 0) {
            strcpy(lastclk, clk);
            draw_menubar(desk, clk);
            restore_rect(0, 0, qz_screen->w, MENUBAR_H);
            if (oy < MENUBAR_H || 1) draw_cursor(ox, oy);
            qz_present_rect(0, 0, qz_screen->w, MENUBAR_H);
        }
        /* yield a little */
        {
            __dpmi_yield();
        }
    }

    qz_gfx_shutdown();
    printf("Quartz exited cleanly.\n");
    return 0;
}

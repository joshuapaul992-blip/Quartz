/* host_main.c - drives the real Quartz WM on the host and dumps PNG-able PPM
 * frames, so Phase 2 rendering can be verified without the DJGPP toolchain. */
#include "gfx/gfx.h"
#include "input/input.h"
#include "wm/wm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int x, y, b; } host_ev;
void host_set_script(const host_ev *s, int n);

/* ---- same windows/callbacks as src/main.c ---- */
static void cb_close_owner(qz_widget *w) { if (w->owner) qz_window_close(w->owner); }
static int g_counter = 0;
static qz_widget *g_count_label = 0;
static void cb_increment(qz_widget *w)
{
    g_counter++;
    if (g_count_label) {
        sprintf(g_count_label->text, "Clicks: %d", g_counter);
        if (g_count_label->owner) qz_window_invalidate(g_count_label->owner);
    }
    (void)w;
}
static void about_paint(qz_window *win)
{
    int cx, cy, cw, ch;
    qz_window_client(win, &cx, &cy, &cw, &ch);
    qz_hline(win->surf, cx + 8, cy + ch - 44, cw - 16, qz_th.dark);
    qz_hline(win->surf, cx + 8, cy + ch - 43, cw - 16, qz_th.light);
}
static void make_about(void)
{
    qz_window *w = qz_window_new(260, 120, 420, 260, "About Quartz", QZ_WF_DEFAULT);
    char info[96];
    if (!w) return;
    w->on_paint = about_paint;
    qz_window_add(w, qz_label(16, 10, "Quartz"));
    qz_window_add(w, qz_label(16, 34, "A from-scratch GUI for FreeDOS."));
    sprintf(info, "Display: %dx%d  %d-bpp VESA LFB", qz_vmode.w, qz_vmode.h, qz_vmode.bpp);
    qz_window_add(w, qz_label(16, 58, info));
    qz_window_add(w, qz_checkbox(16, 86, "Show grid on desktop", 0, 0));
    qz_window_add(w, qz_checkbox(16, 108, "Enable sounds", 1, 0));
    qz_window_add(w, qz_button(16, w->h - QZ_TB_H - QZ_WB - 60, 90, 26, "OK", cb_close_owner));
}
static void make_counter(void)
{
    qz_window *w = qz_window_new(120, 360, 300, 150, "Widgets Demo", QZ_WF_DEFAULT);
    if (!w) return;
    qz_window_add(w, qz_label(16, 12, "Click the button:"));
    g_count_label = qz_label(16, 40, "Clicks: 0");
    qz_window_add(w, g_count_label);
    qz_window_add(w, qz_button(16, 70, 120, 28, "Press me", cb_increment));
    qz_window_add(w, qz_button(150, 70, 110, 28, "Close", cb_close_owner));
}
static void make_files(void)
{
    qz_window *w = qz_window_new(560, 330, 320, 220, "Files", QZ_WF_DEFAULT);
    if (!w) return;
    qz_window_add(w, qz_panel(12, 12, 296, 150));
    qz_window_add(w, qz_label(22, 20, "C:\\QUARTZ"));
    qz_window_add(w, qz_label(30, 44, "[DIR]  SYSTEM"));
    qz_window_add(w, qz_label(30, 64, "[DIR]  APPS"));
    qz_window_add(w, qz_label(30, 84, "QUARTZ   EXE"));
    qz_window_add(w, qz_label(30, 104, "README   TXT"));
    qz_window_add(w, qz_button(w->w - 2*QZ_WB - 96, 168, 88, 24, "Close", cb_close_owner));
}

static void dump_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    int i, n = qz_screen->w * qz_screen->h;
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", qz_screen->w, qz_screen->h);
    for (i = 0; i < n; i++) {
        qz_color px = qz_screen->pixels[i];
        unsigned char rgb[3];
        rgb[0] = (px >> qz_vmode.rpos) & 0xFF;
        rgb[1] = (px >> qz_vmode.gpos) & 0xFF;
        rgb[2] = (px >> qz_vmode.bpos) & 0xFF;
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *out1 = argc > 1 ? argv[1] : "dist/host_desktop.ppm";
    const char *out2 = argc > 2 ? argv[2] : "dist/host_interact.ppm";
    int i;

    /* XRGB8888 @ 1024x768 */
    qz_vmode.w = 1024; qz_vmode.h = 768; qz_vmode.bpp = 32;
    qz_vmode.fb_pitch = 1024 * 4; qz_vmode.phys_base = 0xFD000000;
    qz_vmode.rpos = 16; qz_vmode.rsz = 8;
    qz_vmode.gpos = 8;  qz_vmode.gsz = 8;
    qz_vmode.bpos = 0;  qz_vmode.bsz = 8;
    qz_vmode.vbe_mode = 0x118;
    qz_screen = qz_surface_create(1024, 768);

    qz_font_init();
    qz_wm_init();
    make_files();
    make_counter();
    make_about();

    /* Frame 1: initial desktop (one script step: cursor near top-left). */
    {
        static const host_ev s0[] = { {40, 300, 0} };
        host_set_script(s0, 1);
        qz_wm_run_once();
        dump_ppm(out1);
        printf("wrote %s\n", out1);
    }

    /* Frame 2: drag About by its title bar, then click "Press me". */
    {
        static const host_ev s1[] = {
            {300,130,0},                 /* over About title */
            {300,130,QZ_BTN_LEFT},       /* press -> start drag */
            {360,210,QZ_BTN_LEFT},
            {470,300,QZ_BTN_LEFT},
            {520,360,QZ_BTN_LEFT},
            {520,360,0},                 /* release */
            {200,452,0},                 /* over "Press me" */
            {200,452,QZ_BTN_LEFT},       /* press */
            {200,452,0},                 /* release -> increment */
            {40,300,0}                   /* park cursor */
        };
        host_set_script(s1, (int)(sizeof s1 / sizeof s1[0]));
        for (i = 0; i < (int)(sizeof s1 / sizeof s1[0]); i++) qz_wm_run_once();
        dump_ppm(out2);
        printf("wrote %s\n", out2);
    }

    qz_wm_shutdown();
    qz_surface_free(qz_screen);
    return 0;
}

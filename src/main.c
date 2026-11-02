/* main.c - Quartz desktop shell.
 *
 * Brings up VESA, initializes the window manager, opens a few application
 * windows built from widgets, and runs the WM event loop. ESC quits. */
#include "gfx/gfx.h"
#include "input/input.h"
#include "wm/wm.h"
#include "net/net.h"
#include <stdio.h>
#include <string.h>

/* ---- widget callbacks ---- */
static void cb_close_owner(qz_widget *w) { if (w->owner) qz_window_close(w->owner); }

static int   g_counter = 0;
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
    /* a subtle separator line above the buttons */
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

/* ---- Network app ---- */
static qz_netinfo g_net;
static qz_widget *g_resolve_label = 0;
static void cb_resolve(qz_widget *w)
{
    char ip[20];
    if (!g_resolve_label) return;
    if (g_net.up) {
        qz_net_resolve("www.google.com", ip);
        sprintf(g_resolve_label->text, "www.google.com -> %s", ip);
    } else {
        strcpy(g_resolve_label->text, "offline - no link");
    }
    if (g_resolve_label->owner) qz_window_invalidate(g_resolve_label->owner);
    (void)w;
}
static void make_network(void)
{
    qz_window *w = qz_window_new(610, 90, 360, 210, "Network", QZ_WF_DEFAULT);
    char l1[64], l2[64], l3[80];
    if (!w) return;
    if (g_net.up) {
        sprintf(l1, "Status : UP (DHCP)");
        sprintf(l2, "IP     : %s", g_net.ip);
        sprintf(l3, "Mask   : %s   DNS: %s", g_net.mask, g_net.dns);
    } else {
        sprintf(l1, "Status : offline");
        sprintf(l2, "%.40s", g_net.err[0] ? g_net.err : "no packet driver / link");
        l3[0] = 0;
    }
    qz_window_add(w, qz_label(16, 12, "Quartz TCP/IP (Watt-32)"));
    qz_window_add(w, qz_label(16, 40, l1));
    qz_window_add(w, qz_label(16, 60, l2));
    if (l3[0]) qz_window_add(w, qz_label(16, 80, l3));
    g_resolve_label = qz_label(16, 112, "(resolve test not run)");
    qz_window_add(w, g_resolve_label);
    qz_window_add(w, qz_button(16, 134, 180, 26, "Resolve google.com", cb_resolve));
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

int main(void)
{
    int rc;

    /* Bring up networking in text mode first (packet driver + DHCP), so any
     * Watt-32 console output is visible and DHCP completes before graphics. */
    printf("Quartz: initializing network...\n");
    qz_net_init(&g_net);
    if (g_net.up) printf("Quartz: network up, IP %s\n", g_net.ip);
    else          printf("Quartz: network offline (%s)\n", g_net.err);

    rc = qz_gfx_init(1024, 768);
    if (rc != 0) { printf("Quartz: no VESA LFB mode (err %d)\n", rc); return 1; }
    qz_font_init();
    if (qz_wm_init() != 0) { qz_gfx_shutdown(); printf("Quartz: WM init failed\n"); return 1; }

    make_files();
    make_counter();
    make_network();
    make_about();           /* opened last -> focused, on top */

    while (qz_wm_run_once())
        ;

    qz_wm_shutdown();
    qz_gfx_shutdown();
    printf("Quartz exited.\n");
    return 0;
}

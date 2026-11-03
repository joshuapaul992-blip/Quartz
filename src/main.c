/* main.c - Quartz desktop shell.
 *
 * Brings up VESA, initializes the window manager, opens a few application
 * windows built from widgets, and runs the WM event loop. ESC quits. */
#include "gfx/gfx.h"
#include "input/input.h"
#include "wm/wm.h"
#include "net/net.h"
#include "audio/audio.h"
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
    qz_window_add(w, qz_label(16, 74, "No mouse? Arrows move, Enter clicks, Ctrl+Q quits"));
    qz_window_add(w, qz_checkbox(16, 96, "Show grid on desktop", 0, 0));
    qz_window_add(w, qz_checkbox(16, 118, "Enable sounds", 1, 0));
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

/* ---- Network app (lazy: nothing touches the network until Connect) ---- */
static qz_netinfo g_net;      /* zeroed => not connected */
static qz_widget *g_net_l1 = 0, *g_net_l2 = 0, *g_net_l3 = 0, *g_resolve_label = 0;

static void net_update_labels(void)
{
    if (!g_net_l1) return;
    if (g_net.up) {
        strcpy(g_net_l1->text, "Status : UP (DHCP)");
        sprintf(g_net_l2->text, "IP   : %s", g_net.ip);
        sprintf(g_net_l3->text, "Mask : %s  DNS: %s", g_net.mask, g_net.dns);
    } else {
        strcpy(g_net_l1->text, "Status : not connected");
        snprintf(g_net_l2->text, sizeof g_net_l2->text, "%.59s",
                 g_net.err[0] ? g_net.err : "click Connect to start TCP/IP");
        g_net_l3->text[0] = 0;
    }
    if (g_net_l1->owner) qz_window_invalidate(g_net_l1->owner);
}

static void cb_connect(qz_widget *w)
{
    qz_net_init(&g_net);          /* loads packet driver + DHCP, on demand */
    net_update_labels();
    (void)w;
}

static void cb_resolve(qz_widget *w)
{
    char ip[20];
    if (!g_resolve_label) return;
    if (g_net.up) {
        qz_net_resolve("www.google.com", ip);
        sprintf(g_resolve_label->text, "www.google.com -> %s", ip);
    } else {
        strcpy(g_resolve_label->text, "not connected");
    }
    if (g_resolve_label->owner) qz_window_invalidate(g_resolve_label->owner);
    (void)w;
}

static void make_network(void)
{
    qz_window *w = qz_window_new(610, 90, 360, 210, "Network", QZ_WF_DEFAULT);
    if (!w) return;
    qz_window_add(w, qz_label(16, 12, "Quartz TCP/IP (Watt-32)"));
    g_net_l1 = qz_label(16, 40, "Status : not connected");
    g_net_l2 = qz_label(16, 60, "click Connect to start TCP/IP");
    g_net_l3 = qz_label(16, 80, "");
    qz_window_add(w, g_net_l1);
    qz_window_add(w, g_net_l2);
    qz_window_add(w, g_net_l3);
    g_resolve_label = qz_label(16, 112, "(not connected)");
    qz_window_add(w, g_resolve_label);
    qz_window_add(w, qz_button(16, 134, 110, 26, "Connect", cb_connect));
    qz_window_add(w, qz_button(136, 134, 150, 26, "Resolve google.com", cb_resolve));
}

/* ---- Sound app ---- */
static void cb_tone_a(qz_widget *w)    { qz_audio_tone(440, 300); (void)w; }
static void cb_tone_hi(qz_widget *w)   { qz_audio_tone(880, 300); (void)w; }
static void cb_chime(qz_widget *w)
{
    qz_audio_tone(523, 120); qz_audio_tone(659, 120); qz_audio_tone(784, 180);
    (void)w;
}
static void make_sound(void)
{
    qz_window *w = qz_window_new(150, 150, 330, 170, "Sound", QZ_WF_DEFAULT);
    char s[64];
    if (!w) return;
    if (qz_audio.present)
        sprintf(s, "Sound Blaster @ 0x%X  DSP %d.%02d",
                qz_audio.base, qz_audio.dsp_major, qz_audio.dsp_minor);
    else
        strcpy(s, "No Sound Blaster detected");
    qz_window_add(w, qz_label(16, 12, "Quartz Audio (SB16 / SBEMU)"));
    qz_window_add(w, qz_label(16, 36, s));
    qz_window_add(w, qz_button(16, 70, 80, 28, "Tone A", cb_tone_a));
    qz_window_add(w, qz_button(104, 70, 80, 28, "Tone Hi", cb_tone_hi));
    qz_window_add(w, qz_button(192, 70, 100, 28, "Chime", cb_chime));
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

    /* Quartz never blocks on the network at startup: the GUI comes up first and
     * TCP/IP is brought up on demand from the Network window's Connect button. */
    qz_audio_init();        /* detect Sound Blaster (QEMU SB16 / SBEMU on metal) */

    rc = qz_gfx_init(1024, 768);
    if (rc != 0) { printf("Quartz: no VESA LFB mode (err %d)\n", rc); return 1; }
    qz_font_init();
    if (qz_wm_init() != 0) { qz_gfx_shutdown(); printf("Quartz: WM init failed\n"); return 1; }

    make_files();
    make_counter();
    make_sound();
    make_network();
    make_about();           /* opened last -> focused, on top */

    if (qz_audio.present) {  /* startup chime */
        qz_audio_tone(523, 110); qz_audio_tone(659, 110); qz_audio_tone(784, 160);
    }

    while (qz_wm_run_once())
        ;

    qz_wm_shutdown();
    qz_gfx_shutdown();
    printf("Quartz exited.\n");
    return 0;
}

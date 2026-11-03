/* mouse.c - INT 33h mouse support using relative motion (resolution agnostic).
 *
 * The real-mode INT 33h driver (e.g. FreeDOS CuteMouse / ctmouse) reports
 * position in its own virtual coordinate system that does not track high-res
 * VESA modes. We therefore read motion counters (AX=000Bh) and integrate them
 * ourselves, clamping to the Quartz screen. */
#include "input.h"
#include <dpmi.h>
#include <string.h>

static int g_present = 0;
static int g_x = 0, g_y = 0;
static int g_maxx = 0, g_maxy = 0;

int qz_mouse_init(int max_x, int max_y)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x0000;              /* reset + detect */
    __dpmi_int(0x33, &r);
    g_present = (r.x.ax == 0xFFFF);
    g_maxx = max_x; g_maxy = max_y;
    g_x = max_x / 2; g_y = max_y / 2;
    /* Drain any initial motion. */
    memset(&r, 0, sizeof r);
    r.x.ax = 0x000B;
    __dpmi_int(0x33, &r);
    return g_present ? 0 : -1;
}

int qz_mouse_present(void) { return g_present; }

void qz_mouse_read(qz_mouse_state *out)
{
    __dpmi_regs r;
    int dx, dy;

    if (!g_present) {
        out->present = 0; out->x = g_x; out->y = g_y; out->buttons = 0;
        return;
    }

    /* Relative motion since last call, in mickeys (~8 per pixel default). */
    memset(&r, 0, sizeof r);
    r.x.ax = 0x000B;
    __dpmi_int(0x33, &r);
    dx = (short)r.x.cx;
    dy = (short)r.x.dx;

    g_x += dx / 2;               /* scale mickeys -> pixels */
    g_y += dy / 2;
    if (g_x < 0) g_x = 0;
    if (g_y < 0) g_y = 0;
    if (g_x > g_maxx) g_x = g_maxx;
    if (g_y > g_maxy) g_y = g_maxy;

    /* Button state. */
    memset(&r, 0, sizeof r);
    r.x.ax = 0x0003;
    __dpmi_int(0x33, &r);

    out->present = 1;
    out->x = g_x;
    out->y = g_y;
    out->buttons = r.x.bx & 0x07;
}

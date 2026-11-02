/* host_input.c - scripted mouse/keyboard for the render harness. */
#include "input/input.h"

typedef struct { int x, y, b; } host_ev;
static const host_ev *g_script;
static int g_n, g_i;

void host_set_script(const host_ev *s, int n) { g_script = s; g_n = n; g_i = 0; }

int  qz_mouse_init(int mx, int my) { (void)mx;(void)my; return 0; }

void qz_mouse_read(qz_mouse_state *o)
{
    int k = (g_i < g_n) ? g_i : (g_n > 0 ? g_n - 1 : 0);
    if (g_n == 0) { o->x = 0; o->y = 0; o->buttons = 0; o->present = 1; return; }
    o->x = g_script[k].x; o->y = g_script[k].y; o->buttons = g_script[k].b; o->present = 1;
    if (g_i < g_n) g_i++;
}

int qz_key_waiting(void) { return 0; }
int qz_key_get(void)     { return 0; }

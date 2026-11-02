/* host_gfx.c - host-side replacements for the DOS/VBE layer, so the real
 * wm.c/draw.c/widget.c can be compiled and run on Linux for visual testing. */
#include "gfx/gfx.h"

qz_mode     qz_vmode;
qz_surface *qz_screen = 0;

qz_color qz_rgb(int r, int g, int b)
{
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    return ((qz_color)(r >> (8 - qz_vmode.rsz)) << qz_vmode.rpos)
         | ((qz_color)(g >> (8 - qz_vmode.gsz)) << qz_vmode.gpos)
         | ((qz_color)(b >> (8 - qz_vmode.bsz)) << qz_vmode.bpos);
}

/* On the host the "framebuffer" is just the back buffer; present is a no-op. */
void qz_present_rect(int x, int y, int w, int h) { (void)x;(void)y;(void)w;(void)h; }
void qz_present_all(void) {}
int  qz_gfx_init(int w, int h) { (void)w;(void)h; return 0; }
void qz_gfx_shutdown(void) {}

/* wm.c calls this to yield; no-op on the host. */
int __dpmi_yield(void) { return 0; }

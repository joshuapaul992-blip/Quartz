/* font.c - text rendering using the VGA ROM 8x16 font.
 *
 * Rather than embedding a font, we fetch the real 8x16 glyph bitmap from the
 * video BIOS at startup (INT 10h, AX=1130h, BH=6). This works identically in
 * QEMU and on real hardware. */
#include "gfx.h"
#include <dpmi.h>
#include <go32.h>
#include <sys/movedata.h>
#include <string.h>

static uint8_t g_font[256 * 16];
static int     g_have_font = 0;

int qz_font_init(void)
{
    __dpmi_regs r;
    uint32_t lin;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x1130;
    r.h.bh = 0x06;            /* 8x16 ROM font */
    __dpmi_int(0x10, &r);
    lin = ((uint32_t)r.x.es << 4) + r.x.bp;
    if (lin == 0) return -1;
    dosmemget(lin, sizeof g_font, g_font);
    g_have_font = 1;
    return 0;
}

static void draw_glyph(qz_surface *s, int x, int y, unsigned char ch,
                       qz_color fg, qz_color bg, int opaque)
{
    const uint8_t *gl = g_font + (int)ch * 16;
    int row, col;
    if (!g_have_font) return;
    for (row = 0; row < 16; row++) {
        uint8_t bits = gl[row];
        int py = y + row;
        if ((unsigned)py >= (unsigned)s->h) continue;
        {
            qz_color *p = s->pixels + (long)py * s->pitch;
            for (col = 0; col < 8; col++) {
                int px = x + col;
                if ((unsigned)px >= (unsigned)s->w) continue;
                if (bits & (0x80 >> col)) p[px] = fg;
                else if (opaque)          p[px] = bg;
            }
        }
    }
}

void qz_text(qz_surface *s, int x, int y, const char *str, qz_color fg)
{
    for (; *str; str++) {
        if (*str == '\n') { y += QZ_FONT_H; x = 0; continue; }
        draw_glyph(s, x, y, (unsigned char)*str, fg, 0, 0);
        x += QZ_FONT_W;
    }
}

void qz_text_bg(qz_surface *s, int x, int y, const char *str, qz_color fg, qz_color bg)
{
    for (; *str; str++) {
        if (*str == '\n') { y += QZ_FONT_H; x = 0; continue; }
        draw_glyph(s, x, y, (unsigned char)*str, fg, bg, 1);
        x += QZ_FONT_W;
    }
}

int qz_text_w(const char *str)
{
    int n = 0;
    for (; *str; str++) if (*str != '\n') n++;
    return n * QZ_FONT_W;
}

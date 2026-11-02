/* host_font.c - text rendering for the host harness, using the real VGA ROM
 * font bytes dumped from QEMU (font8x16.h). Mirrors src/gfx/font.c logic. */
#include "gfx/gfx.h"
#include "font8x16.h"

int qz_font_init(void) { return 0; }

static void glyph(qz_surface *s, int x, int y, unsigned char ch,
                  qz_color fg, qz_color bg, int opaque)
{
    const unsigned char *gl = HOST_FONT8X16 + (int)ch * 16;
    int row, col;
    for (row = 0; row < 16; row++) {
        unsigned char bits = gl[row];
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
        glyph(s, x, y, (unsigned char)*str, fg, 0, 0);
        x += QZ_FONT_W;
    }
}
void qz_text_bg(qz_surface *s, int x, int y, const char *str, qz_color fg, qz_color bg)
{
    for (; *str; str++) {
        if (*str == '\n') { y += QZ_FONT_H; x = 0; continue; }
        glyph(s, x, y, (unsigned char)*str, fg, bg, 1);
        x += QZ_FONT_W;
    }
}
int qz_text_w(const char *str)
{
    int n = 0; for (; *str; str++) if (*str != '\n') n++; return n * QZ_FONT_W;
}

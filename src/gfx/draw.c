/* draw.c - surface allocation and clipped 2D primitives. */
#include "gfx.h"
#include <stdlib.h>
#include <string.h>

qz_surface *qz_surface_create(int w, int h)
{
    qz_surface *s;
    if (w <= 0 || h <= 0) return 0;
    s = (qz_surface *)malloc(sizeof *s);
    if (!s) return 0;
    s->pixels = (qz_color *)malloc((size_t)w * h * sizeof(qz_color));
    if (!s->pixels) { free(s); return 0; }
    s->w = w; s->h = h; s->pitch = w; s->owned = 1;
    memset(s->pixels, 0, (size_t)w * h * sizeof(qz_color));
    return s;
}

void qz_surface_free(qz_surface *s)
{
    if (!s) return;
    if (s->owned && s->pixels) free(s->pixels);
    free(s);
}

void qz_put(qz_surface *s, int x, int y, qz_color c)
{
    if ((unsigned)x >= (unsigned)s->w || (unsigned)y >= (unsigned)s->h) return;
    s->pixels[(long)y * s->pitch + x] = c;
}

void qz_clear(qz_surface *s, qz_color c)
{
    long n = (long)s->pitch * s->h, i;
    qz_color *p = s->pixels;
    for (i = 0; i < n; i++) p[i] = c;
}

void qz_hline(qz_surface *s, int x, int y, int w, qz_color c)
{
    qz_color *p;
    int i;
    if (y < 0 || y >= s->h) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > s->w) w = s->w - x;
    if (w <= 0) return;
    p = s->pixels + (long)y * s->pitch + x;
    for (i = 0; i < w; i++) p[i] = c;
}

void qz_vline(qz_surface *s, int x, int y, int h, qz_color c)
{
    qz_color *p;
    int i;
    if (x < 0 || x >= s->w) return;
    if (y < 0) { h += y; y = 0; }
    if (y + h > s->h) h = s->h - y;
    if (h <= 0) return;
    p = s->pixels + (long)y * s->pitch + x;
    for (i = 0; i < h; i++) { *p = c; p += s->pitch; }
}

void qz_fill_rect(qz_surface *s, int x, int y, int w, int h, qz_color c)
{
    int row;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > s->w) w = s->w - x;
    if (y + h > s->h) h = s->h - y;
    if (w <= 0 || h <= 0) return;
    for (row = 0; row < h; row++) {
        qz_color *p = s->pixels + (long)(y + row) * s->pitch + x;
        int i;
        for (i = 0; i < w; i++) p[i] = c;
    }
}

void qz_frame_rect(qz_surface *s, int x, int y, int w, int h, qz_color c)
{
    if (w <= 0 || h <= 0) return;
    qz_hline(s, x, y, w, c);
    qz_hline(s, x, y + h - 1, w, c);
    qz_vline(s, x, y, h, c);
    qz_vline(s, x + w - 1, y, h, c);
}

void qz_bevel(qz_surface *s, int x, int y, int w, int h,
              qz_color face, qz_color light, qz_color dark, int sunken)
{
    qz_color tl = sunken ? dark : light;
    qz_color br = sunken ? light : dark;
    qz_fill_rect(s, x, y, w, h, face);
    qz_hline(s, x, y, w, tl);
    qz_vline(s, x, y, h, tl);
    qz_hline(s, x, y + h - 1, w, br);
    qz_vline(s, x + w - 1, y, h, br);
}

void qz_blit(qz_surface *dst, const qz_surface *src, int dx, int dy, long ckey)
{
    int sx = 0, sy = 0, w = src->w, h = src->h, row;
    if (dx < 0) { sx = -dx; w += dx; dx = 0; }
    if (dy < 0) { sy = -dy; h += dy; dy = 0; }
    if (dx + w > dst->w) w = dst->w - dx;
    if (dy + h > dst->h) h = dst->h - dy;
    if (w <= 0 || h <= 0) return;
    for (row = 0; row < h; row++) {
        const qz_color *sp = src->pixels + (long)(sy + row) * src->pitch + sx;
        qz_color       *dp = dst->pixels + (long)(dy + row) * dst->pitch + dx;
        int i;
        if (ckey < 0) {
            for (i = 0; i < w; i++) dp[i] = sp[i];
        } else {
            for (i = 0; i < w; i++) if (sp[i] != (qz_color)ckey) dp[i] = sp[i];
        }
    }
}

/* gfx.h - Quartz graphics core: VBE mode, surfaces, drawing, text.
 *
 * Quartz draws everything into an off-screen 32-bpp surface (the "back
 * buffer") and copies dirty regions to the VESA linear framebuffer. The
 * pixel format of the back buffer matches the framebuffer exactly, so a
 * present is a straight memory copy.
 */
#ifndef QUARTZ_GFX_H
#define QUARTZ_GFX_H

#include <stdint.h>

typedef uint32_t qz_color; /* packed in the active framebuffer format */

/* A drawable: a flat array of 32-bpp pixels. */
typedef struct {
    qz_color *pixels;
    int       w, h;
    int       pitch;       /* pixels per row (>= w) */
    int       owned;       /* nonzero if pixels was malloc'd by us */
} qz_surface;

/* Active video mode description (filled by qz_gfx_init). */
typedef struct {
    int      w, h, bpp;
    int      fb_pitch;     /* framebuffer bytes per scanline */
    uint32_t phys_base;    /* framebuffer physical address */
    int      rpos, gpos, bpos; /* field shifts for packing colors */
    int      rsz, gsz, bsz;
    uint16_t vbe_mode;
} qz_mode;

extern qz_mode     qz_vmode;
extern qz_surface *qz_screen;   /* the back buffer bound to the display */

/* ---- VBE lifecycle ---- */
/* Pick the best LFB mode >= (want_w x want_h), 32bpp preferred. Returns 0 on
 * success. If want_w/want_h are 0, uses the Quartz default (1024x768). */
int  qz_gfx_init(int want_w, int want_h);
void qz_gfx_shutdown(void);                 /* restore text mode */
void qz_present_rect(int x, int y, int w, int h); /* copy region to screen */
void qz_present_all(void);

/* Pack r,g,b (0..255) into the framebuffer format. */
qz_color qz_rgb(int r, int g, int b);

/* ---- surfaces ---- */
qz_surface *qz_surface_create(int w, int h);
void        qz_surface_free(qz_surface *s);

/* ---- drawing (clipped to surface bounds) ---- */
void qz_clear(qz_surface *s, qz_color c);
void qz_put(qz_surface *s, int x, int y, qz_color c);
void qz_fill_rect(qz_surface *s, int x, int y, int w, int h, qz_color c);
void qz_frame_rect(qz_surface *s, int x, int y, int w, int h, qz_color c);
void qz_hline(qz_surface *s, int x, int y, int w, qz_color c);
void qz_vline(qz_surface *s, int x, int y, int h, qz_color c);
/* 3D-style beveled panel (light top/left, dark bottom/right). */
void qz_bevel(qz_surface *s, int x, int y, int w, int h,
              qz_color face, qz_color light, qz_color dark, int sunken);
/* Blit src onto dst at (dx,dy). If ckey>=0, pixels equal to ckey are skipped. */
void qz_blit(qz_surface *dst, const qz_surface *src, int dx, int dy, long ckey);

/* ---- text (8x16 ROM font) ---- */
int  qz_font_init(void);
void qz_text(qz_surface *s, int x, int y, const char *str, qz_color fg);
void qz_text_bg(qz_surface *s, int x, int y, const char *str, qz_color fg, qz_color bg);
int  qz_text_w(const char *str);    /* pixel width */
#define QZ_FONT_W 8
#define QZ_FONT_H 16

#endif

/* vbe.c - VESA VBE 2.0/3.0 mode setup and linear framebuffer access (DJGPP). */
#include "gfx.h"
#include <dpmi.h>
#include <go32.h>
#include <sys/movedata.h>
#include <sys/farptr.h>
#include <string.h>
#include <stdlib.h>

qz_mode     qz_vmode;
qz_surface *qz_screen = 0;

static int      g_fb_sel  = -1;  /* LDT selector covering the framebuffer */
static int      g_mapped  = 0;
static __dpmi_meminfo g_map;

/* VBE structures as laid out by the BIOS (packed). */
#pragma pack(push, 1)
typedef struct {
    char     sig[4];        /* 'VESA' */
    uint16_t version;
    uint32_t oem_ptr;
    uint32_t caps;
    uint32_t modes_ptr;     /* real-mode far ptr to uint16 list */
    uint16_t total_mem;     /* in 64KB blocks */
    uint8_t  rest[492];
} vbe_info_t;

typedef struct {
    uint16_t attributes;
    uint8_t  win_a, win_b;
    uint16_t granularity;
    uint16_t win_size;
    uint16_t seg_a, seg_b;
    uint32_t win_func;
    uint16_t pitch;         /* bytes per scanline */
    uint16_t w, h;
    uint8_t  cw, ch;
    uint8_t  planes;
    uint8_t  bpp;
    uint8_t  banks;
    uint8_t  memory_model;
    uint8_t  bank_size;
    uint8_t  image_pages;
    uint8_t  rsvd0;
    uint8_t  red_sz, red_pos;
    uint8_t  green_sz, green_pos;
    uint8_t  blue_sz, blue_pos;
    uint8_t  rsv_sz, rsv_pos;
    uint8_t  dc_info;
    uint32_t phys_base;     /* linear framebuffer physical address */
    uint32_t rsvd1;
    uint16_t rsvd2;
    uint8_t  pad[206];
} vbe_mode_t;
#pragma pack(pop)

static int vbe_get_info(vbe_info_t *out)
{
    __dpmi_regs r;
    dosmemput("VBE2", 4, __tb);        /* request VBE 2.0+ data */
    memset(&r, 0, sizeof r);
    r.x.ax = 0x4F00;
    r.x.es = __tb >> 4;
    r.x.di = __tb & 0x0F;
    __dpmi_int(0x10, &r);
    if (r.x.ax != 0x004F) return -1;
    dosmemget(__tb, sizeof(vbe_info_t), out);
    return memcmp(out->sig, "VESA", 4) == 0 ? 0 : -1;
}

static int vbe_get_mode(uint16_t mode, vbe_mode_t *out)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x4F01;
    r.x.cx = mode;
    r.x.es = __tb >> 4;
    r.x.di = __tb & 0x0F;
    __dpmi_int(0x10, &r);
    if (r.x.ax != 0x004F) return -1;
    dosmemget(__tb, sizeof(vbe_mode_t), out);
    return 0;
}

static int vbe_set_mode(uint16_t mode)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x4F02;
    r.x.bx = mode | 0x4000;            /* bit14: linear framebuffer */
    __dpmi_int(0x10, &r);
    return r.x.ax == 0x004F ? 0 : -1;
}

void qz_gfx_shutdown(void)
{
    __dpmi_regs r;
    if (g_fb_sel >= 0) { __dpmi_free_ldt_descriptor(g_fb_sel); g_fb_sel = -1; }
    if (g_mapped)      { __dpmi_free_physical_address_mapping(&g_map); g_mapped = 0; }
    if (qz_screen)     { qz_surface_free(qz_screen); qz_screen = 0; }
    memset(&r, 0, sizeof r);
    r.x.ax = 0x0003;                   /* back to 80x25 text */
    __dpmi_int(0x10, &r);
}

int qz_gfx_init(int want_w, int want_h)
{
    vbe_info_t vi;
    vbe_mode_t mi;
    uint16_t   modelist_off, modelist_seg;
    uint32_t   modelist_lin;
    uint16_t   best = 0xFFFF;
    int        best_score = -1, best_w = 0, best_h = 0;
    int        i;

    if (want_w <= 0) { want_w = 1024; want_h = 768; }

    if (vbe_get_info(&vi) != 0) return -1;

    modelist_off = (uint16_t)(vi.modes_ptr & 0xFFFF);
    modelist_seg = (uint16_t)(vi.modes_ptr >> 16);
    modelist_lin = ((uint32_t)modelist_seg << 4) + modelist_off;

    /* Scan modes; prefer exact-or-larger resolution, 32bpp, smallest area. */
    for (i = 0; i < 512; i++) {
        uint16_t mode = _farpeekw(_dos_ds, modelist_lin + i * 2);
        if (mode == 0xFFFF) break;
        if (vbe_get_mode(mode, &mi) != 0) continue;
        if (!(mi.attributes & 0x01)) continue;   /* supported */
        if (!(mi.attributes & 0x80)) continue;   /* linear fb */
        if (mi.memory_model != 6) continue;       /* direct color */
        if (mi.bpp != 32 && mi.bpp != 24) continue;
        if (mi.w < want_w || mi.h < want_h) continue;
        {
            int score = 100000 - (mi.w * mi.h) / 16; /* smaller area is better */
            if (mi.bpp == 32) score += 50000;        /* strongly prefer 32bpp */
            if (score > best_score) {
                best_score = score; best = mode;
                best_w = mi.w; best_h = mi.h;
            }
        }
    }

    if (best == 0xFFFF) {
        /* Relax: accept any LFB direct-color mode, largest that fits memory. */
        for (i = 0; i < 512; i++) {
            uint16_t mode = _farpeekw(_dos_ds, modelist_lin + i * 2);
            if (mode == 0xFFFF) break;
            if (vbe_get_mode(mode, &mi) != 0) continue;
            if (!(mi.attributes & 0x81)) continue;
            if (mi.memory_model != 6) continue;
            if (mi.bpp < 15) continue;
            best = mode; best_w = mi.w; best_h = mi.h; break;
        }
        if (best == 0xFFFF) return -2;
    }

    /* Fetch chosen mode details and set it. */
    if (vbe_get_mode(best, &mi) != 0) return -3;
    if (vbe_set_mode(best) != 0) return -4;

    qz_vmode.w         = mi.w;
    qz_vmode.h         = mi.h;
    qz_vmode.bpp       = mi.bpp;
    qz_vmode.fb_pitch  = mi.pitch;
    qz_vmode.phys_base = mi.phys_base;
    qz_vmode.rpos = mi.red_pos;   qz_vmode.rsz = mi.red_sz;
    qz_vmode.gpos = mi.green_pos; qz_vmode.gsz = mi.green_sz;
    qz_vmode.bpos = mi.blue_pos;  qz_vmode.bsz = mi.blue_sz;
    qz_vmode.vbe_mode = best;

    /* Map the framebuffer into our address space. */
    g_map.address = mi.phys_base;
    g_map.size    = (uint32_t)mi.pitch * mi.h;
    if (__dpmi_physical_address_mapping(&g_map) != 0) { qz_gfx_shutdown(); return -5; }
    g_mapped = 1;
    g_fb_sel = __dpmi_allocate_ldt_descriptors(1);
    if (g_fb_sel < 0) { qz_gfx_shutdown(); return -6; }
    __dpmi_set_segment_base_address(g_fb_sel, g_map.address);
    __dpmi_set_segment_limit(g_fb_sel, g_map.size - 1);

    /* Back buffer, packed 32bpp (we always compose at 32bpp). */
    qz_screen = qz_surface_create(mi.w, mi.h);
    if (!qz_screen) { qz_gfx_shutdown(); return -7; }
    (void)best_w; (void)best_h;
    return 0;
}

qz_color qz_rgb(int r, int g, int b)
{
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    return ((qz_color)(r >> (8 - qz_vmode.rsz)) << qz_vmode.rpos)
         | ((qz_color)(g >> (8 - qz_vmode.gsz)) << qz_vmode.gpos)
         | ((qz_color)(b >> (8 - qz_vmode.bsz)) << qz_vmode.bpos);
}

/* Copy a dirty rectangle from the back buffer to the framebuffer. For 32bpp
 * the back-buffer row maps 1:1; for 24bpp we pack on the fly. */
void qz_present_rect(int x, int y, int w, int h)
{
    int row;
    if (!qz_screen) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > qz_screen->w) w = qz_screen->w - x;
    if (y + h > qz_screen->h) h = qz_screen->h - y;
    if (w <= 0 || h <= 0) return;

    if (qz_vmode.bpp == 32) {
        for (row = 0; row < h; row++) {
            unsigned src = (unsigned)(qz_screen->pixels
                          + (long)(y + row) * qz_screen->pitch + x);
            unsigned dst = (unsigned)((y + row) * qz_vmode.fb_pitch + x * 4);
            movedata(_my_ds(), src, g_fb_sel, dst, (size_t)w * 4);
        }
    } else { /* 24bpp */
        static uint8_t line[8192 * 3];
        for (row = 0; row < h; row++) {
            qz_color *sp = qz_screen->pixels + (long)(y + row) * qz_screen->pitch + x;
            int i2;
            for (i2 = 0; i2 < w; i2++) {
                line[i2 * 3 + 0] = sp[i2] & 0xFF;
                line[i2 * 3 + 1] = (sp[i2] >> 8) & 0xFF;
                line[i2 * 3 + 2] = (sp[i2] >> 16) & 0xFF;
            }
            movedata(_my_ds(), (unsigned)line, g_fb_sel,
                     (y + row) * qz_vmode.fb_pitch + x * 3, (size_t)w * 3);
        }
    }
}

void qz_present_all(void)
{
    if (qz_screen) qz_present_rect(0, 0, qz_screen->w, qz_screen->h);
}

/* sb16.c - Sound Blaster 16 DSP + DMA playback (DJGPP, protected mode). */
#include "audio.h"
#include <dpmi.h>
#include <go32.h>
#include <sys/movedata.h>
#include <pc.h>
#include <string.h>
#include <stdlib.h>

qz_audio_info qz_audio;

/* DSP ports relative to base */
#define DSP_RESET(b)   ((b) + 0x6)
#define DSP_READ(b)    ((b) + 0xA)
#define DSP_WRITE(b)   ((b) + 0xC)   /* bit7=0 => ready to accept */
#define DSP_RSTAT(b)   ((b) + 0xE)   /* bit7=1 => read data available */

/* DMA buffer (conventional memory, DMA-safe, 64KB-aligned region) */
static int           g_dos_seg = -1;   /* real-mode segment of the block */
static int           g_dos_sel = -1;
static unsigned long g_buf_phys = 0;    /* 64KB-aligned physical address */
static unsigned      g_buf_cap  = 0;

static void io_delay(void)
{
    int i;
    for (i = 0; i < 10; i++) (void)inportb(0x80);
}

static void dsp_write(int base, unsigned char v)
{
    int t = 100000;
    while ((inportb(DSP_WRITE(base)) & 0x80) && --t) ;
    outportb(DSP_WRITE(base), v);
}

static int dsp_read(int base)
{
    int t = 100000;
    while (!(inportb(DSP_RSTAT(base)) & 0x80) && --t) ;
    return inportb(DSP_READ(base));
}

static int dsp_reset(int base)
{
    int t;
    outportb(DSP_RESET(base), 1);
    io_delay(); io_delay(); io_delay();
    outportb(DSP_RESET(base), 0);
    for (t = 0; t < 100000; t++) {
        if (inportb(DSP_RSTAT(base)) & 0x80) {
            if (inportb(DSP_READ(base)) == 0xAA) return 0;
        }
    }
    return -1;
}

static void parse_blaster(void)
{
    const char *b = getenv("BLASTER");
    qz_audio.base = 0x220; qz_audio.irq = 5; qz_audio.dma8 = 1;
    if (!b) return;
    while (*b) {
        while (*b == ' ') b++;
        switch (*b) {
        case 'A': case 'a': qz_audio.base = (int)strtol(b + 1, 0, 16); break;
        case 'I': case 'i': qz_audio.irq  = (int)strtol(b + 1, 0, 10); break;
        case 'D': case 'd': qz_audio.dma8 = (int)strtol(b + 1, 0, 10); break;
        default: break;
        }
        while (*b && *b != ' ') b++;
    }
}

static int alloc_dma_buffer(unsigned want)
{
    /* Allocate 2x + 64KB so we can place a (<64KB) buffer not crossing a
     * 64KB physical boundary. */
    unsigned bytes = want + 0x10000;
    int paras = (int)((bytes + 15) / 16);
    int sel;
    int seg = __dpmi_allocate_dos_memory(paras, &sel);
    if (seg < 0) return -1;
    g_dos_seg = seg; g_dos_sel = sel;
    {
        unsigned long phys = (unsigned long)seg << 4;
        g_buf_phys = (phys + 0xFFFF) & ~0xFFFFUL;  /* 64KB align */
        g_buf_cap  = want;
    }
    return 0;
}

int qz_audio_init(void)
{
    memset(&qz_audio, 0, sizeof qz_audio);
    parse_blaster();
    if (dsp_reset(qz_audio.base) != 0) { qz_audio.present = 0; return -1; }
    /* DSP version (cmd 0xE1) */
    dsp_write(qz_audio.base, 0xE1);
    qz_audio.dsp_major = dsp_read(qz_audio.base);
    qz_audio.dsp_minor = dsp_read(qz_audio.base);
    qz_audio.present = 1;
    return 0;
}

void qz_audio_play_pcm(const unsigned char *pcm, unsigned len, int rate)
{
    int base = qz_audio.base, ch = qz_audio.dma8;
    unsigned off, page, count, tc;
    volatile long spin;

    if (!qz_audio.present || len == 0) return;
    if (g_dos_seg < 0 && alloc_dma_buffer(len > 0x8000 ? 0x8000 : len) != 0) return;
    if (len > g_buf_cap) len = g_buf_cap;

    dosmemput(pcm, len, g_buf_phys);   /* copy PCM into DMA-safe memory */

    off  = (unsigned)(g_buf_phys & 0xFFFF);
    page = (unsigned)((g_buf_phys >> 16) & 0xFF);
    count = len - 1;

    /* turn speaker on */
    dsp_write(base, 0xD1);

    /* program 8237 DMA channel (8-bit: ch 0-3) */
    {
        int ar = ch * 2;                       /* address reg: 0,2,4,6 */
        int cr = ch * 2 + 1;                   /* count reg:   1,3,5,7 */
        /* page register per channel: ch0->0x87 ch1->0x83 ch2->0x81 ch3->0x82 */
        int pr = (ch == 0) ? 0x87 : (ch == 1) ? 0x83 : (ch == 2) ? 0x81 : 0x82;

        outportb(0x0A, 0x04 | ch);             /* mask channel */
        outportb(0x0C, 0x00);                  /* clear flip-flop */
        outportb(0x0B, 0x48 | ch);             /* single mode, read, channel */
        outportb(ar, off & 0xFF);              /* address low */
        outportb(ar, (off >> 8) & 0xFF);       /* address high */
        outportb(pr, page);                    /* page */
        outportb(cr, count & 0xFF);            /* count low */
        outportb(cr, (count >> 8) & 0xFF);     /* count high */
        outportb(0x0A, ch);                    /* unmask channel */
    }

    /* set sample rate via time constant (cmd 0x40) */
    tc = 256 - (1000000 / rate);
    dsp_write(base, 0x40);
    dsp_write(base, (unsigned char)tc);

    /* 8-bit single-cycle DMA output (cmd 0x14), length-1 lo/hi */
    dsp_write(base, 0x14);
    dsp_write(base, count & 0xFF);
    dsp_write(base, (count >> 8) & 0xFF);

    /* block ~ len/rate seconds (approximate busy wait) */
    {
        long ms = (long)len * 1000 / rate;
        long iters = ms * 2000L;
        for (spin = 0; spin < iters; spin++) (void)inportb(0x80);
    }
}

void qz_audio_tone(int freq, int ms)
{
    int rate = 11025;
    unsigned len = (unsigned)((long)rate * ms / 1000);
    unsigned char *buf;
    unsigned i, period;
    if (!qz_audio.present || freq <= 0) return;
    if (len > 0x8000) len = 0x8000;
    buf = (unsigned char *)malloc(len);
    if (!buf) return;
    period = rate / freq;
    if (period < 2) period = 2;
    for (i = 0; i < len; i++)
        buf[i] = ((i % period) < (period / 2)) ? 0xC0 : 0x40;  /* square wave */
    qz_audio_play_pcm(buf, len, rate);
    free(buf);
}

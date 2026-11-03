/* serial.c - COM1 debug output via direct UART I/O. */
#include "serial.h"
#include <pc.h>

#define COM1 0x3F8

void qz_dbg_init(void)
{
    outportb(COM1 + 1, 0x00);   /* no interrupts */
    outportb(COM1 + 3, 0x80);   /* DLAB on */
    outportb(COM1 + 0, 0x01);   /* 115200 divisor low */
    outportb(COM1 + 1, 0x00);   /* divisor high */
    outportb(COM1 + 3, 0x03);   /* 8N1, DLAB off */
    outportb(COM1 + 2, 0xC7);   /* FIFO on, clear */
}

static void sputc(char c)
{
    int t = 100000;
    while (!(inportb(COM1 + 5) & 0x20) && --t) ;  /* THR empty */
    outportb(COM1, (unsigned char)c);
}

void qz_dbg(const char *s) { while (*s) { if (*s == '\n') sputc('\r'); sputc(*s++); } }

void qz_dbg_hex(uint32_t v)
{
    const char *h = "0123456789ABCDEF";
    int i;
    sputc('0'); sputc('x');
    for (i = 28; i >= 0; i -= 4) sputc(h[(v >> i) & 0xF]);
}

void qz_dbg_dec(long v)
{
    char buf[16]; int i = 0;
    if (v < 0) { sputc('-'); v = -v; }
    if (v == 0) { sputc('0'); return; }
    while (v && i < 15) { buf[i++] = '0' + (int)(v % 10); v /= 10; }
    while (i) sputc(buf[--i]);
}

void qz_dbg_nl(void) { sputc('\r'); sputc('\n'); }

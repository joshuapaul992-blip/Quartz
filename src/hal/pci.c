/* pci.c - PCI config mechanism #1 (ports 0xCF8/0xCFC). */
#include "pci.h"
#include <pc.h>

#define CF8 0xCF8
#define CFC 0xCFC

static uint32_t cfg_addr(int bus, int dev, int fn, int reg)
{
    return 0x80000000UL | ((uint32_t)bus << 16) | ((uint32_t)(dev & 0x1F) << 11)
         | ((uint32_t)(fn & 7) << 8) | (uint32_t)(reg & 0xFC);
}

uint32_t qz_pci_read32(int bus, int dev, int fn, int reg)
{
    outportl(CF8, cfg_addr(bus, dev, fn, reg));
    return inportl(CFC);
}

void qz_pci_write32(int bus, int dev, int fn, int reg, uint32_t val)
{
    outportl(CF8, cfg_addr(bus, dev, fn, reg));
    outportl(CFC, val);
}

uint16_t qz_pci_read16(int bus, int dev, int fn, int reg)
{
    uint32_t v = qz_pci_read32(bus, dev, fn, reg & 0xFC);
    return (uint16_t)(v >> ((reg & 2) * 8));
}

void qz_pci_write16(int bus, int dev, int fn, int reg, uint16_t val)
{
    uint32_t v = qz_pci_read32(bus, dev, fn, reg & 0xFC);
    int sh = (reg & 2) * 8;
    v = (v & ~(0xFFFFUL << sh)) | ((uint32_t)val << sh);
    qz_pci_write32(bus, dev, fn, reg & 0xFC, v);
}

static void fill(qz_pci_dev *d, int bus, int dev, int fn)
{
    uint32_t id = qz_pci_read32(bus, dev, fn, 0x00);
    uint32_t cl = qz_pci_read32(bus, dev, fn, 0x08);
    d->bus = bus; d->dev = dev; d->fn = fn;
    d->vendor = (uint16_t)(id & 0xFFFF);
    d->device = (uint16_t)(id >> 16);
    d->progif   = (uint8_t)(cl >> 8);
    d->subclass = (uint8_t)(cl >> 16);
    d->classcode = (uint8_t)(cl >> 24);
}

int qz_pci_enumerate(qz_pci_dev *arr, int max)
{
    int bus, dev, fn, n = 0;
    for (bus = 0; bus < 256; bus++)
        for (dev = 0; dev < 32; dev++) {
            uint16_t v0 = qz_pci_read16(bus, dev, 0, 0x00);
            uint8_t  hdr;
            int nfn;
            if (v0 == 0xFFFF) continue;
            hdr = (uint8_t)(qz_pci_read32(bus, dev, 0, 0x0C) >> 16);
            nfn = (hdr & 0x80) ? 8 : 1;        /* multifunction? */
            for (fn = 0; fn < nfn; fn++) {
                if (qz_pci_read16(bus, dev, fn, 0x00) == 0xFFFF) continue;
                if (n < max) fill(&arr[n], bus, dev, fn);
                n++;
            }
        }
    return n;
}

int qz_pci_find(int classcode, int subclass, int progif, int index, qz_pci_dev *out)
{
    int bus, dev, fn, hit = 0;
    for (bus = 0; bus < 256; bus++)
        for (dev = 0; dev < 32; dev++) {
            uint16_t v0 = qz_pci_read16(bus, dev, 0, 0x00);
            uint8_t  hdr;
            int nfn;
            if (v0 == 0xFFFF) continue;
            hdr = (uint8_t)(qz_pci_read32(bus, dev, 0, 0x0C) >> 16);
            nfn = (hdr & 0x80) ? 8 : 1;
            for (fn = 0; fn < nfn; fn++) {
                qz_pci_dev d;
                if (qz_pci_read16(bus, dev, fn, 0x00) == 0xFFFF) continue;
                fill(&d, bus, dev, fn);
                if (classcode >= 0 && d.classcode != classcode) continue;
                if (subclass  >= 0 && d.subclass  != subclass)  continue;
                if (progif    >= 0 && d.progif    != progif)    continue;
                if (hit++ == index) { *out = d; return 0; }
            }
        }
    return -1;
}

uint32_t qz_pci_bar_base(int bus, int dev, int fn, int bar_index)
{
    uint32_t bar = qz_pci_read32(bus, dev, fn, 0x10 + bar_index * 4);
    if (bar & 1) return bar & ~0x3UL;      /* I/O BAR */
    return bar & ~0xFUL;                    /* memory BAR base (low 32 bits) */
}

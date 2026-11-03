/* pci.h - PCI configuration space access (mechanism #1) + discovery. */
#ifndef QUARTZ_PCI_H
#define QUARTZ_PCI_H
#include <stdint.h>

typedef struct {
    int      bus, dev, fn;
    uint16_t vendor, device;
    uint8_t  classcode, subclass, progif;
} qz_pci_dev;

uint32_t qz_pci_read32(int bus, int dev, int fn, int reg);
void     qz_pci_write32(int bus, int dev, int fn, int reg, uint32_t val);
uint16_t qz_pci_read16(int bus, int dev, int fn, int reg);
void     qz_pci_write16(int bus, int dev, int fn, int reg, uint16_t val);

/* Find the index-th device matching class/subclass/progif (-1 = wildcard).
 * Returns 0 and fills *out on success, nonzero if not found. */
int qz_pci_find(int classcode, int subclass, int progif, int index, qz_pci_dev *out);

/* Enumerate all present functions into arr (up to max). Returns the count. */
int qz_pci_enumerate(qz_pci_dev *arr, int max);

/* Read a (possibly 64-bit) memory BAR's base address, low 4 bits masked off. */
uint32_t qz_pci_bar_base(int bus, int dev, int fn, int bar_index);

#endif

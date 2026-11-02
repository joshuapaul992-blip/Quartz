#ifndef STUB_GO32_H
#define STUB_GO32_H
#include <stddef.h>
extern unsigned long __tb;
extern unsigned short __djgpp_ds_alias;
unsigned short _my_ds(void);
extern unsigned short _dos_ds;
void dosmemget(unsigned long offset, size_t length, void *buffer);
void dosmemput(const void *buffer, size_t length, unsigned long offset);
#endif

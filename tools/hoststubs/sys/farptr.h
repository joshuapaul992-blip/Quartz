#ifndef STUB_FARPTR_H
#define STUB_FARPTR_H
unsigned char  _farpeekb(unsigned short sel, unsigned long off);
unsigned short _farpeekw(unsigned short sel, unsigned long off);
unsigned long  _farpeekl(unsigned short sel, unsigned long off);
void _farpokeb(unsigned short sel, unsigned long off, unsigned char v);
void _farpokew(unsigned short sel, unsigned long off, unsigned short v);
void _farpokel(unsigned short sel, unsigned long off, unsigned long v);
#endif

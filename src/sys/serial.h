/* serial.h - tiny COM1 debug output (headless bring-up logging). */
#ifndef QUARTZ_SERIAL_H
#define QUARTZ_SERIAL_H
#include <stdint.h>

void qz_dbg_init(void);              /* COM1 @ 115200 8N1 */
void qz_dbg(const char *s);
void qz_dbg_hex(uint32_t v);         /* 8 hex digits */
void qz_dbg_dec(long v);
void qz_dbg_nl(void);

#endif

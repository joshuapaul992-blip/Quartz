/* keyboard.c - non-blocking keyboard via BIOS INT 16h. */
#include "input.h"
#include <dpmi.h>
#include <string.h>

int qz_key_waiting(void)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.h.ah = 0x11;               /* enhanced: peek for keystroke */
    __dpmi_int(0x16, &r);
    return (r.x.flags & 0x40) ? 0 : 1;   /* ZF set => none waiting */
}

int qz_key_get(void)
{
    __dpmi_regs r;
    if (!qz_key_waiting()) return 0;
    memset(&r, 0, sizeof r);
    r.h.ah = 0x10;               /* enhanced: read keystroke */
    __dpmi_int(0x16, &r);
    return ((int)r.h.ah << 8) | r.h.al;  /* scan<<8 | ascii */
}

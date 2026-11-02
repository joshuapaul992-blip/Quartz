/* input.h - mouse (INT 33h) and keyboard (INT 16h) for Quartz. */
#ifndef QUARTZ_INPUT_H
#define QUARTZ_INPUT_H

#define QZ_BTN_LEFT   1
#define QZ_BTN_RIGHT  2
#define QZ_BTN_MIDDLE 4

typedef struct {
    int x, y;        /* clamped to [0,max_x] x [0,max_y] */
    int buttons;     /* bitmask of QZ_BTN_* */
    int present;
} qz_mouse_state;

/* Initialize INT 33h mouse driver. Uses relative-delta reads so it works at
 * any resolution. max_x/max_y bound the cursor (screen size - 1). */
int  qz_mouse_init(int max_x, int max_y);
void qz_mouse_read(qz_mouse_state *out);

/* Keyboard. qz_key_get returns 0 if no key waiting, else a code:
 *   low byte  = ASCII (0 for non-ASCII)
 *   high byte = BIOS scan code
 * Special scan codes: ESC=0x01, arrows 0x48/0x50/0x4B/0x4D, F1=0x3B ... */
int qz_key_waiting(void);
int qz_key_get(void);
#define QZ_SCAN(code)  (((code) >> 8) & 0xFF)
#define QZ_ASCII(code) ((code) & 0xFF)
#define QZ_KEY_ESC   0x01
#define QZ_KEY_UP    0x48
#define QZ_KEY_DOWN  0x50
#define QZ_KEY_LEFT  0x4B
#define QZ_KEY_RIGHT 0x4D

#endif

/* audio.h - Quartz audio (Sound Blaster 16 DSP + 8237 DMA).
 *
 * In QEMU this drives the emulated SB16 directly. On modern hardware the same
 * SB16 port I/O is trapped by SBEMU and routed to the Realtek HD Audio codec,
 * so this driver is the single audio path for both. */
#ifndef QUARTZ_AUDIO_H
#define QUARTZ_AUDIO_H

typedef struct {
    int present;
    int base;        /* I/O base, e.g. 0x220 */
    int irq;
    int dma8;        /* 8-bit DMA channel */
    int dsp_major, dsp_minor;
} qz_audio_info;

extern qz_audio_info qz_audio;

/* Detect + reset the DSP. Reads the BLASTER env var if present, else defaults
 * to 0x220/IRQ5/DMA1. Returns 0 if a Sound Blaster was found. */
int  qz_audio_init(void);

/* Play a square-wave tone (blocking for its duration). */
void qz_audio_tone(int freq_hz, int ms);

/* Play 8-bit unsigned mono PCM at sample_rate (blocking). */
void qz_audio_play_pcm(const unsigned char *pcm, unsigned len, int sample_rate);

#endif

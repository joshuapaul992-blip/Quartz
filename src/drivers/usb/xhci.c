/* xhci.c - minimal xHCI host controller + USB HID boot mouse (polled).
 *
 * Enough of xHCI to enumerate one HID device behind the root hub and poll its
 * interrupt-IN endpoint for boot-protocol mouse reports. All DMA structures
 * live in conventional memory (physically addressable); the event ring is
 * polled (no interrupts). Verbose serial logging for headless bring-up. */
#include "usb.h"
#include "../../hal/pci.h"
#include "../../sys/serial.h"
#include <stdint.h>
#include <dpmi.h>
#include <go32.h>
#include <sys/farptr.h>
#include <string.h>

/* ---- MMIO (xHCI register space) ---- */
static int           mmio_sel = -1;
static __dpmi_meminfo mmio_map;
static unsigned      op_off, rt_off, db_off;   /* offsets from MMIO base */

static uint32_t cr32(unsigned off)           { return _farpeekl(mmio_sel, off); }
static uint32_t opr(unsigned off)            { return _farpeekl(mmio_sel, op_off + off); }
static void     opw(unsigned off, uint32_t v){ _farpokel(mmio_sel, op_off + off, v); }
static void     opw64(unsigned off, unsigned long lo){ opw(off, (uint32_t)lo); opw(off + 4, 0); }
static uint32_t rtr(unsigned off)            { return _farpeekl(mmio_sel, rt_off + off); }
static void     rtw(unsigned off, uint32_t v){ _farpokel(mmio_sel, rt_off + off, v); }
static void     dbw(int slot, uint32_t v)    { _farpokel(mmio_sel, db_off + slot * 4, v); }

/* ---- DMA block (conventional memory) ---- */
static int           dma_sel = -1;
static unsigned long dma_phys;                 /* aligned physical base */

#define O_DCBAA    0x0000
#define O_CMDRING  0x1000
#define O_EVTRING  0x2000
#define O_ERST     0x3000
#define O_INCTX    0x3800
#define O_DEVCTX   0x4000
#define O_EP0RING  0x5000
#define O_HIDRING  0x6000
#define O_DATABUF  0x7000
#define O_HIDBUF   0x7200
#define O_SCRATCH  0x8000      /* scratchpad array + pages follow */
#define DMA_BYTES  0x10000     /* 64 KiB */
#define RING_SZ    16          /* TRBs per ring (last is Link) */

static uint32_t dr(unsigned off)             { return _farpeekl(dma_sel, off); }
static void     dw(unsigned off, uint32_t v) { _farpokel(dma_sel, off, v); }
static uint8_t  drb(unsigned off)            { return _farpeekb(dma_sel, off); }
static unsigned long dphys(unsigned off)     { return dma_phys + off; }

typedef struct { unsigned base; int idx; int cycle; } ring_t;
static ring_t cmd_ring, ep0_ring, hid_ring;
static int evt_idx, evt_cycle;

static int  g_slot;
static int  g_ctxsz;         /* 32 or 64 */
static int  g_mouse = 0;
static int  g_hid_ep;        /* xHCI endpoint id of the HID interrupt-IN ep */
static int  g_hid_mps;
static int  g_iface;         /* HID interface number */

/* ---- small helpers ---- */
static void udelay(int n){ volatile int i; while(n-- > 0) for(i=0;i<200;i++); }

static void ring_init(ring_t *r, unsigned base)
{
    int i;
    r->base = base; r->idx = 0; r->cycle = 1;
    for (i = 0; i < RING_SZ * 4; i++) dw(base + i * 4, 0);
    /* Link TRB in the last slot -> back to base, Toggle Cycle */
    dw(base + (RING_SZ - 1) * 16 + 0, (uint32_t)dphys(base));
    dw(base + (RING_SZ - 1) * 16 + 4, 0);
    dw(base + (RING_SZ - 1) * 16 + 8, 0);
    dw(base + (RING_SZ - 1) * 16 + 12, (6 << 10) | (1 << 1) | 1); /* Link, TC, cycle */
}

/* push a TRB (d3 given without the cycle bit); returns its physical address */
static unsigned long ring_push(ring_t *r, uint32_t d0, uint32_t d1, uint32_t d2, uint32_t d3)
{
    unsigned off = r->base + r->idx * 16;
    unsigned long pa = dphys(off);
    dw(off + 0, d0); dw(off + 4, d1); dw(off + 8, d2);
    dw(off + 12, d3 | (r->cycle ? 1 : 0));
    r->idx++;
    if (r->idx == RING_SZ - 1) {
        /* update Link TRB cycle and wrap */
        uint32_t l = dr(r->base + (RING_SZ - 1) * 16 + 12);
        l = (l & ~1u) | (r->cycle ? 1 : 0);
        dw(r->base + (RING_SZ - 1) * 16 + 12, l);
        r->idx = 0; r->cycle ^= 1;
    }
    return pa;
}

/* poll the event ring for the next event; copies 4 dwords into out[4].
 * returns 1 if an event was dequeued, 0 on timeout. */
static int evt_poll(uint32_t out[4], int tries)
{
    while (tries-- > 0) {
        unsigned off = O_EVTRING + evt_idx * 16;
        uint32_t d3 = dr(off + 12);
        if ((d3 & 1) == (uint32_t)evt_cycle) {
            out[0] = dr(off + 0); out[1] = dr(off + 4);
            out[2] = dr(off + 8); out[3] = d3;
            evt_idx++;
            if (evt_idx == RING_SZ) { evt_idx = 0; evt_cycle ^= 1; }
            /* advance ERDP, clearing Event Handler Busy (bit3, write-1-to-clear) */
            rtw(0x38, (uint32_t)dphys(O_EVTRING + evt_idx * 16) | 0x8);
            rtw(0x3C, 0);
            return 1;
        }
        udelay(200);
    }
    return 0;
}

/* wait for a command/transfer completion event, return completion code (bits
 * 24-31 of d2), or -1 on timeout. *slot_out gets the slot id if non-null. */
static int wait_completion(int want_type, int *slot_out)
{
    uint32_t e[4];
    int guard = 64;
    while (guard-- > 0) {
        if (!evt_poll(e, 20000)) return -1;
        {
            int type = (e[3] >> 10) & 0x3F;
            int cc   = (e[2] >> 24) & 0xFF;
            if (type == want_type) {
                if (slot_out) *slot_out = (e[3] >> 24) & 0xFF;
                return cc;
            }
            /* ignore other events (e.g. port status change) */
        }
    }
    return -1;
}

/* ---- xHCI BIOS->OS ownership handoff (real hardware) ---- */
static void legacy_handoff(void)
{
    uint32_t hcc = cr32(0x10);
    unsigned xecp = ((hcc >> 16) & 0xFFFF) * 4;   /* byte offset from base */
    int guard = 64;
    while (xecp && guard-- > 0) {
        uint32_t cap = cr32(xecp);
        int id = cap & 0xFF;
        if (id == 1) {                            /* USB Legacy Support */
            /* set HC OS Owned (bit 24) */
            _farpokel(mmio_sel, xecp, cap | (1u << 24));
            {
                int t = 100000;
                while ((cr32(xecp) & (1u << 16)) && --t) udelay(10); /* BIOS owned clears */
            }
            /* disable SMI sources in USBLEGCTLSTS (xecp+4) */
            _farpokel(mmio_sel, xecp + 4, cr32(xecp + 4) & 0x1F1FFF);
            qz_dbg("xhci: legacy handoff done\n");
            return;
        }
        if (((cap >> 8) & 0xFF) == 0) break;
        xecp += ((cap >> 8) & 0xFF) * 4;
    }
}

/* ---- control transfer on EP0 ---- */
static int control(int req_type, int req, int val, int idx, int len, int dir_in)
{
    uint32_t trt = (len == 0) ? 0 : (dir_in ? 3 : 2);
    /* Setup stage (immediate data) */
    ring_push(&ep0_ring,
              (uint32_t)(req_type & 0xFF) | ((uint32_t)(req & 0xFF) << 8) | ((uint32_t)(val & 0xFFFF) << 16),
              (uint32_t)(idx & 0xFFFF) | ((uint32_t)(len & 0xFFFF) << 16),
              8,
              (1u << 6) | (2u << 10) | (trt << 16));
    if (len) {
        /* Data stage */
        ring_push(&ep0_ring, (uint32_t)dphys(O_DATABUF), 0,
                  (uint32_t)len,
                  (3u << 10) | ((dir_in ? 1u : 0u) << 16));
    }
    /* Status stage (opposite dir), IOC */
    ring_push(&ep0_ring, 0, 0, 0,
              (4u << 10) | ((len && dir_in) ? 0u : (1u << 16)) | (1u << 5));
    dbw(g_slot, 1);                               /* ring EP0 doorbell */
    return wait_completion(32, 0);                /* Transfer Event */
}

/* ---- context helpers ---- */
static void ctx_set(unsigned ctx_off, int entry, int dword, uint32_t v)
{
    dw(ctx_off + entry * g_ctxsz + dword * 4, v);
}
static void zero(unsigned off, unsigned n){ unsigned i; for (i = 0; i < n; i += 4) dw(off + i, 0); }

/* ---- bring-up ---- */
int qz_usb_init(void)
{
    qz_pci_dev pd;
    unsigned long bar;
    uint32_t hcs1, hcc, cmd;
    int maxslots, maxports, p, cc, i;
    int seg, sel, paras;

    qz_dbg("xhci: probing PCI...\n");
    if (qz_pci_find(0x0C, 0x03, 0x30, 0, &pd) != 0) { qz_dbg("xhci: no controller\n"); return -1; }
    qz_dbg("xhci: found at bus "); qz_dbg_dec(pd.bus); qz_dbg(" dev "); qz_dbg_dec(pd.dev);
    qz_dbg(" vendor "); qz_dbg_hex(pd.vendor); qz_dbg_nl();

    /* enable memory space + bus master */
    cmd = qz_pci_read32(pd.bus, pd.dev, pd.fn, 0x04);
    qz_pci_write32(pd.bus, pd.dev, pd.fn, 0x04, cmd | 0x6);

    bar = qz_pci_bar_base(pd.bus, pd.dev, pd.fn, 0);
    qz_dbg("xhci: MMIO base "); qz_dbg_hex((uint32_t)bar); qz_dbg_nl();

    /* map 64 KiB of MMIO */
    mmio_map.address = bar; mmio_map.size = 0x10000;
    if (__dpmi_physical_address_mapping(&mmio_map) != 0) { qz_dbg("xhci: map fail\n"); return -2; }
    mmio_sel = __dpmi_allocate_ldt_descriptors(1);
    __dpmi_set_segment_base_address(mmio_sel, mmio_map.address);
    __dpmi_set_segment_limit(mmio_sel, 0xFFFF);

    /* capability regs */
    {
        uint32_t caplen = cr32(0x00) & 0xFF;
        op_off = caplen;
        rt_off = cr32(0x18) & ~0x1Fu;
        db_off = cr32(0x14) & ~0x3u;
    }
    hcs1 = cr32(0x04);
    hcc  = cr32(0x10);
    maxslots = hcs1 & 0xFF;
    maxports = (hcs1 >> 24) & 0xFF;
    g_ctxsz = (hcc & 0x4) ? 64 : 32;
    qz_dbg("xhci: slots "); qz_dbg_dec(maxslots); qz_dbg(" ports "); qz_dbg_dec(maxports);
    qz_dbg(" ctxsz "); qz_dbg_dec(g_ctxsz); qz_dbg_nl();

    legacy_handoff();

    /* allocate conventional DMA block, 4KB-aligned */
    paras = (DMA_BYTES + 0x1000) / 16;
    seg = __dpmi_allocate_dos_memory(paras, &sel);
    if (seg < 0) { qz_dbg("xhci: dos mem fail\n"); return -3; }
    dma_sel = sel;
    dma_phys = (((unsigned long)seg << 4) + 0xFFF) & ~0xFFFUL;
    __dpmi_set_segment_base_address(dma_sel, dma_phys);
    __dpmi_set_segment_limit(dma_sel, DMA_BYTES - 1);
    for (i = 0; i < DMA_BYTES; i += 4) dw(i, 0);

    /* reset controller */
    opw(0x00, opr(0x00) & ~1u);                   /* R/S = 0 */
    { int t = 100000; while (!(opr(0x04) & 1) && --t) udelay(10); }  /* wait HCH */
    opw(0x00, opr(0x00) | (1u << 1));             /* HCRST */
    { int t = 100000; while ((opr(0x00) & (1u << 1)) && --t) udelay(10); }
    { int t = 100000; while ((opr(0x04) & (1u << 11)) && --t) udelay(10); } /* CNR */
    qz_dbg("xhci: reset done, usbsts "); qz_dbg_hex(opr(0x04)); qz_dbg_nl();

    /* scratchpad buffers if required */
    {
        uint32_t hcs2 = cr32(0x08);
        int spb = (int)(((hcs2 >> 21) & 0x1F) | (((hcs2 >> 27) & 0x1F) << 5));
        dw(O_DCBAA + 0, 0); dw(O_DCBAA + 4, 0);
        if (spb > 0) {
            for (i = 0; i < spb; i++) {
                dw(O_SCRATCH + i * 8, (uint32_t)dphys(0x9000 + i * 0x1000));
                dw(O_SCRATCH + i * 8 + 4, 0);
            }
            dw(O_DCBAA + 0, (uint32_t)dphys(O_SCRATCH));
        }
        qz_dbg("xhci: scratchpad "); qz_dbg_dec(spb); qz_dbg_nl();
    }

    /* DCBAA, command ring, event ring */
    opw(0x38, maxslots);                          /* CONFIG: MaxSlotsEn */
    opw64(0x30, dphys(O_DCBAA));                  /* DCBAAP */
    ring_init(&cmd_ring, O_CMDRING);
    opw64(0x18, dphys(O_CMDRING) | 1);            /* CRCR, RCS=1 */

    /* event ring: one ERST entry */
    evt_idx = 0; evt_cycle = 1;
    dw(O_ERST + 0, (uint32_t)dphys(O_EVTRING));
    dw(O_ERST + 4, 0);
    dw(O_ERST + 8, RING_SZ);
    dw(O_ERST + 12, 0);
    /* interrupter 0 register set lives at RTSOFF + 0x20 */
    rtw(0x28, 1);                                 /* ERSTSZ = 1 */
    rtw(0x38, (uint32_t)dphys(O_EVTRING)); rtw(0x3C, 0);  /* ERDP */
    rtw(0x30, (uint32_t)dphys(O_ERST)); rtw(0x34, 0);     /* ERSTBA (write last) */

    /* run */
    opw(0x00, opr(0x00) | 1);                     /* R/S = 1 */
    { int t = 100000; while ((opr(0x04) & 1) && --t) udelay(10); }
    qz_dbg("xhci: running, usbsts "); qz_dbg_hex(opr(0x04)); qz_dbg_nl();

    /* find a connected, enabled port; reset it */
    g_slot = 0;
    for (p = 1; p <= maxports; p++) {
        uint32_t sc = opr(0x400 + (p - 1) * 0x10);
        if (sc & 1) {                             /* CCS - device connected */
            qz_dbg("xhci: port "); qz_dbg_dec(p); qz_dbg(" connected, sc "); qz_dbg_hex(sc); qz_dbg_nl();
            /* reset (write 1 to PR, preserve PP, write-1-clear change bits off) */
            opw(0x400 + (p - 1) * 0x10, (sc & ~0x80E0u) | (1u << 4));
            { int t = 100000; while (!(opr(0x400 + (p - 1) * 0x10) & (1u << 1)) && --t) udelay(10); }
            sc = opr(0x400 + (p - 1) * 0x10);
            qz_dbg("xhci: port reset, sc "); qz_dbg_hex(sc); qz_dbg_nl();
            {
                int speed = (sc >> 10) & 0xF;
                int mps0 = (speed == 4) ? 512 : (speed == 3) ? 64 : 8;

                /* Enable Slot */
                ring_push(&cmd_ring, 0, 0, 0, (9u << 10));
                dbw(0, 0);
                cc = wait_completion(33, &g_slot);
                qz_dbg("xhci: enable slot cc "); qz_dbg_dec(cc); qz_dbg(" slot "); qz_dbg_dec(g_slot); qz_dbg_nl();
                if (cc != 1 || g_slot == 0) return -4;

                /* Input Context: add slot + EP0 */
                zero(O_INCTX, 33 * g_ctxsz);
                ctx_set(O_INCTX, 0, 1, 0x3);      /* Add A0 | A1 */
                /* slot ctx (entry 1) */
                ctx_set(O_INCTX, 1, 0, ((uint32_t)speed << 20) | (1u << 27)); /* ctx entries=1 */
                ctx_set(O_INCTX, 1, 1, (uint32_t)p << 16);
                /* EP0 ctx (entry 2) */
                ring_init(&ep0_ring, O_EP0RING);
                ctx_set(O_INCTX, 2, 1, (3u << 1) | (4u << 3) | ((uint32_t)mps0 << 16));
                ctx_set(O_INCTX, 2, 2, (uint32_t)dphys(O_EP0RING) | 1);
                ctx_set(O_INCTX, 2, 3, 0);
                ctx_set(O_INCTX, 2, 4, 8);        /* avg TRB len */
                /* output device context base in DCBAA[slot] */
                dw(O_DCBAA + g_slot * 8, (uint32_t)dphys(O_DEVCTX));
                dw(O_DCBAA + g_slot * 8 + 4, 0);
                zero(O_DEVCTX, 33 * g_ctxsz);

                /* Address Device */
                ring_push(&cmd_ring, (uint32_t)dphys(O_INCTX), 0, 0,
                          (11u << 10) | ((uint32_t)g_slot << 24));
                dbw(0, 0);
                cc = wait_completion(33, 0);
                qz_dbg("xhci: address device cc "); qz_dbg_dec(cc); qz_dbg_nl();
                if (cc != 1) return -5;

                /* GET_DESCRIPTOR (device, 18 bytes) */
                cc = control(0x80, 6, (1 << 8), 0, 18, 1);
                qz_dbg("xhci: get devdesc cc "); qz_dbg_dec(cc);
                qz_dbg(" idVendor "); qz_dbg_hex(drb(O_DATABUF + 8) | (drb(O_DATABUF + 9) << 8));
                qz_dbg_nl();

                /* GET_DESCRIPTOR (config, 64 bytes) */
                cc = control(0x80, 6, (2 << 8), 0, 64, 1);
                qz_dbg("xhci: get confdesc cc "); qz_dbg_dec(cc); qz_dbg_nl();

                /* walk config descriptor for a HID interface + interrupt-IN ep */
                {
                    int total = drb(O_DATABUF + 2) | (drb(O_DATABUF + 3) << 8);
                    int off = 0, is_hid = 0, proto = 0, cfgval = drb(O_DATABUF + 5);
                    int ep_addr = 0, ep_mps = 0, ep_interval = 0, iface = 0;
                    if (total > 64) total = 64;
                    while (off + 2 <= total) {
                        int blen = drb(O_DATABUF + off), btype = drb(O_DATABUF + off + 1);
                        if (blen < 2) break;
                        if (btype == 4) {         /* interface */
                            iface = drb(O_DATABUF + off + 2);
                            is_hid = (drb(O_DATABUF + off + 5) == 3);
                            proto  = drb(O_DATABUF + off + 7);
                        } else if (btype == 5 && is_hid) {  /* endpoint */
                            int a = drb(O_DATABUF + off + 2);
                            int at = drb(O_DATABUF + off + 3) & 3;
                            if ((a & 0x80) && at == 3) {    /* interrupt IN */
                                ep_addr = a;
                                ep_mps = drb(O_DATABUF + off + 4) | (drb(O_DATABUF + off + 5) << 8);
                                ep_interval = drb(O_DATABUF + off + 6);
                                break;
                            }
                        }
                        off += blen;
                    }
                    g_iface = iface;
                    qz_dbg("xhci: hid="); qz_dbg_dec(is_hid); qz_dbg(" proto="); qz_dbg_dec(proto);
                    qz_dbg(" iface="); qz_dbg_dec(iface);
                    qz_dbg(" ep="); qz_dbg_hex(ep_addr); qz_dbg(" mps="); qz_dbg_dec(ep_mps); qz_dbg_nl();
                    if (!ep_addr) continue;        /* not a usable HID; try next port */

                    /* SET_CONFIGURATION */
                    control(0x00, 9, cfgval, 0, 0, 0);
                    /* SET_PROTOCOL (boot=0) on the HID interface */
                    control(0x21, 0x0B, 0, iface, 0, 0);

                    /* Configure Endpoint: add the interrupt-IN endpoint */
                    g_hid_ep = (ep_addr & 0xF) * 2 + 1;   /* IN endpoint id */
                    g_hid_mps = ep_mps;
                    ring_init(&hid_ring, O_HIDRING);
                    zero(O_INCTX, 33 * g_ctxsz);
                    ctx_set(O_INCTX, 0, 1, (1u << g_hid_ep) | 1u);  /* A(epid) | A0 */
                    ctx_set(O_INCTX, 1, 0, ((uint32_t)speed << 20) | ((uint32_t)g_hid_ep << 27));
                    ctx_set(O_INCTX, 1, 1, (uint32_t)p << 16);
                    /* interrupt-IN EP context */
                    ctx_set(O_INCTX, g_hid_ep, 0, (3u << 16));  /* Interval exponent */
                    ctx_set(O_INCTX, g_hid_ep, 1, (3u << 1) | (7u << 3) | ((uint32_t)ep_mps << 16));
                    ctx_set(O_INCTX, g_hid_ep, 2, (uint32_t)dphys(O_HIDRING) | 1);
                    ctx_set(O_INCTX, g_hid_ep, 4, (uint32_t)ep_mps | ((uint32_t)ep_mps << 16)); /* avg TRB + Max ESIT */
                    (void)ep_interval;
                    ring_push(&cmd_ring, (uint32_t)dphys(O_INCTX), 0, 0,
                              (12u << 10) | ((uint32_t)g_slot << 24));
                    dbw(0, 0);
                    cc = wait_completion(33, 0);
                    qz_dbg("xhci: config ep cc "); qz_dbg_dec(cc); qz_dbg_nl();
                    if (cc != 1) return -7;

                    g_mouse = (proto == 2 || proto == 0);  /* boot/report mouse */
                    qz_dbg("xhci: HID ready, mouse="); qz_dbg_dec(g_mouse); qz_dbg_nl();
                    if (g_mouse) return 0;                 /* found the mouse */
                    /* not a mouse (e.g. keyboard) -> keep scanning other ports */
                }
            }
        }
    }
    qz_dbg("xhci: no HID mouse found on any port\n");
    return -8;
}

int qz_usb_mouse_present(void) { return g_mouse; }

int qz_usb_poll_mouse(int *dx, int *dy, int *buttons)
{
    int cc, btn;
    signed char bx, by;
    if (!g_mouse) return 0;
    /* HID GET_REPORT (input report 0) on EP0 -- the control path that works
     * reliably on xHCI, rather than the interrupt endpoint. */
    dw(O_DATABUF, 0);
    cc = control(0xA1, 0x01, (1 << 8) | 0, g_iface, 4, 1);
    if (cc != 1) return 0;
    btn = drb(O_DATABUF + 0);
    bx  = (signed char)drb(O_DATABUF + 1);
    by  = (signed char)drb(O_DATABUF + 2);
    { static int n = 0; if (n < 16 && (bx || by || btn)) { n++;
        qz_dbg("rep b="); qz_dbg_hex(btn); qz_dbg(" x="); qz_dbg_dec(bx);
        qz_dbg(" y="); qz_dbg_dec(by); qz_dbg_nl(); } }
    if (dx) *dx += bx;
    if (dy) *dy += by;
    if (buttons) *buttons = btn & 7;
    return (bx || by) ? 1 : 0;
}

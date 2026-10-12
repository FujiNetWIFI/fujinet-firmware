/* s100_card.h -- the FujiNet S-100 card (Rev0): bus decode, pins and registers.
 *
 * The S-100 side sees four blocks, each decoded by 74HCT688s from DIP switches:
 *   pipe   4 I/O ports, FujiBus to the ESP32-S3 (GETC/STATUS/PUTC/CONTROL+TICK)
 *   2SIO   4 I/O ports, two 6850s: A = FujiNet modem, B = FujiNet printer
 *   FDC    08h-0Bh, Altair 88-DCDD / FDC+ serial-drive compatible
 *   page   256 bytes of memory, the boot page the RP serves
 * Any decoded cycle sets the ARM flip-flop on pSYNC rising, which pulls PRDY until
 * the RP pulses /WAIT_CLR, so the RP answers every cycle with no deadline.
 */
#ifndef S100_CARD_H
#define S100_CARD_H

/* ---- glue: block codes the decode presents to the RP with WAITQ ---- */
#define S100_BLK_PIPE 0
#define S100_BLK_SIO  1
#define S100_BLK_FDC  2
#define S100_BLK_PAGE 3

/* ---- DIP defaults (read back through the 74HC165 chain) ---- */
#define S100_DEF_PIPE_BASE 0xA0 /* A2-A7 */
#define S100_DEF_SIO_BASE  0x14 /* A2-A7 */
#define S100_DEF_PAGE      0xFF /* A8-A15 */
#define S100_FDC_BASE      0x08 /* fixed, as on the FDC+ */

/* 74HC165 chain, first bit shifted out first: pipe base A2-A7 (6), page A8-A15 (8),
 * console type (2), PHANTOM enable, POJ enable. */
#define S100_DIP_BITS 18

/* console types handed to CONFIG in the LOAD header */
#define S100_CON_2SIO 0 /* 6850 at 10h/11h */
#define S100_CON_SIO  1 /* 88-SIO at 00h/01h */
#define S100_CON_8251 2 /* IMSAI SIO-2 8251 at 02h/03h */
#define S100_CON_BDOS 3 /* only for CONFIG.COM */

/* ---- RP2354B pins: the PIO window is GPIO 0-31 ---- */
#define S100_PIN_DO0     0  /* DO0-7 in, through a 74LVC245 */
#define S100_PIN_A0      8  /* A0-7 in */
#define S100_PIN_SINP    16
#define S100_PIN_SOUT    17
#define S100_PIN_SMEMR   18
#define S100_PIN_WAITQ   19 /* ARM & DECODE: a cycle is waiting for us */
#define S100_PIN_BLK0    20 /* block code, 2 bits */
#define S100_PIN_DI0     22 /* DI0-7 out, to the 74ACT541 */
#define S100_PIN_WAITCLR 30 /* /WAIT_CLR, active low pulse */
#define S100_PIN_PSYNC   31
#define S100_PIN_RESET   32 /* RESET* in (POC* or'd in by the glue) */
#define S100_PIN_PINTE   33
#define S100_PIN_DIP_LD  34 /* 74HC165 SH/LD* */
#define S100_PIN_DIP_CK  35
#define S100_PIN_DIP_Q   36
#define S100_PIN_ROMOFF  37 /* clears ROM_ON; RESET* sets it */
#define S100_PIN_POJCLR  38 /* clears POJ_ARMED; RESET* sets it when the DIP enables POJ */
#define S100_PIN_INT     39 /* open drain INT*, unused in Rev0 */
#define S100_PIN_TXD     44 /* debug UART */
#define S100_PIN_RXD     45
#define S100_PIN_LED     47

/* ---- pipe registers, offsets from the pipe base ---- */
#define S100_PIPE_GETC    0
#define S100_PIPE_STATUS  1
#define S100_PIPE_PUTC    2
#define S100_PIPE_CONTROL 3 /* write */
#define S100_PIPE_TICK    3 /* read: free-running 1 kHz counter */

#define S100_ST_AVAIL  0x80
#define S100_ST_SIG    0x2C /* bits 5-1 = 10110b; bit 6 is always 0 */
#define S100_ST_SIGMSK 0x7E
#define S100_ST_TXFULL 0x01

#define S100_CTL_FLUSH  0x08 /* empties both directions, as on fujiversal */
#define S100_CTL_ROMOFF 0x10 /* turns the boot page off until RESET* */
#define S100_CTL_LOAD   0x20 /* 0x20|n: flush, then stream image n into GETC */
#define S100_CTL_LOADMSK 0xF8

/* LOAD stream header, then `len` bytes */
#define S100_LOAD_HDR 10 /* u16 len, u16 load, u16 entry, pipe base, page, console, flags */
#define S100_LOADF_PHANTOM 0x01
#define S100_LOADF_NOIMAGE 0x80 /* len is 0: no such image */

/* Card info: an image carrying the magic "FNS1" anywhere gets the four bytes
 * after it rewritten as it streams: pipe base, page, console, flags, and then
 * drive 0's sectors per track (0 while unknown) for the boot loader. */
#define S100_INFO_MAGIC "FNS1"
#define S100_INFO_LEN   5
#define S100_INFOF_PHANTOM 0x01

#define S100_IMG_CONFIG 0
#define S100_IMG_LOADER 1
#define S100_IMG_MAX    8

#endif /* S100_CARD_H */

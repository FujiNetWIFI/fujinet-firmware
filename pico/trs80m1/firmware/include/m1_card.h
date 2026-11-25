/* m1_card.h -- the FujiNet TRS-80 Model I card: an RP2354B on the keyboard
 * unit's 40-pin expansion edge in place of the Expansion Interface.
 *
 *   8000h-FFFFh  32K of RAM                                    (RAM enable)
 *   37E0h-37E3h  read: interrupt latch; write: drive select    (EI enable)
 *   37E8h-37EBh  printer status and data                       (EI enable)
 *   37ECh-37EFh  FD1771: slots 1-4 are drives 0-3              (EI enable)
 *   E8h-EBh      RS-232 board as the FujiNet modem             (RS-232 enable)
 *   D0h-D3h      the FujiBus pipe (fuji_pipe.h), base movable
 *
 * CONTROL (the pipe's +1) also takes the card's settings: a block turned on
 * answers at once, one turned off goes quiet at the next reset, so a program
 * can't pull its own memory out from under itself. A DIP switch forces a block
 * off whatever is saved.
 *
 * The bus side here is static inline so core1 and the MAME device run the same
 * code; a read is a peek, driven onto the bus, then its side effects.
 */
#ifndef M1_CARD_H
#define M1_CARD_H

#include <stdbool.h>
#include <stdint.h>

#include "fuji_iolink.h"
#include "fuji_pipe.h"
#include "fuji_ring.h"
#include "fuji_tcache.h"
#include "m1_inline.h"
#include "m1_uart.h"
#include "m1_wd1771.h"

/* ---- RP2354B pins (provisional until the board): GPIO 0-31 are one read ---- */
#define M1_PIN_A0     0  /* A0-A15 in */
#define M1_PIN_D0     16 /* D0-D7, through a 74LVC245 */
#define M1_PIN_WR     24 /* WR* (memory write) */
#define M1_PIN_RD     25 /* RD* (memory read); odd, so PWM B can count it */
#define M1_PIN_IN     26 /* IN* (port read) */
#define M1_PIN_OUT    27 /* OUT* (port write) */
#define M1_PIN_DRIVE  28 /* '245 DIR; OE* = DRIVE & RD* & IN* in a gate */
#define M1_PIN_SYSRES 29 /* SYSRES* in */
#define M1_PIN_INT    30 /* INT*, open drain */
#define M1_PIN_WAIT   31 /* WAIT*, open drain, unused */
#define M1_PIN_DIP0   33 /* RAM off, EI off, RS-232 off: closed forces it off */
#define M1_PIN_TXD    44 /* debug UART */
#define M1_PIN_RXD    45
#define M1_PIN_LED    47

/* ---- the address map ---- */
#define M1_RAM_BASE    0x8000
#define M1_RAM_SIZE    0x8000
#define M1_EI_BASE     0x37E0 /* 16 bytes */
#define M1_EI_LATCH    0x0 /* -0x3 */
#define M1_EI_CASSETTE 0x4 /* -0x7, write only: never driven */
#define M1_EI_PRINTER  0x8 /* -0xB */
#define M1_EI_FDC      0xC /* -0xF */

#define M1_LATCH_RTC 0x80
#define M1_LATCH_FDC 0x40

#define M1_PRINTER_READY 0x30 /* selected, no fault, paper in, not busy */
#define M1_PRINTER_BUSY  0x80
#define M1_PRINTER_RING  512

#define M1_RTC_US 25000 /* the EI's 40 Hz heartbeat */

/* ---- settings ---- */
#define M1_SET_RAM   0x01
#define M1_SET_EI    0x02
#define M1_SET_RS232 0x04
#define M1_SET_ALL   (M1_SET_RAM | M1_SET_EI | M1_SET_RS232)
#define M1_PIPE_BASE 0xD0

#define M1_CTL_ON   0x40 /* | M1_SET_*: on now, and saved */
#define M1_CTL_OFF  0x50 /* | M1_SET_*: off from the next reset, and saved */
#define M1_CTL_INFO 0x60 /* the card's info into GETC: M1_INFO_LEN bytes */
#define M1_CTL_SETMSK 0xF8
#define M1_INFO_LEN 5 /* 'M', '1', saved, forced off, in force */
#define M1_MARK_INFO 0x200 /* in from_host */

typedef struct {
    uint8_t enabled; /* M1_SET_* */
    uint8_t pipe_base;
} m1_settings_t;

typedef struct {
    m1_settings_t settings; /* in force */
    uint8_t saved;          /* M1_SET_*, as CONTROL last left them */
    uint8_t forced_off;     /* M1_SET_*, the DIP switches */
    uint8_t save_due;       /* atomic: saved changed, for core0 to keep */
    uint8_t info_left;      /* core0: info bytes still to stream */
    wd1771_t fdc;
    fuji_pipe_t pipe;
    m1_uart_t uart;
    ring8_t printer;
    bool rtc;             /* bus side: latch bit 7 */
    uint32_t rtc_ticks;   /* atomic: core0 counts the heartbeat */
    uint32_t rtc_seen;    /* bus side */
    uint32_t rtc_next;    /* core0 */
    fuji_tcache_t cache;
    fuji_iolink_t link;
    uint8_t printer_buf[M1_PRINTER_RING];
    uint8_t ram[M1_RAM_SIZE];
} m1_card_t;

/* ---- bus side ---- */

M1_INLINE bool m1_on(const m1_card_t *card, uint8_t what)
{
    return (card->settings.enabled & what) != 0;
}

M1_INLINE bool m1_is_pipe(const m1_card_t *card, uint8_t port)
{
    return (uint8_t)(port & 0xFC) == card->settings.pipe_base;
}

M1_INLINE bool m1_is_uart(const m1_card_t *card, uint8_t port)
{
    return (port & 0xFC) == M1_UART_BASE && m1_on(card, M1_SET_RS232);
}

/* True when the card answers a memory read of addr; *val is what it drives. */
M1_INLINE bool m1_mem_peek(m1_card_t *card, uint16_t addr, uint32_t now, uint8_t *val)
{
    if (addr & M1_RAM_BASE)
    {
        *val = card->ram[addr & (M1_RAM_SIZE - 1)];
        return m1_on(card, M1_SET_RAM);
    }
    if ((addr & 0xFFF0) != M1_EI_BASE || !m1_on(card, M1_SET_EI))
        return false;
    switch (addr & 0xC)
    {
    case M1_EI_LATCH:
        *val = (uint8_t)((card->rtc ? M1_LATCH_RTC : 0) | (card->fdc.intrq ? M1_LATCH_FDC : 0));
        return true;
    case M1_EI_PRINTER:
        *val = ring8_space(&card->printer) ? M1_PRINTER_READY
                                           : M1_PRINTER_READY | M1_PRINTER_BUSY;
        return true;
    case M1_EI_FDC:
        *val = wd_peek(&card->fdc, (uint8_t)(addr & 3), now);
        return true;
    default:
        return false;
    }
}

/* The side effects of a read m1_mem_peek() answered. */
M1_INLINE void m1_mem_after_read(m1_card_t *card, uint16_t addr)
{
    if ((addr & 0xFFF0) != M1_EI_BASE)
        return;
    if ((addr & 0xC) == M1_EI_LATCH)
        card->rtc = false;
    else if ((addr & 0xC) == M1_EI_FDC)
        wd_after_read(&card->fdc, (uint8_t)(addr & 3));
}

M1_INLINE void m1_mem_write(m1_card_t *card, uint16_t addr, uint8_t val, uint32_t now)
{
    if (addr & M1_RAM_BASE)
    {
        if (m1_on(card, M1_SET_RAM))
            card->ram[addr & (M1_RAM_SIZE - 1)] = val;
        return;
    }
    if ((addr & 0xFFF0) != M1_EI_BASE || !m1_on(card, M1_SET_EI))
        return;
    switch (addr & 0xC)
    {
    case M1_EI_LATCH:
        wd_select(&card->fdc, val, now);
        break;
    case M1_EI_PRINTER:
        ring8_put(&card->printer, val);
        break;
    case M1_EI_FDC:
        wd_write(&card->fdc, (uint8_t)(addr & 3), val);
        break;
    default:
        break;
    }
}

M1_INLINE bool m1_io_peek(m1_card_t *card, uint8_t port, uint32_t now, uint8_t *val)
{
    if (m1_is_pipe(card, port))
    {
        switch (port & 3)
        {
        case FUJI_PIPE_GETC:
            *val = fuji_pipe_peekc(&card->pipe);
            break;
        case FUJI_PIPE_STATUS:
            *val = fuji_pipe_status(&card->pipe);
            break;
        case FUJI_PIPE_TICK:
            *val = fuji_pipe_tick(&card->pipe, now);
            break;
        default:
            *val = 0xFF;
            break;
        }
        return true;
    }
    if (m1_is_uart(card, port))
    {
        *val = m1_uart_peek(&card->uart, port);
        return true;
    }
    return false;
}

M1_INLINE void m1_io_after_read(m1_card_t *card, uint8_t port)
{
    if (m1_is_pipe(card, port) && (port & 3) == FUJI_PIPE_GETC)
        fuji_pipe_getc(&card->pipe);
    else if (m1_is_uart(card, port))
        m1_uart_after_read(&card->uart, port);
}

M1_INLINE void m1_control(m1_card_t *card, uint8_t val)
{
    uint8_t blocks = val & M1_SET_ALL;
    if (val == FUJI_PIPE_CTL_FLUSH)
        fuji_pipe_flush(&card->pipe);
    else if (val == M1_CTL_INFO)
        fuji_pipe_mark(&card->pipe, M1_MARK_INFO);
    else if ((val & M1_CTL_SETMSK) == M1_CTL_ON)
    {
        card->saved |= blocks;
        card->settings.enabled |= (uint8_t)(blocks & ~card->forced_off);
        __atomic_store_n(&card->save_due, 1, __ATOMIC_RELEASE);
    }
    else if ((val & M1_CTL_SETMSK) == M1_CTL_OFF)
    {
        card->saved &= (uint8_t)~blocks;
        __atomic_store_n(&card->save_due, 1, __ATOMIC_RELEASE);
    }
}

M1_INLINE void m1_io_write(m1_card_t *card, uint8_t port, uint8_t val)
{
    if (m1_is_pipe(card, port))
    {
        if ((port & 3) == FUJI_PIPE_PUTC)
            fuji_pipe_putc(&card->pipe, val);
        else if ((port & 3) == FUJI_PIPE_CONTROL)
            m1_control(card, val);
    }
    else if (m1_is_uart(card, port))
        m1_uart_write(&card->uart, port, val);
}

/* The heartbeats core0 has counted since the last look. */
M1_INLINE void m1_take_ticks(m1_card_t *card)
{
    uint32_t ticks = __atomic_load_n(&card->rtc_ticks, __ATOMIC_ACQUIRE);
    if (ticks != card->rtc_seen)
    {
        card->rtc_seen = ticks;
        card->rtc = true;
    }
}

/* Between cycles: the FDC engine's posts and the heartbeat. */
M1_INLINE void m1_bus_idle(m1_card_t *card)
{
    wd_apply(&card->fdc);
    m1_take_ticks(card);
}

/* INT*: the latch holds something and the EI is the card's. */
M1_INLINE bool m1_int_pending(const m1_card_t *card)
{
    return m1_on(card, M1_SET_EI) && (card->rtc || card->fdc.intrq);
}

/* SYSRES*: the EI's latches clear and the 1771 restores; RAM and the pipe stay,
 * and the saved settings take hold. */
M1_INLINE void m1_bus_reset(m1_card_t *card)
{
    card->settings.enabled = (uint8_t)(card->saved & ~card->forced_off);
    card->rtc = false;
    wd_reset(&card->fdc);
}

/* ---- core0 ---- */

/* `settings` are the saved ones; forced_off the DIP switches. */
void m1_card_init(m1_card_t *card, const m1_settings_t *settings, uint8_t forced_off,
                  fuji_tx_fn send, void *send_ctx, uint32_t now);
/* The FDC engine, the heartbeat, the modem pacing and the link; call it often. */
void m1_card_service(m1_card_t *card, uint32_t now);
/* The DIP switches moved: what they force off goes off at once. */
void m1_card_force_off(m1_card_t *card, uint8_t forced_off);
/* True once after CONTROL changed the saved settings, for keeping them. */
bool m1_card_take_save(m1_card_t *card);
/* The Model I has just been switched on: CONFIG goes back into slot 1, as on a
 * FujiNet the computer powers. */
void m1_card_power_on(m1_card_t *card);

#endif /* M1_CARD_H */

/* bench.h -- a card on the host: the CPU's reads and writes go through the same
 * inline bus side core1 runs, time moves in steps that run core0's service, and
 * fake_esp.h stands in for the ESP32. */
#ifndef BENCH_H
#define BENCH_H

#include "check.h"
#include "fake_esp.h"
#include "m1_card.h"

static m1_card_t card;
static uint32_t now = 1000;

/* Lets `micros` of time pass, core0 and the link running every 50 us. */
static inline void run_us(uint32_t micros)
{
    uint32_t end = now + micros;
    while ((int32_t)(end - now) > 0)
    {
        m1_card_service(&card, now);
        fe_pump(&card.link, now);
        m1_bus_idle(&card);
        now += 50;
    }
}

static inline uint8_t peek(uint16_t addr)
{
    uint8_t val = 0xFF;
    m1_bus_idle(&card);
    if (m1_mem_peek(&card, addr, now, &val))
        m1_mem_after_read(&card, addr);
    else
        val = 0xFF;
    now += 2;
    return val;
}

static inline void poke(uint16_t addr, uint8_t val)
{
    m1_bus_idle(&card);
    m1_mem_write(&card, addr, val, now);
    now += 2;
}

static inline uint8_t in_port(uint8_t port)
{
    uint8_t val = 0xFF;
    m1_bus_idle(&card);
    if (m1_io_peek(&card, port, now, &val))
        m1_io_after_read(&card, port);
    else
        val = 0xFF;
    now += 2;
    return val;
}

static inline void out_port(uint8_t port, uint8_t val)
{
    m1_bus_idle(&card);
    m1_io_write(&card, port, val);
    now += 2;
}

/* A card with everything on and four empty slots; the link up or not. */
static inline void bench_start(bool link_up)
{
    fe_reset();
    m1_settings_t settings = {M1_SET_ALL, M1_PIPE_BASE};
    m1_card_init(&card, &settings, 0, fe_tx, NULL, now);
    if (link_up)
        fuji_iolink_set_up(&card.link, true, now);
}

/* Waits up to `limit` us for status bits `mask` to read `want`; false on timeout. */
static inline bool wait_status(uint8_t mask, uint8_t want, uint32_t limit)
{
    for (uint32_t spent = 0; spent < limit; spent += 100)
    {
        if ((peek(0x37EC) & mask) == want)
            return true;
        run_us(100);
    }
    return false;
}

/* Reads one sector through the data register as the ROM does; true when all 256 came. */
static inline bool read_sector_bytes(uint8_t *buf, uint32_t limit)
{
    int got = 0;
    for (uint32_t spent = 0; spent < limit && got < 256;)
    {
        uint8_t status = peek(0x37EC);
        if (status & WD_ST_DRQ)
        {
            buf[got++] = peek(0x37EF);
            continue;
        }
        if (!(status & WD_ST_BUSY))
            break;
        run_us(100);
        spent += 100;
    }
    return got == 256;
}

#endif /* BENCH_H */

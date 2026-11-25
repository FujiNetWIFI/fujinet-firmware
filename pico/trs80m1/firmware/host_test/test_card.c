// The card's decode and the rest of the EI: RAM, the latch and its heartbeat,
// the printer, the RS-232 board, the pipe, and each block turned off.
#include "bench.h"

int main(void)
{
    bench_start(true);
    fe_disk(0, 35, 10, 1);
    run_us(50000);

    // RAM at 8000h-FFFFh; nothing below but the EI's block.
    poke(0x8000, 0x12);
    poke(0xFFFF, 0x34);
    CHECK_EQ(peek(0x8000), 0x12);
    CHECK_EQ(peek(0xFFFF), 0x34);
    uint8_t val;
    CHECK(!m1_mem_peek(&card, 0x4000, now, &val));
    CHECK(!m1_mem_peek(&card, 0x37E4, now, &val));
    CHECK(!m1_mem_peek(&card, 0x37F0, now, &val));

    // The heartbeat: 40 a second, each cleared by reading the latch.
    peek(0x37E0);
    run_us(M1_RTC_US + 100);
    CHECK(m1_int_pending(&card));
    CHECK(peek(0x37E0) & M1_LATCH_RTC);
    CHECK_EQ(peek(0x37E0) & M1_LATCH_RTC, 0);
    int ticks = 0;
    for (int msec = 0; msec < 1000; msec++)
    {
        run_us(1000);
        ticks += (peek(0x37E0) & M1_LATCH_RTC) != 0;
    }
    CHECK(ticks >= 39 && ticks <= 41);

    // The printer: ready, a line goes to the ESP32's printer.
    CHECK_EQ(peek(0x37E8) & 0xF0, 0x30);
    for (const char *text = "LPRINT\r\n"; *text; text++)
        poke(0x37E8, (uint8_t)*text);
    run_us(100000);
    CHECK_EQ(fe.nprinted, 8);

    // The RS-232 board: bytes out batch to MODEM_WRITE, bytes in arrive at the baud rate.
    CHECK(in_port(0xEA) & M1_UART_THRE);
    out_port(0xE9, 0x55); // 300 baud
    out_port(0xEB, 'A');
    out_port(0xEB, 'T');
    run_us(20000);
    CHECK_EQ(fe.nmodem, 2);
    fe_modem_out((const uint8_t *)"OK", 2);
    run_us(1000);
    CHECK(in_port(0xEA) & M1_UART_DR);
    CHECK_EQ(in_port(0xEB), 'O');
    CHECK_EQ(in_port(0xEA) & M1_UART_DR, 0);
    run_us(10000); // under one 300-baud character
    CHECK_EQ(in_port(0xEA) & M1_UART_DR, 0);
    run_us(30000);
    CHECK_EQ(in_port(0xEB), 'K');

    // The pipe at D0h: the signature, then a round trip to FUJI.
    CHECK_EQ(in_port(0xD1) & FUJI_PIPE_ST_SIGMSK, FUJI_PIPE_ST_SIG);
    uint8_t req[32];
    size_t len = fujibus_build_request(FUJI_DEVICEID_FUJINET, 0xE8, NULL, 0, NULL, 0, req,
                                       sizeof(req));
    for (size_t idx = 0; idx < len; idx++)
        out_port(0xD2, req[idx]);
    run_us(5000);
    CHECK(in_port(0xD1) & FUJI_PIPE_ST_AVAIL);
    uint8_t rep[64];
    int got = 0;
    while (in_port(0xD1) & FUJI_PIPE_ST_AVAIL)
        rep[got++] = in_port(0xD0);
    fb_reply_t reply;
    CHECK(fujibus_parse_reply(rep, (size_t)got, &reply));
    CHECK_EQ(reply.command, CMD_FUJI_ACK);

    // Settings through CONTROL: off waits for the next reset, on is at once,
    // and both are kept; a DIP switch wins.
    out_port(0xD3, M1_CTL_OFF | M1_SET_RAM);
    CHECK(m1_mem_peek(&card, 0x8000, now, &val));
    CHECK(m1_card_take_save(&card));
    CHECK(!m1_card_take_save(&card));
    m1_bus_reset(&card);
    CHECK(!m1_mem_peek(&card, 0x8000, now, &val));
    out_port(0xD3, M1_CTL_ON | M1_SET_RAM);
    CHECK(m1_mem_peek(&card, 0x8000, now, &val));
    m1_card_force_off(&card, M1_SET_RAM);
    CHECK(!m1_mem_peek(&card, 0x8000, now, &val));
    out_port(0xD3, M1_CTL_ON | M1_SET_RAM);
    CHECK(!m1_mem_peek(&card, 0x8000, now, &val));
    out_port(0xD3, M1_CTL_INFO);
    run_us(1000);
    uint8_t info[M1_INFO_LEN];
    for (int idx = 0; idx < M1_INFO_LEN; idx++)
        info[idx] = in_port(0xD0);
    CHECK_EQ(info[0], 'M');
    CHECK_EQ(info[1], '1');
    CHECK_EQ(info[2], M1_SET_ALL);
    CHECK_EQ(info[3], M1_SET_RAM);
    CHECK_EQ(info[4], M1_SET_EI | M1_SET_RS232);
    CHECK_EQ(in_port(0xD1) & FUJI_PIPE_ST_AVAIL, 0);
    m1_card_force_off(&card, 0);
    m1_bus_reset(&card);

    // Power-on: CONFIG goes back into slot 1 by SET_BOOT_MODE.
    m1_card_power_on(&card);
    run_us(1000);
    CHECK_EQ(fe.last_fuji, CMD_FUJI_SET_BOOT_MODE);

    // Each block off: the bus floats there.
    card.settings.enabled = M1_SET_EI | M1_SET_RS232;
    CHECK(!m1_mem_peek(&card, 0x8000, now, &val));
    card.settings.enabled = M1_SET_RAM | M1_SET_RS232;
    CHECK(!m1_mem_peek(&card, 0x37EC, now, &val));
    CHECK(!m1_int_pending(&card));
    card.settings.enabled = M1_SET_RAM | M1_SET_EI;
    CHECK(!m1_io_peek(&card, 0xEA, now, &val));
    CHECK(m1_io_peek(&card, 0xD1, now, &val));

    fe_reset();
    return check_done("test_card");
}

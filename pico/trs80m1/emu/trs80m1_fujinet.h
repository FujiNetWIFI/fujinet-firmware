// license:BSD-3-Clause
/* trs80m1_fujinet.h -- the FujiNet Model I card in MAME: the card's own bus side
 * and core0 code (pico/trs80m1/firmware), linked over TCP to a fujinet-pc's
 * BoIP port in place of the USB link to the ESP32-S3.
 *
 * $FUJINET_TCP is host:port (default 127.0.0.1:1985); $FUJINET_ASYNC=1 lets
 * emulated time run on while the link waits, instead of stalling it, and
 * $FUJINET_POWER_CONFIG=1 puts CONFIG back in slot 1 at every power-on, as the
 * card does. The configuration switches stand for its DIPs; the settings CONTROL
 * saves are the device's NVRAM.
 */
#ifndef MAME_TRS_TRS80M1_FUJINET_H
#define MAME_TRS_TRS80M1_FUJINET_H

#pragma once

#include <memory>

class trs80m1_fujinet_device : public device_t, public device_nvram_interface
{
public:
    trs80m1_fujinet_device(const machine_config &mconfig, const char *tag, device_t *owner,
                           u32 clock = 0);
    ~trs80m1_fujinet_device();

    auto int_cb() { return m_int_cb.bind(); }

    // 37E0h-37EFh
    u8 ei_r(offs_t offset);
    void ei_w(offs_t offset, u8 data);
    // 8000h-FFFFh
    u8 ram_r(offs_t offset);
    void ram_w(offs_t offset, u8 data);
    // every port but FFh
    u8 io_r(offs_t offset);
    void io_w(offs_t offset, u8 data);
    // SYSRES*, active high here
    void sysres_w(int state);

protected:
    void device_start() override ATTR_COLD;
    void device_reset() override ATTR_COLD;
    void device_stop() override ATTR_COLD;
    ioport_constructor device_input_ports() const override ATTR_COLD;

    void nvram_default() override;
    bool nvram_read(util::read_stream &file) override;
    bool nvram_write(util::write_stream &file) override;

private:
    TIMER_CALLBACK_MEMBER(service);

    u32 now_us();
    u8 forced_off();
    void update_int();
    void link_connect();
    void link_receive();
    static size_t link_send(void *ctx, const u8 *buf, size_t len);

    devcb_write_line m_int_cb;
    required_ioport m_switches;
    emu_timer *m_timer;
    struct state;
    std::unique_ptr<state> m_state;
};

DECLARE_DEVICE_TYPE(TRS80M1_FUJINET, trs80m1_fujinet_device)

#endif // MAME_TRS_TRS80M1_FUJINET_H

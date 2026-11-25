// license:BSD-3-Clause
/* trs80fn.ipp -- a Model I with the FujiNet card on its expansion edge in place
 * of the Expansion Interface: the card answers 37E0h-37EFh, 8000h-FFFFh and its
 * ports, and owns the heartbeat and the interrupt. Included at the end of
 * trs80.cpp by pico/trs80m1/emu/apply.sh, for the driver's input ports. */

#include "trs80m1_fujinet.h"
#include "trs80_quik.h"

namespace {

class trs80fn_state : public trs80_state
{
public:
    trs80fn_state(const machine_config &mconfig, device_type type, const char *tag)
        : trs80_state(mconfig, type, tag), m_card(*this, "fujinet")
    {
    }

    void trs80fn(machine_config &config) ATTR_COLD;

private:
    void fn_mem(address_map &map) ATTR_COLD;
    void fn_io(address_map &map) ATTR_COLD;

    required_device<trs80m1_fujinet_device> m_card;
};

void trs80fn_state::fn_mem(address_map &map)
{
    map(0x0000, 0x37ff).rom();
    map(0x37e0, 0x37ef).rw(m_card, FUNC(trs80m1_fujinet_device::ei_r),
                           FUNC(trs80m1_fujinet_device::ei_w));
    map(0x3800, 0x3bff).r(FUNC(trs80fn_state::keyboard_r));
    map(0x3c00, 0x3fff).ram().share(m_p_videoram);
    map(0x4000, 0x7fff).ram();
    map(0x8000, 0xffff).rw(m_card, FUNC(trs80m1_fujinet_device::ram_r),
                           FUNC(trs80m1_fujinet_device::ram_w));
}

void trs80fn_state::fn_io(address_map &map)
{
    map.global_mask(0xff);
    map.unmap_value_high();
    map(0x00, 0xfe).rw(m_card, FUNC(trs80m1_fujinet_device::io_r),
                       FUNC(trs80m1_fujinet_device::io_w));
    map(0xff, 0xff).rw(FUNC(trs80fn_state::port_ff_r), FUNC(trs80fn_state::port_ff_w));
}

void trs80fn_state::trs80fn(machine_config &config)
{
    level1(config);
    m_maincpu->set_addrmap(AS_PROGRAM, &trs80fn_state::fn_mem);
    m_maincpu->set_addrmap(AS_IO, &trs80fn_state::fn_io);
    m_cassette->set_formats(trs80l2_cassette_formats);
    TRS80_QUICKLOAD(config, "quickload", m_maincpu, attotime::from_seconds(1));

    TRS80M1_FUJINET(config, m_card);
    m_card->int_cb().set_inputline(m_maincpu, INPUT_LINE_IRQ0);
    // RESET reaches the expansion edge as SYSRES* as well as the CPU's NMI.
    subdevice<input_merger_device>("nmigate")->output_handler().append(
        m_card, FUNC(trs80m1_fujinet_device::sysres_w));
}

ROM_START(trs80fn)
    ROM_REGION(0x3800, "maincpu", ROMREGION_ERASEFF)
    ROM_LOAD("rom-a.z1", 0x0000, 0x1000, CRC(37c59db2) SHA1(e8f8f6a4460a6f6755873580be6ff70cebe14969))
    ROM_LOAD("rom-b.z2", 0x1000, 0x1000, CRC(05818718) SHA1(43c538ca77623af6417474ca5b95fb94205500c1))
    ROM_LOAD("rom-c.z3", 0x2000, 0x1000, CRC(306e5d66) SHA1(1e1abcfb5b02d4567cf6a81ffc35318723442369))

    ROM_REGION(0x0400, "chargen", 0)
    ROM_LOAD("mcm6670p.z29", 0x0000, 0x0400, CRC(0033f2b9) SHA1(0d2cd4197d54e2e872b515bbfdaa98efe502eda7))
ROM_END

} // anonymous namespace

// Not flagged not-working, unlike trs80l2: MAME would hold the rig's window on its warning.
//    YEAR  NAME     PARENT   COMPAT  MACHINE  INPUT  CLASS          INIT          COMPANY    FULLNAME                                FLAGS
COMP( 2026, trs80fn, trs80l2, 0,      trs80fn, trs80, trs80fn_state, init_trs80l2, "FujiNet", "TRS-80 Model I with the FujiNet card", MACHINE_SUPPORTS_SAVE )

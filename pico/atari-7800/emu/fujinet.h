// license:BSD-3-Clause
// copyright-holders:Thomas Cherryhomes
#ifndef MAME_BUS_A7800_FUJINET_H
#define MAME_BUS_A7800_FUJINET_H

#pragma once

#include "a78_slot.h"
#include "sound/pokey.h"
#include "a78_cart.h"
#include "a78map.h"
#include "fuji_load.h"
#include "hsc.h"

#include <vector>

// ======================> a78_fujinet_device
//
// Model of the FujiNet RP2354B cartridge: 512K of SRAM behind the slot table,
// the signed boot block at $F000 until the first image is in, the loader at
// $0600 and the arena at $0800, a POKEY at $4000 or $0450 (MAME's own, so a
// game's RANDOM reads match its native cart), and the High Score Cart. The
// mapper engine (a78map.c), the bus decode and glue (a78_cart.h), the load
// sequence (fuji_load.c), the HSC's saves (hsc.c), the protocol (fujimail.c)
// and the wire codec (fujibus.c) are the cartridge firmware's own sources,
// compiled in by pico/atari-7800/emu/apply.sh; frames go over TCP to
// fujinet-pc ($FUJINET_TCP, default 127.0.0.1:9995).
//
//   -cartslot fujinet -cart client.a78   a claimed client in place of CONFIG
//   FUJINET_IMAGE=game.bin               that image in the SRAM from power-on,
//                                        so the BIOS boots it (the A/B tests)
//   FUJINET_HSC=hsc.bin                  the HSC ROM, installed and on
// MAME's 7800 needs a cart in the slot, so there is always a -cart.

class a78_fujinet_device : public device_t, public device_a78_cart_interface
{
public:
	a78_fujinet_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);
	virtual ~a78_fujinet_device();

	virtual uint8_t read_40xx(offs_t offset) override;
	virtual void write_40xx(offs_t offset, uint8_t data) override;

	// fujimail / fuji_load port plumbing, called from the C callbacks
	void poke(unsigned offset, uint8_t value);
	void paint(const uint8_t *src, uint8_t fill);
	void window(uint16_t word) { m_load_slots[FN_LOADWIN_BASE >> 13] = word; }
	uint8_t stream_open(int stream, uint32_t size);
	void stream_write(int stream, const uint8_t *chunk, unsigned len);
	uint8_t stream_close(int stream, uint32_t got, bool aborted);
	void arm_swap();

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override;
	virtual void device_add_mconfig(machine_config &config) override ATTR_COLD;

private:
	uint8_t bus_read(uint16_t a);
	void bus_write(uint16_t a, uint8_t data);
	uint16_t slot_word(uint16_t a) const;
	void to_boot();
	void to_load();
	void flip();
	void mailbox_event(uint16_t offset, uint8_t data);
	bool own_register(unsigned reg, uint8_t data);
	void load_file(std::vector<uint8_t> &out, const char *path);
	void direct_boot(const std::vector<uint8_t> &img);
	void dump_sram();
	void publish_staged(const a78map_plan_t &p);
	TIMER_CALLBACK_MEMBER(service);

	required_device<pokey_device> m_pokey;

	uint8_t m_arena[FN_ARENA_SIZE];
	std::vector<uint8_t> m_sram;
	std::vector<uint8_t> m_config;            // CONFIG, or the -cart client
	std::vector<uint8_t> m_file;
	std::vector<uint8_t> m_store[2];          // the cart's RAM and flash tiers
	std::vector<uint8_t> m_cfg;
	std::vector<uint8_t> m_hsc_rom;
	int m_open_tier = -1;
	std::string m_cfg_mapper;

	a78_bus_t m_bus{};
	a78map_t *m_live = nullptr;
	uint16_t m_load_slots[A78MAP_SLOTS]{};
	fuji_load_t m_loader{};
	fuji_load_port_t m_load_port{};
	hsc_t m_hsc{};
	hsc_port_t m_hsc_port{};
	uint8_t m_hsc_ram[A78MAP_HSC_RAM_SIZE];
	uint32_t m_hsc_dirty = 0;
	bool m_hsc_on = false;
	bool m_direct = false;
	std::vector<uint8_t> m_direct_img;

	memory_passthrough_handler m_tap_tia, m_tap_bios;
	emu_timer *m_service_timer = nullptr;
	output_finder<> m_mode_out;
	output_finder<> m_handover_out;
	bool m_init_done = false;
	bool m_debug = false;
	const char *m_sramdump = nullptr;
	cpu_device *m_cpu = nullptr;
};

DECLARE_DEVICE_TYPE(A78_FUJINET, a78_fujinet_device)

#endif // MAME_BUS_A7800_FUJINET_H

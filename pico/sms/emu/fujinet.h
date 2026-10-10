// license:BSD-3-Clause
// copyright-holders:Thomas Cherryhomes
#ifndef MAME_BUS_SEGA8_FUJINET_H
#define MAME_BUS_SEGA8_FUJINET_H

#pragma once

#include "sega8_slot.h"
#include "sms_cart.h"
#include "smsmap.h"
#include "fuji_load.h"

#include <vector>

// ======================> sega8_fujinet_device
//
// Model of the FujiNet RP2354B cartridge: 1 MB of SRAM behind the cart's page
// table, the 4K arena at $B000, and CONFIG served RESIDENT at $0000-$7FFF.
// The mapper engine (smsmap.c), the bus decode and glue (sms_cart.h), the load
// sequence (fuji_load.c), the protocol (fujimail.c) and the wire codec
// (fujibus.c) are the cartridge firmware's own sources, compiled in by
// pico/sms/emu/apply.sh; frames go over TCP to fujinet-pc ($FUJINET_TCP,
// default 127.0.0.1:9995).
//
//   -cart client.sms      a claimed 32K client (CONFIG itself), RESIDENT
//   -cart anything else   the image straight into the SRAM, from power-on;
//                         FN_HOT_CONFIG goes to the baked CONFIG
// The SMS driver ignores a slot with no image, so there is no -cart-less run.

class sega8_fujinet_device : public device_t, public device_sega8_cart_interface
{
public:
	sega8_fujinet_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);
	virtual ~sega8_fujinet_device();

	virtual uint8_t read_cart(offs_t offset) override;
	virtual void write_cart(offs_t offset, uint8_t data) override;
	virtual void write_mapper(offs_t offset, uint8_t data) override;

	// fujimail / fuji_load port plumbing, called from the C callbacks
	void poke(unsigned offset, uint8_t value);
	void set_load(bool on) { m_load = on; }
	uint8_t stream_open(int stream, uint32_t size);
	void stream_write(int stream, const uint8_t *chunk, unsigned len);
	uint8_t stream_close(int stream, uint32_t got, bool aborted);
	void arm_swap();

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override;

private:
	bool fetching(uint16_t a);
	sms_glue_t glue() const;
	void to_resident();
	void flip();
	void mapper_write(uint16_t a, uint8_t d);
	void mailbox_event(uint16_t offset, uint8_t data);
	void load_file();
	void direct_boot(const uint8_t *img, uint32_t len, const smsmap_plan_t *plan);
	void dump_sram();

	uint8_t m_arena[FN_ARENA_SIZE];
	uint8_t m_window[FN_LOADWIN_SIZE];
	std::vector<uint8_t> m_sram;
	std::vector<uint8_t> m_resident;          // 32K served RESIDENT
	std::vector<uint8_t> m_file;              // the -cart image as on disk
	std::vector<uint8_t> m_store[2];          // the cart's RAM and flash tiers
	std::vector<uint8_t> m_cfg;
	int m_open_tier = -1;
	std::string m_cfg_mapper;

	const uint8_t *m_ptab_res[SMS_PAGES]{};
	const uint8_t *m_ptab_app[SMS_PAGES]{};
	const uint8_t *m_ptab_game[SMS_PAGES]{};
	sms_bus_t m_bus{};
	smsmap_t m_res_map{}, m_game_map{};
	smsmap_t *m_live = nullptr;
	fuji_load_t m_loader{};
	fuji_load_port_t m_load_port{};
	bool m_game = false, m_mbox = false, m_ram_we = false, m_load = false;
	bool m_direct = false;

	memory_passthrough_handler m_tap_c000, m_tap_iow, m_tap_ior;
	output_finder<> m_mode_out;
	bool m_init_done = false;
	bool m_debug = false;
	const char *m_sramdump = nullptr;
	cpu_device *m_cpu = nullptr;
};

DECLARE_DEVICE_TYPE(SEGA8_FUJINET, sega8_fujinet_device)

#endif // MAME_BUS_SEGA8_FUJINET_H

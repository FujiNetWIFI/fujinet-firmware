// license:BSD-3-Clause
// copyright-holders:Thomas Cherryhomes
#ifndef MAME_BUS_COLECO_CARTRIDGE_FUJINET_H
#define MAME_BUS_COLECO_CARTRIDGE_FUJINET_H

#pragma once

#include "exp.h"
#include "colmap.h"

#include <vector>

// ======================> colecovision_fujinet_cartridge_device
//
// Model of the FujiNet RP2040 ColecoVision cartridge, built from the firmware's own mailbox
// (fujimail), codec (fujibus) and mapper (colmap) sources; frames go over TCP to fujinet-pc
// ($FUJINET_TCP, default 127.0.0.1:9995). Every mapper is served by colmap_serve, the code
// core1 inlines. An image without the "FUJI" claim runs with the mailbox dead. The real
// port can't tell a write from a read, so write() takes the read path too.

class colecovision_fujinet_cartridge_device : public device_t,
							public device_colecovision_cartridge_interface
{
public:
	colecovision_fujinet_cartridge_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);
	virtual ~colecovision_fujinet_cartridge_device();

	virtual uint8_t read(offs_t offset, int _8000, int _a000, int _c000, int _e000) override;
	virtual void write(offs_t offset, uint8_t data, int _8000, int _a000, int _c000, int _e000) override;

	// fujimail port plumbing, called from the C callbacks
	void poke(unsigned offset, uint8_t value);
	uint8_t stream_open(int stream, uint32_t size);
	void stream_write(int stream, const uint8_t *chunk, unsigned len);
	uint8_t stream_close(int stream, uint32_t got, bool aborted);
	void arm_swap();

protected:
	virtual void device_start() override ATTR_COLD;
	// The image is loaded after device_start, so the window is built here --
	// once: a machine reset must not reset the "cart", which on hardware never
	// sees the console's reset line at all.
	virtual void device_reset() override;

private:
	void apply_serving(const colmap_plan_t &plan);
	void do_swap();

	uint8_t m_window[COLMAP_WINDOW];
	uint8_t m_staged[COLMAP_WINDOW];
	// The full image for the banked kinds (the firmware's RAM store); flat
	// images live wholly in the windows above.
	std::vector<uint8_t> m_image, m_staged_image;
	std::vector<uint8_t> m_rx[2];           // per-stream push buffers
	colmap_plan_t m_staged_plan{};

	// What read() serves: the emulator's copy of core1's fuji_live.
	colmap_serve_t m_serve{};
	const uint8_t *m_base = nullptr;

	colmap_kind_t m_pending_hint = COLMAP_KIND_AUTO;

	bool m_init_done = false;
	bool m_mailbox_live = false;
	bool m_have_staged = false;
	bool m_staged_claims = false;
	bool m_swap_armed = false;
	bool m_debug = false;
	const char *m_bootdump = nullptr;
};

// device type definition
DECLARE_DEVICE_TYPE(COLECOVISION_FUJINET, colecovision_fujinet_cartridge_device)

#endif // MAME_BUS_COLECO_CARTRIDGE_FUJINET_H

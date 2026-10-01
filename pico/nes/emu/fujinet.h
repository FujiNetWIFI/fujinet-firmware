// license:BSD-3-Clause
// copyright-holders:Thomas Cherryhomes
#ifndef MAME_BUS_NES_FUJINET_H
#define MAME_BUS_NES_FUJINET_H

#pragma once

#include "nxrom.h"
#include "nes_cart.h"
#include "nesmap.h"

#include <deque>
#include <vector>

// ======================> nes_fujinet_device
//
// Model of the FujiNet RP2354B cartridge: two 512K SRAMs (PRG, CHR) behind
// bank tables, a 4K mailbox arena at $5000 with a 2K loader ROM at $5800,
// cart-served WRAM at $6000, and a vector page at $FF00 while the SRAM is
// off. The protocol (fujimail.c), the wire codec (fujibus.c), the mapper
// engine (nesmap.c) and the bus decode (nes_cart.h) are the cartridge
// firmware's own sources, compiled in verbatim by pico/nes/emu/apply.sh; this
// device is only the port -- bytes go into the arena, frames go over a TCP
// socket to fujinet-pc ($FUJINET_TCP, default 127.0.0.1:9995).
//
// What the hardware does with PIO -- the bank tables -- this device does with
// two slot arrays indexed exactly the same way, and what core1 does with the
// ring -- the RMW dummy-write drop -- this device does with a cycle-stamped
// queue drained on the next cart access.
//
// Two ways in: `-cart file.nes` runs that image directly (FUJINET_LOADER=1
// sends it through the loader ROM instead, as the cart would); no -cart
// boots the baked CONFIG through the loader, exactly like power-on.

class nes_fujinet_device : public device_t, public device_nes_cart_interface
{
public:
	nes_fujinet_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);
	virtual ~nes_fujinet_device();

	virtual uint8_t read_l(offs_t offset) override;
	virtual uint8_t read_m(offs_t offset) override;
	virtual uint8_t read_h(offs_t offset) override;
	virtual void write_l(offs_t offset, uint8_t data) override;
	virtual void write_m(offs_t offset, uint8_t data) override;
	virtual void write_h(offs_t offset, uint8_t data) override;
	virtual uint8_t chr_r(offs_t offset) override;
	virtual void chr_w(offs_t offset, uint8_t data) override;
	virtual void hblank_irq(int scanline, bool vblank, bool blanked) override;
	virtual void pcb_reset() override;

	// fujimail port plumbing, called from the C callbacks
	void poke(unsigned offset, uint8_t value);
	uint8_t stream_open(int stream, uint32_t size);
	void stream_write(int stream, const uint8_t *chunk, unsigned len);
	uint8_t stream_close(int stream, uint32_t got, bool aborted);
	void arm_swap();

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override;

private:
	uint32_t cycles() const;
	void service();
	void mailbox_event(uint16_t offset, uint8_t data);
	void mapper_write(uint16_t addr, uint8_t data);
	void apply_map();
	void apply_mirroring();
	void begin_load(const uint8_t *base, const nesmap_plan_t *plan);
	void publish_slice();
	void slice_acked();
	void finish_load();
	void direct_boot(const uint8_t *base, const nesmap_plan_t *plan);
	void load_file();

	uint8_t m_arena[FN_ARENA_SIZE];
	uint8_t m_wram[NESMAP_WRAM_MAX];
	uint8_t m_vectors[256];
	std::vector<uint8_t> m_prg_sram;       // 512K
	std::vector<uint8_t> m_chr_sram;       // 512K
	std::vector<uint8_t> m_file;           // the -cart image, header and all
	std::vector<uint8_t> m_rx[2];          // per-stream push buffers

	nes_serve_t m_serve{};
	nesmap_t m_map{};
	nesmap_plan_t m_staged_plan{};
	const uint8_t *m_staged_base = nullptr;
	nesmap_plan_t m_resident_plan{};
	const uint8_t *m_resident_base = nullptr;
	bool m_autoload = false;
	bool m_have_staged = false;
	bool m_armed = false;
	bool m_map_live = false;

	// the "PIO tables" and the '595
	uint8_t m_prg_slot[NESMAP_PRG_SLOTS]{};
	uint16_t m_chr_slot[NESMAP_CHR_SLOTS]{};
	bool m_prg_we = false;
	bool m_chr_we = false;
	uint8_t m_mirror = NESMAP_MIR_V;

	// the load sequence (fuji_cart.c)
	enum { LS_IDLE, LS_RUN, LS_DONE } m_ls_state = LS_IDLE;
	const uint8_t *m_ls_base = nullptr;
	nesmap_plan_t m_ls_plan{};
	uint32_t m_ls_slice = 0, m_ls_nprg = 0, m_ls_nchr = 0;
	uint8_t m_ls_seq = 0;
	bool m_ls_ack_pending = false;

	std::deque<nes_event_t> m_events;
	uint8_t m_diag_rmw = 0;

	bool m_init_done = false;
	bool m_debug = false;
	bool m_use_loader = false;
	const char *m_bootdump = nullptr;
	cpu_device *m_cpu = nullptr;
};

// device type definition
DECLARE_DEVICE_TYPE(NES_FUJINET, nes_fujinet_device)

#endif // MAME_BUS_NES_FUJINET_H

// license:BSD-3-Clause
// copyright-holders:Thomas Cherryhomes
/***********************************************************************************************************

 Nintendo Entertainment System FujiNet cartridge emulation

 See fujinet.h for the design; pico/nes/README.md in fujinet-firmware for the
 bring-up this device serves.

 ***********************************************************************************************************/

#include "emu.h"
#include "fujinet.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// The cartridge firmware's own protocol sources, compiled in verbatim (copied
// next to this file by pico/nes/emu/apply.sh -- as C++, to live with MAME's
// forced C++ precompiled header, so no extern "C").
#include "fuji_mailbox.h"
#include "fujimail.h"
#include "fujitcp.h"
#include "fujiconfigrom.h"
#include "nesloaderrom.h"

DEFINE_DEVICE_TYPE(NES_FUJINET, nes_fujinet_device, "nes_fujinet", "NES FujiNet Cartridge")

// fujimail's port interface is C function pointers with no context argument,
// so the single device instance is reached through this. One cart slot, one
// cart: the constraint is real hardware's too.
static nes_fujinet_device *s_fujinet = nullptr;

/*-------------------------------------------------
    C port callbacks
-------------------------------------------------*/

static void c_poke(unsigned offset, uint8_t value) { s_fujinet->poke(offset, value); }
static bool c_link_up() { return fujitcp_active(); }
static uint8_t c_stream_open(int stream, uint32_t size) { return s_fujinet->stream_open(stream, size); }
static void c_stream_write(int stream, const uint8_t *chunk, unsigned len) { s_fujinet->stream_write(stream, chunk, len); }
static uint8_t c_stream_close(int stream, uint32_t got, bool aborted) { return s_fujinet->stream_close(stream, got, aborted); }
static void c_arm_swap() { s_fujinet->arm_swap(); }

static void c_on_txn(const fujimail_txn_t *t)
{
	char txt[40];
	unsigned k, m = 0;

	for (k = 0; k < t->rxlen && m < sizeof txt - 1; k++)
	{
		uint8_t c = t->rx[k];
		if (c == 0)
			break;
		txt[m++] = (c >= 0x20 && c < 0x7F) ? char(c) : '.';
	}
	txt[m] = '\0';
	fprintf(stderr,
			"fujinet: dev=%02X cmd=%02X nparam=%u txlen=%u seq=%u"
			" -> err=%d reply=%02X rxlen=%u%s%s%s\n",
			t->device, t->command, t->nparam, t->txlen, t->seq,
			t->status, t->reply_cmd, t->rxlen,
			m ? " \"" : "", txt, m ? "\"" : "");
}

static void c_on_dbc(fujimail_dbc_ev_t ev, int stream, uint32_t expect, unsigned got, bool aborted)
{
	if (ev == FUJIMAIL_DBC_OPEN)
		fprintf(stderr, "fujinet: DBC open stream=%d size=%u\n", stream, expect);
	else
		fprintf(stderr, "fujinet: DBC close stream=%d got=%u%s\n",
				stream, got, aborted ? " ABORTED" : "");
}

static const fujimail_port_t mame_port = {
	c_poke, c_link_up, fujitcp_transact, fujitcp_send_bare,
	c_stream_open, c_stream_write, c_stream_close, c_arm_swap,
	nullptr, nullptr, c_on_txn, c_on_dbc,
};

static const fujimail_port_t mame_port_quiet = {
	c_poke, c_link_up, fujitcp_transact, fujitcp_send_bare,
	c_stream_open, c_stream_write, c_stream_close, c_arm_swap,
	nullptr, nullptr, nullptr, nullptr,
};

/*-------------------------------------------------
    device
-------------------------------------------------*/

nes_fujinet_device::nes_fujinet_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, NES_FUJINET, tag, owner, clock)
	, device_nes_cart_interface(mconfig, *this)
{
}

nes_fujinet_device::~nes_fujinet_device()
{
	if (s_fujinet == this)
	{
		fujitcp_close();
		s_fujinet = nullptr;
	}
}

void nes_fujinet_device::device_start()
{
	s_fujinet = this;
	m_debug = getenv("FUJINET_DEBUG") != nullptr;
	m_bootdump = getenv("FUJINET_BOOTDUMP");
	m_use_loader = getenv("FUJINET_LOADER") != nullptr;
	m_cpu = dynamic_cast<cpu_device *>(machine().root_device().subdevice("maincpu"));

	m_prg_sram.assign(NESMAP_PRG_MAX, 0xff);
	m_chr_sram.assign(NESMAP_CHR_MAX, 0x00);
	std::memset(m_arena, 0, sizeof m_arena);
	std::memset(m_wram, 0, sizeof m_wram);
	std::memset(m_vectors, 0, sizeof m_vectors);
	std::memcpy(m_arena + FN_LOADER, _loaderrom, FUJI_LOADERROM_SIZE);
	// RESET into the loader, NMI and IRQ onto its RTI at $5803 (fuji_cart.c)
	m_vectors[0xFA] = 0x03; m_vectors[0xFB] = 0x58;
	m_vectors[0xFC] = 0x00; m_vectors[0xFD] = 0x58;
	m_vectors[0xFE] = 0x03; m_vectors[0xFF] = 0x58;

	m_serve.arena = m_arena;
	m_serve.wram = m_wram;
	m_serve.wram_size = NESMAP_WRAM_MAX;
	m_serve.vectors = m_vectors;
	m_serve.sram_en = false;
	m_serve.mailbox = true;
	m_serve.wram_en = false;
	m_serve.wram_wp = false;
	m_serve.loading = false;

	// No save_item for the mailbox: the protocol state lives in the shared C
	// service's globals, which a save state cannot capture.
}

void nes_fujinet_device::pcb_reset()
{
	// Nothing: bank state, ACKSEQ and the loaded image all survive a console
	// reset on the cart, whose edge has no reset line.
}

// The -cart file, header and all. MAME has already split it into PRG and
// CHR; the cart parses the header itself, so re-read the file.
void nes_fujinet_device::load_file()
{
	m_file.clear();
	auto *slot = dynamic_cast<device_image_interface *>(owner());
	if (!slot || !slot->exists())
		return;
	FILE *f = fopen(slot->filename(), "rb");
	if (!f)
		return;
	uint8_t buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0)
		m_file.insert(m_file.end(), buf, buf + n);
	fclose(f);
}

void nes_fujinet_device::device_reset()
{
	nesmap_plan_t plan;

	// Once only: later resets (F3) must leave the running mailbox alone, as
	// the Reset button does on the cart.
	if (m_init_done)
		return;
	m_init_done = true;

	fujimail_init(m_debug ? &mame_port : &mame_port_quiet);
	fujitcp_init(nullptr);
	fujimail_paint();

	load_file();
	if (!m_file.empty() && nesmap_plan(m_file.data(), uint32_t(m_file.size()), &plan) == NESMAP_OK)
	{
		fprintf(stderr, "fujinet: %u-byte image, mapper %u, %s%s\n",
				unsigned(m_file.size()), plan.mapper,
				plan.fuji_claim ? "claims the mailbox" : "no claim",
				m_use_loader ? " (via the loader ROM)" : " (direct)");
		if (m_use_loader)
		{
			// as the cart at power-on: the loader asks, then the load begins
			m_resident_base = m_file.data();
			m_resident_plan = plan;
			m_autoload = true;
		}
		else
			direct_boot(m_file.data(), &plan);
	}
	else
	{
		if (!m_file.empty())
			fprintf(stderr, "fujinet: -cart image is not mappable; booting the baked CONFIG\n");
		if (nesmap_plan(_configrom, FUJI_CONFIGROM_SIZE, &plan) == NESMAP_OK)
		{
			m_resident_base = _configrom;
			m_resident_plan = plan;
			m_autoload = true;
		}
		else
			fprintf(stderr, "fujinet: the baked CONFIG is not mappable either; serving nothing\n");
	}
}

uint32_t nes_fujinet_device::cycles() const
{
	return m_cpu ? uint32_t(m_cpu->total_cycles()) : 0;
}

/*-------------------------------------------------
    the mapper's outputs: slot tables, gates, mirroring
-------------------------------------------------*/

void nes_fujinet_device::apply_mirroring()
{
	switch (m_mirror)
	{
	case NESMAP_MIR_V:   set_nt_mirroring(PPU_MIRROR_VERT); break;
	case NESMAP_MIR_H:   set_nt_mirroring(PPU_MIRROR_HORZ); break;
	case NESMAP_MIR_1LO: set_nt_mirroring(PPU_MIRROR_LOW); break;
	case NESMAP_MIR_1HI: set_nt_mirroring(PPU_MIRROR_HIGH); break;
	default:             set_nt_mirroring(PPU_MIRROR_VERT); break;   // four-screen: deferred
	}
}

void nes_fujinet_device::apply_map()
{
	for (int i = 0; i < NESMAP_PRG_SLOTS; i++)
		m_prg_slot[i] = m_map.out.prg[i];
	for (int i = 0; i < NESMAP_CHR_SLOTS; i++)
		m_chr_slot[i] = m_map.out.chr[i];
	m_chr_we = !m_map.out.chr_wp;
	m_serve.wram_en = m_map.out.wram_en;
	m_serve.wram_wp = m_map.out.wram_wp;
	if (m_mirror != m_map.out.mirror)
	{
		m_mirror = m_map.out.mirror;
		apply_mirroring();
	}
	if (m_map.dirty & NESMAP_DIRTY_IRQ)
		set_irq_line(m_map.irq_line ? ASSERT_LINE : CLEAR_LINE);
	m_map.dirty = 0;
}

void nes_fujinet_device::mapper_write(uint16_t addr, uint8_t data)
{
	if (!m_map_live || m_serve.loading)
		return;
	nesmap_write(&m_map, addr, data, cycles());
	if (m_map.dirty)
		apply_map();
}

/*-------------------------------------------------
    loading: the same sequence as fuji_cart.c
-------------------------------------------------*/

void nes_fujinet_device::publish_slice()
{
	const uint8_t *src;
	unsigned dst, off;

	if (m_ls_slice < m_ls_nprg)
	{
		m_prg_slot[0] = uint8_t(m_ls_slice / 8);
		src = m_ls_base + nesmap_prg_offset(&m_ls_plan) + m_ls_slice * 1024;
		dst = FN_LOAD_DST_PRG;
		off = m_ls_slice % 8;
	}
	else
	{
		unsigned k = unsigned(m_ls_slice - m_ls_nprg);
		unsigned w = k / 8;
		if (k == 0)
			m_chr_we = true;
		for (int i = 0; i < NESMAP_CHR_SLOTS; i++)
			m_chr_slot[i] = uint16_t(w * 8 + i);
		src = m_ls_base + nesmap_chr_offset(&m_ls_plan) + k * 1024;
		dst = FN_LOAD_DST_CHR;
		off = k % 8;
	}
	std::memcpy(m_arena + FN_R_DATA, src, FN_R_SLICE_LEN);
	poke(FN_R_LOAD_DST, uint8_t(dst));
	poke(FN_R_LOAD_OFF, uint8_t(off));
	poke(FN_R_LOAD_PCT, uint8_t((m_ls_slice * 100u) / (m_ls_nprg + m_ls_nchr)));
	m_ls_seq = uint8_t(m_ls_seq == 255 ? 1 : m_ls_seq + 1);
	poke(FN_R_LOAD_SEQ, m_ls_seq);
	poke(FN_R_LOAD_STATE, FN_LOAD_SLICE);
	m_ls_ack_pending = true;
}

void nes_fujinet_device::begin_load(const uint8_t *base, const nesmap_plan_t *plan)
{
	m_ls_base = base;
	m_ls_plan = *plan;
	m_ls_nprg = plan->prg_size / 1024;
	m_ls_nchr = plan->chr_size / 1024;
	m_ls_slice = 0;
	m_ls_ack_pending = false;
	m_ls_state = LS_RUN;
	m_have_staged = false;
	m_armed = false;
	m_map_live = false;

	m_serve.loading = true;
	m_serve.mailbox = true;
	m_serve.sram_en = true;
	m_prg_we = true;
	m_chr_we = false;
	set_irq_line(CLEAR_LINE);
	poke(FN_R_SRAM_STATE, 1);
	poke(FN_R_BOOT_STATE, FN_BOOT_IDLE);
	if (m_debug)
		fprintf(stderr, "fujinet: loading %u PRG + %u CHR slices\n", m_ls_nprg, m_ls_nchr);
	publish_slice();
}

void nes_fujinet_device::finish_load()
{
	nesmap_init(&m_map, &m_ls_plan);
	m_map_live = true;
	m_prg_we = false;
	m_serve.loading = false;
	m_mirror = 0xff;                       // force the mirroring apply
	apply_map();
	if (m_ls_plan.fuji_claim && !m_ls_plan.claims_5000)
		fujimail_paint();
	poke(FN_R_MAPPER, uint8_t(m_ls_plan.mapper));
	poke(FN_R_LOAD_STATE, FN_LOAD_DONE);
	m_ls_state = LS_DONE;
	if (m_bootdump)
	{
		char path[512];
		snprintf(path, sizeof path, "%s.prg", m_bootdump);
		FILE *f = fopen(path, "wb");
		if (f) { fwrite(m_prg_sram.data(), 1, m_ls_plan.prg_size, f); fclose(f); }
		snprintf(path, sizeof path, "%s.chr", m_bootdump);
		f = fopen(path, "wb");
		if (f) { fwrite(m_chr_sram.data(), 1, m_ls_plan.chr_size ? m_ls_plan.chr_size : m_ls_plan.chr_ram_size, f); fclose(f); }
	}
	fprintf(stderr, "fujinet: image in place, mapper %u; mailbox %s\n", m_ls_plan.mapper,
			(m_ls_plan.fuji_claim && !m_ls_plan.claims_5000) ? "kept" : "off after the loader leaves");
}

void nes_fujinet_device::slice_acked()
{
	switch (m_ls_state)
	{
	case LS_RUN:
		if (!m_ls_ack_pending)
			return;
		m_ls_ack_pending = false;
		m_ls_slice++;
		if (m_ls_slice < m_ls_nprg + m_ls_nchr)
			publish_slice();
		else
			finish_load();
		break;
	case LS_DONE:
		m_serve.mailbox = m_ls_plan.fuji_claim && !m_ls_plan.claims_5000;
		poke(FN_R_LOAD_STATE, FN_LOAD_IDLE);
		m_ls_state = LS_IDLE;
		break;
	default:
		break;
	}
}

// Put an image straight into the SRAMs, as if the loader had run: for
// iterating on a client without the 0.2 s copy on every launch.
void nes_fujinet_device::direct_boot(const uint8_t *base, const nesmap_plan_t *plan)
{
	std::memcpy(m_prg_sram.data(), base + nesmap_prg_offset(plan), plan->prg_size);
	if (plan->chr_size)
		std::memcpy(m_chr_sram.data(), base + nesmap_chr_offset(plan), plan->chr_size);
	m_ls_plan = *plan;
	nesmap_init(&m_map, plan);
	m_map_live = true;
	m_serve.sram_en = true;
	m_serve.loading = false;
	m_prg_we = false;
	m_mirror = 0xff;
	apply_map();
	m_serve.mailbox = plan->fuji_claim && !plan->claims_5000;
	poke(FN_R_SRAM_STATE, 1);
	poke(FN_R_MAPPER, uint8_t(plan->mapper));
	m_ls_state = LS_IDLE;
}

/*-------------------------------------------------
    the mailbox
-------------------------------------------------*/

void nes_fujinet_device::poke(unsigned offset, uint8_t value)
{
	if (offset < FN_R_PAINT_END)
		m_arena[offset] = value;
}

// One hotspot write, decoded as fujinet.c on the cart does.
void nes_fujinet_device::mailbox_event(uint16_t offset, uint8_t data)
{
	unsigned page = offset & FN_H_PAGE_MASK;
	unsigned low = offset & 0xFF;

	if (page == FN_H_REGSEL || page == FN_H_REGDATA)
	{
		if (low == FN_HOT_SWAP)
		{
			if (m_ls_state != LS_IDLE)
				return;
			if (m_armed && m_have_staged)
				begin_load(m_staged_base, &m_staged_plan);
			else if (m_autoload && m_resident_base)
			{
				m_autoload = false;
				begin_load(m_resident_base, &m_resident_plan);
			}
			return;
		}
		if (low == FN_REG_SLICE_ACK)
		{
			slice_acked();
			return;
		}
		if (low >= 0x80)
			return;
		fujimail_read_hotspot(uint16_t(FN_H_REGSEL + low));
		fujimail_read_hotspot(uint16_t(FN_H_REGDATA + data));
	}
	else if (page == FN_H_DATA)
	{
		fujimail_read_hotspot(uint16_t(FN_H_DATA + data));
	}
}

// Drain events at least two cycles old, dropping an RMW's dummy write (same
// offset, adjacent cycle) exactly as fuji_cart.c does.
void nes_fujinet_device::service()
{
	uint32_t now = cycles();

	while (!m_events.empty())
	{
		nes_event_t e = m_events.front();
		if (int32_t(now - e.cycle) < 2)
			break;
		m_events.pop_front();
		if (!m_events.empty() && m_events.front().offset == e.offset
			&& m_events.front().cycle == e.cycle + 1)
		{
			m_diag_rmw++;
			poke(FN_R_DIAG_RMW, m_diag_rmw);
			continue;
		}
		mailbox_event(e.offset, e.data);
	}
}

/*-------------------------------------------------
    the buses
-------------------------------------------------*/

uint8_t nes_fujinet_device::read_l(offs_t offset)
{
	uint16_t a = uint16_t(0x4100 + offset);

	if (!machine().side_effects_disabled())
		service();
	const uint8_t *p = nes_serve_ptr(&m_serve, nes_region_from_addr(a), a);
	return p ? *p : get_open_bus();
}

void nes_fujinet_device::write_l(offs_t offset, uint8_t data)
{
	uint16_t a = uint16_t(0x4100 + offset);

	if (machine().side_effects_disabled())
		return;
	service();
	switch (nes_write_kind(&m_serve, nes_region_from_addr(a), a))
	{
	case NES_W_MAILBOX:
		m_events.push_back(nes_event_t{ uint16_t(a & (FN_ARENA_SIZE - 1)), data, NES_EV_MAILBOX, cycles() });
		break;
	case NES_W_MAPPER:
		mapper_write(a, data);
		break;
	default:
		break;
	}
}

uint8_t nes_fujinet_device::read_m(offs_t offset)
{
	uint16_t a = uint16_t(0x6000 + offset);
	const uint8_t *p = nes_serve_ptr(&m_serve, NES_R_WRAM, a);
	return p ? *p : get_open_bus();
}

void nes_fujinet_device::write_m(offs_t offset, uint8_t data)
{
	uint16_t a = uint16_t(0x6000 + offset);

	if (machine().side_effects_disabled())
		return;
	if (m_serve.wram_en && !m_serve.wram_wp)
		m_wram[a & (FN_WRAM_SIZE - 1)] = data;
	mapper_write(a, data);                 // NINA-001 keeps registers here
}

uint8_t nes_fujinet_device::read_h(offs_t offset)
{
	uint16_t a = uint16_t(0x8000 + offset);

	if (!m_serve.sram_en)
	{
		if (a >= FN_VECTOR_BASE)
			return m_vectors[a & 0xFF];
		return get_open_bus();
	}
	return m_prg_sram[(uint32_t(m_prg_slot[offset >> 13]) << 13) | (offset & 0x1FFF)];
}

void nes_fujinet_device::write_h(offs_t offset, uint8_t data)
{
	uint16_t a = uint16_t(0x8000 + offset);

	if (machine().side_effects_disabled())
		return;
	if (m_serve.loading)
	{
		if (m_prg_we && m_serve.sram_en)
			m_prg_sram[(uint32_t(m_prg_slot[offset >> 13]) << 13) | (offset & 0x1FFF)] = data;
		return;
	}
	mapper_write(a, data);
}

uint8_t nes_fujinet_device::chr_r(offs_t offset)
{
	return m_chr_sram[(uint32_t(m_chr_slot[(offset >> 10) & 7]) << 10) | (offset & 0x3FF)];
}

void nes_fujinet_device::chr_w(offs_t offset, uint8_t data)
{
	if (m_chr_we)
		m_chr_sram[(uint32_t(m_chr_slot[(offset >> 10) & 7]) << 10) | (offset & 0x3FF)] = data;
}

// MMC3's scanline counter, on MAME's per-line callback: the hardware clocks
// it from filtered PPU A12 rises (nes_irq.c); one per rendered line is the
// same approximation MAME's own TxROM uses.
void nes_fujinet_device::hblank_irq(int scanline, bool vblank, bool blanked)
{
	if (!m_map_live || m_serve.loading || m_map.desc == nullptr || m_map.desc->irq != NESMAP_IRQ_A12)
		return;
	if (vblank || blanked || scanline >= 240)
		return;
	if (nesmap_a12_clock(&m_map))
		set_irq_line(ASSERT_LINE);
}

/*-------------------------------------------------
    push streams
-------------------------------------------------*/

uint8_t nes_fujinet_device::stream_open(int stream, uint32_t size)
{
	uint8_t err = (stream == FN_STREAM_ROM) ? nesmap_gate(size) : 0;

	if (err != 0)
		return err;
	std::vector<uint8_t> &v = m_rx[stream & 1];
	v.clear();
	if (size)
		v.reserve(size);
	return 0;
}

void nes_fujinet_device::stream_write(int stream, const uint8_t *chunk, unsigned len)
{
	m_rx[stream & 1].insert(m_rx[stream & 1].end(), chunk, chunk + len);
}

uint8_t nes_fujinet_device::stream_close(int stream, uint32_t got, bool aborted)
{
	std::vector<uint8_t> &v = m_rx[stream & 1];
	nesmap_plan_t plan;

	(void) got;
	if (stream != FN_STREAM_ROM || aborted)
	{
		v.clear();
		return 0;
	}
	if (v.empty() || nesmap_plan(v.data(), uint32_t(v.size()), &plan) != NESMAP_OK)
	{
		fprintf(stderr, "fujinet: pushed image (%u bytes) is not mappable\n", unsigned(v.size()));
		v.clear();
		return FN_BOOT_ERR_NOMAP;
	}
	// The pushed image stays in m_rx[0] until the load consumes it: the
	// cart's fuji_store keeps it in RAM or flash the same way.
	m_staged_base = v.data();
	m_staged_plan = plan;
	m_have_staged = true;
	return 0;
}

void nes_fujinet_device::arm_swap()
{
	if (m_have_staged)
		m_armed = true;
}

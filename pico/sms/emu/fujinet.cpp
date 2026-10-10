// license:BSD-3-Clause
// copyright-holders:Thomas Cherryhomes
/***********************************************************************************************************

 Sega Master System FujiNet cartridge emulation

 See fujinet.h for the design; pico/sms/README.md in fujinet-firmware for the
 bring-up this device serves.

 ***********************************************************************************************************/

#include "emu.h"
#include "fujinet.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// The cartridge firmware's own sources, compiled in as C++ (apply.sh copies
// them next to this file), so no extern "C".
#include "fuji_mailbox.h"
#include "fujimail.h"
#include "fujitcp.h"
#include "fujiconfigrom.h"
#include "smsloaderrom.h"

DEFINE_DEVICE_TYPE(SEGA8_FUJINET, sega8_fujinet_device, "sega8_fujinet", "SMS FujiNet Cartridge")

// fujimail's port is C function pointers with no context argument; one slot,
// one cart, as on the hardware.
static sega8_fujinet_device *s_fujinet = nullptr;

#define STORE_RAM_MAX  (256u * 1024)
#define STORE_MAX      0x110000u

/*-------------------------------------------------
    C port callbacks
-------------------------------------------------*/

static void c_poke(unsigned offset, uint8_t value) { s_fujinet->poke(offset, value); }
static void c_set_load(bool on) { s_fujinet->set_load(on); }
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

sega8_fujinet_device::sega8_fujinet_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, SEGA8_FUJINET, tag, owner, clock)
	, device_sega8_cart_interface(mconfig, *this)
	, m_mode_out(*this, "fujinet_mode")
{
}

sega8_fujinet_device::~sega8_fujinet_device()
{
	if (s_fujinet == this)
	{
		fujitcp_close();
		s_fujinet = nullptr;
	}
}

void sega8_fujinet_device::device_start()
{
	s_fujinet = this;

	m_debug = getenv("FUJINET_DEBUG") != nullptr;
	m_sramdump = getenv("FUJINET_SRAMDUMP");
	m_cpu = dynamic_cast<cpu_device *>(machine().root_device().subdevice("maincpu"));

	m_sram.assign(SMSMAP_SRAM_SIZE, 0x00);
	m_store[0].reserve(STORE_MAX);
	m_store[1].reserve(STORE_MAX);
	std::memset(m_arena, 0, sizeof m_arena);
	std::memset(m_window, 0, sizeof m_window);
	std::memcpy(m_arena + FN_LOADER, _loaderrom, FUJI_LOADERROM_SIZE);

	m_load_port.poke = c_poke;
	m_load_port.set_load = c_set_load;
	m_loader.port = &m_load_port;
	m_loader.window = m_window;
	m_loader.ptab_resident = m_ptab_res;
	m_loader.resident_map = &m_res_map;
	m_loader.game_map = &m_game_map;
	m_loader.bus = &m_bus;
	fuji_load_init(&m_loader);
	m_live = &m_res_map;

	// No save_item: the protocol state lives in the shared C service's globals.
}

// The -cart file as on disk: the slot has already padded and maybe stripped
// it, and the cart plans from the bytes it was pushed.
void sega8_fujinet_device::load_file()
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

void sega8_fujinet_device::direct_boot(const uint8_t *img, uint32_t len, const smsmap_plan_t *plan)
{
	(void)len;
	std::fill(m_sram.begin(), m_sram.end(), 0);
	std::memcpy(m_sram.data(), img + plan->offset, plan->size);
	if (plan->kind == SMSMAP_JANGGUN)
		for (uint32_t i = 0; i < plan->padded; i++)
			m_sram[SMSMAP_REV_BANK * 0x2000 + i] = bitswap<8>(m_sram[i], 0, 1, 2, 3, 4, 5, 6, 7);
	smsmap_init(&m_game_map, plan);
	m_loader.next = &m_game_map;
	m_loader.next_mode = plan->claim ? FN_MODE_APP : FN_MODE_GAME;
	flip();
	m_direct = true;
}

void sega8_fujinet_device::device_reset()
{
	if (m_init_done)
	{
		// A soft reset is the console's /RESET: back to CONFIG, the BIOS runs
		// again and is snooped again. The mailbox's state survives.
		if (m_direct)
		{
			smsmap_plan_t plan = m_game_map.plan;     // init clears the struct it reads
			sms_bus_reset(&m_bus, m_ptab_res);
			smsmap_init(&m_game_map, &plan);
			m_loader.next = &m_game_map;
			flip();
			return;
		}
		fuji_load_abort(&m_loader);
		to_resident();
		sms_bus_reset(&m_bus, m_ptab_res);
		return;
	}
	m_init_done = true;

	fujimail_init(m_debug ? &mame_port : &mame_port_quiet);
	fujimail_paint();

	for (int p = 0; p < 4; p++)
	{
		m_ptab_res[(FN_ARENA_BASE >> 10) + p] = m_arena + p * 0x400;
		m_ptab_app[(FN_ARENA_BASE >> 10) + p] = m_arena + p * 0x400;
	}

	// The BIOS snoop: its $C000 byte is written with the cart disabled, and
	// I/O never reaches a cart in MAME, so both come off taps.
	if (m_cpu)
	{
		m_tap_c000 = m_cpu->space(AS_PROGRAM).install_write_tap(0xc000, 0xc000, "fujinet_c000",
				[this](offs_t, u8 &data, u8) { if (m_bus.bios_phase) m_bus.c000 = data; }, &m_tap_c000);
		m_tap_iow = m_cpu->space(AS_IO).install_write_tap(0x00, 0xff, "fujinet_iow",
				[this](offs_t offset, u8 &data, u8) { sms_io_write(&m_bus, uint8_t(offset), data); }, &m_tap_iow);
		m_tap_ior = m_cpu->space(AS_IO).install_read_tap(0x00, 0xff, "fujinet_ior",
				[this](offs_t offset, u8 &, u8) { if (!machine().side_effects_disabled()) sms_io_read(&m_bus, uint8_t(offset)); }, &m_tap_ior);
	}

	m_resident.assign(_configrom, _configrom + FUJI_CONFIGROM_SIZE);
	sms_bus_reset(&m_bus, m_ptab_res);
	load_file();
	if (!m_file.empty())
	{
		smsmap_plan_t plan;
		int err = smsmap_plan(m_file.data(), uint32_t(m_file.size()), getenv("FUJINET_MAPPER"), &plan);

		if (err != SMSMAP_OK)
			fprintf(stderr, "fujinet: -cart image refused (%d); serving the baked CONFIG\n", err);
		else if (plan.claim && plan.size <= FN_RESIDENT_MAX && plan.offset == 0)
		{
			m_resident.assign(m_file.begin(), m_file.end());
			fprintf(stderr, "fujinet: %u-byte client, resident\n", unsigned(m_file.size()));
		}
		else
		{
			fprintf(stderr, "fujinet: plan kind=%s crc=%08X size=%u ram=%u claim=%d (direct)\n",
					smsmap_kind_name(plan.kind), plan.crc, plan.size, plan.ram_size, plan.claim);
			for (int p = 0; p < FN_RESIDENT_MAX / 0x400; p++)
				m_ptab_res[p] = nullptr;
			m_resident.resize(FN_RESIDENT_MAX, 0xff);
			for (int p = 0; p < FN_RESIDENT_MAX / 0x400; p++)
				m_ptab_res[p] = m_resident.data() + p * 0x400;
			// A game alone needs no FujiNet: no socket, so A/B runs can go
			// in parallel against fujinet-pc's single BoIP client.
			if (plan.claim)
				fujitcp_init(nullptr);
			direct_boot(m_file.data(), uint32_t(m_file.size()), &plan);
			return;
		}
	}
	fujitcp_init(nullptr);
	m_resident.resize(FN_RESIDENT_MAX, 0xff);
	for (int p = 0; p < FN_RESIDENT_MAX / 0x400; p++)
		m_ptab_res[p] = m_resident.data() + p * 0x400;
	to_resident();
}

/*-------------------------------------------------
    the modes, as core1 switches them
-------------------------------------------------*/

sms_glue_t sega8_fujinet_device::glue() const
{
	sms_glue_t g;
	g.pwr_ok = true;
	g.game = m_game;
	g.mbox = m_mbox;
	g.ram_we = m_ram_we;
	g.load = m_load;
	return g;
}

void sega8_fujinet_device::to_resident()
{
	m_live = &m_res_map;
	m_bus.ptab = m_ptab_res;
	m_bus.mode = FN_MODE_RESIDENT;
	m_game = m_mbox = m_ram_we = false;
	m_mode_out = FN_MODE_RESIDENT;
}

void sega8_fujinet_device::flip()
{
	uint8_t mode = m_loader.next_mode;

	m_live = m_loader.next;
	m_bus.mode = mode;
	m_bus.ptab = mode == FN_MODE_RESIDENT ? m_ptab_res : mode == FN_MODE_APP ? m_ptab_app : m_ptab_game;
	m_game = mode != FN_MODE_RESIDENT;
	m_mbox = mode == FN_MODE_APP;
	m_ram_we = m_game && m_live->ram_we;
	m_mode_out = mode;                     // for the soak harness
	if (m_debug)
		fprintf(stderr, "fujinet: flip to mode %u (%s)\n", mode,
				mode == FN_MODE_RESIDENT ? "CONFIG" : smsmap_kind_name(m_live->plan.kind));
}

// An opcode fetch, as /M1 would say: the Z80 is reading at its own PC.
bool sega8_fujinet_device::fetching(uint16_t a)
{
	return m_cpu && uint16_t(m_cpu->pc()) == a;
}

void sega8_fujinet_device::mapper_write(uint16_t a, uint8_t d)
{
	if (smsmap_write(m_live, a, d))
		m_ram_we = m_live->ram_we;
}

/*-------------------------------------------------
    the bus
-------------------------------------------------*/

uint8_t sega8_fujinet_device::read_cart(offs_t offset)
{
	uint16_t a = uint16_t(offset);

	if (!machine().side_effects_disabled() && a == 0 && fetching(0) && sms_fetch(&m_bus, 0, true))
		flip();

	sms_glue_t g = glue();
	if (sms_glue_oe(g, a, true, true))
		return m_sram[smsmap_sram_offset(m_live, a)];

	const uint8_t *p = sms_serve_ptr(&m_bus, a);
	uint8_t d = p ? *p : 0xff;
	if (!machine().side_effects_disabled() && sms_glue_we(g, a, true, false, true))
		m_sram[smsmap_sram_offset(m_live, a)] = d;
	return d;
}

void sega8_fujinet_device::write_cart(offs_t offset, uint8_t data)
{
	uint16_t a = uint16_t(offset);

	if (machine().side_effects_disabled())
		return;
	if (sms_glue_we(glue(), a, false, true, true))
		m_sram[smsmap_sram_offset(m_live, a)] = data;
	switch (sms_write_kind(&m_bus, a, true))
	{
	case SMS_W_MAPPER:
		mapper_write(a, data);
		break;
	case SMS_W_MAILBOX:
		mailbox_event(uint16_t(a & (FN_ARENA_SIZE - 1)), data);
		break;
	case SMS_W_SWAP:
		to_resident();
		fuji_load_event(&m_loader, SMS_W_SWAP);
		break;
	case SMS_W_CONFIG:
		to_resident();
		fuji_load_event(&m_loader, SMS_W_CONFIG);
		break;
	case SMS_W_GO:
		m_bus.go_armed = true;
		fuji_load_event(&m_loader, SMS_W_GO);
		break;
	default:
		break;
	}
}

void sega8_fujinet_device::write_mapper(offs_t offset, uint8_t data)
{
	uint16_t a = uint16_t(0xfffc + offset);

	if (!machine().side_effects_disabled() && sms_write_kind(&m_bus, a, true) == SMS_W_MAPPER)
		mapper_write(a, data);
}

/*-------------------------------------------------
    the mailbox
-------------------------------------------------*/

void sega8_fujinet_device::dump_sram()
{
	char path[512];
	const smsmap_plan_t &pl = m_loader.plan;

	snprintf(path, sizeof path, "%s.sram", m_sramdump);
	FILE *f = fopen(path, "wb");
	if (!f)
		return;
	fwrite(m_sram.data(), 1, pl.padded, f);
	if (pl.kind == SMSMAP_JANGGUN)
		fwrite(m_sram.data() + SMSMAP_REV_BANK * 0x2000, 1, pl.padded, f);
	fclose(f);
}

void sega8_fujinet_device::poke(unsigned offset, uint8_t value)
{
	if (offset < FN_R_PAINT_END)
		m_arena[offset] = value;
	if (offset == FN_R_LOAD_STATE && value == FN_LOAD_DONE && m_loader.next != &m_res_map)
	{
		fprintf(stderr, "fujinet: loaded kind=%s crc=%08X size=%u ram=%u claim=%d\n",
				smsmap_kind_name(m_loader.plan.kind), m_loader.plan.crc, m_loader.plan.size,
				m_loader.plan.ram_size, m_loader.plan.claim);
		if (m_sramdump)
			dump_sram();
	}
}

// One hotspot write, decoded as fujinet.c does on the cart.
void sega8_fujinet_device::mailbox_event(uint16_t offset, uint8_t data)
{
	unsigned page = offset & FN_H_PAGE_MASK;
	unsigned low = offset & 0xFF;

	if (page == FN_H_REGSEL || page == FN_H_REGDATA)
	{
		if (low == FN_REG_SLICE_ACK)
		{
			fuji_load_ack(&m_loader);
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

/*-------------------------------------------------
    push streams: fuji_store's two tiers
-------------------------------------------------*/

uint8_t sega8_fujinet_device::stream_open(int stream, uint32_t size)
{
	if (stream != FN_STREAM_ROM)
	{
		m_cfg.clear();
		return 0;
	}
	uint8_t err = smsmap_gate(size);
	if (err)
		return err;
	fuji_load_unstage(&m_loader);
	m_open_tier = -1;
	if (size <= STORE_RAM_MAX && !fuji_load_busy(&m_loader, m_store[0].data()))
		m_open_tier = 0;
	else if (size <= STORE_MAX && !fuji_load_busy(&m_loader, m_store[1].data()))
		m_open_tier = 1;
	else
		return size <= STORE_MAX ? FN_BOOT_ERR_STOREBUSY : FN_BOOT_ERR_TOOBIG;
	m_store[m_open_tier].clear();
	return 0;
}

void sega8_fujinet_device::stream_write(int stream, const uint8_t *chunk, unsigned len)
{
	if (stream != FN_STREAM_ROM)
	{
		m_cfg.insert(m_cfg.end(), chunk, chunk + len);
		return;
	}
	if (m_open_tier >= 0 && m_store[m_open_tier].size() + len <= STORE_MAX)
		m_store[m_open_tier].insert(m_store[m_open_tier].end(), chunk, chunk + len);
}

uint8_t sega8_fujinet_device::stream_close(int stream, uint32_t got, bool aborted)
{
	smsmap_plan_t plan;
	int tier = m_open_tier;

	if (stream != FN_STREAM_ROM)
	{
		m_cfg_mapper.clear();
		std::string text(m_cfg.begin(), m_cfg.end());
		size_t at = text.find("mapper=");
		if (!aborted && at != std::string::npos)
		{
			size_t end = text.find_first_of(" \r\n", at + 7);
			m_cfg_mapper = text.substr(at + 7, end == std::string::npos ? std::string::npos : end - at - 7);
		}
		return 0;
	}
	m_open_tier = -1;
	if (aborted || tier < 0 || m_store[tier].empty())
	{
		m_cfg_mapper.clear();
		return 0;
	}
	int err = smsmap_plan(m_store[tier].data(), got, m_cfg_mapper.empty() ? nullptr : m_cfg_mapper.c_str(), &plan);
	m_cfg_mapper.clear();
	if (err == SMSMAP_ETOOBIG)
		return FN_BOOT_ERR_TOOBIG;
	if (err != SMSMAP_OK)
	{
		fprintf(stderr, "fujinet: pushed image (%u bytes) is not mappable (%d)\n", got, err);
		return FN_BOOT_ERR_NOMAP;
	}
	fprintf(stderr, "fujinet: staged kind=%s crc=%08X size=%u ram=%u claim=%d\n",
			smsmap_kind_name(plan.kind), plan.crc, plan.size, plan.ram_size, plan.claim);
	fuji_load_stage(&m_loader, m_store[tier].data(), &plan);
	return 0;
}

void sega8_fujinet_device::arm_swap()
{
	fuji_load_arm(&m_loader);
}

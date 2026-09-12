// license:BSD-3-Clause
// copyright-holders:Thomas Cherryhomes
/***********************************************************************************************************

 Atari 2600 FujiNet cartridge emulation

 See fujinet.h for the design; pico/atari-2600/README-fujinet.md in
 fujinet-firmware for the bring-up this device serves.

 ***********************************************************************************************************/

#include "emu.h"
#include "fujinet.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// The cartridge firmware's own sources, compiled in verbatim (copied next to
// this file by pico/atari-2600/emu/apply.sh -- as C++, to live with MAME's
// forced C++ precompiled header, so no extern "C").
#include "fuji_mailbox.h"
#include "fujimail.h"
#include "vcs_render.h"
#include "fujitcp.h"

DEFINE_DEVICE_TYPE(A26_ROM_FUJINET, a26_rom_fujinet_device, "a26_fujinet", "Atari 2600 FujiNet Cartridge")

// fujimail's port interface is C function pointers with no context argument,
// so the single device instance is reached through this. One cart slot, one
// cart: the constraint is real hardware's too.
static a26_rom_fujinet_device *s_fujinet = nullptr;

/*-------------------------------------------------
    C port callbacks
-------------------------------------------------*/

static void c_poke(unsigned offset, uint8_t value)
{
	s_fujinet->poke(offset, value);
}

static bool c_link_up()
{
	return fujitcp_active();
}

static uint8_t c_stream_open(int stream, uint32_t size)
{
	return s_fujinet->stream_open(stream, size);
}

static void c_stream_write(int stream, const uint8_t *chunk, unsigned len)
{
	s_fujinet->stream_write(stream, chunk, len);
}

static uint8_t c_stream_close(int stream, uint32_t got, bool aborted)
{
	return s_fujinet->stream_close(stream, got, aborted);
}

static void c_arm_swap()
{
	s_fujinet->arm_swap();
}

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
	c_poke,
	c_link_up,
	fujitcp_transact,
	fujitcp_send_bare,
	c_stream_open,
	c_stream_write,
	c_stream_close,
	c_arm_swap,
	nullptr,        // wait_link_ms: the socket round trip is synchronous
	nullptr,        // bootsel: nothing to reboot into under emulation
	c_on_txn,
	c_on_dbc,
};

static const fujimail_port_t mame_port_quiet = {
	c_poke,
	c_link_up,
	fujitcp_transact,
	fujitcp_send_bare,
	c_stream_open,
	c_stream_write,
	c_stream_close,
	c_arm_swap,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
};

/*-------------------------------------------------
    device
-------------------------------------------------*/

a26_rom_fujinet_device::a26_rom_fujinet_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, A26_ROM_FUJINET, tag, owner, clock)
	, device_vcs_cart_interface(mconfig, *this)
{
	s_fujinet = this;
}

a26_rom_fujinet_device::~a26_rom_fujinet_device()
{
	fujitcp_close();
	if (s_fujinet == this)
		s_fujinet = nullptr;
}

// Lazy, because the order of device_start() and the image's call_load() is
// not ours to rely on and install_memory_handlers() -- which call_load calls --
// paints the mailbox. Painting before fujimail_init() would dereference a null
// port. Calling this from both entry points makes the order irrelevant.
void a26_rom_fujinet_device::ensure_init()
{
	if (m_init_done)
		return;
	m_init_done = true;

	m_debug = getenv("FUJINET_DEBUG") != nullptr;
	m_bootdump = getenv("FUJINET_BOOTDUMP");

	fujitcp_init(getenv("FUJINET_TCP"));
	fujimail_init(m_debug ? &mame_port : &mame_port_quiet);
}

void a26_rom_fujinet_device::device_start()
{
	ensure_init();
	save_item(NAME(m_mem.win));
}

void a26_rom_fujinet_device::serve(const uint8_t *img, uint32_t len)
{
	m_image.assign(img, img + len);
	vcs_set_image(&m_mem, m_image.data(), (uint32_t)m_image.size());
	// Painting republishes ACKSEQ as 0 and resets fujimail's own sequence
	// interlock, which is right: a fresh image is a fresh session.
	if (m_mem.mailbox)
		fujimail_paint();
	if (m_debug)
	{
		// "banks" means our layout's banks, which a booted game does not have
		// -- it has whatever board it shipped on. Say which.
		if (m_mem.mailbox)
			fprintf(stderr, "fujinet: serving %u bytes as a client, "
					"%u banks + the fixed half, mailbox live\n",
					len, len / FN_BANK_SIZE - 1);
		else
			fprintf(stderr, "fujinet: serving %u bytes as a game, mapper %s%s, "
					"mailbox DEAD\n", len, vcsmap_name(m_mem.map.kind),
					m_mem.map.superchip ? "SC" : "");
	}
}

void a26_rom_fujinet_device::install_memory_handlers(address_space *space)
{
	// Both directions over the whole window. Unlike every stock VCS cart this
	// device needs the write side: the console -> cart half of the mailbox is
	// real stores, and page $1D/$1E are write-only by construction.
	space->install_read_handler(0x1000, 0x1fff,
			read8sm_delegate(*this, FUNC(a26_rom_fujinet_device::read)));
	space->install_write_handler(0x1000, 0x1fff,
			write8sm_delegate(*this, FUNC(a26_rom_fujinet_device::write)));

	// Two of the boards a pushed game may need switch on addresses BELOW A12,
	// where the cartridge is not selected and is only watching the bus: UA on
	// $0200-$027F, FE on $01FE and the access that follows it. MAME's own
	// a26_rom_ua_device and a26_rom_fe_device install exactly these taps.
	//
	// They are installed unconditionally because the board is not known until
	// a game is pushed, and they are inert for every other one -- vcs_watch
	// tests vcsmap's own watch_low flag. FE's second half, the access after
	// the trigger, arrives through the $1000-$1fff read handler above when it
	// is a fetch, and through the $1ff tap when it is a stack pull.
	auto watch = [this] (offs_t offset, u8 &data, u8)
	{
		if (!machine().side_effects_disabled())
		{
			ensure_init();
			vcs_watch(&m_mem, u16(offset), data);
		}
	};
	space->install_readwrite_tap(0x01fe, 0x01ff, "fujinet_lowwatch", watch, watch);
	space->install_readwrite_tap(0x0200, 0x027f, "fujinet_uabank", watch, watch);

	ensure_init();
	serve(get_rom_base(), get_rom_size());
}

/*-------------------------------------------------
    the bus
-------------------------------------------------*/

uint8_t a26_rom_fujinet_device::read(offs_t offset)
{
	// install_read_handler hands back an offset relative to its start, which
	// for $1000-$1FFF is exactly the window offset.
	//
	// THE GUARD MATTERS ONCE A GAME IS BOOTED. No read in the FujiNet mailbox
	// has a side effect, but every classic 2600 mapper switches banks from the
	// address -- so without this, the MAME debugger (and every harness that
	// dumps the window with readv_u8) would move the bank under the running
	// game and read a mixture of banks.
	return vcs_read_ex(&m_mem, (uint16_t)(FN_WINDOW_BASE + offset),
			!machine().side_effects_disabled());
}

void a26_rom_fujinet_device::write(offs_t offset, uint8_t data)
{
	uint8_t a = 0, b = 0;
	vcs_ev_t ev = vcs_write(&m_mem, (uint16_t)(FN_WINDOW_BASE + offset), data, &a, &b);

	switch (ev)
	{
	case VCS_EV_REG:
		// One completed arm-and-commit is one whole register write. Expand it
		// into the REGSEL/REGDATA pair the shared fujimail.c decodes, so that
		// file stays byte-identical to the sibling ports.
		fujimail_read_hotspot((uint16_t)(FN_H_REGSEL + a));
		fujimail_read_hotspot((uint16_t)(FN_H_REGDATA + b));
		break;

	case VCS_EV_TX:
		fujimail_read_hotspot((uint16_t)(FN_H_DATA + b));
		break;

	case VCS_EV_PATHTX:
	case VCS_EV_PATHRAW:
	{
		// The cartridge holds the working directory because the console has
		// nowhere to put it, and emits it straight into the stream. On the
		// RP2040 this is deferred to core0 -- 256 pushes would miss bus
		// cycles -- but here there is no bus to miss.
		//
		// Padded for a payload that is nothing but the path; raw when the
		// client is going to append a filename and pad it itself.
		unsigned n = (ev == VCS_EV_PATHTX) ? FN_PATH_MAX : m_mem.path_len;
		if (m_debug)
			fprintf(stderr, "fujinet: path -> \"%.*s\" (%u bytes, %s)\n",
					(int)m_mem.path_len, (const char *)m_mem.path,
					m_mem.path_len,
					(ev == VCS_EV_PATHTX) ? "padded to 256" : "raw");
		for (unsigned i = 0; i < n; i++)
			fujimail_read_hotspot((uint16_t)(FN_H_DATA + vcs_path_byte(&m_mem, i)));
		break;
	}

	case VCS_EV_BANK:
		vcs_set_bank(&m_mem, b);
		break;

	case VCS_EV_SWAP:
		do_swap();
		break;

	case VCS_EV_TROW:
	case VCS_EV_TCHR:
		// The composed row lives in vcs_mem_t, maintained by the shared
		// decode, so this device and the cartridge firmware cannot drift on
		// what a half-composed row looks like.
		break;

	case VCS_EV_TEND:
		vcs_render_row(m_mem.win, m_mem.trow, m_mem.tbuf, m_mem.tlen);
		m_mem.win[FN_B_TEXTGEN - FN_WINDOW_BASE] =
				(uint8_t)((m_mem.win[FN_B_TEXTGEN - FN_WINDOW_BASE] ^ 0x80) | (m_mem.trow & 0x1F));
		break;

	case VCS_EV_BLIT:
		if (!vcs_blit(m_mem.win, m_mem.blit_src, m_mem.blit_dst,
					  m_mem.blit_cnt, b) && m_debug)
			fprintf(stderr, "fujinet: blit transform %u not implemented\n", b);
		break;

	case VCS_EV_ARMED:
		m_mem.win[FN_B_FLAGS - FN_WINDOW_BASE] |= 0x01;
		if (m_debug)
			fprintf(stderr, "fujinet: decode armed\n");
		break;

	case VCS_EV_NONE:
	default:
		break;
	}
}

/*-------------------------------------------------
    the fujimail port
-------------------------------------------------*/

void a26_rom_fujinet_device::poke(unsigned offset, uint8_t value)
{
	// fujimail pokes console addresses, straight out of fuji_mailbox.h.
	if (offset >= FN_WINDOW_BASE && offset < FN_WINDOW_BASE + FN_WINDOW_SIZE)
		m_mem.win[offset - FN_WINDOW_BASE] = value;
}

uint8_t a26_rom_fujinet_device::stream_open(int stream, uint32_t size)
{
	if (stream < 0 || stream > 1)
		return FN_BOOT_ERR_NOMAP;

	// A CLIENT is (N+1) * 2048; a GAME is whatever its board was, and the
	// smallest real 2600 cartridge is 2K. Which one this is cannot be known
	// until the claim arrives, so accept any whole number of 2K blocks and let
	// vcs_set_image decide. Refusing here rather than at close is the point:
	// it happens before the ESP32 drags the whole file over TNFS.
	if (stream == FN_STREAM_ROM)
	{
		if (size < FN_BANK_SIZE || (size % FN_BANK_SIZE) != 0)
			return FN_BOOT_ERR_NOMAP;
		if (size > 32u * 1024u)
			return FN_BOOT_ERR_TOOBIG;
	}
	m_rx[stream].clear();
	m_rx[stream].reserve(size);
	return 0;
}

void a26_rom_fujinet_device::stream_write(int stream, const uint8_t *chunk, unsigned len)
{
	if (stream >= 0 && stream <= 1)
		m_rx[stream].insert(m_rx[stream].end(), chunk, chunk + len);
}

uint8_t a26_rom_fujinet_device::stream_close(int stream, uint32_t got, bool aborted)
{
	if (stream == FN_STREAM_CFG)
	{
		// Names the board for an image whose size cannot: an 8K F8, E0, UA and
		// FE are all 8192 bytes, and nothing inside the file tells them apart.
		// Held until the ROM stream is served; vcs_set_image spends it once.
		const std::vector<uint8_t> &c = m_rx[FN_STREAM_CFG];
		vcs_set_cfg(&m_mem, aborted || c.empty() ? nullptr
					: reinterpret_cast<const char *>(c.data()),
					unsigned(c.size()));
		if (m_debug && !aborted && !c.empty())
			fprintf(stderr, "fujinet: .cfg names mapper %s\n",
					vcsmap_name(m_mem.hint));
		m_rx[FN_STREAM_CFG].clear();
		return 0;
	}
	if (stream != FN_STREAM_ROM)
		return 0;
	if (aborted)
		return FN_BOOT_ERR_TRUNCATED;
	if (got < FN_BANK_SIZE || (got % FN_BANK_SIZE) != 0)
		return FN_BOOT_ERR_NOMAP;

	m_staged = m_rx[FN_STREAM_ROM];
	m_have_staged = true;

	if (m_bootdump)
	{
		FILE *f = fopen(m_bootdump, "wb");
		if (f)
		{
			fwrite(m_staged.data(), 1, m_staged.size(), f);
			fclose(f);
		}
	}
	return 0;
}

void a26_rom_fujinet_device::arm_swap()
{
	m_mem.swap_armed = true;
}

void a26_rom_fujinet_device::do_swap()
{
	if (!m_have_staged)
		return;
	// The swap replaces every byte of the window including whatever code
	// triggered it, which is why the client's stub runs from zero-page RAM.
	serve(m_staged.data(), (uint32_t)m_staged.size());
}

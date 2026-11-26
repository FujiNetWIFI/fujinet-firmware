// license:BSD-3-Clause
// copyright-holders:Thomas Cherryhomes
// Atari 2600 FujiNet cartridge emulation; see fujinet.h and fujinet-firmware's
// pico/atari-2600/firmware/.

#include "emu.h"
#include "fujinet.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// The firmware's own sources, grafted in as C++ by apply.sh (MAME forces a C++
// precompiled header), so no extern "C".
#include "fuji_mailbox.h"
#include "fujimail.h"
#include "vcs_render.h"
#include "fujitcp.h"

DEFINE_DEVICE_TYPE(A26_ROM_FUJINET, a26_rom_fujinet_device, "a26_fujinet", "Atari 2600 FujiNet Cartridge")

// fujimail's callbacks take no context, so the one device is reached through this.
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

// Called from device_start() and from call_load()'s install_memory_handlers(),
// whose order is not ours: painting before fujimail_init() has no port.
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
	// A fresh image is a fresh session: painting zeroes ACKSEQ and the interlock.
	if (m_mem.mailbox)
		fujimail_paint();
	if (m_debug)
	{
		// A booted game has its own board, not our layout's banks.
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
	// Unlike a stock VCS cart this needs writes: the console stores to the mailbox.
	space->install_read_handler(0x1000, 0x1fff,
			read8sm_delegate(*this, FUNC(a26_rom_fujinet_device::read)));
	space->install_write_handler(0x1000, 0x1fff,
			write8sm_delegate(*this, FUNC(a26_rom_fujinet_device::write)));

	// UA ($0200-$027F) and FE ($01FE) switch below A12, where the cart is not
	// selected; these are MAME's own a26_rom_ua/fe taps. Installed always, since
	// the board is unknown until a push; vcs_watch() ignores them otherwise.
	// FE's following access arrives as a fetch above or a stack pull at $1FF.
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
	// The offset is already the window offset. Mailbox reads have no side
	// effects, but a booted game's mapper switches banks on reads, so the
	// debugger and readv_u8 dumps must not commit.
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
		// One arm-and-commit is one register write: the REGSEL/REGDATA pair
		// fujimail.c decodes.
		fujimail_read_hotspot((uint16_t)(FN_H_REGSEL + a));
		fujimail_read_hotspot((uint16_t)(FN_H_REGDATA + b));
		break;

	case VCS_EV_TX:
		fujimail_read_hotspot((uint16_t)(FN_H_DATA + b));
		break;

	case VCS_EV_PATHTX:
	case VCS_EV_PATHRAW:
	{
		// The cart holds the working directory (the console has nowhere to) and
		// streams it out; the cart defers this to core0, here it runs inline.
		// Padded when the path is the payload; raw when the client appends a name.
		unsigned sel = m_mem.path_sel;
		unsigned len = m_mem.path_len[sel];
		unsigned n = (ev == VCS_EV_PATHTX) ? FN_PATH_MAX : len;
		if (m_debug)
			fprintf(stderr, "fujinet: path[%u] -> \"%.*s\" (%u bytes, %s)\n",
					sel, (int)len, (const char *)m_mem.path[sel], len,
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
		// The composed row lives in vcs_mem_t, kept by the shared decode.
		break;

	case VCS_EV_TEND:
		vcs_render_row(m_mem.win, m_mem.trow, m_mem.tbuf, m_mem.tlen);
		m_mem.win[FN_B_TEXTGEN - FN_WINDOW_BASE] =
				(uint8_t)((m_mem.win[FN_B_TEXTGEN - FN_WINDOW_BASE] ^ 0x80) | (m_mem.trow & 0x1F));
		break;

	case VCS_EV_BLIT:
		// PATH, PATHPOKE and PATHTILE read the path buffer, not the reply window,
		// so they bypass vcs_blit() as fuji_cart.c does; keep the two in step.
		if (b == FN_BLIT_TCELL)
			vcs_render_cell(m_mem.win,
							(uint8_t)(m_mem.blit_dst / FN_T_COLS),
							(uint8_t)(m_mem.blit_dst % FN_T_COLS),
							(uint8_t)m_mem.blit_src);
		else if (b == FN_BLIT_PATH)
			vcs_render_path_row(m_mem.win, m_mem.path[m_mem.path_sel],
								m_mem.path_len[m_mem.path_sel],
								m_mem.blit_src, (uint8_t)m_mem.blit_dst,
								m_mem.blit_cnt);
		else if (b == FN_BLIT_PATHPOKE)
			vcs_render_path_poke(m_mem.win, m_mem.path[m_mem.path_sel],
								 m_mem.path_len[m_mem.path_sel],
								 m_mem.blit_src, m_mem.blit_dst,
								 m_mem.blit_cnt);
		else if (b == FN_BLIT_PATHTILE)
			vcs_render_path_tile(m_mem.win, m_mem.path[m_mem.path_sel],
								 m_mem.path_len[m_mem.path_sel],
								 m_mem.blit_src, (uint8_t)m_mem.blit_dst,
								 m_mem.blit_cnt);
		else if (!vcs_blit(m_mem.win, m_mem.board, m_mem.blit_src, m_mem.blit_dst,
					  m_mem.blit_cnt, b) && m_debug)
			fprintf(stderr, "fujinet: blit transform %u not implemented\n", b);
		// Synchronous here, deferred on the cart; the generation byte covers both.
		m_mem.win[FN_B_BLITGEN - FN_WINDOW_BASE]++;
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
	if (stream == FN_STREAM_CFG)
	{
		fuji_cfg_open(&m_cfg);
		return 0;
	}
	if (stream != FN_STREAM_ROM)
		return FN_BOOT_ERR_NOMAP;
	uint8_t err = vcs_gate(size);
	if (err)
		return err;
	m_rx.clear();
	m_rx.reserve(size);
	return 0;
}

void a26_rom_fujinet_device::stream_write(int stream, const uint8_t *chunk, unsigned len)
{
	if (stream == FN_STREAM_CFG)
		fuji_cfg_write(&m_cfg, chunk, len);
	else if (stream == FN_STREAM_ROM)
		m_rx.insert(m_rx.end(), chunk, chunk + len);
}

uint8_t a26_rom_fujinet_device::stream_close(int stream, uint32_t got, bool aborted)
{
	if (stream == FN_STREAM_CFG)
	{
		fuji_cfg_close(&m_cfg, aborted);
		return 0;
	}
	if (stream != FN_STREAM_ROM)
		return 0;
	// The .cfg names the board where size cannot (an 8K F8, E0, UA and FE);
	// vcs_set_image() spends it once, at the swap.
	const char *mapper = fuji_cfg_mapper(&m_cfg);
	if (!aborted && vcs_gate(got) == 0)
	{
		vcs_set_cfg(&m_mem, mapper, mapper ? unsigned(strlen(mapper)) : 0u);
		if (m_debug && mapper)
			fprintf(stderr, "fujinet: .cfg names mapper %s\n", vcsmap_name(m_mem.hint));
	}
	fuji_cfg_clear(&m_cfg);
	if (aborted)
		return FN_BOOT_ERR_TRUNCATED;
	if (vcs_gate(got) != 0)
		return FN_BOOT_ERR_NOMAP;

	m_staged = m_rx;
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

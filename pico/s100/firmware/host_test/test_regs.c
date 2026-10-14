// The pipe, TICK, CONTROL, the LOAD stream and the boot page.
#include "check.h"
#include "fake_esp.h"

static s100_card_t c;
static uint32_t now = 1000;

static uint8_t in(uint8_t a)
{
    return s100_in(&c, S100_BLK_PIPE, a, now);
}

static void out(uint8_t a, uint8_t v)
{
    s100_out(&c, S100_BLK_PIPE, a, v, now);
}

static void poll(int n)
{
    for (int i = 0; i < n; i++)
    {
        s100_link_poll(&c, now);
        fe_pump(&c, now);
        now += 10;
    }
}

static uint8_t config_img[300] = {1, 2, 3, 'F', 'N', 'S', '1', 9, 9, 9, 9, 9, 0x77};
static const uint8_t loader_img[] = {0xAA, 0x55};

int main(void)
{
    s100_dip_t dip = {0xC4, 0xFE, S100_CON_8251, true, true};
    s100_image_t imgs[2] = {
        {config_img, sizeof(config_img), 0x0100, 0x0100},
        {loader_img, sizeof(loader_img), 0x4C00, 0x4C00},
    };
    uint8_t tmpl[256] = {0};
    // OUT A3h (CONTROL at the reference base) and JMP FF10h
    tmpl[0] = 0xD3;
    tmpl[1] = S100_REF_PIPE + 3;
    tmpl[2] = 0xC3;
    tmpl[3] = 0x10;
    tmpl[4] = S100_REF_PAGE;
    s100_patch_t patches[] = {{S100_PATCH_PIPE, 1}, {S100_PATCH_PAGE, 4}};
    s100_init(&c, &dip, imgs, 2, tmpl, patches, 2, now);
    s100_link_init(&c, fe_tx, NULL);
    s100_link_set_up(&c, true, now);

    // POJ: JMP FE00h for the first three memory reads, then the page itself.
    CHECK_EQ(s100_mem(&c, 0x0000), 0xC3);
    CHECK_EQ(s100_mem(&c, 0x0001), 0x00);
    CHECK_EQ(s100_mem(&c, 0x0002), 0xFE);
    CHECK_EQ(s100_mem(&c, 0xFE01), 0xC7); // patched pipe base + 3
    CHECK_EQ(s100_mem(&c, 0xFE03), 0x10);
    CHECK_EQ(s100_mem(&c, 0xFE04), 0xFE); // patched page
    CHECK(c.rom_on);

    // STATUS: signature, nothing waiting.
    CHECK_EQ(in(S100_PIPE_STATUS) & S100_ST_SIGMSK, S100_ST_SIG);
    CHECK_EQ(in(S100_PIPE_STATUS) & S100_ST_AVAIL, 0);
    CHECK_EQ(in(S100_PIPE_STATUS) & 0x40, 0);

    // TICK counts milliseconds and wraps.
    uint8_t t0 = in(S100_PIPE_TICK);
    now += 5000;
    CHECK_EQ((uint8_t)(in(S100_PIPE_TICK) - t0), 5);
    now += 256000;
    CHECK_EQ((uint8_t)(in(S100_PIPE_TICK) - t0), 5);

    // LOAD 0: header then the image, through GETC.
    out(S100_PIPE_CONTROL, S100_CTL_LOAD | S100_IMG_CONFIG);
    poll(4);
    uint8_t hdr[S100_LOAD_HDR];
    for (int i = 0; i < S100_LOAD_HDR; i++)
    {
        CHECK(in(S100_PIPE_STATUS) & S100_ST_AVAIL);
        hdr[i] = in(S100_PIPE_GETC);
    }
    CHECK_EQ(hdr[0] | hdr[1] << 8, sizeof(config_img));
    CHECK_EQ(hdr[2] | hdr[3] << 8, 0x0100);
    CHECK_EQ(hdr[4] | hdr[5] << 8, 0x0100);
    CHECK_EQ(hdr[6], 0xC4);
    CHECK_EQ(hdr[7], 0xFE);
    CHECK_EQ(hdr[8], S100_CON_8251);
    CHECK_EQ(hdr[9], S100_LOADF_PHANTOM);
    // The card info after "FNS1" arrives filled in; everything else as stored.
    uint8_t want[sizeof(config_img)];
    memcpy(want, config_img, sizeof(want));
    want[7] = 0xC4;
    want[8] = 0xFE;
    want[9] = S100_CON_8251;
    want[10] = S100_INFOF_PHANTOM;
    want[11] = 0; // drive 0 not probed
    int got = 0;
    for (int i = 0; i < (int)sizeof(config_img); i++)
    {
        if (!(in(S100_PIPE_STATUS) & S100_ST_AVAIL))
            poll(1);
        got += in(S100_PIPE_GETC) == want[i];
    }
    CHECK_EQ(got, (int)sizeof(config_img));
    CHECK_EQ(config_img[7], 9); // the image itself is untouched
    CHECK_EQ(in(S100_PIPE_STATUS) & S100_ST_AVAIL, 0);
    // Empty GETC repeats the last byte.
    CHECK_EQ(in(S100_PIPE_GETC), config_img[sizeof(config_img) - 1]);

    // LOAD of a missing image: a header that says so.
    out(S100_PIPE_CONTROL, S100_CTL_LOAD | 5);
    poll(2);
    for (int i = 0; i < S100_LOAD_HDR; i++)
        hdr[i] = in(S100_PIPE_GETC);
    CHECK_EQ(hdr[0] | hdr[1] << 8, 0);
    CHECK(hdr[9] & S100_LOADF_NOIMAGE);

    // A LOAD cut short by FLUSH leaves nothing behind.
    out(S100_PIPE_CONTROL, S100_CTL_LOAD | S100_IMG_CONFIG);
    poll(1);
    in(S100_PIPE_GETC);
    out(S100_PIPE_CONTROL, S100_CTL_FLUSH);
    poll(3);
    CHECK_EQ(in(S100_PIPE_STATUS) & S100_ST_AVAIL, 0);

    // A FujiBus round trip through PUTC/GETC.
    uint8_t req[64];
    size_t n = fujibus_build_request(0x70, 0xE8, NULL, 0, NULL, 0, req, sizeof(req));
    for (size_t i = 0; i < n; i++)
        out(S100_PIPE_PUTC, req[i]);
    poll(3);
    CHECK_EQ(fe.fuji, 1);
    uint8_t rep[64];
    int rn = 0;
    while (in(S100_PIPE_STATUS) & S100_ST_AVAIL)
        rep[rn++] = in(S100_PIPE_GETC);
    fb_reply_t r;
    CHECK(fujibus_parse_reply(rep, rn, &r));
    CHECK_EQ(r.device, 0x70);
    CHECK_EQ(r.command, 0x06);

    // ROM_OFF, then RESET* puts the page back and re-arms POJ.
    out(S100_PIPE_CONTROL, S100_CTL_ROMOFF);
    CHECK(!c.rom_on);
    s100_reset(&c, now);
    CHECK(c.rom_on);
    CHECK_EQ(s100_mem(&c, 0x1234), 0xC3);

    // Ports that don't read: PUTC and CONTROL addresses.
    CHECK_EQ(in(S100_PIPE_PUTC), 0xFF);

    return check_done("test_regs");
}

/* s100_fujinet.c -- SIMH AltairZ80 device for the FujiNet S-100 card.
 *
 * Wraps the card core from pico/s100/firmware (the same C the RP2354B runs) and
 * stands a TCP connection to fujinet-pc's BoIP port in for the USB link. Installed
 * by pico/s100/emu/apply.sh.
 *
 *   SET FUJINET IOBASE=A0 SIOBASE=14 PAGE=FF CONSOLE=2SIO|SIO|8251
 *   SET FUJINET SYNC|ASYNC, FDC|NOFDC, SIO|NOSIO, PHANTOM|NOPHANTOM, MHZ=n
 *   SET FUJINET IMAGE0=file[;load]  (CONFIG, default 0100h), IMAGE1 (loader, 4C00h);
 *       without them, the images build.sh baked
 *   ATTACH FUJINET 127.0.0.1:1995
 *   BOOT FUJINET  (runs the boot page)
 *
 * SYNC stalls the emulated CPU, in wall-clock time, while the FDC waits for a
 * sector or the host waits for a reply, so an unthrottled CPU sees the link as
 * instant. ASYNC never stalls; use it with DEPOSIT CPU CLOCK to see real latency.
 */
#include "altairz80_defs.h"
#include "sim_sock.h"

#include "s100_regs.h"

#if defined(__has_include)
#if __has_include("s100_bootpage.h")
#include "s100_bootpage.h"
#define HAVE_BOOTPAGE 1
#endif
#if __has_include("s100_images.h")
#include "s100_images.h"
#define HAVE_IMAGES 1
#endif
#endif

extern uint32 sim_map_resource(uint32 baseaddr, uint32 size, uint32 resource_type,
                               int32 (*routine)(const int32, const int32, const int32),
                               const char *name, uint8 unmap);
extern uint32 getClockFrequency(void);
extern uint32 PCX;
extern t_uint64 fujinet_tstates(void);

#define FN_NAME "FujiNet S-100 card"
#define FN_SYNC_FDC_MS  5000
#define FN_SYNC_HOST_MS 35000

static s100_card_t card;
static int fn_inited;
static SOCKET fn_sock = INVALID_SOCKET;
static int fn_up;
static int fn_sync = 1;
static int fn_fdc_en = 1;
static int fn_sio_en = 1;
static uint8 fn_sio_base = 0x14;
static s100_dip_t fn_dip = {S100_DEF_PIPE_BASE, S100_DEF_PAGE, S100_CON_2SIO, false, false};
static s100_image_t fn_images[S100_IMG_MAX];
static int fn_images_owned[S100_IMG_MAX];
static uint8 fn_mapped_page = 0;
static int fn_page_mapped;
static int fn_settings_changed = 1;
static uint32 fn_mhz = 2; /* for time when the CPU clock is unthrottled */

static t_stat fn_reset(DEVICE *dptr);
static t_stat fn_svc(UNIT *uptr);
static t_stat fn_attach(UNIT *uptr, CONST char *cptr);
static t_stat fn_detach(UNIT *uptr);
static t_stat fn_boot(int32 unitno, DEVICE *dptr);
static const char *fn_description(DEVICE *dptr);

static UNIT fn_unit = {UDATA(&fn_svc, UNIT_ATTABLE | UNIT_IDLE, 0), 10000};

static uint32 fn_now(void)
{
    uint32 khz = getClockFrequency();
    if (khz == 0)
        khz = fn_mhz * 1000;
    return (uint32)(fujinet_tstates() * 1000 / khz);
}

/* ---- the link: TCP to fujinet-pc ---- */

static void fn_trace_io(const char *dir, uint8 a, uint8 v);

static void fn_close(void)
{
    fn_trace_io("CLOSE", 0, (uint8)fn_up);
    if (fn_sock != INVALID_SOCKET)
        sim_close_sock(fn_sock);
    fn_sock = INVALID_SOCKET;
    if (fn_up)
        s100_link_set_up(&card, false, fn_now());
    fn_up = 0;
}

static size_t fn_tx(void *ctx, const uint8_t *buf, size_t n)
{
    size_t off = 0;
    (void)ctx;
    while (off < n && fn_sock != INVALID_SOCKET)
    {
        int32 r = sim_write_sock(fn_sock, (const char *)buf + off, (int32)(n - off));
        if (r < 0)
        {
            fn_trace_io("WERR", 0, 0);
            fn_close();
            return 0;
        }
        if (r == 0)
            sim_os_ms_sleep(1);
        off += (size_t)r;
    }
    return off;
}

static void fn_net(void)
{
    uint8_t buf[4096];
    uint32 now = fn_now();

    if (fn_sock == INVALID_SOCKET && (fn_unit.flags & UNIT_ATT))
        fn_sock = sim_connect_sock_ex(NULL, fn_unit.filename, "127.0.0.1", "1995",
                                      SIM_SOCK_OPT_NODELAY);
    if (fn_sock != INVALID_SOCKET && !fn_up)
    {
        int c = sim_check_conn(fn_sock, FALSE);
        if (c < 0)
            fn_close();
        else if (c > 0)
        {
            fn_trace_io("UP", 0, 1);
            fn_up = 1;
            s100_link_set_up(&card, true, now);
        }
    }
    if (fn_up)
    {
        int32 r;
        while ((r = sim_read_sock(fn_sock, (char *)buf, sizeof(buf))) > 0)
            s100_link_rx(&card, buf, (size_t)r, now);
        if (r < 0)
        {
            fn_trace_io("RERR", 0, 0);
            fn_close();
        }
    }
    s100_link_poll(&card, now);
}

/* SYNC mode: spin the link in wall-clock time until `done` or `ms` pass. */
static void fn_wait(int (*done)(void), uint32 ms)
{
    uint32 start = sim_os_msec();
    fn_net();
    while (!done() && fn_up && sim_os_msec() - start < ms)
    {
        sim_os_ms_sleep(1);
        fn_net();
    }
}

static int fdc_fed(void)
{
    return !fdc_starved(&card.fdc);
}

static int mount_known(void)
{
    return !fdc_mount_pending(&card.fdc);
}

static int host_fed(void)
{
    return ring8_count(&card.to_host) || !s100_link_host_busy(&card);
}

/* ---- bus handlers ---- */

static void fn_map_page(int on);

static int32 fn_pipe(const int32 port, const int32 io, const int32 data)
{
    uint8 a = (uint8)(port & 3);
    fn_net();
    if (io == 0)
    {
        if (fn_sync && (a == S100_PIPE_GETC || a == S100_PIPE_STATUS))
            fn_wait(host_fed, FN_SYNC_HOST_MS);
        return s100_in(&card, S100_BLK_PIPE, a, fn_now());
    }
    s100_out(&card, S100_BLK_PIPE, a, (uint8)data, fn_now());
    if (!card.rom_on && fn_page_mapped)
        fn_map_page(0);
    fn_net();
    return 0;
}

static void fn_trace_io(const char *dir, uint8 a, uint8 v);

static int32 fn_sio(const int32 port, const int32 io, const int32 data)
{
    uint8 v;
    fn_net();
    if (io == 0)
    {
        v = s100_in(&card, S100_BLK_SIO, (uint8)(port & 3), fn_now());
        if (port & 1)
            fn_trace_io("SIN", (uint8)(port & 3), v);
        return v;
    }
    fn_trace_io("SOUT", (uint8)(port & 3), (uint8)data);
    s100_out(&card, S100_BLK_SIO, (uint8)(port & 3), (uint8)data, fn_now());
    return 0;
}

/* FUJINET_TRACE=1 in the environment logs FDC and 2SIO accesses to stderr. */
static int fn_trace = -1;

static void fn_trace_io(const char *dir, uint8 a, uint8 v)
{
    if (fn_trace < 0)
        fn_trace = getenv("FUJINET_TRACE") != NULL;
    if (fn_trace)
        fprintf(stderr, "%10u %04X %s %02X %02X\n", fn_now(), PCX, dir, 8 + a, v);
}

static int32 fn_fdc(const int32 port, const int32 io, const int32 data)
{
    uint8 a = (uint8)(port & 3);
    uint8 v;
    fn_net();
    if (io == 0)
    {
        if (fn_sync && a == 1)
            fn_wait(fdc_fed, FN_SYNC_FDC_MS);
        else if (fn_sync && a == 0)
            fn_wait(mount_known, FN_SYNC_FDC_MS);
        v = s100_in(&card, S100_BLK_FDC, a, fn_now());
        if (a != 2)
            fn_trace_io("IN ", a, v);
        return v;
    }
    fn_trace_io("OUT", a, (uint8)data);
    s100_out(&card, S100_BLK_FDC, a, (uint8)data, fn_now());
    return 0;
}

static int32 fn_page(const int32 addr, const int32 rw, const int32 data)
{
    (void)data;
    if (rw == 0)
        return s100_mem(&card, (uint16_t)addr);
    return 0; /* read-only; on the bus PHANTOM* lets writes through to RAM */
}

static void fn_map_page(int on)
{
    if (fn_page_mapped)
        sim_map_resource((uint32)fn_mapped_page << 8, 256, RESOURCE_TYPE_MEMORY, &fn_page,
                         "fujinet page", TRUE);
    fn_page_mapped = 0;
    if (on)
    {
        fn_mapped_page = fn_dip.page;
        sim_map_resource((uint32)fn_mapped_page << 8, 256, RESOURCE_TYPE_MEMORY, &fn_page,
                         "fujinet page", FALSE);
        fn_page_mapped = 1;
    }
}

static void fn_map_io(int on)
{
    uint8 unmap = on ? FALSE : TRUE;
    sim_map_resource(fn_dip.pipe_base & 0xFC, 4, RESOURCE_TYPE_IO, &fn_pipe, "fujinet pipe", unmap);
    if (fn_sio_en || !on)
        sim_map_resource(fn_sio_base & 0xFC, 4, RESOURCE_TYPE_IO, &fn_sio, "fujinet 2sio", unmap);
    if (fn_fdc_en || !on)
        sim_map_resource(S100_FDC_BASE, 4, RESOURCE_TYPE_IO, &fn_fdc, "fujinet fdc", unmap);
}

static void fn_trace_link(const char *ev, uint32_t a, uint32_t b)
{
    fprintf(stderr, "%10u %04X LINK %s %X %X\n", fn_now(), PCX, ev, a, b);
}

/* ---- device ---- */

static void fn_init(void)
{
#ifdef HAVE_IMAGES
    for (int i = 0; i < S100_IMAGE_COUNT; i++)
        if (fn_images[i].data == NULL)
            fn_images[i] = s100_images[i];
#endif
#ifdef HAVE_BOOTPAGE
    s100_init(&card, &fn_dip, fn_images, S100_IMG_MAX, s100_boot_page, s100_boot_patches,
              S100_BOOT_PATCH_COUNT, fn_now());
#else
    static const uint8_t blank[256];
    s100_init(&card, &fn_dip, fn_images, S100_IMG_MAX, blank, NULL, 0, fn_now());
#endif
    s100_link_init(&card, fn_tx, NULL);
    if (getenv("FUJINET_TRACE"))
        card.link.trace = fn_trace_link;
    if (fn_up)
        s100_link_set_up(&card, true, fn_now());
    fn_inited = 1;
    fn_settings_changed = 0;
}

static t_stat fn_reset(DEVICE *dptr)
{
    fn_map_io(0);
    fn_map_page(0);
    if (dptr->flags & DEV_DIS)
    {
        sim_cancel(&fn_unit);
        return SCPE_OK;
    }
    if (!fn_inited || fn_settings_changed)
        fn_init();
    s100_reset(&card, fn_now());
    fn_map_io(1);
    fn_map_page(1);
    sim_activate(&fn_unit, fn_unit.wait);
    return SCPE_OK;
}

static t_stat fn_svc(UNIT *uptr)
{
    fn_net();
    sim_activate(uptr, uptr->wait);
    return SCPE_OK;
}

/* ATTACH takes host:port; attach_unit() would want a file. */
static t_stat fn_attach(UNIT *uptr, CONST char *cptr)
{
    if (uptr->flags & UNIT_ATT)
        fn_detach(uptr);
    if (sim_parse_addr(cptr, NULL, 0, "127.0.0.1", NULL, 0, "1995", NULL))
        return SCPE_ARG;
    uptr->filename = (char *)calloc(CBUFSIZE, sizeof(char));
    if (uptr->filename == NULL)
        return SCPE_MEM;
    strncpy(uptr->filename, cptr, CBUFSIZE - 1);
    uptr->flags |= UNIT_ATT;
    fn_close();
    fn_net();
    return SCPE_OK;
}

static t_stat fn_detach(UNIT *uptr)
{
    fn_close();
    if (uptr->flags & UNIT_ATT)
    {
        free(uptr->filename);
        uptr->filename = NULL;
        uptr->flags &= ~UNIT_ATT;
    }
    return SCPE_OK;
}

static t_stat fn_boot(int32 unitno, DEVICE *dptr)
{
    (void)unitno;
    (void)dptr;
    *((int32 *)sim_PC->loc) = (int32)fn_dip.page << 8;
    return SCPE_OK;
}

/* ---- SET/SHOW ---- */

static t_stat fn_set_hex(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    t_stat r;
    uint32 v;
    (void)uptr;
    if (cptr == NULL)
        return SCPE_ARG;
    v = (uint32)get_uint(cptr, val == 3 ? 10 : 16, 0xFF, &r);
    if (r != SCPE_OK)
        return r;
    if (val == 0)
        fn_dip.pipe_base = (uint8)(v & 0xFC);
    else if (val == 1)
        fn_sio_base = (uint8)(v & 0xFC);
    else if (val == 2)
        fn_dip.page = (uint8)v;
    else
        fn_mhz = v ? v : 2;
    (void)desc;
    fn_settings_changed = 1;
    return SCPE_OK;
}

static t_stat fn_show_hex(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    (void)uptr;
    (void)desc;
    fprintf(st, val == 0 ? "IOBASE=%02X" : val == 1 ? "SIOBASE=%02X" : "PAGE=%02X",
            val == 0 ? fn_dip.pipe_base : val == 1 ? fn_sio_base : fn_dip.page);
    return SCPE_OK;
}

static t_stat fn_set_flag(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    (void)uptr;
    (void)cptr;
    (void)desc;
    switch (val)
    {
    case 0: fn_sync = 1; break;
    case 1: fn_sync = 0; break;
    case 2: fn_fdc_en = 1; break;
    case 3: fn_fdc_en = 0; break;
    case 4: fn_sio_en = 1; break;
    case 5: fn_sio_en = 0; break;
    case 6: fn_dip.phantom = true; break;
    case 7: fn_dip.phantom = false; break;
    default: break;
    }
    fn_settings_changed = 1;
    return SCPE_OK;
}

static t_stat fn_set_console(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    (void)uptr;
    (void)val;
    (void)desc;
    if (cptr == NULL)
        return SCPE_ARG;
    if (!strcasecmp(cptr, "2SIO"))
        fn_dip.console = S100_CON_2SIO;
    else if (!strcasecmp(cptr, "SIO"))
        fn_dip.console = S100_CON_SIO;
    else if (!strcasecmp(cptr, "8251"))
        fn_dip.console = S100_CON_8251;
    else
        return SCPE_ARG;
    fn_settings_changed = 1;
    return SCPE_OK;
}

/* IMAGEn=file[;load], load in hex */
static t_stat fn_set_image(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    char path[CBUFSIZE];
    const char *semi;
    FILE *f;
    long len;
    uint8_t *data;
    uint32 load = val == 0 ? 0x0100 : 0x4C00;
    (void)uptr;
    (void)desc;
    if (cptr == NULL || val < 0 || val >= S100_IMG_MAX)
        return SCPE_ARG;
    semi = strchr(cptr, ';');
    strncpy(path, cptr, sizeof(path) - 1);
    path[sizeof(path) - 1] = 0;
    if (semi)
    {
        path[semi - cptr] = 0;
        load = (uint32)strtoul(semi + 1, NULL, 16);
    }
    f = fopen(path, "rb");
    if (f == NULL)
        return SCPE_OPENERR;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    rewind(f);
    if (len <= 0 || len > 0xF000)
    {
        fclose(f);
        return SCPE_ARG;
    }
    data = (uint8_t *)malloc((size_t)len);
    if (data == NULL || fread(data, 1, (size_t)len, f) != (size_t)len)
    {
        free(data);
        fclose(f);
        return SCPE_IOERR;
    }
    fclose(f);
    if (fn_images_owned[val])
        free((void *)fn_images[val].data);
    fn_images_owned[val] = 1;
    fn_images[val].data = data;
    fn_images[val].len = (uint16_t)len;
    fn_images[val].load = (uint16_t)load;
    fn_images[val].entry = (uint16_t)load;
    return SCPE_OK;
}

static MTAB fn_mod[] = {
    {MTAB_XTD | MTAB_VDV | MTAB_VALR, 0, "IOBASE", "IOBASE", &fn_set_hex, &fn_show_hex, NULL,
     "Pipe base port (4 ports)"},
    {MTAB_XTD | MTAB_VDV | MTAB_VALR, 1, "SIOBASE", "SIOBASE", &fn_set_hex, &fn_show_hex, NULL,
     "2SIO base port (4 ports)"},
    {MTAB_XTD | MTAB_VDV | MTAB_VALR, 2, "PAGE", "PAGE", &fn_set_hex, &fn_show_hex, NULL,
     "Boot page, high address byte"},
    {MTAB_XTD | MTAB_VDV | MTAB_VALR, 3, NULL, "MHZ", &fn_set_hex, NULL, NULL,
     "CPU MHz the card's clock assumes when the simulator runs unthrottled"},
    {MTAB_XTD | MTAB_VDV | MTAB_VALR, 0, NULL, "CONSOLE", &fn_set_console, NULL, NULL,
     "Console type CONFIG drives: 2SIO, SIO or 8251"},
    {MTAB_XTD | MTAB_VDV | MTAB_VALR | MTAB_NMO | MTAB_NC, 0, NULL, "IMAGE0", &fn_set_image, NULL, NULL,
     "CONFIG image: file[;load]"},
    {MTAB_XTD | MTAB_VDV | MTAB_VALR | MTAB_NMO | MTAB_NC, 1, NULL, "IMAGE1", &fn_set_image, NULL, NULL,
     "Boot loader image: file[;load]"},
    {MTAB_XTD | MTAB_VDV, 0, NULL, "SYNC", &fn_set_flag, NULL, NULL, "Stall the CPU for the link"},
    {MTAB_XTD | MTAB_VDV, 1, NULL, "ASYNC", &fn_set_flag, NULL, NULL, "Never stall the CPU"},
    {MTAB_XTD | MTAB_VDV, 2, NULL, "FDC", &fn_set_flag, NULL, NULL, "FDC+ at 08h-0Bh"},
    {MTAB_XTD | MTAB_VDV, 3, NULL, "NOFDC", &fn_set_flag, NULL, NULL, "No FDC+"},
    {MTAB_XTD | MTAB_VDV, 4, NULL, "SIO", &fn_set_flag, NULL, NULL, "2SIO emulation on"},
    {MTAB_XTD | MTAB_VDV, 5, NULL, "NOSIO", &fn_set_flag, NULL, NULL, "2SIO emulation off"},
    {MTAB_XTD | MTAB_VDV, 6, NULL, "PHANTOM", &fn_set_flag, NULL, NULL, "The page uses PHANTOM*"},
    {MTAB_XTD | MTAB_VDV, 7, NULL, "NOPHANTOM", &fn_set_flag, NULL, NULL, "No PHANTOM*"},
    {0}};

static const char *fn_description(DEVICE *dptr)
{
    (void)dptr;
    return FN_NAME;
}

DEVICE fujinet_dev = {
    "FUJINET", &fn_unit, NULL, fn_mod, 1, 16, 16, 1, 16, 8,
    NULL, NULL, &fn_reset, &fn_boot, &fn_attach, &fn_detach,
    NULL, DEV_DISABLE | DEV_DIS, 0, NULL, NULL, NULL, NULL, NULL, NULL, &fn_description};

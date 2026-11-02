/* fujicfg.c -- milestone 3: a stand-in CONFIG for bring-up, driven headlessly by
 * emu/drive.lua; the real one is fujinet-config's coleco/. Pick and rename hosts, browse a
 * host's root and boot what you land on. Filenames are never copied into console RAM:
 * they are drawn from the reply window and streamed back with fn_tx_path.
 */

#include <os7.h>

#include "fujidisp.h"
#include "fujisnd.h"
#include "fujiin.h"
#include "fujisplash.h"
#include "fujiedit.h"
#include "fujilib.h"

#define CMD_READ_HOST_SLOTS         0xF4
#define CMD_WRITE_HOST_SLOTS        0xF3
#define CMD_MOUNT_HOST              0xF9
#define CMD_OPEN_DIRECTORY          0xF7
#define CMD_READ_DIR_ENTRY          0xF6
#define CMD_CLOSE_DIRECTORY         0xF5
#define CMD_SET_DIRECTORY_POSITION  0xE4
#define CMD_SET_DEVICE_FULLPATH     0xE2
#define CMD_MOUNT_IMAGE             0xF8
#define CMD_ADAPTERCONFIG_EXT       0xC4
#define CMD_SCAN_NETWORKS           0xFD
#define CMD_GET_SCAN_RESULT         0xFC
#define CMD_SET_SSID                0xFB

#define HOST_SLOTS  8
#define HOST_STRIDE 32          /* READ_HOST_SLOTS: 8 x 32 bytes */

#define LIST_TOP    5           /* first list row on screen */
#define LIST_ROWS   16
#define NAMELEN     29          /* display width: col 2..30 */
#define FULLLEN     120         /* re-read width when building a boot path */
#define PATH_LEN    256         /* the fixed buffer the ESP32 expects */

#define DEVSLOT     0
#define MODE_READ   1

#define PAGE_HOSTS  0
#define PAGE_FILES  1
#define PAGE_CONFIG 2

/* AdapterConfigExtended field offsets, from lib/device/fujiDevice/fujiDevice.h */
#define ACX_SSID      0
#define ACX_VERSION   125
#define ACX_SLOCALIP  140
#define ACX_SMAC      204

/* SSIDInfo: char ssid[33]; uint8_t rssi -- 34 bytes, one per GET_SCAN_RESULT */
#define SSID_LEN      33
#define SSID_STRIDE   34

#define CFG_TOP     11          /* first network row */
#define CFG_ROWS    8
#define PASS_MAX    63

static unsigned char page;
static unsigned char host;      /* selected host slot */
static unsigned char cur;       /* cursor row within the visible window */
static unsigned int  top;       /* index of the first visible entry */
static unsigned char nrows;     /* rows actually filled */
static unsigned char at_end;    /* the last row on screen is the last entry */

static void status(const char *msg)
{
    disp_row_clear(22);
    disp_at(1, 22, msg);
}

static void oops(const char *what, unsigned char code)
{
    disp_row_clear(22);
    disp_at(1, 22, what);
    disp_at_hex8((unsigned char)(28), 22, code);
}

/* Commit and insist on an ACK. Returns 0 on success; on failure it has already
 * said so on the status line, so callers just bail out to the redraw. */
static unsigned char need_ack(const char *what)
{
    unsigned char err = fn_commit();

    if (err != FN_OK) {
        oops(what, err);
        return err;
    }
    if (!fn_acked()) {
        oops(what, FN_NAK);
        return FN_NAK;
    }
    return 0;
}

static void draw_frame(void)
{
    disp_cls();
    disp_at(1, 1, "FUJINET   C O N F I G");
}

/* The selection bar. Always applied AFTER a row's text is drawn, because it
 * works by re-pointing the characters already in the name table. */
static void draw_cursor(unsigned char row, bool on)
{
    unsigned char top = (page == PAGE_CONFIG) ? CFG_TOP : LIST_TOP;

    disp_row_invert((unsigned char)(top + row), on);
}

/* ---- host page ---- */

static void host_page(void)
{
    unsigned char idx;

    draw_frame();
    disp_at(1, 3, "SELECT A HOST");
    status("FIRE OPEN  9 RENAME  # CONFIG");

    fn_start(FN_DEV_FUJINET, CMD_READ_HOST_SLOTS);
    if (need_ack("EHOSTS") != 0) {
        nrows = 0;
        return;
    }

    nrows = 0;
    for (idx = 0; idx < HOST_SLOTS; idx++) {
        volatile unsigned char *name = FN_REPLY + (unsigned int)idx * HOST_STRIDE;
        unsigned char row = (unsigned char)(LIST_TOP + idx);
        unsigned char col;

        disp_row_clear(row);
        disp_at_u16(1, row, (unsigned int)(idx + 1));
        if (*name == 0) {
            disp_at(3, row, "(empty)");
            continue;
        }
        /* Straight out of the reply window, a character at a time -- there is
         * nowhere in RAM to put it and no reason to. */
        for (col = 0; col < HOST_STRIDE && name[col] != 0; col++)
            disp_char((unsigned char)(3 + col), row, (char)name[col]);
        nrows = (unsigned char)(idx + 1);
    }
    if (nrows == 0)
        nrows = HOST_SLOTS;     /* still navigable, just all empty */
    at_end = 1;
    if (cur >= nrows)
        cur = 0;
    draw_cursor(cur, true);
}

/* ---- configuration page ---- */

/* Draw a label and a NUL-terminated field out of the reply window. Always via
 * a local pointer -- sccz80 mis-indexes the FN_REPLY macro's cast constant. */
static void draw_field(unsigned char row, const char *label,
                       unsigned int off, unsigned int max)
{
    volatile unsigned char *src = FN_REPLY + off;
    unsigned int idx;

    disp_row_clear(row);
    disp_at(1, row, label);
    for (idx = 0; idx < max; idx++) {
        char chr = (char)src[idx];

        if (chr == '\0')
            break;
        disp_char((unsigned char)(10 + idx), row, chr);
    }
}

/* RSSI arrives as a uint8_t but means dBm, so it has to be read back as
 * signed or every network looks like a very strong +200. */
static void draw_rssi(unsigned char row, unsigned char raw)
{
    signed char dbm = (signed char)raw;
    unsigned int mag = (unsigned int)(dbm < 0 ? -dbm : dbm);

    disp_at(26, row, dbm < 0 ? "-" : " ");
    disp_at_u16(27, row, mag);
}

static void config_page(void)
{
    volatile unsigned char *src;
    unsigned char idx;

    draw_frame();
    status("SCANNING...");

    /* Adapter info first, and drawn completely before anything else runs: the
     * scan below repaints the reply window this is being read out of. */
    fn_start(FN_DEV_FUJINET, CMD_ADAPTERCONFIG_EXT);
    if (need_ack("EINFO") != 0) {
        nrows = 0;
        return;
    }
    draw_field(3, "SSID", ACX_SSID, 32);
    draw_field(4, "IP", ACX_SLOCALIP, 15);
    draw_field(5, "MAC", ACX_SMAC, 17);
    draw_field(6, "FW", ACX_VERSION, 14);

    disp_at(1, 9, "NETWORKS");

    fn_start(FN_DEV_FUJINET, CMD_SCAN_NETWORKS);
    if (need_ack("ESCAN") != 0) {
        nrows = 0;
        return;
    }
    src = FN_REPLY;
    nrows = src[0] > CFG_ROWS ? CFG_ROWS : src[0];

    for (idx = 0; idx < nrows; idx++) {
        unsigned char row = (unsigned char)(CFG_TOP + idx);
        unsigned char col;

        fn_start(FN_DEV_FUJINET, CMD_GET_SCAN_RESULT);
        fn_param8(idx);
        if (fn_commit() != FN_OK || !fn_acked())
            break;
        src = FN_REPLY;
        disp_row_clear(row);
        for (col = 0; col < 24 && src[col] != 0; col++)
            disp_char((unsigned char)(2 + col), row, (char)src[col]);
        draw_rssi(row, src[SSID_LEN]);
    }

    status("FIRE CONNECT   * BACK");
    if (nrows == 0) {
        disp_at(2, CFG_TOP, "(none found)");
        return;
    }
    cur = 0;
    at_end = 1;
    draw_cursor(cur, true);
}

/* Join the highlighted network. SET_SSID wants nparam >= 1 (the value is
 * ignored) and a payload of exactly ssid[33] + password[64]. The SSID is
 * re-fetched so it is sitting in the reply window, and the editor runs no
 * transactions, so it is still there afterwards to stream straight back. */
static void connect_network(void)
{
    fn_start(FN_DEV_FUJINET, CMD_GET_SCAN_RESULT);
    fn_param8(cur);
    if (need_ack("ESCAN") != 0)
        return;

    fn_entry[0] = '\0';
    if (!fn_edit("WIFI PASSWORD", PASS_MAX)) {
        config_page();
        return;
    }

    status("CONNECTING...");
    fn_start(FN_DEV_FUJINET, CMD_SET_SSID);
    fn_param8(0);
    fn_tx_from_reply(0, SSID_LEN);
    fn_tx_padded(fn_entry, 64);
    if (need_ack("ESSID") != 0)
        return;

    config_page();
}

/* Rename the highlighted host slot. WRITE_HOST_SLOTS takes all eight 32-byte slots at
 * once, so re-read them and stream them back from the reply window, the edited one from
 * RAM, rather than keep 256 bytes in console RAM. */
static void rename_host(void)
{
    volatile unsigned char *name;
    unsigned char idx;

    fn_start(FN_DEV_FUJINET, CMD_READ_HOST_SLOTS);
    if (need_ack("EHOSTS") != 0)
        return;

    name = FN_REPLY + (unsigned int)cur * HOST_STRIDE;
    for (idx = 0; idx < HOST_STRIDE - 1 && name[idx] != 0; idx++)
        fn_entry[idx] = (char)name[idx];
    fn_entry[idx] = '\0';

    if (!fn_edit("EDIT HOST NAME", HOST_STRIDE - 1)) {
        host_page();            /* cancel leaves the slot untouched */
        return;
    }

    fn_start(FN_DEV_FUJINET, CMD_READ_HOST_SLOTS);
    if (need_ack("EHOSTS") != 0)
        return;

    fn_start(FN_DEV_FUJINET, CMD_WRITE_HOST_SLOTS);
    for (idx = 0; idx < HOST_SLOTS; idx++) {
        if (idx == cur)
            fn_tx_padded(fn_entry, HOST_STRIDE);
        else
            fn_tx_from_reply((unsigned int)idx * HOST_STRIDE, HOST_STRIDE);
    }
    if (need_ack("EWRITE") != 0)
        return;

    host_page();
}

/* ---- file page ---- */

/* Read one directory entry into the reply window. Returns false at end of
 * directory or on any error. maxlen is the crunch width the ESP32 applies --
 * ask for the display width when drawing and the full width when booting,
 * because READ_DIR_ENTRY really does truncate to what you asked for. */
static bool read_entry(unsigned char maxlen)
{
    volatile unsigned char *src;
    unsigned char ch0, ch1, ch2;

    fn_start(FN_DEV_FUJINET, CMD_READ_DIR_ENTRY);
    fn_param8(maxlen);
    fn_param8(0);
    if (fn_commit() != FN_OK || !fn_acked())
        return false;

    /* Through a local pointer, never FN_REPLY[...]: sccz80 indexes the macro's cast
     * constant wrongly. Two 0x7F bytes end a directory; an SD host instead repeats "..",
     * and reading past the end NAKs every later seek, so "." and ".." end it too. */
    src = FN_REPLY;
    ch0 = src[0];
    ch1 = src[1];
    ch2 = src[2];

    if (ch0 == 0x7F && ch1 == 0x7F)
        return false;
    if (ch0 == '.' && (ch1 == 0 || (ch1 == '.' && ch2 == 0)))
        return false;
    return true;
}

static bool seek_dir(unsigned int pos)
{
    fn_start(FN_DEV_FUJINET, CMD_SET_DIRECTORY_POSITION);
    fn_param8((unsigned char)pos);
    return fn_commit() == FN_OK && fn_acked();
}

static void file_page(void)
{
    volatile unsigned char *name;
    unsigned char idx;

    draw_frame();
    disp_at(1, 3, "SELECT A FILE");
    status("FIRE BOOT   * BACK");

    /* Position once, then read sequentially: READ_DIR_ENTRY advances the
     * cursor itself, so a page costs one seek plus one read per row rather
     * than a seek per row. */
    if (!seek_dir(top)) {
        oops("ESEEK", 0);
        nrows = 0;
        return;
    }

    nrows = 0;
    at_end = 0;
    for (idx = 0; idx < LIST_ROWS; idx++) {
        unsigned char row = (unsigned char)(LIST_TOP + idx);
        unsigned char col;

        disp_row_clear(row);
        if (!read_entry(NAMELEN)) {
            at_end = 1;
            break;
        }
        name = FN_REPLY;
        for (col = 0; col < NAMELEN && name[col] != 0; col++)
            disp_char((unsigned char)(2 + col), row, (char)name[col]);
        nrows = (unsigned char)(idx + 1);
    }

    if (nrows == 0) {
        disp_at(2, LIST_TOP, "(empty)");
        return;
    }
    if (cur >= nrows)
        cur = (unsigned char)(nrows - 1);
    draw_cursor(cur, true);
}

/* ---- actions ---- */

static void enter_host(void)
{
    host = cur;

    status("MOUNTING HOST...");
    fn_start(FN_DEV_FUJINET, CMD_MOUNT_HOST);
    fn_param8(host);
    if (need_ack("EHOST") != 0)
        return;

    fn_start(FN_DEV_FUJINET, CMD_OPEN_DIRECTORY);
    fn_param8(host);
    fn_tx_padded("/", PATH_LEN);
    if (need_ack("EOPEN") != 0)
        return;

    page = PAGE_FILES;
    top = 0;
    cur = 0;
    file_page();
}

static void leave_host(void)
{
    fn_start(FN_DEV_FUJINET, CMD_CLOSE_DIRECTORY);
    (void)fn_commit();          /* best effort: the host page redraws anyway */
    page = PAGE_HOSTS;
    /* `cur` is the file cursor right now; put it back on the host we came
     * from, or coming back from a long listing lands on an arbitrary slot. */
    cur = host;
    host_page();
}

static void boot_selected(void)
{
    volatile unsigned char *name;
    unsigned char pct = 0xFF;

    /* Re-read the entry at full width: what is on screen was crunched to the
     * display width, and a boot path built from that would be a filename that
     * does not exist. */
    status("READING...");
    if (!seek_dir(top + cur) || !read_entry(FULLLEN)) {
        oops("EREAD", 0);
        return;
    }
    name = FN_REPLY;
    if (name[0] == 0) {
        oops("EEMPTY", 0);
        return;
    }

    /* Directories come back with a trailing '/'. Descending into them is the
     * real CONFIG's job; here, say so rather than NAKing mysteriously. */
    {
        unsigned int len = 0;

        while (len < FULLLEN && name[len] != 0)
            len++;
        if (len > 0 && name[len - 1] == '/') {
            oops("EISDIR", 0);
            return;
        }
    }

    status("SET PATH...");
    fn_start(FN_DEV_FUJINET, CMD_SET_DEVICE_FULLPATH);
    fn_param8(DEVSLOT);
    fn_param8(host);
    fn_param8(MODE_READ);
    fn_tx_path("/", name, PATH_LEN);
    if (need_ack("EPATH") != 0)
        return;

    status("MOUNTING...");
    fn_start(FN_DEV_FUJINET, CMD_MOUNT_IMAGE);
    fn_param8(DEVSLOT);
    fn_param8(MODE_READ);
    if (need_ack("EMOUNT") != 0)
        return;

    status("LOADING");
    for (;;) {
        unsigned char stat = FN_BOOTSTAT;

        if (stat == FNB_READY)
            break;
        if (stat == FNB_FAILED) {
            oops("ELOAD", FN_BOOTERR);
            return;
        }
        if (FN_BOOTPCT != pct) {
            pct = FN_BOOTPCT;
            disp_at_u16(24, 22, pct);
        }
    }

    status("BOOTING");
    fn_boot_swap();             /* does not return */
}

/* ---- main ---- */

/* Every accepted move clicks; a move that goes nowhere -- the top of the first
 * page, the bottom of the last -- stays silent, so the sound says whether the
 * cursor actually went anywhere rather than just that a direction was held. */
static void move_cursor(signed char delta)
{
    if (nrows == 0)
        return;

    if (delta < 0) {
        if (cur > 0) {
            draw_cursor(cur, false);
            cur--;
            draw_cursor(cur, true);
            snd_click();
        } else if (page == PAGE_FILES && top >= LIST_ROWS) {
            top -= LIST_ROWS;
            cur = LIST_ROWS - 1;
            snd_click();
            file_page();
        }
    } else {
        if (cur + 1 < nrows) {
            draw_cursor(cur, false);
            cur++;
            draw_cursor(cur, true);
            snd_click();
        } else if (page == PAGE_FILES && !at_end) {
            top += LIST_ROWS;
            cur = 0;
            snd_click();
            file_page();
        }
    }
}

void main(void)
{
    snd_init();     /* the PSG powers up buzzing; see fujisnd.h */

    /* Splash first: it owns the VDP, loading the logo glyphs over the pattern
     * range disp_init() wants for its inverse charset. disp_init() afterwards
     * puts both back. */
    splash_show();
    in_init();
    {
        unsigned char start = in_frames();

        while ((unsigned char)(in_frames() - start) < 120)
            if (in_read() == IN_FIRE)
                break;
    }

    disp_init();

    if (!fn_present()) {
        disp_at(4, 8, "NO FUJINET CART");
        for (;;)
            ;
    }

    page = PAGE_HOSTS;
    host_page();

    for (;;) {
        unsigned char event = in_read();

        switch (event) {
        case IN_NONE:
            break;
        case IN_UP:
            move_cursor(-1);
            break;
        case IN_DOWN:
            move_cursor(1);
            break;
        case IN_LEFT:
            if (page == PAGE_FILES && top >= LIST_ROWS) {
                top -= LIST_ROWS;
                cur = 0;
                snd_click();
                file_page();
            }
            break;
        case IN_RIGHT:
            if (page == PAGE_FILES && !at_end) {
                top += LIST_ROWS;
                cur = 0;
                snd_click();
                file_page();
            }
            break;
        case IN_FIRE:
            if (page == PAGE_HOSTS)
                enter_host();
            else if (page == PAGE_CONFIG)
                connect_network();
            else
                boot_selected();
            break;
        case IN_KEYSTAR:
            if (page == PAGE_FILES) {
                leave_host();
            } else if (page == PAGE_CONFIG) {
                page = PAGE_HOSTS;
                cur = host;
                host_page();
            }
            break;
        case IN_KEYHASH:
            /* # opens the configuration screen. */
            if (page == PAGE_HOSTS) {
                page = PAGE_CONFIG;
                config_page();
            }
            break;
        case IN_KEY0 + 9:
            /* 9 renames the highlighted host slot. */
            if (page == PAGE_HOSTS)
                rename_host();
            break;

        default:
            /* Keypad 1-8 jumps straight to a host slot. */
            if (page == PAGE_HOSTS && event >= IN_KEY0 + 1
                && event <= IN_KEY0 + HOST_SLOTS) {
                draw_cursor(cur, false);
                cur = (unsigned char)(event - IN_KEY0 - 1);
                draw_cursor(cur, true);
                snd_click();
                enter_host();
            }
            break;
        }
    }
}

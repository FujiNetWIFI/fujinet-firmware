// Services FUJI_DEVICEID_CARTFS requests (see cartfs_bridge.h/fujibus.h) by
// running them against our own local SD card via vfs.c -- the same driver
// filesystem.c's (now-unused, CONFIG-only-boot-path) local ROM launcher used,
// and the same one the vendored Minty firmware you'd sanity-check with reads
// directly. Nothing here talks USB; it is only ever called from inside
// dbc_inbound_handler()'s dispatch in fujinet.c, itself only ever reached
// from fujibus_usb.c's inbound-frame demux while fujibus_transact() is
// waiting on some other, outer command's reply.
//
// One directory open at a time (matches vfs.c's own VFS_MAX_DIRS=1), but TWO
// files (matches VFS_MAX_FILES=2): mounting a ROM with a .cfg sidecar needs
// the main image file to stay open across the sidecar's own open/read/close
// cycle (MediaTypeROM::mount() in the main tree keeps _disk_fileh around while
// resolve_memory_map()/open_memory_map() opens and fully consumes _map_fileh
// in between) -- a single shared slot closes the image's handle out from under
// it. CARTFS_FILE_OPEN hands back a one-byte handle (slot in bit 0, that
// slot's generation in bits 1-7); every CARTFS_FILE_READ/CARTFS_FILE_CLOSE
// names which slot it means.
//
// Slots can leak: a FILE_OPEN whose reply the ESP32 never saw (timeout, or
// dropped as stale) leaves a slot open that nobody holds a handle to, and so
// does an ESP32 reboot while the cart stays powered. So when both slots are
// taken, FILE_OPEN reclaims the least recently used one rather than NAKing
// forever. The generation bits make any handle still pointing at the
// reclaimed slot fail cleanly instead of reading the new occupant.

#include "cartfs_bridge.h"

#include <stdio.h>
#include <string.h>

#include "tusb.h"

// Debug builds route stdio over USB CDC too (CMakeLists.txt enables
// pico_stdio_usb), i.e. onto the very link these replies travel on -- so
// tracing stays off unless explicitly asked for with -DCARTFS_DEBUG.
#ifdef CARTFS_DEBUG
#define CARTFS_LOG(...) printf(__VA_ARGS__)
#else
#define CARTFS_LOG(...) ((void)0)
#endif

// ---------------------------------------------------------------------------
// cartfs_send_frame: builds and sends one SLIP-framed reply addressed to
// FUJI_DEVICEID_CARTFS, with an arbitrary payload. Same blocking send loop
// as fujinet.c's (file-local) dbc_send_frame(), generalized to carry a
// payload -- fujibus_build_request() already supports one, dbc_send_frame()
// just never needed it before now. Outside the CONFIG_SD_STORAGE guard so
// the no-SD stub below can still NAK.
//
// Every reply payload starts with s_tag: the sequence byte the ESP32 put in
// front of the request we're answering (see cartfs_transact() in
// fnFsCartSD.cpp). It uses that to spot and drop a late reply to a request
// it already gave up on, instead of mistaking it for the next one's.
// ---------------------------------------------------------------------------
static uint8_t s_tag = 0;

static void cartfs_send_frame(uint8_t command, const uint8_t *payload, size_t len) {
    static uint8_t buf[1100]; // static: this runs nested several calls deep already
    static uint8_t tagged[384]; // fujibus_build_request()'s own pkt[] cap
    if (len + 1 > sizeof(tagged)) {
        CARTFS_LOG("cartfs: reply cmd=0x%02x len=%u too big to tag -- dropped\n",
                   command, (unsigned)len);
        return;
    }
    tagged[0] = s_tag;
    if (len)
        memcpy(&tagged[1], payload, len);

    size_t flen = fujibus_build_request(FUJI_DEVICEID_CARTFS, command, NULL, 0,
                                         tagged, (uint16_t)(len + 1), buf, sizeof(buf));
    if (!flen) {
        CARTFS_LOG("cartfs: reply cmd=0x%02x len=%u too big to frame -- dropped\n",
                   command, (unsigned)len);
        return;
    }
    size_t sent = 0;
    while (sent < flen) {
        uint32_t w = tud_cdc_write(&buf[sent], (uint32_t)(flen - sent));
        sent += w;
        tud_cdc_write_flush();
        tud_task();
    }
    tud_cdc_write_flush();
}

static void cartfs_nak(void)  { cartfs_send_frame(FUJICMD_NAK, NULL, 0); }

// Pulls the sequence tag off the front of an incoming request into s_tag and
// returns the rest as *body, so the handlers below never see it.
static void cartfs_untag(const fb_reply_t *req, fb_reply_t *body) {
    *body = *req;
    if (req->data_len > 0) {
        s_tag = req->data[0];
        body->data = req->data + 1;
        body->data_len = req->data_len - 1;
    } else {
        s_tag = 0; // malformed (no tag) -- the ESP32 will discard our reply
    }
}

#if CONFIG_SD_STORAGE

#include <strings.h>  // strcasecmp
#include <stdlib.h>
#include <ctype.h>

#include "vfs.h"

// Deliberately much smaller than filesystem.c's read_directory() 512-entry
// cap: that buffer is caller-allocated (SCREEN_ENTRY *dst), but this one is
// static, and RAM here is scarce -- pirto_ii_duo.cmake's MAX_ROM_SIZE alone
// (1024*225*2 bytes) already claims most of the RP2350's 520K SRAM for
// cart.ROM[]. Names live packed in s_names[] rather than in fixed-width
// slots: the ESP32 sends a listed name straight back in CARTFS_FILE_OPEN, so
// it has to be the full on-disk name (No-Intro-style ROM names routinely run
// past 64 chars), and a fixed 256-byte slot per entry would cost 32K.
// Listing stops at whichever limit is hit first.
#define CARTFS_MAX_ENTRIES  128
#define CARTFS_NAME_POOL    (12 * 1024)
#define CARTFS_MAX_PATH     256
// Longest name we'll list: FatFs LFNs top out at 255, and 1 + 255 still fits
// a READ_DIR_ENTRY reply in fujibus_build_request()'s 384-byte pkt[].
#define CARTFS_MAX_NAME     255
// Must fit fujibus_build_request()'s 384-byte unescaped pkt[] (6-byte header
// + payload) -- 512 silently failed the build and no reply was ever sent.
// Keep in sync with fnFsCartSD.cpp's CARTFS_READ_CHUNK.
#define CARTFS_READ_CHUNK   256

typedef struct {
    uint16_t name_off;  // into s_names[], NUL-terminated
    bool isDir;
} cartfs_dirent_t;

static cartfs_dirent_t s_entries[CARTFS_MAX_ENTRIES];
static char s_names[CARTFS_NAME_POOL];
static size_t s_names_used = 0;
static int s_entry_count = 0;
static int s_entry_cursor = 0;
static bool s_dir_open = false;

#define CARTFS_MAX_FILES 2  // matches vfs.c's own VFS_MAX_FILES
static vfs_file_t *s_open_files[CARTFS_MAX_FILES] = { NULL, NULL };
static uint8_t s_file_gen[CARTFS_MAX_FILES];       // 7-bit, bumped per open
static uint32_t s_file_last_use[CARTFS_MAX_FILES]; // s_use_clock at last open/read
static uint32_t s_use_clock = 0;

#define CARTFS_HANDLE(slot)       ((uint8_t)((s_file_gen[slot] << 1) | (slot)))
#define CARTFS_HANDLE_SLOT(h)     ((h) & 1)

// ---------------------------------------------------------------------------
// build_fullpath: "/sd" + the path CONFIG sent (always starts with '/', same
// convention FileSystemSDFAT::_make_fullpath() uses on the ESP32 side).
// ---------------------------------------------------------------------------
static void build_fullpath(char *out, size_t outsz, const char *relpath) {
    snprintf(out, outsz, "/sd%s", relpath ? relpath : "/");
}

// ---------------------------------------------------------------------------
// wildcard_match: '*'/'?' glob, case-insensitive -- same semantics as the
// main tree's util_wildcard_match() (lib/utils/utils.cpp), reimplemented
// here as an iterative backtracking matcher rather than that function's DP
// table, which would want an n*m-byte stack VLA per call.
// ---------------------------------------------------------------------------
static bool wildcard_match(const char *str, const char *pattern) {
    const char *s = str, *p = pattern;
    const char *star_p = NULL, *star_s = NULL;

    while (*s) {
        if (*p == '?' || tolower((unsigned char)*p) == tolower((unsigned char)*s)) {
            s++; p++;
        } else if (*p == '*') {
            star_p = p++;
            star_s = s;
        } else if (star_p != NULL) {
            p = star_p + 1;
            s = ++star_s;
        } else {
            return false;
        }
    }
    while (*p == '*')
        p++;
    return *p == '\0';
}

static int cartfs_entry_compare(const void *a, const void *b) {
    const cartfs_dirent_t *ea = (const cartfs_dirent_t *)a;
    const cartfs_dirent_t *eb = (const cartfs_dirent_t *)b;
    if (ea->isDir != eb->isDir)
        return ea->isDir ? -1 : 1;
    return strcasecmp(&s_names[ea->name_off], &s_names[eb->name_off]);
}

static void cartfs_ack(void)  { cartfs_send_frame(FUJICMD_ACK, NULL, 0); }
static void cartfs_ack_data(const uint8_t *payload, size_t len) {
    cartfs_send_frame(FUJICMD_ACK, payload, len);
}

static void cartfs_close_open_dir(void) {
    s_dir_open = false;
    s_entry_count = 0;
    s_entry_cursor = 0;
    s_names_used = 0;
}

static void cartfs_close_file_slot(int slot) {
    if (s_open_files[slot] != NULL) {
        vfs_close(s_open_files[slot]);
        s_open_files[slot] = NULL;
    }
}

// Maps a wire handle to its slot, or -1 if that slot is closed or has been
// reclaimed and reopened since this handle was issued.
static int cartfs_slot_for_handle(uint8_t handle) {
    int slot = CARTFS_HANDLE_SLOT(handle);
    if (s_open_files[slot] == NULL || CARTFS_HANDLE(slot) != handle)
        return -1;
    return slot;
}

// Returns a free slot index, reclaiming the least recently used one if both
// are taken (see the banner comment for why that can only be a leak).
static int cartfs_alloc_file_slot(void) {
    int lru = 0;
    for (int i = 0; i < CARTFS_MAX_FILES; i++) {
        if (s_open_files[i] == NULL)
            return i;
        if (s_file_last_use[i] < s_file_last_use[lru])
            lru = i;
    }
    CARTFS_LOG("cartfs: both file slots busy -- reclaiming slot %d\n", lru);
    cartfs_close_file_slot(lru);
    return lru;
}

// ---------------------------------------------------------------------------
// handle_open_directory: payload is "path\0" or "path\0pattern\0" -- same
// wire trick fujicmd.bas already uses for FUJI_OPEN_DIRECTORY (see
// fujicore_open_directory_success() in the main tree's fujiDevice.cpp).
// Reads the whole directory up front into s_entries[], like fnFsSD.cpp does,
// since vfs.c has no native telldir/seekdir of its own to serve
// CARTFS_DIR_TELL/SEEK from directly.
// ---------------------------------------------------------------------------
static void handle_open_directory(const fb_reply_t *req) {
    char path[CARTFS_MAX_PATH] = "/";
    char pattern[64] = "";
    char fullpath[CARTFS_MAX_PATH + 4];

    if (req->data_len > 0) {
        // Clamp to the copied buffer's size and do the "is there a pattern
        // after the path" check against that same clamped length (n), not
        // the original req->data_len -- otherwise a path long enough to be
        // truncated by the clamp above would throw pathlen and data_len out
        // of sync with each other.
        size_t n = req->data_len < sizeof(path) - 1 ? req->data_len : sizeof(path) - 1;
        memcpy(path, req->data, n);
        path[n] = '\0';

        size_t pathlen = strlen(path);
        if (pathlen + 1 < n) {
            const uint8_t *rest = req->data + pathlen + 1;
            size_t restlen = n - (pathlen + 1);
            size_t pn = restlen < sizeof(pattern) - 1 ? restlen : sizeof(pattern) - 1;
            memcpy(pattern, rest, pn);
            pattern[pn] = '\0';
        }
    }

    cartfs_close_open_dir();

    build_fullpath(fullpath, sizeof(fullpath), path);

    vfs_dir_t *d = vfs_opendir(fullpath);
    if (!d) {
        cartfs_nak();
        return;
    }

    vfs_dirent_t ent;
    s_entry_count = 0;
    s_names_used = 0;
    while (vfs_readdir(d, &ent) > 0) {
        // Keep the USB link serviced -- unlike fuji_mailbox_service()'s own
        // wait loops, nothing else here calls tud_task(), and a directory
        // full of entries (each a real SD SPI transaction) can add up.
        tud_task();

        if (strcmp(ent.name, ".") == 0 || strcmp(ent.name, "..") == 0)
            continue;
        if ((ent.type & VFS_TYPE_HIDDEN) || (ent.type & VFS_TYPE_SYSTEM))
            continue;

        bool isdir = (ent.type & VFS_TYPE_DIR) != 0;

        // Pattern (if any) filters files only -- directories always show, so
        // browsing/navigation still works while a filter is active. Matches
        // FileSystemSDFAT::dir_open()'s default (non-filter_dirs) behaviour.
        if (!isdir && pattern[0] != '\0' && !wildcard_match(ent.name, pattern))
            continue;

        // Never truncate: a shortened name lists fine but can't be opened.
        // Skip anything that couldn't round-trip instead.
        size_t namelen = strlen(ent.name);
        if (namelen > CARTFS_MAX_NAME) {
            CARTFS_LOG("cartfs: skipping over-long name (%u chars)\n", (unsigned)namelen);
            continue;
        }
        if (s_entry_count >= CARTFS_MAX_ENTRIES ||
            s_names_used + namelen + 1 > sizeof(s_names)) {
            CARTFS_LOG("cartfs: \"%s\" listing truncated at %d entries\n",
                       fullpath, s_entry_count);
            break;
        }

        cartfs_dirent_t *e = &s_entries[s_entry_count++];
        e->isDir = isdir;
        e->name_off = (uint16_t)s_names_used;
        memcpy(&s_names[s_names_used], ent.name, namelen + 1);
        s_names_used += namelen + 1;
    }
    vfs_closedir(d);

    qsort(s_entries, s_entry_count, sizeof(s_entries[0]), cartfs_entry_compare);

    s_entry_cursor = 0;
    s_dir_open = true;
    cartfs_ack();
}

static void handle_read_dir_entry(void) {
    if (!s_dir_open || s_entry_cursor >= s_entry_count) {
        cartfs_ack_data(NULL, 0); // empty payload = EOF
        return;
    }

    cartfs_dirent_t *e = &s_entries[s_entry_cursor++];

    const char *name = &s_names[e->name_off];
    uint8_t out[1 + CARTFS_MAX_NAME];
    out[0] = e->isDir ? 1 : 0;
    size_t namelen = strlen(name);
    memcpy(&out[1], name, namelen);
    cartfs_ack_data(out, 1 + namelen);
}

static void handle_close_directory(void) {
    cartfs_close_open_dir();
    cartfs_ack();
}

static void handle_dir_tell(void) {
    uint16_t pos = s_dir_open ? (uint16_t)s_entry_cursor : 0xFFFF;
    uint8_t out[2] = { (uint8_t)(pos & 0xFF), (uint8_t)(pos >> 8) };
    cartfs_ack_data(out, sizeof(out));
}

static void handle_dir_seek(const fb_reply_t *req) {
    if (req->data_len < 2) {
        cartfs_nak();
        return;
    }
    uint16_t pos = req->data[0] | (req->data[1] << 8);
    if (!s_dir_open || pos > (uint16_t)s_entry_count) {
        cartfs_nak();
        return;
    }
    s_entry_cursor = pos;
    cartfs_ack();
}

static void handle_file_exists(const fb_reply_t *req) {
    char path[CARTFS_MAX_PATH] = "/";
    char fullpath[CARTFS_MAX_PATH + 4];
    vfs_stat_t st;

    if (req->data_len > 0) {
        size_t n = req->data_len < sizeof(path) - 1 ? req->data_len : sizeof(path) - 1;
        memcpy(path, req->data, n);
        path[n] = '\0';
    }

    build_fullpath(fullpath, sizeof(fullpath), path);
    if (vfs_stat(fullpath, &st) == 0)
        cartfs_ack();
    else
        cartfs_nak();
}

static void handle_file_open(const fb_reply_t *req) {
    char path[CARTFS_MAX_PATH] = "/";
    char fullpath[CARTFS_MAX_PATH + 4];

    if (req->data_len > 0) {
        size_t n = req->data_len < sizeof(path) - 1 ? req->data_len : sizeof(path) - 1;
        memcpy(path, req->data, n);
        path[n] = '\0';
    }

    int slot = cartfs_alloc_file_slot();

    build_fullpath(fullpath, sizeof(fullpath), path);

    s_open_files[slot] = vfs_open(fullpath, "r");
    if (!s_open_files[slot]) {
        cartfs_nak();
        return;
    }
    s_file_gen[slot] = (s_file_gen[slot] + 1) & 0x7F;
    s_file_last_use[slot] = ++s_use_clock;
    uint8_t handle = CARTFS_HANDLE(slot);

    vfs_stat_t st;
    uint32_t size = (vfs_stat(fullpath, &st) == 0) ? (uint32_t)st.size : 0;
    uint8_t out[5] = {
        (uint8_t)(size & 0xFF), (uint8_t)((size >> 8) & 0xFF),
        (uint8_t)((size >> 16) & 0xFF), (uint8_t)((size >> 24) & 0xFF),
        (uint8_t)handle
    };
    CARTFS_LOG("cartfs: FILE_OPEN \"%s\" -> handle=%d size=%lu\n", fullpath, handle, (unsigned long)size);
    cartfs_ack_data(out, sizeof(out));
}

static void handle_file_read(const fb_reply_t *req) {
    if (req->data_len < 9) {
        cartfs_nak();
        return;
    }

    int handle = req->data[0];
    int slot = cartfs_slot_for_handle((uint8_t)handle);
    if (slot < 0) {
        CARTFS_LOG("cartfs: FILE_READ handle=%d -- not open (or reclaimed)\n", handle);
        cartfs_nak();
        return;
    }
    s_file_last_use[slot] = ++s_use_clock;

    uint32_t offset = req->data[1] | (req->data[2] << 8) |
                       ((uint32_t)req->data[3] << 16) | ((uint32_t)req->data[4] << 24);
    uint32_t want = req->data[5] | (req->data[6] << 8) |
                    ((uint32_t)req->data[7] << 16) | ((uint32_t)req->data[8] << 24);
    if (want > CARTFS_READ_CHUNK)
        want = CARTFS_READ_CHUNK;

    CARTFS_LOG("cartfs: FILE_READ handle=%d offset=%lu want=%lu\n",
               handle, (unsigned long)offset, (unsigned long)want);

    if (vfs_lseek(s_open_files[slot], offset) != 0) {
        CARTFS_LOG("cartfs: FILE_READ handle=%d -- lseek failed\n", handle);
        cartfs_nak();
        return;
    }

    static uint8_t buf[CARTFS_READ_CHUNK]; // static, same reasoning as cartfs_send_frame's buf
    int got = vfs_read(s_open_files[slot], buf, want);
    if (got < 0) {
        CARTFS_LOG("cartfs: FILE_READ handle=%d -- vfs_read failed\n", handle);
        cartfs_nak();
        return;
    }
    CARTFS_LOG("cartfs: FILE_READ handle=%d -> got=%d\n", handle, got);
    cartfs_ack_data(buf, (size_t)got);
}

static void handle_file_close(const fb_reply_t *req) {
    if (req->data_len >= 1) {
        CARTFS_LOG("cartfs: FILE_CLOSE handle=%d\n", req->data[0]);
        // A stale handle (slot already reclaimed) must not close the slot's
        // new occupant -- just ACK, since the file it named is gone anyway.
        int slot = cartfs_slot_for_handle(req->data[0]);
        if (slot >= 0)
            cartfs_close_file_slot(slot);
    }
    cartfs_ack();
}

bool cartfs_inbound_handler(const fb_reply_t *tagged_req) {
    if (tagged_req->device != FUJI_DEVICEID_CARTFS)
        return false;

    fb_reply_t body;
    cartfs_untag(tagged_req, &body);
    const fb_reply_t *req = &body;

    switch (req->command) {
    case CARTFS_OPEN_DIRECTORY:  handle_open_directory(req); break;
    case CARTFS_READ_DIR_ENTRY:  handle_read_dir_entry();     break;
    case CARTFS_CLOSE_DIRECTORY: handle_close_directory();    break;
    case CARTFS_DIR_TELL:        handle_dir_tell();           break;
    case CARTFS_DIR_SEEK:        handle_dir_seek(req);        break;
    case CARTFS_FILE_EXISTS:     handle_file_exists(req);     break;
    case CARTFS_FILE_OPEN:       handle_file_open(req);       break;
    case CARTFS_FILE_READ:       handle_file_read(req);       break;
    case CARTFS_FILE_CLOSE:      handle_file_close(req);      break;
    default:                     cartfs_nak();                break;
    }
    return true;
}

#else /* !CONFIG_SD_STORAGE */

// No local SD on this board, but the ESP32 build can't tell which cart it's
// talking to, so it may still ask. Claim and NAK every CARTFS frame: left
// unclaimed, fujibus_transact() would take it as the outer command's reply.
bool cartfs_inbound_handler(const fb_reply_t *req) {
    if (req->device != FUJI_DEVICEID_CARTFS)
        return false;
    fb_reply_t body;
    cartfs_untag(req, &body); // so the NAK carries the right tag
    cartfs_nak();
    return true;
}

#endif

#ifdef ENABLE_OLED_DISPLAY

#include "oled_display.h"

#include <cstdio>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "font5x7.h"
#include "font7x10.h"
#include "splash_bitmap.h"
#include "rom_icon_bitmap.h"
#include "../../include/debug.h"
#include "../../include/pinmap.h"
#include "../../include/version.h"
#include "bus.h"
#include "compat_string.h"
#include "drivewireFuji.h"
#include "fujiHost.h"
#include "media.h"

OledDisplay fnOledDisplay;

#define STATUS_POLL_MS    250

// The status view's content rows (mounted disks/ROM) stay at 1x font5x7,
// 8px tall, so up to 6 fit above a single fixed bottom row reserved for
// sector activity or the ROM power-cycle reminder. That one row uses the
// native-rasterized font7x10 instead of a scaled-up font5x7 -- 2x nearest-
// neighbor scaling looked blocky and cost 3 extra content rows for the size
// it bought; font7x10 is a real mid-size font, not a scaled one.
#define LIST_SCALE        1
#define LIST_ROW_HEIGHT   8
#define STATUS_ROW_HEIGHT 16
#define NAME_BUF_LEN      20 // up to 19 visible chars + NUL
#define ACTIVITY_LINE_CHARS 21 // 128px / 6px per char at LIST_SCALE

// Splits text into fixed-width chunks, one per screen line -- plain
// character wrapping, not word-aware, which is fine for a filesystem path.
static void wrap_text(const std::string &text, size_t width, std::vector<std::string> &out)
{
    for (size_t pos = 0; pos < text.size(); pos += width)
        out.push_back(text.substr(pos, width));
}

static char host_type_tag(fujiHostType type)
{
    switch (type)
    {
    case HOSTTYPE_LOCAL:
        return 'S';
    case HOSTTYPE_TNFS:
        return 'T';
    case HOSTTYPE_SMB:
        return 'M';
    case HOSTTYPE_NFS:
        return 'N';
    case HOSTTYPE_FTP:
        return 'F';
    case HOSTTYPE_HTTP:
        return 'H';
    default:
        return '?';
    }
}

// Drops any directory the filename carries (fujiDisk::filename is set from
// fujidev_set_device_fullpath(), a full devicespec path, not a bare name),
// then truncates to out_size - 1 visible chars, keeping the extension when
// it fits rather than cutting it off.
static void format_short_name(char *out, size_t out_size, const char *filename)
{
    const char *base = strrchr(filename, '/');
    base = base ? base + 1 : filename;

    if (strlen(base) < out_size)
    {
        strlcpy(out, base, out_size);
        return;
    }

    const char *dot = strrchr(base, '.');
    size_t ext_len = dot ? strlen(dot) : 0;
    if (dot != nullptr && ext_len < out_size - 1)
        snprintf(out, out_size, "%.*s%s", (int)(out_size - 1 - ext_len), base, dot);
    else
        strlcpy(out, base, out_size);
}

void OledDisplay::draw_text(int x, int y, const char *text, int scale)
{
    if (!_ready)
        return;

    for (const char *p = text; *p != '\0'; p++)
    {
        unsigned char ch = static_cast<unsigned char>(*p);
        if (ch < FONT5X7_FIRST_CHAR || ch > FONT5X7_LAST_CHAR)
            ch = ' ';

        const uint8_t *cols = font5x7[ch - FONT5X7_FIRST_CHAR];
        for (int col = 0; col < FONT5X7_WIDTH; col++)
        {
            uint8_t bits = cols[col];
            for (int row = 0; row < FONT5X7_HEIGHT; row++)
            {
                if (!(bits & (1 << row)))
                    continue;
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                        _oled.set_pixel(x + col * scale + sx, y + row * scale + sy, true);
            }
        }
        x += (FONT5X7_WIDTH + 1) * scale;
    }
}

void OledDisplay::draw_status_text(int x, int y, const char *text)
{
    if (!_ready)
        return;

    for (const char *p = text; *p != '\0'; p++)
    {
        unsigned char ch = static_cast<unsigned char>(*p);
        if (ch < FONT7X10_FIRST_CHAR || ch > FONT7X10_LAST_CHAR)
            ch = ' ';

        const uint16_t *cols = font7x10[ch - FONT7X10_FIRST_CHAR];
        for (int col = 0; col < FONT7X10_WIDTH; col++)
        {
            uint16_t bits = cols[col];
            for (int row = 0; row < FONT7X10_HEIGHT; row++)
                if (bits & (1 << row))
                    _oled.set_pixel(x + col, y + row, true);
        }
        x += FONT7X10_WIDTH + 1;
    }
}

void OledDisplay::show_splash()
{
    _oled.clear();
    _oled.draw_bitmap(4, 8, SPLASH_ICON_W, SPLASH_ICON_H, splash_icon_bitmap);
    draw_text(60, 23, "FujiNet", 1);
    draw_text(60, 34, FN_VERSION_FULL, 1);
    _oled.flush();
}

// A ROM takes over the whole screen while loaded: there's no disk traffic
// left to show, and the power-cycle reminder deserves more than a shared
// bottom row. See redraw_rom_status().
static bool any_rom_active()
{
    for (unsigned int slot = 0; slot < MAX_DWDISK_DEVICES; slot++)
    {
        auto *dev = theFuji->get_disk_dev(slot);
        if (dev->device_active && dev->disktype() == MEDIATYPE_ROM)
            return true;
    }
    return false;
}

void OledDisplay::redraw_rom_status()
{
    _oled.clear();

    char name[NAME_BUF_LEN] = {0};
    for (unsigned int slot = 0; slot < MAX_DWDISK_DEVICES; slot++)
    {
        auto *dev = theFuji->get_disk_dev(slot);
        if (dev->device_active && dev->disktype() == MEDIATYPE_ROM)
        {
            format_short_name(name, sizeof(name), theFuji->get_disk(slot)->filename);
            break;
        }
    }

    _oled.draw_bitmap(2, 2, ROM_ICON_W, ROM_ICON_H, rom_icon_bitmap);
    draw_text(ROM_ICON_W + 4, 2, name, LIST_SCALE);

    draw_status_text(20, 26, "POWER CYCLE");
    draw_status_text(24, 44, "FOR CONFIG");

    _oled.flush();
}

// Mounted disks and sector activity are read straight from the DriveWire
// Fuji device and bus each tick, rather than pushed, so mounting, unmounting
// or rotating a disk needs no call into this class. A ROM is read the same
// way but takes over the whole screen; see redraw_rom_status().
void OledDisplay::redraw_status()
{
    if (any_rom_active())
    {
        redraw_rom_status();
        return;
    }

    _oled.clear();

    int y = 0;

    // Content rows stay available up to the fixed bottom status row.
    for (unsigned int slot = 0;
         slot < MAX_DWDISK_DEVICES && y <= SSD1306_HEIGHT - STATUS_ROW_HEIGHT - LIST_ROW_HEIGHT;
         slot++)
    {
        auto *dev = theFuji->get_disk_dev(slot);
        if (!dev->device_active || dev->is_config_device)
            continue;

        char name[NAME_BUF_LEN];
        format_short_name(name, sizeof(name), theFuji->get_disk(slot)->filename);

        char line[NAME_BUF_LEN + 2];
        char tag = host_type_tag(theFuji->get_host(theFuji->get_disk(slot)->host_slot)->get_type());
        snprintf(line, sizeof(line), "%c:%s", tag, name);
        draw_text(0, y, line, LIST_SCALE);
        y += LIST_ROW_HEIGHT;
    }

    // Opportunistic activity block: DriveWire network access, SD/TNFS
    // directory browsing, or WiFi scan/connect status, whichever happened
    // most recently -- one or more lines (a browsed path wraps across as
    // many as it needs). Sticky, like the bottom row below: stays up until
    // the next activity event replaces it, not on a timer. Takes whatever
    // free content rows remain.
    //
    // Copied out under the lock rather than iterated in place, since
    // show_activity()/show_browsing() can replace it from another task
    // between any two lines.
    std::vector<std::string> activity_lines;
    {
        std::scoped_lock<std::mutex> lock(_activity_mutex);
        activity_lines = _activity_lines;
    }
    if (!activity_lines.empty())
    {
        for (const std::string &line : activity_lines)
        {
            if (y > SSD1306_HEIGHT - STATUS_ROW_HEIGHT - LIST_ROW_HEIGHT)
                break;
            draw_text(0, y, line.c_str(), LIST_SCALE);
            y += LIST_ROW_HEIGHT;
        }
    }

    // Fixed bottom row: the most recent sector read/write. Sticky -- it
    // stays up until the next transfer or a reboot, rather than clearing
    // itself after a timeout.
    int bottom_y = SSD1306_HEIGHT - STATUS_ROW_HEIGHT + 3; // font7x10 is 10px in a 16px row
    systemBus::LastSectorOp io = SYSTEM_BUS.get_last_sector_op();
    if (io.at_ms != 0)
    {
        char line[20];
        snprintf(line, sizeof(line), "%s D%u S:%lu",
                 io.is_write ? "WR" : "RD", io.drive_num, (unsigned long)io.lsn);
        draw_status_text(0, bottom_y, line);
    }

    _oled.flush();
}

static bool any_real_disk_active()
{
    for (unsigned int slot = 0; slot < MAX_DWDISK_DEVICES; slot++)
    {
        auto *dev = theFuji->get_disk_dev(slot);
        if (dev->device_active && !dev->is_config_device)
            return true;
    }
    return false;
}

void OledDisplay::poll()
{
    if (!_ready)
        return;

    // Leave the splash not just for a mounted disk, but for any activity --
    // browsing a host or working with WiFi happens before anything is
    // mounted, and that's exactly when a status display is most useful.
    // Once left, the status view stays up -- even once nothing is mounted --
    // rather than reverting to the splash; only reset_to_boot_logo() does
    // that.
    if (_mode == Mode::BOOT_LOGO)
    {
        bool has_activity;
        {
            std::scoped_lock<std::mutex> lock(_activity_mutex);
            has_activity = !_activity_lines.empty();
        }
        if (any_real_disk_active() || has_activity)
            _mode = Mode::STATUS;
    }

    if (_mode == Mode::STATUS)
        redraw_status();
}

void OledDisplay::task(void *arg)
{
    OledDisplay *self = static_cast<OledDisplay *>(arg);
    while (true)
    {
        self->poll();
        vTaskDelay(STATUS_POLL_MS / portTICK_PERIOD_MS);
    }
}

void OledDisplay::show_activity(const char *text)
{
    if (!_ready || text == nullptr || *text == '\0')
        return;

    std::scoped_lock<std::mutex> lock(_activity_mutex);
    _activity_lines.clear();
    _activity_lines.emplace_back(text);
}

// Shows the whole path, wrapped across as many lines as it needs, rather
// than just the current folder -- unlike the mounted-disk list, which stays
// to a short name since it's naming something specific, not somewhere
// you're navigating.
void OledDisplay::show_browsing(fujiHostType type, const char *hostname, const char *dirpath)
{
    if (!_ready || dirpath == nullptr)
        return;

    std::string path = (*dirpath != '\0') ? dirpath : (hostname ? hostname : "");

    char tag[3];
    snprintf(tag, sizeof(tag), "%c:", host_type_tag(type));

    std::scoped_lock<std::mutex> lock(_activity_mutex);
    _activity_lines.clear();
    wrap_text(tag + path, ACTIVITY_LINE_CHARS, _activity_lines);
}

void OledDisplay::reset_to_boot_logo()
{
    if (!_ready)
        return;

    _mode = Mode::BOOT_LOGO;
    {
        std::scoped_lock<std::mutex> lock(_activity_mutex);
        _activity_lines.clear();
    }
    show_splash();
}

void OledDisplay::start()
{
    esp_err_t err = _oled.init(I2C_NUM_1, PIN_OLED_SDA, PIN_OLED_SCL);
    if (err != ESP_OK)
    {
        Debug_printf("OledDisplay::start() panel init failed: %d\n", err);
        return;
    }

    _ready = true;
    show_splash();

    if (xTaskCreatePinnedToCore(task, "oled_task", 3072, this, 4, NULL, 0) != pdTRUE)
        Debug_printf("OledDisplay::start() could not start oled_task\n");
}

#endif // ENABLE_OLED_DISPLAY

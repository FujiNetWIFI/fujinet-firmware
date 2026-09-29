#if defined(FUJINET_CYD) && defined(BUILD_COCO)

#include "cyd_ui.h"
#include "cyd_lcd.h"
#include "cyd_touch.h"
#include "cyd_font6847.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#include "bus.h"
#include "fujiDevice.h"
#include "fnConfig.h"
#include "fnSystem.h"
#include "fnWiFi.h"
#include "media.h"
#include "httpService.h"
#include "httpServiceBrowse.h"
#include "PSRAMAllocator.h"

#include "../../include/debug.h"

// Every row is 24px tall (font scaled 2x vertically), giving 10 rows. Status
// lines are 1x wide (40 columns); the title is 2x wide (20 columns).
#define SCALE_Y    2
#define CELL_H     (CYD_FONT_H * SCALE_Y)
#define ROWS       (CYD_LCD_HEIGHT / CELL_H)
#define MAX_COLS   (CYD_LCD_WIDTH / CYD_FONT_W)
#define TITLE      "FUJINET COCO"

#define UI_DRIVES         4 // DriveWire drives 0-3
#define UI_TICK_MS        30
#define UI_REFRESH_MS     250
#define UI_MESSAGE_MS     2000
#define TOUCH_RETRY_MS    5000
#define UI_TASK_PRIO      2
#define UI_TASK_STACK     6144
#define BROWSE_PAGE_ROWS  8   // rows 1-8 list entries
#define BROWSE_MAX_FILES  512
#define NAV_BUTTONS       5   // bottom bar, 8 columns each

// CoCo VDG text colours: dark green on bright green
#define COL_BG  CYD_RGB(0x08, 0xFF, 0x08)
#define COL_FG  CYD_RGB(0x00, 0x40, 0x00)

enum ui_row
{
    ROW_TITLE = 0,
    ROW_WIFI,
    ROW_IP,
    ROW_BECKER,
    ROW_BLANK1,
    ROW_DRIVE0,
    ROW_FOOTER = ROW_DRIVE0 + UI_DRIVES,
};

enum ui_screen
{
    SCREEN_STATUS,
    SCREEN_BROWSE,
    SCREEN_CONFIRM,
    SCREEN_MESSAGE,
};

enum nav_button
{
    NAV_CANCEL,
    NAV_UP,
    NAV_PREV,
    NAV_NEXT,
    NAV_EJECT,
};

// Confirm screen rows that act as buttons
#define ROW_CONFIRM_RO     4
#define ROW_CONFIRM_RW     6
#define ROW_CONFIRM_CANCEL 8

struct ui_line
{
    char text[MAX_COLS + 1];
    bool inverse;
    int sx; // horizontal scale: 1 = 40 columns, 2 = 20 columns
};

struct browse_entry
{
    char name[MAX_PATHLEN];
    bool is_dir;
};

static ui_line s_shown[ROWS];

static ui_screen s_screen = SCREEN_STATUS;
static int s_drive = 0;                  // drive being changed
static std::string s_path = "/";         // directory shown in the browser
static std::vector<browse_entry, PSRAMAllocator<browse_entry>> s_entries;
static int s_page = 0;
static int s_choice = -1;                // entry picked for mounting
static char s_message[MAX_COLS + 1];
static int64_t s_message_until = 0;

static void set_line(ui_line *lines, int row, bool inverse, int sx, const char *fmt, ...)
    __attribute__((format(printf, 5, 6)));

static void set_line(ui_line *lines, int row, bool inverse, int sx, const char *fmt, ...)
{
    char buf[MAX_PATHLEN + 16];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    // Pad to full width so a shorter line erases the previous one
    int cols = MAX_COLS / sx;
    snprintf(lines[row].text, sizeof(lines[row].text), "%-*.*s", cols, cols, buf);
    lines[row].inverse = inverse;
    lines[row].sx = sx;
}

static const char *basename_of(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static int64_t now_ms()
{
    return esp_timer_get_time() / 1000;
}

static const char *becker_status()
{
    if (!SYSTEM_BUS.isBoIP())
        return "OFF (SERIAL)";
    if (!fnWiFi.connected())
        return "NO WIFI";
    switch (SYSTEM_BUS.boipState())
    {
    case BoIPConnected:
        return "CONNECTED";
    case BoIPWaitConn:
        return "LISTENING";
    case BoIPSuspended:
        return "RETRYING";
    default:
        return "STOPPED";
    }
}

// Host slot of the local SD card, or -1 when none is configured. Uses the
// config, since a fujiHost only learns its type when first mounted.
static int sd_host_slot()
{
    for (int i = 0; i < MAX_HOSTS; i++)
        if (Config.get_host_type(i) == fnConfig::host_types::HOSTTYPE_SD)
            return i;
    return -1;
}

static void show_message(const char *msg)
{
    strlcpy(s_message, msg, sizeof(s_message));
    s_message_until = now_ms() + UI_MESSAGE_MS;
    s_screen = SCREEN_MESSAGE;
}

// Directories and images the DriveWire disk device can mount
static bool is_listable(const fsdir_entry_t *e)
{
    if (e->filename[0] == '.' || strcmp(e->filename, "System Volume Information") == 0)
        return false;
    if (e->isDir)
        return true;
    mediatype_t mt = MediaType::discover_mediatype(e->filename);
    return mt != MEDIATYPE_UNKNOWN && mt != MEDIATYPE_CAS;
}

// Read the whole directory up front: the host's directory stream is shared
// with CONFIG on the CoCo, so keep it open as briefly as possible.
static bool load_directory()
{
    s_entries.clear();
    s_page = 0;

    int slot = sd_host_slot();
    if (slot < 0)
        return false;
    fujiHost *host = theFuji->get_host(slot);
    if (!host->mount().is_success() || !host->dir_open(s_path.c_str(), "", 0).is_success())
        return false;

    fsdir_entry_t *e;
    while ((e = host->dir_nextfile()) != nullptr && s_entries.size() < BROWSE_MAX_FILES)
    {
        if (!is_listable(e))
            continue;
        browse_entry be;
        strlcpy(be.name, e->filename, sizeof(be.name));
        size_t len = strlen(be.name);
        if (len > 1 && be.name[len - 1] == '/')
            be.name[len - 1] = '\0';
        be.is_dir = e->isDir;
        s_entries.push_back(be);
    }
    host->dir_close();
    return true;
}

static void open_browser(int drive)
{
    s_drive = drive;
    s_path = "/";
    if (sd_host_slot() < 0)
        show_message("NO SD HOST CONFIGURED");
    else if (!load_directory())
        show_message("CANNOT READ SD CARD");
    else
        s_screen = SCREEN_BROWSE;
}

static void mount_choice(bool writable)
{
    fnHttpBrowse::mount_params params;
    params.host_slot = sd_host_slot();
    params.device_slot = s_drive;
    params.mode = writable ? 2 : 1;
    params.filename = s_path + s_entries[s_choice].name;
    params.filename_given = true;

    fnHTTPD.clearErrMsg();
    bool ok = fnHttpBrowse::mount_file(params);
    fnHTTPD.clearErrMsg();

    char msg[MAX_COLS + 1];
    snprintf(msg, sizeof(msg), ok ? "D%d MOUNTED" : "D%d MOUNT FAILED", s_drive);
    show_message(msg);
}

static void eject_drive()
{
    fnHTTPD.clearErrMsg();
    bool ok = fnHttpBrowse::eject_slot(s_drive);
    fnHTTPD.clearErrMsg();

    char msg[MAX_COLS + 1];
    snprintf(msg, sizeof(msg), ok ? "D%d EJECTED" : "D%d EJECT FAILED", s_drive);
    show_message(msg);
}

static int page_count()
{
    int n = (int)s_entries.size();
    return n == 0 ? 1 : (n + BROWSE_PAGE_ROWS - 1) / BROWSE_PAGE_ROWS;
}

// ---- Screen builders ----

static void build_status(ui_line *lines, bool active)
{
    // Title centred in the 20 wide columns, activity marker in the last one
    const int title_cols = MAX_COLS / 2, title_len = sizeof(TITLE) - 1;
    const int left = (title_cols - title_len) / 2;
    set_line(lines, ROW_TITLE, true, 2, "%*s%s%*s%c", left, "", TITLE,
             title_cols - title_len - left - 1, "", active ? '*' : ' ');

    if (fnWiFi.connected())
    {
        set_line(lines, ROW_WIFI, false, 1, "WIFI %s", fnWiFi.get_current_ssid().c_str());
        set_line(lines, ROW_IP, false, 1, "IP %s", fnSystem.Net.get_ip4_address_str().c_str());
    }
    else
    {
        set_line(lines, ROW_WIFI, false, 1, "WIFI CONNECTING...");
        set_line(lines, ROW_IP, false, 1, "IP -");
    }

    set_line(lines, ROW_BECKER, false, 1, "DW %s %d", becker_status(), Config.get_boip_port());

    for (int d = 0; d < UI_DRIVES; d++)
    {
        const char *name = theFuji->get_disk(d)->filename;
        set_line(lines, ROW_DRIVE0 + d, false, 1, "D%d %s", d, name[0] ? basename_of(name) : "-");
    }

    int64_t secs = esp_timer_get_time() / 1000000;
    set_line(lines, ROW_FOOTER, true, 1, " UP %02d:%02d:%02d   TAP A DRIVE TO CHANGE",
             (int)(secs / 3600), (int)(secs / 60 % 60), (int)(secs % 60));
}

static void build_browse(ui_line *lines)
{
    set_line(lines, 0, true, 1, "D%d SD:%s  %d/%d", s_drive, s_path.c_str(), s_page + 1, page_count());

    if (s_entries.empty())
        set_line(lines, 1, false, 1, "  (NO DISK IMAGES HERE)");

    for (int r = 0; r < BROWSE_PAGE_ROWS; r++)
    {
        int i = s_page * BROWSE_PAGE_ROWS + r;
        if (i >= (int)s_entries.size())
            break;
        const browse_entry &e = s_entries[i];
        if (e.is_dir)
            set_line(lines, 1 + r, false, 1, "%-34.34s <DIR>", e.name);
        else
            set_line(lines, 1 + r, false, 1, "%s", e.name);
    }

    set_line(lines, ROWS - 1, true, 1, " CANCEL    UP    <PREV   NEXT>   EJECT ");
}

static void build_confirm(ui_line *lines)
{
    set_line(lines, 0, true, 1, "MOUNT IN D%d:", s_drive);
    set_line(lines, 2, false, 1, "%s", s_entries[s_choice].name);
    set_line(lines, ROW_CONFIRM_RO, true, 1, "%24s", "READ ONLY");
    set_line(lines, ROW_CONFIRM_RW, true, 1, "%25s", "READ/WRITE");
    set_line(lines, ROW_CONFIRM_CANCEL, true, 1, "%23s", "CANCEL");
}

static void build_message(ui_line *lines)
{
    int left = (MAX_COLS - (int)strlen(s_message)) / 2;
    set_line(lines, 4, false, 1, "%*s%s", left > 0 ? left : 0, "", s_message);
}

static void build_screen(ui_line *lines, bool active)
{
    for (int r = 0; r < ROWS; r++)
        set_line(lines, r, false, 1, "%s", "");

    switch (s_screen)
    {
    case SCREEN_STATUS:
        build_status(lines, active);
        break;
    case SCREEN_BROWSE:
        build_browse(lines);
        break;
    case SCREEN_CONFIRM:
        build_confirm(lines);
        break;
    case SCREEN_MESSAGE:
        build_message(lines);
        break;
    }
}

// ---- Touch handling ----

static void on_tap(int x, int y)
{
    int row = y / CELL_H;
    Debug_printf("CYD touch: tap x=%d y=%d row=%d\r\n", x, y, row);

    switch (s_screen)
    {
    case SCREEN_STATUS:
        if (row >= ROW_DRIVE0 && row < ROW_DRIVE0 + UI_DRIVES)
            open_browser(row - ROW_DRIVE0);
        break;

    case SCREEN_BROWSE:
        if (row == ROWS - 1)
        {
            switch (x / (CYD_LCD_WIDTH / NAV_BUTTONS))
            {
            case NAV_CANCEL:
                s_screen = SCREEN_STATUS;
                break;
            case NAV_UP:
                if (s_path.size() > 1)
                {
                    size_t cut = s_path.find_last_of('/', s_path.size() - 2);
                    s_path.resize(cut + 1);
                    if (!load_directory())
                        show_message("CANNOT READ SD CARD");
                }
                break;
            case NAV_PREV:
                if (s_page > 0)
                    s_page--;
                break;
            case NAV_NEXT:
                if (s_page < page_count() - 1)
                    s_page++;
                break;
            case NAV_EJECT:
                eject_drive();
                break;
            }
        }
        else if (row >= 1 && row <= BROWSE_PAGE_ROWS)
        {
            int i = s_page * BROWSE_PAGE_ROWS + row - 1;
            if (i >= (int)s_entries.size())
                break;
            if (s_entries[i].is_dir)
            {
                s_path += s_entries[i].name;
                s_path += "/";
                if (!load_directory())
                    show_message("CANNOT READ SD CARD");
            }
            else
            {
                s_choice = i;
                s_screen = SCREEN_CONFIRM;
            }
        }
        break;

    case SCREEN_CONFIRM:
        if (row == ROW_CONFIRM_RO)
            mount_choice(false);
        else if (row == ROW_CONFIRM_RW)
            mount_choice(true);
        else if (row == ROW_CONFIRM_CANCEL)
            s_screen = SCREEN_BROWSE;
        break;

    case SCREEN_MESSAGE:
        s_screen = SCREEN_STATUS;
        break;
    }
}

// ---- Task ----

static void draw_line(int row, const ui_line &line)
{
    cyd_lcd_text(0, row * CELL_H, line.text,
                 line.inverse ? COL_BG : COL_FG,
                 line.inverse ? COL_FG : COL_BG,
                 line.sx, SCALE_Y);
}

static void redraw_changed(bool active)
{
    static ui_line next[ROWS];
    build_screen(next, active);
    for (int r = 0; r < ROWS; r++)
    {
        if (strcmp(next[r].text, s_shown[r].text) != 0 || next[r].inverse != s_shown[r].inverse
            || next[r].sx != s_shown[r].sx)
        {
            draw_line(r, next[r]);
            s_shown[r] = next[r];
        }
    }
}

static void ui_task(void *arg)
{
    if (!cyd_lcd_init())
    {
        vTaskDelete(nullptr);
        return;
    }
    bool have_touch = cyd_touch_init();

    cyd_lcd_fill(0, 0, CYD_LCD_WIDTH, CYD_LCD_HEIGHT, COL_BG);
    memset(s_shown, 0, sizeof(s_shown));

    uint32_t last_activity = SYSTEM_BUS.activityCount;
    int64_t next_refresh = 0;
    int64_t next_touch_retry = now_ms() + TOUCH_RETRY_MS;
    bool was_touched = false;

    while (true)
    {
        bool changed = false;

        // Keep retrying if the touch controller did not answer at start-up
        if (!have_touch && now_ms() >= next_touch_retry)
        {
            have_touch = cyd_touch_init();
            next_touch_retry = now_ms() + TOUCH_RETRY_MS;
        }

        int x, y;
        bool touched = have_touch && cyd_touch_read(&x, &y);
        if (touched && !was_touched)
        {
            on_tap(x, y);
            changed = true;
        }
        was_touched = touched;

        if (s_screen == SCREEN_MESSAGE && now_ms() >= s_message_until)
        {
            s_screen = SCREEN_STATUS;
            changed = true;
        }

        if (changed || now_ms() >= next_refresh)
        {
            uint32_t activity = SYSTEM_BUS.activityCount;
            redraw_changed(activity != last_activity);
            last_activity = activity;
            next_refresh = now_ms() + UI_REFRESH_MS;
        }

        vTaskDelay(pdMS_TO_TICKS(UI_TICK_MS));
    }
}

void cyd_ui_start()
{
    // Core 0: keep the DriveWire bus loop's core free
    xTaskCreatePinnedToCore(ui_task, "cyd_ui", UI_TASK_STACK, nullptr, UI_TASK_PRIO, nullptr, 0);
}

#endif // FUJINET_CYD && BUILD_COCO

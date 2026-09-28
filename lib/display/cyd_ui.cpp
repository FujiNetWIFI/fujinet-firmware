#if defined(FUJINET_CYD) && defined(BUILD_COCO)

#include "cyd_ui.h"
#include "cyd_lcd.h"
#include "cyd_font6847.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#include "bus.h"
#include "fujiDevice.h"
#include "fnConfig.h"
#include "fnSystem.h"
#include "fnWiFi.h"

#include "../../include/debug.h"

// Every row is 24px tall (font scaled 2x vertically), giving 10 rows. Status
// lines are 1x wide (40 columns); the title is 2x wide (20 columns).
#define SCALE_Y    2
#define CELL_H     (CYD_FONT_H * SCALE_Y)
#define ROWS       (CYD_LCD_HEIGHT / CELL_H)
#define MAX_COLS   (CYD_LCD_WIDTH / CYD_FONT_W)
#define TITLE      "FUJINET COCO"

#define UI_DRIVES      4 // DriveWire drives 0-3
#define UI_PERIOD_MS   250
#define UI_TASK_PRIO   2
#define UI_TASK_STACK  4096

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

struct ui_line
{
    char text[MAX_COLS + 1];
    bool inverse;
    int sx; // horizontal scale: 1 = 40 columns, 2 = 20 columns
};

static ui_line s_shown[ROWS];

static void set_line(ui_line *lines, int row, bool inverse, int sx, const char *fmt, ...)
    __attribute__((format(printf, 5, 6)));

static void set_line(ui_line *lines, int row, bool inverse, int sx, const char *fmt, ...)
{
    char buf[64];
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

static void build_screen(ui_line *lines, bool active)
{
    for (int r = 0; r < ROWS; r++)
        set_line(lines, r, false, 1, "%s", "");

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
    set_line(lines, ROW_FOOTER, true, 1, " UP %02d:%02d:%02d",
             (int)(secs / 3600), (int)(secs / 60 % 60), (int)(secs % 60));
}

static void draw_line(int row, const ui_line &line)
{
    cyd_lcd_text(0, row * CELL_H, line.text,
                 line.inverse ? COL_BG : COL_FG,
                 line.inverse ? COL_FG : COL_BG,
                 line.sx, SCALE_Y);
}

static void ui_task(void *arg)
{
    if (!cyd_lcd_init())
    {
        vTaskDelete(nullptr);
        return;
    }

    cyd_lcd_fill(0, 0, CYD_LCD_WIDTH, CYD_LCD_HEIGHT, COL_BG);
    memset(s_shown, 0, sizeof(s_shown));

    uint32_t last_activity = SYSTEM_BUS.activityCount;
    ui_line next[ROWS];

    while (true)
    {
        uint32_t activity = SYSTEM_BUS.activityCount;
        bool active = activity != last_activity;
        last_activity = activity;

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
        vTaskDelay(pdMS_TO_TICKS(UI_PERIOD_MS));
    }
}

void cyd_ui_start()
{
    // Core 0: keep the DriveWire bus loop's core free
    xTaskCreatePinnedToCore(ui_task, "cyd_ui", UI_TASK_STACK, nullptr, UI_TASK_PRIO, nullptr, 0);
}

#endif // FUJINET_CYD && BUILD_COCO

#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <mutex>
#include <string>
#include <vector>

#include "fujiHost.h"
#include "ssd1306.h"

// Content layer on top of SSD1306: text rendering, the boot splash, and the
// runtime status view (mounted disks, sticky sector activity, a loaded ROM,
// and a transient "activity" block covering DriveWire network access,
// SD/TNFS directory browsing, and WiFi scan/connect status).
//
// The boot splash and the disk/sector status are polled from the existing
// DriveWire Fuji/bus state by a dedicated low-priority task (see poll()), so
// mounting or unmounting a disk needs no call into this class. A ROM is read
// the same way, since it's just another disk slot's media type, and takes
// over the whole screen while loaded. The activity block has no persistent
// state to poll, so show_activity()/show_browsing() are pushes from
// drivewireFuji/drivewireNetwork; like the sector-activity row, each is
// sticky -- it stays up until the next activity event overwrites it, not on
// a timer. Once the status view is reached it stays up, even once nothing is
// mounted, rather than reverting to the splash -- only reset_to_boot_logo()
// does that.
class OledDisplay
{
public:
    void start();
    void draw_text(int x, int y, const char *text, int scale = 1);
    void show_activity(const char *text);
    void show_browsing(fujiHostType type, const char *hostname, const char *dirpath);
    void reset_to_boot_logo();

private:
    enum class Mode
    {
        BOOT_LOGO,
        STATUS
    };

    void show_splash();
    void redraw_status();
    void redraw_rom_status();
    void poll();
    static void task(void *arg);

    // Renders with font7x10 instead of the scaled-up font5x7, for the fixed
    // bottom status row and the ROM view: a native mid-size font reads
    // better there than nearest-neighbor-doubled 5x7 glyphs, without costing
    // as many rows as a full 2x scale would.
    void draw_status_text(int x, int y, const char *text);

    SSD1306 _oled;
    bool _ready = false;
    Mode _mode = Mode::BOOT_LOGO;

    // One line per show_activity() call, or one per wrapped chunk from
    // show_browsing() for a long path -- as many as fit above the fixed
    // bottom row get drawn. Sticky, like _activity_lines' bottom-row
    // counterpart: stays until the next event replaces it.
    //
    // Pushed from whatever task called show_activity()/show_browsing() (the
    // FUJI command dispatch, an HTTP worker, or a console command) and read
    // from poll()'s own task - _activity_mutex guards every access.
    std::mutex _activity_mutex;
    std::vector<std::string> _activity_lines;
};

extern OledDisplay fnOledDisplay;

#endif // OLED_DISPLAY_H

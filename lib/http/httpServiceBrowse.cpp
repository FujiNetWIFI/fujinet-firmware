#include "httpServiceBrowse.h"

#include "compat_string.h"

#include "httpService.h"

#include "fnConfig.h"
#include "fujiDevice.h"
#include "utils.h"

#ifdef BUILD_ATARI
#include "sio/sioFuji.h"
#endif /* BUILD_ATARI */

#include "../../include/debug.h"

using namespace std;

namespace
{

/* Pick the glyph shown next to a directory entry.
   Matched with find() rather than a suffix test, which is what the ESP32 web UI
   has always done - a name only has to contain the extension somewhere.
*/
const char *file_glyph(const string &filename)
{
    if ( // Atari
        (filename.find(".atr") != string::npos) ||
        (filename.find(".ATR") != string::npos) ||
        (filename.find(".atx") != string::npos) ||
        (filename.find(".ATX") != string::npos) ||
        // Apple II
        (filename.find(".po") != string::npos) ||
        (filename.find(".PO") != string::npos) ||
        (filename.find(".woz") != string::npos) ||
        (filename.find(".WOZ") != string::npos) ||
        (filename.find(".hdv") != string::npos) || // Hard Disk emoji not implemented
        (filename.find(".HDV") != string::npos) || // Hard Disk emoji not implemented
        // ADAM
        (filename.find(".dsk") != string::npos) ||
        (filename.find(".DSK") != string::npos) ||
        // Commodore
        (filename.find(".prg") != string::npos) ||
        (filename.find(".PRG") != string::npos) ||
        (filename.find(".d64") != string::npos) ||
        (filename.find(".D64") != string::npos)
        )
    {
        return "&#128190; "; // floppy disk
    }

    if ( // ATARI
        (filename.find(".cas") != string::npos) ||
        (filename.find(".CAS") != string::npos) ||
        // ADAM
        (filename.find(".ddp") != string::npos) ||
        (filename.find(".DDP") != string::npos)
        )
    {
        return "&#10175; "; // cassette tape (double curly loop)
    }

    return "&#128196; "; // std document (page facing up)
}

bool is_cassette(const string &filename)
{
    return (filename.find(".cas") != string::npos) ||
           (filename.find(".CAS") != string::npos);
}

/* Describe what is currently in a device slot, e.g. "myhost :: /games/foo.atr (R)"
*/
string describe_slot(int device_slot)
{
    fujiDisk *disk = theFuji->get_disk(device_slot);

    if (disk->host_slot == INVALID_HOST_SLOT)
        return "(Empty)";

    string desc = util_html_escape(theFuji->get_host(disk->host_slot)->get_hostname());
    desc += " :: ";
    desc += util_html_escape(disk->filename);
    desc += disk->access_mode == DISK_ACCESS_MODE_WRITE ? " (W)" : " (R)";

    return desc;
}

} // anonymous namespace

bool fnHttpBrowse::render_hostdir(int host_slot, const string &path, const string &pattern,
                                  const render_opts &opts, const chunk_sink &emit)
{
    if (host_slot < 0 || host_slot >= MAX_HOSTS)
    {
        Debug_printf("render_hostdir bad host slot %d\n", host_slot);
        return false;
    }

    fujiHost *host = theFuji->get_host(host_slot);

    theFuji->populate_slots_from_config();

    // Open the directory before emitting anything, so a failure can still be
    // answered with the error page rather than a half-rendered listing.
    if (!host->mount() || !host->dir_open(path.c_str(), pattern.c_str()))
    {
        Debug_printf("render_hostdir couldn't open host %d path \"%s\"\n", host_slot, path.c_str());
        return false;
    }

    const string slot_str = to_string(host_slot);
    const string enc_path = util_url_encode(path);

    emit("        <div class=\"fileflex\">\n"
         "            <div class=\"filechild\">\n"
         "               <header>SELECT DISK TO MOUNT<span class=\"logowob\"></span>" +
         util_html_escape(host->get_hostname()) + util_html_escape(path) +
         "</header>\n"
         "               <div class=\"abortline\"><a href=\"/\">ABORT</a></div>\n"
         "               <div class=\"fileline\">\n"
         "                   <ul>\n");

    // Create link to parent
    if (!path.empty())
    {
        string parent = path.substr(0, path.find_last_of("/"));
        emit("<a href=\"/hsdir?hostslot=" + slot_str + "&path=" + util_url_encode(parent) +
             "\"><li>&#8617; Parent</li></a>");
    }

    fsdir_entry_t *f;
    while ((f = host->dir_nextfile()) != nullptr)
    {
        const string filename(f->filename);
        const string enc_full = enc_path + "%2F" + util_url_encode(filename);
        string chunk = "                          <li>";

        if (f->isDir)
        {
            chunk += "<a href=\"/hsdir?hostslot=" + slot_str + "&path=" + enc_full + "\">";
            chunk += "&#128193; "; // file folder
        }
        else
        {
            chunk += "<a href=\"/dslot?hostslot=" + slot_str + "&filename=" + enc_full + "\">";
            chunk += file_glyph(filename);
        }

        chunk += util_html_escape(filename);
        chunk += "</a>";

        if (opts.download_links && !f->isDir)
        {
            chunk += " <a class=\"filedl\" title=\"Download\" href=\"/download?hostslot=" +
                     slot_str + "&filename=" + enc_full + "\">&#11015;</a>";
        }

        chunk += "                          </li>\r\n";

        emit(chunk);
    }

    host->dir_close();

    emit("                      </ul>\r\n"
         "               </div>\n"
         "               <div class=\"abortline\"><a href=\"/\">ABORT</a></div>\n"
         "           </div>\n"
         "        </div>\n");

    return true;
}

fnHttpBrowse::slot_result fnHttpBrowse::render_slotpicker(int host_slot, const string &filename,
                                                          const chunk_sink &emit)
{
    if (host_slot < 0 || host_slot >= MAX_HOSTS)
    {
        Debug_printf("render_slotpicker bad host slot %d\n", host_slot);
        return slot_result::RENDER_ERROR;
    }

    // Cassette images always go to their own slot - no picker to show.
    if (is_cassette(filename))
        return slot_result::CASSETTE;

    const string slot_str = to_string(host_slot);
    const string enc_filename = util_url_encode(filename);

    emit("        <div class=\"fileflex\">\n"
         "            <div class=\"filechild\">\n"
         "               <header>SELECT DRIVE SLOT<span class=\"logowob\"></span>" +
         util_html_escape(theFuji->get_host(host_slot)->get_hostname()) + " :: " +
         util_html_escape(filename) +
         "</header>\n"
         "               <div class=\"abortline\"><a href=\"/\">ABORT</a></div>\n"
         "               <div class=\"fileline\">\n"
         "                      <ul>\n");

    for (int i = 0; i < MAX_DISK_DEVICES; i++)
    {
        const string ds = to_string(i);
        const string mount_url = "/mount?hostslot=" + slot_str + "&deviceslot=" + ds +
                                 "&filename=" + enc_filename + "&mode=";

        emit("<li>&#128190; <a href=\"" + mount_url + "1\">READ</a> or " +
             "<a href=\"" + mount_url + "2\">R/W</a> " +
             "<strong>" + to_string(i + 1) + "</strong>: " + describe_slot(i) + "</li>");
    }

    emit("                      </ul>\r\n"
         "               </div>\n"
         "               <div class=\"abortline\"><a href=\"/\">ABORT</a></div>\n"
         "           </div>\n"
         "        </div>\n");

    return slot_result::OK;
}

bool fnHttpBrowse::mount_file(const mount_params &params)
{
    if (params.host_slot < 0)
        fnHTTPD.addToErrMsg("<li>hostslot is empty</li>");
    else if (params.host_slot >= MAX_HOSTS)
        fnHTTPD.addToErrMsg("<li>hostslot must be between 0 and " + to_string(MAX_HOSTS - 1) + "</li>");

    if (params.device_slot < 0)
        fnHTTPD.addToErrMsg("<li>deviceslot is empty</li>");
    else if (params.device_slot >= MAX_DISK_DEVICES)
        fnHTTPD.addToErrMsg("<li>deviceslot must be between 0 and " + to_string(MAX_DISK_DEVICES - 1) + "</li>");

    if (params.mode < 0)
        fnHTTPD.addToErrMsg("<li>mode is empty</li>");
    else if (params.mode != 1 && params.mode != 2)
        fnHTTPD.addToErrMsg("<li>mode should be either 1 for read, or 2 for write.</li>");

    if (!params.filename_given)
        fnHTTPD.addToErrMsg("<li>filename is empty</li>");

    // Bail out before touching any slot - the old code carried on with whatever
    // atoi() made of the missing parameters.
    if (!fnHTTPD.errMsgEmpty())
        return false;

    if (!theFuji->get_host(params.host_slot)->mount())
    {
        fnHTTPD.addToErrMsg("<li>Could not mount host slot " + to_string(params.host_slot) + "</li>");
        return false;
    }

    disk_access_flags_t mode = params.mode == 2 ? DISK_ACCESS_MODE_WRITE : DISK_ACCESS_MODE_READ;

    fujiDisk *disk = theFuji->get_disk(params.device_slot);
    disk->host_slot = params.host_slot;
    strlcpy(disk->filename, params.filename.c_str(), sizeof(disk->filename));

    if (!theFuji->fujicore_mount_disk_image_success(params.device_slot, mode))
    {
        fnHTTPD.addToErrMsg("<li>Could not mount disk: " + util_html_escape(params.filename) + "</li>");
        return false;
    }

    Config.store_mount(params.device_slot, params.host_slot, params.filename.c_str(),
                       mode == DISK_ACCESS_MODE_WRITE ? fnConfig::mount_modes::MOUNTMODE_WRITE
                                                      : fnConfig::mount_modes::MOUNTMODE_READ);
    Config.save();
    theFuji->populate_slots_from_config(); // otherwise they don't show up in config.

    return true;
}

bool fnHttpBrowse::eject_slot(int device_slot)
{
    if (device_slot < 0)
    {
        fnHTTPD.addToErrMsg("<li>deviceslot is empty</li>");
        return false;
    }

    if (device_slot >= MAX_DISK_DEVICES)
    {
        fnHTTPD.addToErrMsg("<li>deviceslot should be between 0 and " + to_string(MAX_DISK_DEVICES - 1) + "</li>");
        return false;
    }

    // get_disk_dev() is virtual - on Apple it maps the slot to the right
    // SmartPort/Disk ][ device rather than the fujiDisk's own member.
    DISK_DEVICE *disk_dev = theFuji->get_disk_dev(device_slot);

#ifdef BUILD_APPLE
    if (disk_dev->device_active) // set disk switched only if device was previously mounted.
        disk_dev->switched = true;
#endif

    disk_dev->unmount();

#ifdef BUILD_ATARI
    if (theFuji->get_disk(device_slot)->disk_type == MEDIATYPE_CAS ||
        theFuji->get_disk(device_slot)->disk_type == MEDIATYPE_WAV)
    {
        platformFuji.cassette()->umount_cassette_file();
        platformFuji.cassette()->sio_disable_cassette();
    }
#endif

    theFuji->get_disk(device_slot)->reset();
    Config.clear_mount(device_slot);
    Config.save();
    theFuji->populate_slots_from_config(); // otherwise they don't show up in config.
    disk_dev->device_active = false;

    // Finally, scan all device slots, if all empty, and config enabled, enable the config device.
    if (Config.get_general_config_enabled())
    {
        bool all_empty = true;
        for (int i = 0; i < MAX_DISK_DEVICES; i++)
        {
            if (theFuji->get_disk(i)->host_slot != INVALID_HOST_SLOT)
            {
                all_empty = false;
                break;
            }
        }

        if (all_empty)
        {
            theFuji->boot_config = true;
#ifdef BUILD_ATARI
            theFuji->status_wait_count = 5;
#endif
            theFuji->device_active = true;
        }
    }

    return true;
}

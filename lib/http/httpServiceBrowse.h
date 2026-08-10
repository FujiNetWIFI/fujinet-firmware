/* Shared host-browsing / device-slot mounting logic for the web UI.

   The same UI is served by two different web servers: esp_http_server on the
   ESP32 (httpService.cpp) and mongoose on FujiNet-PC (mgHttpService.cpp).
   Everything below is server-agnostic - HTML is handed to the caller through a
   chunk sink, so neither server's types leak in here - and each server keeps
   only a thin adapter that parses the query and forwards the chunks.
*/

#ifndef HTTPSERVICEBROWSE_H
#define HTTPSERVICEBROWSE_H

#include <functional>
#include <string>

namespace fnHttpBrowse
{
    // Receives a piece of the response body. Callers wire this to their
    // server's chunked-send function.
    using chunk_sink = std::function<void(const std::string &)>;

    struct render_opts
    {
        // Emit a per-file download link. Only FujiNet-PC serves /download.
        bool download_links = false;
    };

    struct mount_params
    {
        int host_slot = -1;   // -1 = not supplied
        int device_slot = -1; // -1 = not supplied
        int mode = -1;        // 1 = read, 2 = write, -1 = not supplied
        std::string filename;
        bool filename_given = false;
    };

    enum class slot_result
    {
        OK,       // picker was rendered
        CASSETTE, // a cassette image - caller redirects to the cassette slot
        ERROR     // nothing was emitted; caller sends error_page.html
    };

    // Device slot a cassette image is mounted into, and the mode used for it.
    constexpr int CASSETTE_DEVICE_SLOT = 7;
    constexpr int CASSETTE_MOUNT_MODE = 1;

    /* Emit the host directory listing body (no page header/footer).
       Returns false without emitting anything if the host could not be mounted
       or the directory could not be opened; the caller then sends the error page.
    */
    bool render_hostdir(int host_slot, const std::string &path, const std::string &pattern,
                        const render_opts &opts, const chunk_sink &emit);

    /* Emit the drive-slot picker body for a chosen file (no page header/footer).
       Returns CASSETTE for cassette images, in which case nothing is emitted and
       the caller redirects to /mount for CASSETTE_DEVICE_SLOT.
    */
    slot_result render_slotpicker(int host_slot, const std::string &filename, const chunk_sink &emit);

    /* Mount a file from a host slot into a device slot, updating config.
       Returns false on failure, with details appended to fnHTTPD's error message.
    */
    bool mount_file(const mount_params &params);

    /* Unmount whatever is in a device slot and clear it from config.
       Returns false on failure, with details appended to fnHTTPD's error message.
    */
    bool eject_slot(int device_slot);
}

#endif // HTTPSERVICEBROWSE_H

/* SD card file manager for the FujiNet web server

Backs the password-protected /files pages: directory listing, download, upload,
rename and delete. Paths are always relative to the SD card root and always
begin with '/'.

Listing goes through FileSystemSDFAT::dir_open(), which hides dotfiles and the
FujiNet's own working files (paper, fnconfig.ini, rs232dump) - those are not
manageable from here.

As with the app key manager, only the HTTP plumbing differs between the ESP32
and FujiNet-PC servers, so everything else lives here.
*/
#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

// Upper bound on a posted /files/action form (delete and rename only).
#define FILEMANAGER_MAX_POST_SIZE 4096

struct FileManagerEntry
{
    std::string name;
    bool isDir = false;
    long size = 0;
};

struct UploadedFile
{
    std::string name;
    size_t bytes = 0;
};

// Streams one multipart/form-data upload straight to the SD card.
//
// feed() may be called with the whole request body or with arbitrary chunks of
// it; the part boundary is matched across calls, so nothing larger than a
// single chunk is ever held in memory. Both web servers push data through this
// same parser, in chunks, so the two platforms share one code path.
//
// Every part carrying a filename is stored, not just the first: a request can
// upload several files at once. That matters for the formats that come as a
// set - an Intellivision cartridge is a .bin plus a same-named .cfg memory map,
// and a .bin that arrives without its .cfg boots against a guessed map - so
// storing only the first file of a selection would silently break the pair.
class MultipartFileWriter
{
public:
    ~MultipartFileWriter();

    // 'content_type' is the request's Content-Type header, which carries the
    // boundary. 'dir' is the SD directory to write into.
    bool begin(const std::string &content_type, const std::string &dir, std::string &error);
    bool feed(const char *data, size_t len, std::string &error);
    bool finish(std::string &error);

    const std::vector<UploadedFile> &files() const { return _files; }
    // "a.bin (16384 bytes), a.cfg (155 bytes)" - what the page reports back.
    std::string summary() const;

private:
    enum State
    {
        WANT_PART_HEADERS, // looking for the end of a part's headers
        WANT_BODY,         // streaming the part body to disk
        DONE               // closing boundary seen, or a part we don't want
    };

    bool open_output(std::string &error);
    bool consume_headers(std::string &error);
    void flush_body();
    void close_part();

    State _state = WANT_PART_HEADERS;
    std::string _boundary;   // "--<boundary>"
    std::string _dir;
    std::string _filename;   // name of the part being written right now
    std::string _pending;    // bytes not yet classified as body or boundary
    FILE *_out = nullptr;
    size_t _part_written = 0;
    std::vector<UploadedFile> _files;
};

class FileManager
{
public:
    // Normalise a user-supplied path to an absolute SD path with no ".."
    // components. Returns false if the path escapes the root or is too long.
    static bool normalize_path(const std::string &in, std::string &out);

    // Join a directory and a leaf name, rejecting names with path separators.
    static bool join(const std::string &dir, const std::string &name, std::string &out);

    static std::vector<FileManagerEntry> list(const std::string &dir);

    // Apply one posted /files/action form. Recognised fields:
    //   action  "delete" | "rename"
    //   path    directory being viewed
    //   name    entry to act on
    //   newname new name (action=rename)
    // Returns a human-readable result message for the page to display.
    static std::string handle_action(const std::map<std::string, std::string> &postvals,
                                     std::string &redirect_path);

    // Complete HTML page for 'dir'. 'message' is shown above the listing when
    // not empty.
    static std::string render_page(const std::string &dir, const std::string &message);

    static std::string html_escape(const std::string &in);
    static std::string url_encode(const std::string &in);
};

#endif // FILEMANAGER_H

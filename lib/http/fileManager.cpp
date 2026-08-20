#include "fileManager.h"

#include <cstring>

#include "../../include/debug.h"
#include "fnFsSD.h"
#include "fnPassword.h"

#define FM_MAX_PATH 255

// ─── path helpers ────────────────────────────────────────────────────────────

bool FileManager::normalize_path(const std::string &in, std::string &out)
{
    out = "/";
    if (in.size() > FM_MAX_PATH)
        return false;

    std::vector<std::string> parts;
    size_t i = 0;
    while (i < in.size())
    {
        size_t j = in.find('/', i);
        if (j == std::string::npos)
            j = in.size();
        std::string part = in.substr(i, j - i);
        i = j + 1;

        if (part.empty() || part == ".")
            continue;
        if (part == "..")
            return false; // never let a request walk above the SD root
        if (part.find('\\') != std::string::npos)
            return false;
        parts.push_back(part);
    }

    for (const std::string &part : parts)
        out += (out == "/" ? "" : "/") + part;

    return out.size() <= FM_MAX_PATH;
}

bool FileManager::join(const std::string &dir, const std::string &name, std::string &out)
{
    if (name.empty() || name == "." || name == "..")
        return false;
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos)
        return false;

    out = dir + (dir == "/" ? "" : "/") + name;
    return out.size() <= FM_MAX_PATH;
}

std::string FileManager::html_escape(const std::string &in)
{
    std::string out;
    out.reserve(in.size());
    for (char c : in)
    {
        switch (c)
        {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&#39;"; break;
        default: out += c; break;
        }
    }
    return out;
}

std::string FileManager::url_encode(const std::string &in)
{
    static const char digits[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(in.size());
    for (unsigned char c : in)
    {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/')
        {
            out += (char)c;
        }
        else
        {
            out += '%';
            out += digits[c >> 4];
            out += digits[c & 0x0F];
        }
    }
    return out;
}

// ─── listing ─────────────────────────────────────────────────────────────────

std::vector<FileManagerEntry> FileManager::list(const std::string &dir)
{
    std::vector<FileManagerEntry> entries;

    if (!fnSDFAT.running())
        return entries;

    if (!fnSDFAT.dir_open(dir.c_str(), nullptr, 0))
    {
        Debug_printf("FileManager: could not open \"%s\"\n", dir.c_str());
        return entries;
    }

    fsdir_entry *dp;
    while ((dp = fnSDFAT.dir_read()) != nullptr)
    {
        FileManagerEntry entry;
        entry.name = dp->filename;
        entry.isDir = dp->isDir;
        entries.push_back(entry);
    }
    fnSDFAT.dir_close();

    // Ask for the sizes separately: on FujiNet-PC dir_open() stats the bare
    // filename against the SD root, so its size is wrong inside subdirectories.
    for (FileManagerEntry &entry : entries)
    {
        if (entry.isDir)
            continue;
        std::string full;
        if (join(dir, entry.name, full))
        {
            long size = fnSDFAT.filesize(full.c_str());
            entry.size = (size > 0) ? size : 0;
        }
    }

    return entries;
}

// ─── delete / rename ─────────────────────────────────────────────────────────

std::string FileManager::handle_action(const std::map<std::string, std::string> &postvals,
                                       std::string &redirect_path)
{
    auto field = [&postvals](const char *name) -> std::string {
        auto it = postvals.find(name);
        return (it != postvals.end()) ? it->second : std::string();
    };

    std::string dir;
    if (!normalize_path(field("path"), dir))
        return "Invalid path.";
    redirect_path = dir;

    if (!fnSDFAT.running())
        return "No SD card is mounted.";

    std::string name = field("name");
    std::string full;
    if (!join(dir, name, full))
        return "Invalid file name.";

    std::string action = field("action");

    if (action == "delete")
    {
        if (fnSDFAT.is_dir(full.c_str()))
        {
            if (!fnSDFAT.rmdir(full.c_str()))
                return "Could not delete folder " + html_escape(name) + " - it may not be empty.";
            return "Deleted folder " + html_escape(name) + ".";
        }
        if (!fnSDFAT.remove(full.c_str()))
            return "Could not delete " + html_escape(name) + ".";
        return "Deleted " + html_escape(name) + ".";
    }

    if (action == "mkdir")
    {
        if (fnSDFAT.exists(full.c_str()))
            return html_escape(name) + " already exists.";
        if (!fnSDFAT.mkdir(full.c_str()))
            return "Could not create folder " + html_escape(name) + ".";
        return "Created folder " + html_escape(name) + ".";
    }

    if (action == "rename")
    {
        std::string newname = field("newname");
        std::string newfull;
        if (!join(dir, newname, newfull))
            return "Invalid new name.";
        if (newfull == full)
            return "";
        if (fnSDFAT.exists(newfull.c_str()))
            return html_escape(newname) + " already exists.";
        if (!fnSDFAT.rename(full.c_str(), newfull.c_str()))
            return "Could not rename " + html_escape(name) + ".";
        return "Renamed " + html_escape(name) + " to " + html_escape(newname) + ".";
    }

    return "Unknown action.";
}

// ─── multipart upload ────────────────────────────────────────────────────────

MultipartFileWriter::~MultipartFileWriter()
{
    if (_out != nullptr)
        fclose(_out);
}

bool MultipartFileWriter::begin(const std::string &content_type, const std::string &dir,
                                std::string &error)
{
    size_t pos = content_type.find("boundary=");
    if (pos == std::string::npos)
    {
        error = "Upload is missing its multipart boundary";
        return false;
    }

    std::string boundary = content_type.substr(pos + 9);
    // Trim a quoted boundary and anything following it
    if (!boundary.empty() && boundary[0] == '"')
    {
        size_t end = boundary.find('"', 1);
        boundary = boundary.substr(1, end == std::string::npos ? std::string::npos : end - 1);
    }
    else
    {
        size_t end = boundary.find_first_of("; \t\r\n");
        if (end != std::string::npos)
            boundary = boundary.substr(0, end);
    }

    if (boundary.empty())
    {
        error = "Upload has an empty multipart boundary";
        return false;
    }

    _boundary = "--" + boundary;
    _dir = dir;
    _state = WANT_PART_HEADERS;
    return true;
}

bool MultipartFileWriter::open_output(std::string &error)
{
    std::string full;
    if (!FileManager::join(_dir, _filename, full))
    {
        error = "Invalid upload file name";
        return false;
    }

    _out = fnSDFAT.file_open(full.c_str(), FILE_WRITE);
    if (_out == nullptr)
    {
        error = "Could not create " + FileManager::html_escape(_filename);
        return false;
    }
    return true;
}

/* Consume complete part headers out of _pending. Returns false only on error;
   when the headers are not yet complete the state is left unchanged. */
bool MultipartFileWriter::consume_headers(std::string &error)
{
    size_t end = _pending.find("\r\n\r\n");
    if (end == std::string::npos)
        return true; // need more data

    std::string headers = _pending.substr(0, end);
    _pending.erase(0, end + 4);

    // Every part carrying a filename is written out; any other field (a stray
    // text input, say) is skipped.
    size_t fn = headers.find("filename=\"");
    if (fn == std::string::npos)
    {
        // Skip to the next boundary without writing anything
        _state = WANT_BODY;
        return true;
    }

    fn += 10;
    size_t fend = headers.find('"', fn);
    if (fend == std::string::npos)
    {
        error = "Malformed upload headers";
        return false;
    }

    std::string name = headers.substr(fn, fend - fn);
    // Browsers may send a full path; keep only the leaf
    size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos)
        name = name.substr(slash + 1);

    if (name.empty())
    {
        error = "No file was selected";
        return false;
    }

    _filename = name;
    _part_written = 0;
    if (!open_output(error))
        return false;

    _state = WANT_BODY;
    return true;
}

/* Finish the part currently being written, if any, and record what it stored.
   Safe to call more than once for the same part: _out is the "still open"
   flag, so a re-entry after a "need more data" return does nothing. */
void MultipartFileWriter::close_part()
{
    if (_out == nullptr)
        return;

    fclose(_out);
    _out = nullptr;

    UploadedFile stored;
    stored.name = _filename;
    stored.bytes = _part_written;
    _files.push_back(stored);
}

/* Write everything in _pending that cannot be the start of a boundary. */
void MultipartFileWriter::flush_body()
{
    // Keep back enough bytes that a boundary split across chunks is still found:
    // "\r\n" + boundary + "--"
    size_t keep = _boundary.size() + 4;
    if (_pending.size() <= keep)
        return;

    size_t writable = _pending.size() - keep;
    if (_out != nullptr)
    {
        size_t n = fwrite(_pending.data(), 1, writable, _out);
        _part_written += n;
    }
    _pending.erase(0, writable);
}

bool MultipartFileWriter::feed(const char *data, size_t len, std::string &error)
{
    if (_state == DONE)
        return true;

    _pending.append(data, len);

    for (;;)
    {
        if (_state == WANT_PART_HEADERS)
        {
            size_t before = _pending.size();
            if (!consume_headers(error))
                return false;
            if (_state == WANT_PART_HEADERS || _pending.size() == before)
                return true; // need more data
            continue;
        }

        if (_state != WANT_BODY)
            return true;

        size_t at = _pending.find("\r\n" + _boundary);
        if (at == std::string::npos)
        {
            flush_body();
            return true;
        }

        // Everything before the boundary is the last of this part's content.
        if (_out != nullptr)
        {
            size_t n = fwrite(_pending.data(), 1, at, _out);
            _part_written += n;
            close_part();
        }
        if (at > 0)
            _pending.erase(0, at); // _pending now starts at the boundary

        // Two more bytes decide whether this is the closing boundary
        size_t after = 2 + _boundary.size();
        if (_pending.size() < after + 2)
            return true;

        if (_pending.compare(after, 2, "--") == 0)
        {
            _state = DONE;
            return true;
        }
        if (_pending.compare(after, 2, "\r\n") != 0)
        {
            error = "Malformed upload boundary";
            return false;
        }

        // Another part follows
        _pending.erase(0, after + 2);
        _state = WANT_PART_HEADERS;
    }
}

bool MultipartFileWriter::finish(std::string &error)
{
    // A part still open here never reached its closing boundary, so the upload
    // was cut short: record it anyway (the bytes are on the card) and let the
    // _state check below report the failure.
    close_part();

    if (_files.empty())
    {
        error = "No file was selected";
        return false;
    }
    if (_state != DONE)
    {
        error = "Upload ended unexpectedly";
        return false;
    }
    return true;
}

std::string MultipartFileWriter::summary() const
{
    std::string out;
    for (const UploadedFile &file : _files)
    {
        if (!out.empty())
            out += ", ";
        out += FileManager::html_escape(file.name) + " (" +
               std::to_string(file.bytes) + " bytes)";
    }
    return out;
}

// ─── page ────────────────────────────────────────────────────────────────────

/* Escape a name for use inside a single-quoted JS string that itself sits in an
   HTML attribute: backslash-escape first, then HTML-escape, so the HTML parser
   hands the JS parser a correctly quoted literal. */
static std::string js_attr_string(const std::string &in)
{
    std::string js;
    js.reserve(in.size());
    for (char c : in)
    {
        if (c == '\\' || c == '\'')
            js += '\\';
        js += c;
    }
    return FileManager::html_escape(js);
}

/* Escape a name for a single-quoted JS string inside a <script> element. Script
   content is raw text, so it must NOT be HTML-escaped; '<' is escaped instead so
   a filename can never close the element early. */
static std::string js_script_string(const std::string &in)
{
    std::string out;
    out.reserve(in.size());
    for (char c : in)
    {
        switch (c)
        {
        case '\\': out += "\\\\"; break;
        case '\'': out += "\\'"; break;
        case '<': out += "\\x3C"; break;
        case '\r': out += "\\r"; break;
        case '\n': out += "\\n"; break;
        default: out += c; break;
        }
    }
    return out;
}

static std::string format_size(long size)
{
    char buf[32];
    if (size < 1024)
        snprintf(buf, sizeof(buf), "%ld B", size);
    else if (size < 1024 * 1024)
        snprintf(buf, sizeof(buf), "%.1f KB", size / 1024.0);
    else
        snprintf(buf, sizeof(buf), "%.1f MB", size / (1024.0 * 1024.0));
    return std::string(buf);
}

std::string FileManager::render_page(const std::string &dir, const std::string &message)
{
    std::string page =
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
        "<title>FujiNet - File Manager</title>"
        "<link rel=\"stylesheet\" href=\"/file?css/core.css\">"
        "<style>"
        ".fmmsg{margin:0.5rem;padding:0.5rem;background:#eee;border:1px solid #999;}"
        ".fmcrumbs{padding:0.5rem;font-family:Inconsolata,monospace;}"
        ".fmtable{border-collapse:collapse;width:100%;}"
        ".fmtable th,.fmtable td{border-bottom:1px solid #ccc;padding:0.25rem 0.5rem;text-align:left;}"
        ".fmtable th{background:#ccc;}"
        ".fmtable td.name{font-family:Inconsolata,monospace;}"
        ".fmtable td.size{text-align:right;width:7rem;white-space:nowrap;}"
        ".fmtable td.acts{text-align:right;width:16rem;white-space:nowrap;}"
        ".fmbutton{border:1px solid rgba(0,0,0,0.2);height:2rem;line-height:1.9rem;"
        "border-radius:0.5rem;background:#999;color:#fff;padding:0 0.6rem;margin-left:0.25rem;"
        "display:inline-block;text-decoration:none;vertical-align:middle;}"
        ".fmbutton svg{width:1rem;height:1rem;fill:#fff;vertical-align:-0.15rem;}"
        ".fmbutton .ico{display:none;}"
        ".fmupload{padding:0.75rem 0.5rem;}"
        ".fmnote{padding:0 0.5rem 0.5rem;font-size:small;}"
        ".fmdrag .module{outline:2px dashed #999;outline-offset:-4px;}"
        "@media only screen and (max-width:600px){"
        ".fmbutton .lbl{display:none;}"
        ".fmbutton .ico{display:inline;}"
        ".fmbutton{padding:0 0.5rem;margin-left:0.15rem;}"
        ".fmtable td.acts{width:8rem;}"
        "}"
        "</style></head><body><div class=\"wrapper\">"
        "<svg style=\"display:none\" xmlns=\"http://www.w3.org/2000/svg\">"
        "<symbol id=\"ic-dl\" viewBox=\"0 0 24 24\">"
        "<path d=\"M11 3h2v9l3.5-3.5 1.4 1.4L12 16 6.1 9.9l1.4-1.4L11 12z\"/>"
        "<path d=\"M4 18h16v2H4z\"/></symbol>"
        "<symbol id=\"ic-ren\" viewBox=\"0 0 24 24\">"
        "<path d=\"M3 17.2V21h3.8L18 9.8 14.2 6zM20.7 7.3a1 1 0 0 0 0-1.4l-2.6-2.6a1 1 0 0 0-1.4 0"
        "l-1.8 1.8L18.9 9z\"/></symbol>"
        "<symbol id=\"ic-del\" viewBox=\"0 0 24 24\">"
        "<path d=\"M6 7h12l-1 14H7zM9 4h6l1 2H8z\"/></symbol>"
        "</svg>"
        "<div class=\"headerlogo\"><a href=\"/\"><img src=\"/file?logo.svg\" alt=\"FujiNet\""
        " style=\"width:100%\"></a></div>"
        "<div class=\"module-container\"><div class=\"module\">"
        "<header class=\"module-header\">FILE<span class=\"logowob\"></span>MANAGER</header>";

    if (fnPassword.is_set())
        page += "<form method=\"post\" action=\"/logout\" style=\"margin:0.5rem;\">"
                "<button type=\"submit\" class=\"fmbutton\">Log Out</button>"
                "</form>";

    if (!message.empty())
        page += "<div class=\"fmmsg\">" + message + "</div>";

    if (!fnSDFAT.running())
    {
        page += "<div class=\"fmnote\">No SD card is mounted.</div>"
                "</div></div></div></body></html>";
        return page;
    }

    // Breadcrumbs: "/" followed by one link per path component
    page += "<div class=\"fmcrumbs\"><a href=\"/files?path=%2F\">SD</a>";
    {
        std::string walk;
        size_t i = 1; // skip the leading '/'
        while (i <= dir.size())
        {
            size_t j = dir.find('/', i);
            if (j == std::string::npos)
                j = dir.size();
            if (j > i)
            {
                std::string part = dir.substr(i, j - i);
                walk += "/" + part;
                page += " / <a href=\"/files?path=" + url_encode(walk) + "\">" +
                        html_escape(part) + "</a>";
            }
            i = j + 1;
        }
    }
    page += "</div>";

    std::vector<FileManagerEntry> entries = list(dir);

    page += "<table class=\"fmtable\"><tr><th>Name</th><th class=\"size\">Size</th>"
            "<th class=\"acts\"></th></tr>";

    if (dir != "/")
    {
        std::string parent = dir.substr(0, dir.find_last_of('/'));
        if (parent.empty())
            parent = "/";
        page += "<tr><td class=\"name\"><a href=\"/files?path=" + url_encode(parent) +
                "\">../</a></td><td class=\"size\"></td><td class=\"acts\"></td></tr>";
    }

    for (const FileManagerEntry &entry : entries)
    {
        std::string full;
        if (!join(dir, entry.name, full))
            continue;

        std::string esc = html_escape(entry.name);
        page += "<tr><td class=\"name\">";
        if (entry.isDir)
            page += "<a href=\"/files?path=" + url_encode(full) + "\">" + esc + "/</a>";
        else
            page += esc;
        page += "</td>";

        page += "<td class=\"size\">" + (entry.isDir ? std::string() : format_size(entry.size)) +
                "</td><td class=\"acts\">";

        if (!entry.isDir)
        {
            page += "<a class=\"fmbutton\" href=\"/files/download?path=" + url_encode(full) +
                    "\" download><span class=\"lbl\">Download</span>"
                    "<span class=\"ico\"><svg><use href=\"#ic-dl\"/></svg></span></a>";
        }

        // Rename and delete post the row's name back through a shared form
        const std::string jsname = js_attr_string(entry.name);
        page += "<button class=\"fmbutton\" type=\"button\" onclick=\"fmRename('" + jsname +
                "')\"><span class=\"lbl\">Rename</span>"
                "<span class=\"ico\"><svg><use href=\"#ic-ren\"/></svg></span></button>";
        page += "<button class=\"fmbutton\" type=\"button\" onclick=\"fmDelete('" + jsname +
                "')\"><span class=\"lbl\">Delete</span>"
                "<span class=\"ico\"><svg><use href=\"#ic-del\"/></svg></span></button>";

        page += "</td></tr>";
    }

    if (entries.empty() && dir == "/")
        page += "<tr><td class=\"name\" colspan=\"3\">This folder is empty.</td></tr>";

    page += "</table>";

    const std::string enc_dir = url_encode(dir);

    page += "<div class=\"fmupload\">"
            "<button class=\"fmbutton\" type=\"button\" onclick=\"fmNewDir()\">New Dir</button>"
            "</div>";

    page += "<form id=\"fmupform\" class=\"fmupload\" method=\"post\" enctype=\"multipart/form-data\""
            " action=\"/files/upload?path=" + enc_dir + "\""
            " onsubmit=\"return fmCheckUpload(this)\">"
            "<input id=\"fmupfile\" type=\"file\" name=\"file\" multiple required> "
            "<button class=\"fmbutton\" type=\"submit\">Upload</button></form>";

    // Hidden form used by the rename and delete buttons
    page += "<form id=\"fmaction\" method=\"post\" action=\"/files/action\">"
            "<input type=\"hidden\" name=\"action\" id=\"fmaction-action\">"
            "<input type=\"hidden\" name=\"path\" value=\"" + html_escape(dir) + "\">"
            "<input type=\"hidden\" name=\"name\" id=\"fmaction-name\">"
            "<input type=\"hidden\" name=\"newname\" id=\"fmaction-newname\">"
            "</form>";

    page += "<div class=\"fmnote\">Uploads go into the folder shown above; several files can be "
            "sent at once (a cartridge and its .cfg, say). Dotfiles and the "
            "FujiNet's own working files are not listed.</div>";

    // Names already in this folder, so an upload that would replace one can ask first
    page += "<script>var fmExisting=[";
    {
        bool first = true;
        for (const FileManagerEntry &entry : entries)
        {
            if (entry.isDir)
                continue;
            if (!first)
                page += ",";
            page += "'" + js_script_string(entry.name) + "'";
            first = false;
        }
    }
    page += "];"
            "function fmCheckUpload(form){"
            "var input=form.querySelector('input[type=file]');"
            "if(!input||!input.files||!input.files.length)return true;"
            "var clash=[];"
            "for(var i=0;i<input.files.length;i++){"
            // File.name is always the bare filename, never a path, so it must
            // not be split on separators - names may legitimately contain them
            "var name=input.files[i].name;"
            "var lower=name.toLowerCase();"
            "for(var j=0;j<fmExisting.length;j++){"
            "if(fmExisting[j].toLowerCase()===lower){clash.push(name);break;}}}"
            "if(clash.length)"
            "return confirm((clash.length>1?'These files already exist here: ':"
            "'A file named ')+clash.join(', ')+"
            "(clash.length>1?'. Replace them?':' already exists here. Replace it?'));"
            "return true;}"
            "</script>";

    page += "<script>"
            "(function(){"
            "var f=document.getElementById('fmupform');"
            "if(!f)return;"
            "var input=f.querySelector('input[type=file]');"
            "var cnt=0;"
            "document.addEventListener('dragenter',function(e){"
            "cnt++;"
            "e.preventDefault();"
            "e.dataTransfer.dropEffect='copy';"
            "document.body.classList.add('fmdrag');"
            "});"
            "document.addEventListener('dragover',function(e){"
            "e.preventDefault();"
            "e.dataTransfer.dropEffect='copy';"
            "});"
            "document.addEventListener('dragleave',function(e){"
            "cnt--;"
            "if(cnt<=0){"
            "cnt=0;"
            "document.body.classList.remove('fmdrag');"
            "}"
            "});"
            "document.addEventListener('drop',function(e){"
            "e.preventDefault();"
            "cnt=0;"
            "document.body.classList.remove('fmdrag');"
            "if(!e.dataTransfer||!e.dataTransfer.files||!e.dataTransfer.files.length)return;"
            // Everything dropped goes up, so dragging a cartridge and its .cfg
            // together sends both rather than silently keeping only the first
            "try{"
            "var dt=new DataTransfer();"
            "for(var i=0;i<e.dataTransfer.files.length;i++)"
            "dt.items.add(e.dataTransfer.files[i]);"
            "input.files=dt.files;"
            "}catch(err){"
            "input.files=e.dataTransfer.files;"
            "}"
            "if(fmCheckUpload(f))f.submit();"
            "});"
            "})();"
            "</script>";

    page += "<script>"
            "function fmSubmit(action,name,newname){"
            "document.getElementById('fmaction-action').value=action;"
            "document.getElementById('fmaction-name').value=name;"
            "document.getElementById('fmaction-newname').value=newname||'';"
            "document.getElementById('fmaction').submit();}"
            "function fmDelete(name){"
            "if(confirm('Delete '+name+'?'))fmSubmit('delete',name);}"
            "function fmRename(name){"
            "var n=prompt('New name for '+name,name);"
            "if(n&&n!==name)fmSubmit('rename',name,n);}"
            "function fmNewDir(){"
            "var n=prompt('Name for the new folder','');"
            "if(n)fmSubmit('mkdir',n);}"
            "</script>";

    page += "</div></div></div></body></html>";
    return page;
}

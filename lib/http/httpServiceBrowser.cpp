#ifndef ESP_PLATFORM

#include "compat_string.h"

#include "fujiDevice.h"
#include "fnFsSD.h"
#include "fnFsTNFS.h"
#include "fnFsSMB.h"
#include "fnFsFTP.h"
#include "fnFsHTTP.h"
#include "fnTaskManager.h"
#include "fnConfig.h"
#include "fnio.h"

#include "httpServiceBrowser.h"
#include "httpService.h"

#include "debug.h"


class fnHttpSendFileTask : public fnTask
{
public:
    fnHttpSendFileTask(FileSystem *fs, fnFile *fh, mg_connection *c);
protected:
    virtual int start() override;
    virtual int abort() override;
    virtual int step() override;
private:
    char buf[FNWS_SEND_BUFF_SIZE];
    FileSystem * _fs;
    fnFile * _fh;
    mg_connection * _c;
    size_t _filesize;
    size_t _total;
};

fnHttpSendFileTask::fnHttpSendFileTask(FileSystem *fs, fnFile *fh, mg_connection *c)
{
    _fs = fs;
    _fh = fh;
    _c = c;
    _filesize = 0;
    _total = 0;
}

int fnHttpSendFileTask::start()
{
    _filesize = _fs->filesize(_fh);
    Debug_printf("fnHttpSendFileTask started #%d\n", _id);
    return 0;
}

int fnHttpSendFileTask::abort()
{
    _c->is_draining = 1;
    fnio::fclose(_fh); // close (and delete _fh)
    delete _fs; // delete temporary FileSystem
    Debug_printf("fnHttpSendFileTask aborted #%d\n", _id);
    return 0;
}

int fnHttpSendFileTask::step()
{
    Debug_printf("fnHttpSendFileTask::step #%d\n", _id);

    // Send the file content out in chunks
    size_t count = 0;
    count = fnio::fread((uint8_t *)buf, 1, FNWS_SEND_BUFF_SIZE, _fh);
    _total += count;
    mg_send(_c, buf, count);

    if (count)
        return 0; // continue

    // done
    _c->is_resp = 0;
    fnio::fclose(_fh); // close (and delete _fh)
    delete _fs;  // delete temporary FileSystem
    Debug_printf("Sent %lu of %lu bytes\n", (unsigned long)_total, (unsigned long)_filesize);

    return 1; // task has completed
}

int fnHttpServiceBrowser::browse_url_encode(const char *src, size_t src_len, char *dst, size_t dst_len)
{
    static const char hex[] = "0123456789abcdef";
    size_t i, j;
    for (i = j = 0; i < src_len && j + 1 < dst_len; i++, j++)
    {
        if ((src[i] < 'A' || src[i] > 'Z') &&
            (src[i] < 'a' || src[i] > 'z') &&
            (src[i] < '0' || src[i] > '9') &&
            src[i] != '-' && src[i] != '.' && src[i] != '_' && src[i] != '~')
        {
            if (j + 3 < dst_len)
            {
                dst[j++] = '%';
                dst[j++] = hex[src[i] >> 4];
                dst[j] = hex[src[i] & 0x0f];
            } else
            {
                return -1;
            }
        } else
        {
            dst[j] = src[i];
        }
    }
    if (j < dst_len) dst[j] = '\0';  // Null-terminate the destination
    return i >= src_len ? (int) j : -1;
}


int fnHttpServiceBrowser::browse_html_escape(const char *src, size_t src_len, char *dst, size_t dst_len)
{
    size_t i, j;
    for (i = j = 0; i < src_len && j + 1 < dst_len; i++, j++)
    {
        if (src[i] == '<' || src[i] == '>' || src[i] == '&' || src[i] == '\'' || src[i] == '"')
        {
            // replace above with &#NN; note: above character codes are < 100
            if (j + 5 < dst_len)
            {
                dst[j++] = '&';
                dst[j++] = '#';
                dst[j++] = '0' + src[i] / 10;
                dst[j++] = '0' + src[i] % 10;
                dst[j] = ';';
            } else
            {
                return -1;
            }
        } else
        {
            dst[j] = src[i];
        }
    }
    if (j < dst_len) dst[j] = '\0';  // Null-terminate the destination
    return i >= src_len ? (int) j : -1;
}

int fnHttpServiceBrowser::validate_path(const char *path, size_t path_len)
{
    char tokenized[path_len+1];
    char *segment;
    strlcpy(tokenized, path, sizeof(tokenized));
    segment = strtok(tokenized, "/");
    while (segment != NULL)
    {
        if (strcmp(segment, "..") == 0)
        {
            return -1;
        }
        segment = strtok(NULL, "/");
    }
    return 0;
}

int fnHttpServiceBrowser::browse_listdir(mg_connection *c, mg_http_message *hm, FileSystem *fs, int slot, const char *host_path, unsigned pathlen)
{
    char path[256];
    char enc_path[1024]; // URL encoded path
    char esc_path[1024]; // HTML escaped path

    if (pathlen > 0)
    {
        // trim trailing slash(es)
        while (pathlen > 1 && host_path[pathlen-1] == '/') --pathlen;
        // decode host_path to path
        if ((pathlen >= sizeof(enc_path)) || (mg_url_decode(host_path, pathlen, path, sizeof(path), 0) < 0))
        {
            mg_http_reply(c, 403, "", "Path too long\n");
            return -1;
        }
        else
        {
            // enc_path =  host_path + '\0'
            strlcpy(enc_path, host_path, pathlen+1);
        }
        if (validate_path(path, strlen(path)) < 0)
        {
            mg_http_reply(c, 403, "", "Path is invalid\n");
            return -1;
        }
    }
    else
    {
        // root dir
        strcpy(path, "/");
        strcpy(enc_path, "/");
    }

    // HTML escape the path
    if (browse_html_escape(path, strlen(path), esc_path, sizeof(esc_path)) < 0)
    {
        strcpy(esc_path, "&lt;-- Path too long --&gt;");
    }

    // get "mount" query variable
    char action[10] = "";
    mg_http_get_var(&hm->query, "action", action, sizeof(action));

    if (action[0] != 0)
    {
        // get "slot" and "mode" query variables
        char slot_str[3] = "", mode_str[3] = "";
        mg_http_get_var(&hm->query, "slot", slot_str, sizeof(slot_str));
        mg_http_get_var(&hm->query, "mode", mode_str, sizeof(mode_str));

        int drive_slot = \
            (slot_str[0] >= '1' && slot_str[0] <= '8' && slot_str[1] == '\0') \
            ? drive_slot = slot_str[0] - '1' : -1;

        fnConfig::mount_mode_t mount_mode = (mode_str[0] == 'w' && mode_str[1] == '\0') \
            ? fnConfig::MOUNTMODE_WRITE : fnConfig::MOUNTMODE_READ;

        if (strcmp(action, "newmount") == 0)
        {
            // mount image to drive slot
            if (drive_slot >=0 && drive_slot < MAX_DISK_DEVICES)
            {
                // update config
                Config.store_mount(drive_slot, slot, path, mount_mode);
                Config.save();

#ifdef BUILD_ATARI // OS
#warning "Why does only Atari need to unmount a disk?"
                // umount current image, if any - close image file, reset drive slot
                theFuji->fujicore_unmount_disk_image_success(drive_slot);
#endif

                // update drive slot
                fujiDisk &fnDisk = *theFuji->get_disk(drive_slot);
                fnDisk.host_slot = slot;
                fnDisk.access_mode = (mount_mode == fnConfig::MOUNTMODE_WRITE) ? DISK_ACCESS_MODE_WRITE : DISK_ACCESS_MODE_READ;
                strlcpy(fnDisk.filename, path, sizeof(fnDisk.filename));

#ifdef BUILD_ATARI // OS
#warning "Why does only Atari need to mount the host and disk?"
                // mount host (file system)
                if (theFuji->fujicore_mount_host_success(slot))
                {
                    // mount disk image
                    theFuji->fujicore_mount_disk_image_success(drive_slot, 0);
                }
#endif
            }
        }
        else if (strcmp(action, "mount") == 0)
        {
            if (drive_slot >=0 && drive_slot < MAX_DISK_DEVICES)
            {
#ifdef BUILD_ATARI // OS
#warning "Why does only Atari need to mount the host and disk?"
                // mount host (file system)
                if (theFuji->fujicore_mount_host_success(slot))
                {
                    // mount disk image
                    theFuji->fujicore_mount_disk_image_success(drive_slot, 0);
                }
#endif
            }
        }
        else if (strcmp(action, "eject") == 0)
        {
            // umount image from drive slot
            if (drive_slot >=0 && drive_slot < MAX_DISK_DEVICES)
            {
                Config.clear_mount(drive_slot);
                Config.save();
#ifdef BUILD_ATARI // OS
#warning "Why does only Atari need to unmount a disk?"
                theFuji->fujicore_unmount_disk_image_success(drive_slot);
#endif
                // Finally, scan all device slots, if all empty, and config enabled, enable the config device.
                if (Config.get_general_config_enabled())
                {
                    if ((theFuji->get_disk(0)->host_slot == 0xFF) &&
                        (theFuji->get_disk(1)->host_slot == 0xFF) &&
                        (theFuji->get_disk(2)->host_slot == 0xFF) &&
                        (theFuji->get_disk(3)->host_slot == 0xFF) &&
                        (theFuji->get_disk(4)->host_slot == 0xFF) &&
                        (theFuji->get_disk(5)->host_slot == 0xFF) &&
                        (theFuji->get_disk(6)->host_slot == 0xFF) &&
                        (theFuji->get_disk(7)->host_slot == 0xFF))
                    {
                        theFuji->boot_config = true;
            #ifdef BUILD_ATARI
                        theFuji->status_wait_count = 5;
            #endif
                        theFuji->device_active = true;
                    }
                }
            }
        }
        else if (strcmp(action, "download") == 0)
        {
            fnFile *fh = fs->fnfile_open(path);
            if (fh != nullptr)
            {
                // file download
                return browse_sendfile(c, fs, fh, fnHttpService::get_basename(path), fs->filesize(fh));
            }
            else
            {
                Debug_printf("Couldn't open host file: %s\n", path);
                mg_http_reply(c, 400, "", "Failed to open file.\n");
                return -1;
            }
        }
        // action "slotlist" goes here
        return browse_listdrives(c, slot, esc_path, enc_path);
    }

    // no special action -> entering sub-directory
    if (!fs->dir_open(path, "", 0))
    {
        Debug_printf("Couldn't open host directory: %s\n", path);
        mg_http_reply(c, 400, "", "Failed to open directory.\n");
        return -1;
    }

    mg_printf(c, "%s\r\n", "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nTransfer-Encoding: chunked\r\n");
    print_head(c, slot);
    print_navi(c, slot, esc_path, enc_path);
    mg_http_printf_chunk(
        c,
        "<table cellpadding=\"0\"><thead>"
        "<tr><th>Size</th><th>Modified</th><th>Name</th></tr>"
        "<tr><td colspan=\"3\"><hr></td></tr></thead><tbody>\r\n");

    // list directory
    fsdir_entry *dp;
    while ((dp = fs->dir_read()) != nullptr)
    {
        // Debug_printf("%d %s\t%d\t%lu\n", dp->isDir, dp->filename, (int)dp->size, (unsigned long)dp->modified_time);
        // Do not show current dir and hidden files
        if (!strcmp(dp->filename, ".") || !strcmp(dp->filename, ".."))
            continue;
        print_dentry(c, dp, slot, enc_path);
    }
    fs->dir_close();

    mg_http_printf_chunk(
        c,
        "</tbody><tfoot><tr><td colspan=\"3\"><hr></td></tr></tfoot>"
        "</table></body></html>");
    mg_http_write_chunk(c, "", 0);

    return 0;
}

int fnHttpServiceBrowser::browse_listdrives(mg_connection *c, int slot, const char *esc_path, const char *enc_path)
{
    mg_printf(c, "%s\r\n", "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nTransfer-Encoding: chunked\r\n");
    print_head(c, slot);
    print_navi(c, slot, esc_path, enc_path, true);
    mg_http_printf_chunk(
        c,
        "<table cellpadding=\"0\"><thead>"
        "<tr><th>Slot</th><th>Action</th><th>Current disk image (Mode)</th></tr>"
        "<tr><td colspan=\"3\"><hr></td></tr></thead><tbody>\r\n");

    // list drive slots
    char disk_id;
    char slot_disk[10]; // "(Dn:)"
    int host_slot;
    bool is_mounted;
    for(int drive_slot = 0; drive_slot < MAX_DISK_DEVICES; drive_slot++)
    {
        disk_id = (char) theFuji->get_disk_id(drive_slot);
        // "(Dn:)" if any rotation has occurred
        if (disk_id != (char) (0x31 + drive_slot))
            snprintf(slot_disk, sizeof slot_disk, " (D%c:)", disk_id);
        else
            *slot_disk = '\0';
        host_slot = Config.get_mount_host_slot(drive_slot);
        is_mounted = (theFuji->get_disk(drive_slot)->fileh != nullptr);
        mg_http_printf_chunk(c, "<tr>"
                "<td>Drive Slot %d%s</td>"
                "<td><a title=\"Mount Read-Only\" href=\"?action=newmount&slot=%d&mode=r\">[ R ]</a>"
                "<a title=\"Mount Read-Write\" href=\"?action=newmount&slot=%d&mode=w\">[ W ]</a> "
                "<a title=\"%s\" href=\"?action=%s&slot=%d\">[ %s ]</a></td>"
                "<td>%s (%s)</td>"
            "</tr>\r\n",
            drive_slot+1, slot_disk,
            // action=newmount&slot=..
            drive_slot+1, drive_slot+1,
            (host_slot == HOST_SLOT_INVALID) ? "Nothing to do" :
                is_mounted ? "Eject current image" : "Mount current image",
            (host_slot == HOST_SLOT_INVALID) ? "none" : is_mounted ? "eject" : "mount",
            drive_slot+1,
            (host_slot == HOST_SLOT_INVALID) ? "-" : is_mounted ? "E" : "M",
            // From what host is each disk mounted on and what disk is mounted - TODO escape host and path
            (host_slot == HOST_SLOT_INVALID) ? "" :
                (Config.get_host_name(host_slot) + " :: "+ Config.get_mount_path(drive_slot)).c_str(),
            // Mount mode: R / W or "Empty" for empty slot
            (host_slot == HOST_SLOT_INVALID) ? "Empty" :
                Config.get_mount_mode(drive_slot) == fnConfig::mount_modes::MOUNTMODE_READ ?
                    (is_mounted ? "R" : "R-") : (is_mounted ? "W" : "W-")
        );
    }

    mg_http_printf_chunk(
        c,
        "</tbody><tfoot><tr><td colspan=\"3\"><hr></td></tr></tfoot>"
        "</table></body></html>");
    mg_http_write_chunk(c, "", 0);
    return 0;
}


void fnHttpServiceBrowser::print_head(mg_connection *c, int slot)
{
    mg_http_printf_chunk(
        c,
        "<!DOCTYPE html><html><head><title>FujiNet - %s : Host %d</title>"
        "<style>th,td {text-align: left; padding-right: 1em; "
        "font-family: monospace; font-size: 14px;} "
        "a {color: black; text-decoration: none; border-radius: .2em; padding: .1em;} "
        "a:hover {background: gold; transition: background-color .4s;}"
        "</style></head>"
        "<body><h1><a href=\"/\" title=\"Back to Config\">&#215;</a> Host %d</h1>",
        Config.get_general_label().c_str() ,slot+1, slot+1);
        // &#129128; &#129120; - nice wide leftwards arrow but not working in Safari
}

void fnHttpServiceBrowser::print_navi(mg_connection *c, int slot, const char *esc_path, const char*enc_path, bool download)
{
    const char *p1_esc = esc_path;
    const char *p2_esc;
    const char *p_enc = enc_path;

    mg_http_printf_chunk(c,
        "<h2><a href=\"/browse/host/%d\">%s</a>", slot+1, theFuji->get_host(slot)->get_hostname()); // TODO escape hostname

    for(;;)
    {
        // separator
        mg_http_printf_chunk(c, "/");
        // skip slash(es)
        while (*p1_esc == '/') ++p1_esc;
        while (*p_enc == '/') ++p_enc;
        if (*p1_esc == '\0' || *p_enc == '\0') break;
        // find slash
        p2_esc = p1_esc;
        while (*p2_esc != '/' && *p2_esc != '\0') ++p2_esc;
        while (*p_enc != '/' && *p_enc != '\0') ++p_enc;
        // end of path?
        if (*p2_esc == '\0' || *p_enc == '\0')
        {
            // send last path element
            // // (a) without link
            // mg_http_write_chunk(c, p1_esc, p2_esc - p1_esc);
            // (b) as link
            mg_http_printf_chunk(c, "<a href=\"/browse/host/%d", slot+1);
            mg_http_write_chunk(c, enc_path, p_enc - enc_path);
            mg_http_printf_chunk(c, "%s\">", download ? "?action=slotlist" : "");
            mg_http_write_chunk(c, p1_esc, p2_esc - p1_esc);
            mg_http_printf_chunk(c, "</a>");
            // add [Download] link
            if (download)
            {
                mg_http_printf_chunk(c, "&nbsp;&nbsp;<a href=\"/browse/host/%d", slot+1);
                mg_http_write_chunk(c, enc_path, p_enc - enc_path);
                mg_http_printf_chunk(c, "?action=download\" title=\"Download file\">[&#11015;]</a>");
            }
            break;
        }
        // send path element
        mg_http_printf_chunk(c, "<a href=\"/browse/host/%d", slot+1);
        mg_http_write_chunk(c, enc_path, p_enc - enc_path);
        mg_http_printf_chunk(c, "\">");
        mg_http_write_chunk(c, p1_esc, p2_esc - p1_esc);
        mg_http_printf_chunk(c, "</a>");
        p1_esc = p2_esc;
    }
    mg_http_printf_chunk(c, "</h2>");
}

void fnHttpServiceBrowser::print_dentry(mg_connection *c, fsdir_entry *dp, int slot, const char *enc_path)
{
    char size[64], mod[64];
    const char *slash = dp->isDir ? "/" : "";
    const char *form = dp->isDir ? "" : "?action=slotlist";
    const char *sep = enc_path[strlen(enc_path)-1] == '/' ? "" : "/";
    char enc_filename[1024]; // URL encoded file name
    char esc_filename[1024]; // HTML escaped file name

    if (browse_url_encode(dp->filename, strlen(dp->filename), enc_filename, sizeof(enc_filename)) < 0)
    {
        enc_filename[0] = '\0';
    }

    if (browse_html_escape(dp->filename, strlen(dp->filename), esc_filename, sizeof(esc_filename)) < 0)
    {
        strcpy(esc_filename, "&lt;-- Name too long --&gt;");
    }

    if (dp->isDir) {
        snprintf(size, sizeof(size), "%s", "[DIR]");
    } else {
        if (dp->size < 1024) {
            snprintf(size, sizeof(size), "%d", (int) dp->size);
        } else if (dp->size < 0x100000) {
            snprintf(size, sizeof(size), "%.1fk", (double) dp->size / 1024.0);
        } else if (dp->size < 0x40000000) {
            snprintf(size, sizeof(size), "%.1fM", (double) dp->size / 1048576);
        } else {
            snprintf(size, sizeof(size), "%.1fG", (double) dp->size / 1073741824);
        }
    }
    strftime(mod, sizeof(mod), "%d-%b-%Y %H:%M", localtime(&dp->modified_time));
    mg_http_printf_chunk(c,
        "<tr><td>%s</td><td>%s</td><td><a href=\"/browse/host/%d%s%s%s%s\">%s%s</a></td></tr>\r\n",
        size, mod, slot+1, enc_path, sep, enc_filename, form, esc_filename, slash);
}


int fnHttpServiceBrowser::browse_sendfile(mg_connection *c, FileSystem *fs, fnFile *fh, const char *filename, unsigned long filesize)
{
    mg_printf(c, "HTTP/1.1 200 OK\r\n");
    // Set the response content type
    fnHttpService::set_file_content_type(c, filename);
    // Set the expected length of the content
    mg_printf(c, "Content-Length: %lu\r\n\r\n", filesize);

    // Create a task to send the file content out
    fnTask *task = new fnHttpSendFileTask(fs, fh, c);
    if (task == nullptr)
    {
        Debug_println("Failed to create fnHttpSendFileTask");
        mg_http_reply(c, 400, "", "Failed to create a task\n");
        fnio::fclose(fh); // close (and delete _fh)
        return -1;
    }
    return (taskMgr.submit_task(task) > 0) ? 1 : 0; // 1 -> do not delete the file system, if task was submitted
}


int fnHttpServiceBrowser::process_browse_get(mg_connection *c, mg_http_message *hm, int host_slot, const char *host_path, unsigned pathlen)
{
    fujiHost &fnHost = *theFuji->get_host(host_slot);
    FileSystem *fs;
    int host_type;
    bool started = false;

    Debug_printf("Browse host %d (%s) host_path=\"%.*s\"\n", host_slot, fnHost.get_hostname(), pathlen, host_path);

    char hostname[MAX_HOSTNAME_LEN];
    fnHost.get_hostname(hostname, MAX_HOSTNAME_LEN);

    if (hostname[0] == '\0')
    {
        Debug_println("Empty Host Slot");
        mg_http_reply(c, 400, "", "Empty Host Slot\n");
        return -1;
    }
    if (strcmp("SD", hostname) == 0)
    {
        fs = new FileSystemSDFAT;
        host_type = HOSTTYPE_LOCAL;
    }
    else if (strncasecmp("smb://", hostname, 6) == 0)
    {
        hostname[0] = 's';
        hostname[1] = 'm';
        hostname[2] = 'b';
        fs = new FileSystemSMB;
        host_type = HOSTTYPE_SMB;
    }
    else if (strncasecmp("ftp://", hostname, 6) == 0)
    {
        fs = new FileSystemFTP;
        host_type = HOSTTYPE_FTP;
    }
    else if (strncasecmp("http://", hostname, 7) == 0 || strncasecmp("https://", hostname, 8) == 0)
    {
        fs = new FileSystemHTTP;
        host_type = HOSTTYPE_HTTP;
    }
    else
    {
        fs = new FileSystemTNFS;
        host_type = HOSTTYPE_TNFS;
    }

    if (fs == nullptr)
    {
        Debug_println("Couldn't create a new File System");
        mg_http_reply(c, 400, "", "Couldn't create a new File System\n");
        return -1;
    }

    Debug_println("Starting temporary File System");
    switch(host_type)
    {
    case HOSTTYPE_LOCAL:
        started = ((FileSystemSDFAT *)fs)->start();
        break;
    case HOSTTYPE_SMB:
        started = ((FileSystemSMB *)fs)->start(hostname);
        break;
    case HOSTTYPE_FTP:
        started = ((FileSystemFTP *)fs)->start(hostname);
        break;
    case HOSTTYPE_HTTP:
        started = ((FileSystemHTTP *)fs)->start(hostname);
        break;
    case HOSTTYPE_TNFS:
        started = ((FileSystemTNFS *)fs)->start(hostname);
        break;
    }

    if (!started)
    {
        Debug_println("Couldn't start File System");
        mg_http_reply(c, 400, "", "File System error\n");
        delete fs;
        return -1;
    }

    int result = browse_listdir(c, hm, fs, host_slot, host_path, pathlen);

    if (result != 1)
    {
        Debug_println("Destroying temporary File System");
        delete fs;
    }

    return result;
}

#endif // !ESP_PLATFORM

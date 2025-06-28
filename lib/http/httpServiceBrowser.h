#ifndef HTTPSERVICEBROWSER_H
#define HTTPSERVICEBROWSER_H

#include "fnFS.h"
#include "mongoose.h"
#undef mkdir

class fnHttpServiceBrowser
{
    static int browse_url_encode(const char *src, size_t src_len, char *dst, size_t dst_len);
    static int browse_html_escape(const char *src, size_t src_len, char *dst, size_t dst_len);
    static int validate_path(const char *path, size_t path_len);

    static int browse_listdir(mg_connection *c, mg_http_message *hm, FileSystem *pFS, int slot, const char *host_path, unsigned pathlen);
    static int browse_listdrives(mg_connection *c, int slot, const char *esc_path, const char *enc_path);
    static void print_head(mg_connection *c, int slot);
    static void print_navi(mg_connection *c, int slot, const char *esc_path, const char*enc_path, bool download = false);
    static void print_dentry(mg_connection *c, fsdir_entry *dp, int slot, const char *enc_path);

    static int browse_sendfile(mg_connection *c, FileSystem *fs, fnFile *fh, const char *filename, unsigned long filesize);

public:
    static int process_browse_get(mg_connection *c, mg_http_message *hm, int host_slot, const char *host_path, unsigned pathlen);
};

#endif // HTTPSERVICEBROWSER_H

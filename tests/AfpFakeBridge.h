#ifndef AFP_FAKE_BRIDGE_H
#define AFP_FAKE_BRIDGE_H

// A stand-in for afpBridge.c that behaves like the library: one server per
// user, a server is logged out when its last volume is unmounted, and each
// volume is a small in-memory tree.  Include it in exactly one file per test.

#include "afpBridge.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <map>
#include <string>
#include <vector>

struct FakeNode
{
    bool dir = false;
    std::string data;
};

struct afp_volume
{
    std::string name;
    afp_server *server = nullptr;
    bool attached = false;
    std::map<std::string, FakeNode> nodes; // by volume-relative path; "/" is implied
};

struct afp_server
{
    std::string user;
    std::vector<afp_volume> volumes;
    bool logged_in = false;
};

// An open file, or an entry of a listing.
struct afp_file_info
{
    afp_volume *volume = nullptr;
    std::string path;
    bool dir = false;
    unsigned long long size = 0;
    afp_file_info *next = nullptr;
};

namespace
{
struct Fake
{
    std::map<std::string, afp_server> servers;
    afp_bridge_gone_fn gone = nullptr;
    int starts = 0, logins = 0, attaches = 0, detaches = 0, releases = 0, logouts = 0;
    int opens = 0, closes = 0;
    bool fail_start = false, fail_attach = false;
    bool dirs_open_as_files = false; // some servers let a directory be opened
    int login_error = 0;

    void reset()
    {
        servers.clear();
        starts = logins = attaches = detaches = releases = logouts = 0;
        opens = closes = 0;
        fail_start = fail_attach = false;
        dirs_open_as_files = false;
        login_error = 0;
    }
} fake;

bool anything_attached(const afp_server &s)
{
    for (const auto &v : s.volumes)
        if (v.attached) return true;
    return false;
}

std::string parent_of(const std::string &path)
{
    size_t slash = path.find_last_of('/');
    return slash == 0 ? "/" : path.substr(0, slash);
}

std::string leaf_of(const std::string &path)
{
    return path.substr(path.find_last_of('/') + 1);
}

bool is_dir(const afp_volume &v, const std::string &path)
{
    if (path == "/") return true;
    auto it = v.nodes.find(path);
    return it != v.nodes.end() && it->second.dir;
}
} // namespace

extern "C" {

int afp_bridge_start(afp_bridge_log_fn, afp_bridge_gone_fn gone)
{
    fake.starts++;
    fake.gone = gone;
    return fake.fail_start ? -1 : 0;
}

afp_server *afp_bridge_login(const char *, int, const char *user, const char *, int *error)
{
    fake.logins++;
    if (fake.login_error)
    {
        *error = fake.login_error;
        return nullptr;
    }
    afp_server &s = fake.servers[user];
    if (s.volumes.empty())
    {
        s.user = user;
        s.volumes.resize(2);
        s.volumes[0].name = "Alpha";
        s.volumes[1].name = "Beta";
        for (auto &v : s.volumes) v.server = &s;
    }
    s.logged_in = true;
    return &s;
}

int afp_bridge_volume_count(afp_server *server) { return static_cast<int>(server->volumes.size()); }
const char *afp_bridge_volume_name(afp_server *server, int index) { return server->volumes[index].name.c_str(); }

afp_volume *afp_bridge_find_volume(afp_server *server, const char *name)
{
    for (auto &v : server->volumes)
        if (v.name == name) return &v;
    return nullptr;
}

int afp_bridge_attach(afp_volume *volume)
{
    fake.attaches++;
    if (fake.fail_attach) return -1;
    volume->attached = true;
    return 0;
}

void afp_bridge_detach(afp_volume *volume)
{
    fake.detaches++;
    volume->attached = false;
    if (fake.gone) fake.gone(volume);
    if (!anything_attached(*volume->server))
    {
        volume->server->logged_in = false;
        fake.logouts++;
    }
}

void afp_bridge_release(afp_server *server)
{
    fake.releases++;
    if (!anything_attached(*server))
    {
        server->logged_in = false;
        fake.logouts++;
    }
}

int afp_bridge_stat(afp_volume *volume, const char *path, int *dir, unsigned long long *size)
{
    if (std::string(path) == "/")
    {
        *dir = 1;
        *size = 0;
        return 0;
    }
    auto it = volume->nodes.find(path);
    if (it == volume->nodes.end()) return -ENOENT;
    *dir = it->second.dir ? 1 : 0;
    *size = it->second.data.size();
    return 0;
}

int afp_bridge_open(afp_volume *volume, const char *path, int mode, afp_file_info **file)
{
    *file = nullptr;
    auto it = volume->nodes.find(path);
    const bool exists = it != volume->nodes.end();
    if (exists && it->second.dir && !fake.dirs_open_as_files) return -EISDIR;
    if (!exists)
    {
        if (mode == AFP_BRIDGE_READ || mode == AFP_BRIDGE_UPDATE) return -ENOENT;
        if (!is_dir(*volume, parent_of(path))) return -ENOENT;
        volume->nodes[path] = FakeNode{};
    }
    else if (mode == AFP_BRIDGE_WRITE)
    {
        it->second.data.clear();
    }
    fake.opens++;
    *file = new afp_file_info;
    (*file)->volume = volume;
    (*file)->path = path;
    return 0;
}

int afp_bridge_close(afp_volume *, const char *, afp_file_info *file)
{
    fake.closes++;
    delete file;
    return 0;
}

int afp_bridge_read(afp_volume *volume, const char *path, afp_file_info *, void *buf, unsigned int len,
                    unsigned long long offset)
{
    const std::string &data = volume->nodes[path].data;
    if (offset >= data.size()) return 0;
    size_t n = std::min<size_t>(len, data.size() - offset);
    std::memcpy(buf, data.data() + offset, n);
    return static_cast<int>(n);
}

int afp_bridge_write(afp_volume *volume, const char *path, afp_file_info *, const void *buf, unsigned int len,
                     unsigned long long offset)
{
    std::string &data = volume->nodes[path].data;
    if (data.size() < offset + len) data.resize(offset + len);
    std::memcpy(&data[offset], buf, len);
    return static_cast<int>(len);
}

int afp_bridge_list(afp_volume *volume, const char *path, afp_file_info **entries)
{
    *entries = nullptr;
    const std::string dir = path;
    if (dir != "/" && volume->nodes.find(dir) == volume->nodes.end()) return -ENOENT;
    if (!is_dir(*volume, dir)) return -ENOTDIR;

    afp_file_info *tail = nullptr;
    for (const auto &node : volume->nodes)
    {
        if (parent_of(node.first) != dir) continue;
        auto *e = new afp_file_info;
        e->path = leaf_of(node.first);
        e->dir = node.second.dir;
        e->size = node.second.data.size();
        if (tail) tail->next = e;
        else *entries = e;
        tail = e;
    }
    return 0;
}

const char *afp_bridge_entry_name(const afp_file_info *entry) { return entry->path.c_str(); }
int afp_bridge_entry_is_dir(const afp_file_info *entry) { return entry->dir ? 1 : 0; }
unsigned long long afp_bridge_entry_size(const afp_file_info *entry) { return entry->size; }
afp_file_info *afp_bridge_entry_next(const afp_file_info *entry) { return entry->next; }

void afp_bridge_list_free(afp_file_info *entries)
{
    while (entries)
    {
        afp_file_info *next = entries->next;
        delete entries;
        entries = next;
    }
}

int afp_bridge_remove(afp_volume *volume, const char *path)
{
    auto it = volume->nodes.find(path);
    if (it == volume->nodes.end()) return -ENOENT;
    if (it->second.dir) return -EISDIR;
    volume->nodes.erase(it);
    return 0;
}

int afp_bridge_rename(afp_volume *volume, const char *from, const char *to)
{
    auto it = volume->nodes.find(from);
    if (it == volume->nodes.end()) return -ENOENT;
    if (volume->nodes.count(to)) return -EEXIST;
    volume->nodes[to] = it->second;
    volume->nodes.erase(from);
    return 0;
}

int afp_bridge_mkdir(afp_volume *volume, const char *path)
{
    if (volume->nodes.count(path)) return -EEXIST;
    if (!is_dir(*volume, parent_of(path))) return -ENOENT;
    volume->nodes[path].dir = true;
    return 0;
}

int afp_bridge_rmdir(afp_volume *volume, const char *path)
{
    auto it = volume->nodes.find(path);
    if (it == volume->nodes.end()) return -ENOENT;
    if (!it->second.dir) return -ENOTDIR;
    for (const auto &node : volume->nodes)
        if (parent_of(node.first) == path) return -ENOTEMPTY;
    volume->nodes.erase(it);
    return 0;
}

} // extern "C"

#endif /* AFP_FAKE_BRIDGE_H */

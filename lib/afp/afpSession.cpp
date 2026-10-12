#include "afpSession.h"

#include <cerrno>
#include <map>
#include <mutex>

#include "../../include/debug.h"

#include "afpBridge.h"

namespace
{
struct Attachment
{
    int holders;
    unsigned id; // tells an attachment apart from a later one at the same address
};

// afpfs-ng keeps one process-wide server list; this table is its owner here.
std::mutex open_lock;     // serialises logins, attaches and detaches
std::mutex attached_lock; // guards the table; never held across a bridge call
std::map<struct afp_volume *, Attachment> attached;
unsigned next_attachment = 1;

void log_line(const char *message)
{
    Debug_printf("AFP: %s\r\n", message);
}

// Runs for every unmount, including one the server forced.
void volume_gone(struct afp_volume *volume)
{
    std::lock_guard<std::mutex> guard(attached_lock);
    attached.erase(volume);
}
} // namespace

AfpSession::~AfpSession()
{
    close();
}

success_is_true AfpSession::open(const std::string &host, int port, const std::string &user,
                                 const std::string &password, const std::string &volume)
{
    close();

    std::lock_guard<std::mutex> guard(open_lock);
    _error = 0;
    _volume_names.clear();

    if (afp_bridge_start(log_line, volume_gone) != 0)
    {
        _error = -EIO;
        RETURN_ERROR_AS_FALSE();
    }

    struct afp_server *server =
        afp_bridge_login(host.c_str(), port, user.c_str(), password.c_str(), &_error);
    if (server == nullptr)
        RETURN_ERROR_AS_FALSE();

    for (int i = 0; i < afp_bridge_volume_count(server); i++)
        _volume_names.emplace_back(afp_bridge_volume_name(server, i));

    if (volume.empty())
    {
        // Nothing holds a login open but an attached volume.
        afp_bridge_release(server);
        RETURN_SUCCESS_AS_TRUE();
    }

    struct afp_volume *found = afp_bridge_find_volume(server, volume.c_str());
    if (found == nullptr)
    {
        afp_bridge_release(server);
        _error = -ENOENT;
        RETURN_ERROR_AS_FALSE();
    }

    // Joining an existing attachment is one step, so an unmount cannot slip in.
    {
        std::lock_guard<std::mutex> table(attached_lock);
        auto it = attached.find(found);
        if (it != attached.end())
        {
            it->second.holders++;
            _attachment = it->second.id;
            _volume = found;
            RETURN_SUCCESS_AS_TRUE();
        }
    }

    if (afp_bridge_attach(found) != 0)
    {
        afp_bridge_release(server);
        _error = -EACCES;
        RETURN_ERROR_AS_FALSE();
    }

    {
        std::lock_guard<std::mutex> table(attached_lock);
        _attachment = next_attachment++;
        attached[found] = Attachment{1, _attachment};
    }
    _volume = found;
    RETURN_SUCCESS_AS_TRUE();
}

void AfpSession::close()
{
    if (_volume == nullptr)
        return;

    std::lock_guard<std::mutex> guard(open_lock);
    bool last = false;
    {
        std::lock_guard<std::mutex> table(attached_lock);
        auto it = attached.find(_volume);
        if (it != attached.end() && it->second.id == _attachment && --it->second.holders == 0)
        {
            attached.erase(it);
            last = true;
        }
    }
    if (last)
        afp_bridge_detach(_volume);
    _volume = nullptr;
    _attachment = 0;
}

struct afp_volume *AfpSession::volume() const
{
    std::lock_guard<std::mutex> table(attached_lock);
    auto it = attached.find(_volume);
    if (it == attached.end() || it->second.id != _attachment)
        return nullptr;
    return _volume;
}

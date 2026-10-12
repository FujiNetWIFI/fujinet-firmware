#ifndef AFP_SESSION_H
#define AFP_SESSION_H

#include <string>
#include <vector>

#include "global_types.h"

struct afp_volume;

// One user's hold on one AFP volume.  Logins and attachments are shared
// between sessions and last until the final holder closes.
class AfpSession
{
public:
    AfpSession() = default;
    ~AfpSession();

    AfpSession(const AfpSession &) = delete;
    AfpSession &operator=(const AfpSession &) = delete;

    // port 0 is the default.  An empty volume lists the names and attaches nothing.
    success_is_true open(const std::string &host, int port, const std::string &user,
                         const std::string &password, const std::string &volume);

    void close();

    // nullptr when nothing is attached, or once the library has unmounted it.
    struct afp_volume *volume() const;

    const std::vector<std::string> &volume_names() const { return _volume_names; }

    // 0, or the negative errno of the last failed open.
    int error() const { return _error; }

private:
    struct afp_volume *_volume = nullptr;
    unsigned _attachment = 0;
    std::vector<std::string> _volume_names;
    int _error = 0;
};

#endif /* AFP_SESSION_H */

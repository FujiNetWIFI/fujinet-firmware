#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "afpBridge.h"
#include "afpSession.h"

#include <cerrno>
#include <map>
#include <string>
#include <vector>

// A stand-in for afpBridge.c that behaves like the library: one server per
// user, and a server is logged out when its last volume is unmounted.
struct afp_volume
{
    std::string name;
    afp_server *server = nullptr;
    bool attached = false;
};

struct afp_server
{
    std::string user;
    std::vector<afp_volume> volumes;
    bool logged_in = false;
};

namespace
{
struct Fake
{
    std::map<std::string, afp_server> servers;
    afp_bridge_gone_fn gone = nullptr;
    int starts = 0, logins = 0, attaches = 0, detaches = 0, releases = 0, logouts = 0;
    bool fail_start = false, fail_attach = false;
    int login_error = 0;

    void reset()
    {
        servers.clear();
        starts = logins = attaches = detaches = releases = logouts = 0;
        fail_start = fail_attach = false;
        login_error = 0;
    }
} fake;

bool anything_attached(const afp_server &s)
{
    for (const auto &v : s.volumes)
        if (v.attached) return true;
    return false;
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

} // extern "C"

static bool opens(AfpSession &s, const char *user, const char *volume)
{
    return s.open("server", 0, user, "secret", volume).is_success();
}

TEST_CASE("opening a volume attaches it once and lists the server's volumes")
{
    fake.reset();
    AfpSession s;
    REQUIRE(opens(s, "alice", "Alpha"));
    CHECK(s.volume() != nullptr);
    CHECK(s.error() == 0);
    CHECK(fake.attaches == 1);
    CHECK(s.volume_names() == std::vector<std::string>{"Alpha", "Beta"});
}

TEST_CASE("two users of one volume share one attachment, whichever closes first")
{
    fake.reset();
    AfpSession a, b;
    REQUIRE(opens(a, "alice", "Alpha"));
    REQUIRE(opens(b, "alice", "Alpha"));
    CHECK(fake.attaches == 1);
    CHECK(a.volume() == b.volume());

    SUBCASE("the first one closes first")
    {
        a.close();
        CHECK(fake.detaches == 0);
        CHECK(a.volume() == nullptr);
        CHECK(b.volume() != nullptr);
        b.close();
    }
    SUBCASE("the second one closes first")
    {
        b.close();
        CHECK(fake.detaches == 0);
        CHECK(a.volume() != nullptr);
        a.close();
    }
    CHECK(fake.detaches == 1);
    CHECK(fake.logouts == 1);
}

TEST_CASE("the destructor closes")
{
    fake.reset();
    {
        AfpSession s;
        REQUIRE(opens(s, "alice", "Alpha"));
    }
    CHECK(fake.detaches == 1);
}

TEST_CASE("opening again closes what was open")
{
    fake.reset();
    AfpSession s;
    REQUIRE(opens(s, "alice", "Alpha"));
    REQUIRE(opens(s, "alice", "Beta"));
    CHECK(fake.attaches == 2);
    CHECK(fake.detaches == 1);
    s.close();
    CHECK(fake.detaches == 2);
}

TEST_CASE("two volumes on one login: the login outlives the first to close")
{
    fake.reset();
    AfpSession a, b;
    REQUIRE(opens(a, "alice", "Alpha"));
    REQUIRE(opens(b, "alice", "Beta"));
    a.close();
    CHECK(fake.logouts == 0);
    b.close();
    CHECK(fake.logouts == 1);
}

TEST_CASE("different users get their own logins and attachments")
{
    fake.reset();
    AfpSession a, b;
    REQUIRE(opens(a, "alice", "Alpha"));
    REQUIRE(opens(b, "bob", "Alpha"));
    CHECK(fake.attaches == 2);
    CHECK(a.volume() != b.volume());
}

TEST_CASE("an unknown volume fails and leaves no login behind")
{
    fake.reset();
    AfpSession s;
    CHECK_FALSE(opens(s, "alice", "Nope"));
    CHECK(s.error() == -ENOENT);
    CHECK(s.volume() == nullptr);
    CHECK(fake.attaches == 0);
    CHECK(fake.releases == 1);
    CHECK(fake.logouts == 1);
}

TEST_CASE("a refused attach fails and leaves no login behind")
{
    fake.reset();
    fake.fail_attach = true;
    AfpSession s;
    CHECK_FALSE(opens(s, "alice", "Alpha"));
    CHECK(s.error() == -EACCES);
    CHECK(s.volume() == nullptr);
    CHECK(fake.releases == 1);
    s.close();
    CHECK(fake.detaches == 0);
}

TEST_CASE("a failed login reports the bridge's error")
{
    fake.reset();
    fake.login_error = -ETIMEDOUT;
    AfpSession s;
    CHECK_FALSE(opens(s, "alice", "Alpha"));
    CHECK(s.error() == -ETIMEDOUT);
    CHECK(fake.attaches == 0);
    CHECK(fake.releases == 0);
}

TEST_CASE("a library that will not start is reported before any login")
{
    fake.reset();
    fake.fail_start = true;
    AfpSession s;
    CHECK_FALSE(opens(s, "alice", "Alpha"));
    CHECK(s.error() == -EIO);
    CHECK(fake.logins == 0);
}

TEST_CASE("no volume: lists the names and keeps nothing open")
{
    fake.reset();
    AfpSession s;
    REQUIRE(opens(s, "alice", ""));
    CHECK(s.volume() == nullptr);
    CHECK(s.volume_names() == std::vector<std::string>{"Alpha", "Beta"});
    CHECK(fake.attaches == 0);
    CHECK(fake.releases == 1);
    CHECK(fake.logouts == 1);
}

TEST_CASE("listing names does not disturb a volume someone else has open")
{
    fake.reset();
    AfpSession holder, lister;
    REQUIRE(opens(holder, "alice", "Alpha"));
    REQUIRE(opens(lister, "alice", ""));
    CHECK(fake.logouts == 0);
    CHECK(holder.volume() != nullptr);
}

TEST_CASE("when the server ends the session, holders see no volume and do not detach it")
{
    fake.reset();
    AfpSession a, b;
    REQUIRE(opens(a, "alice", "Alpha"));
    REQUIRE(opens(b, "alice", "Alpha"));
    afp_volume *v = a.volume();
    REQUIRE(v != nullptr);

    // What the library does when the server closes the session.
    v->attached = false;
    fake.gone(v);

    CHECK(a.volume() == nullptr);
    CHECK(b.volume() == nullptr);
    a.close();
    b.close();
    CHECK(fake.detaches == 0);
}

TEST_CASE("an old holder is not confused by a new attachment at the same address")
{
    fake.reset();
    AfpSession old_holder, new_holder;
    REQUIRE(opens(old_holder, "alice", "Alpha"));
    afp_volume *v = old_holder.volume();
    v->attached = false;
    fake.gone(v);

    REQUIRE(opens(new_holder, "alice", "Alpha"));
    REQUIRE(new_holder.volume() == v);

    CHECK(old_holder.volume() == nullptr);
    old_holder.close();
    CHECK(fake.detaches == 0);
    CHECK(new_holder.volume() == v);
    new_holder.close();
    CHECK(fake.detaches == 1);
}

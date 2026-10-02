#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "sio/sioNetwork.h"

// --------------------------------------------------------------------------------
// recognizesCommand() and get_dstats_for_command() are protected -- this test
// only needs read access to them, not to change their behavior, so a subclass
// with `using` declarations to re-expose them is enough. No friend declaration
// needed in the production header, and it's obvious from this file alone
// exactly what's being poked at and why.
// --------------------------------------------------------------------------------
class TestableSioNetwork : public sioNetwork
{
public:
    using sioNetwork::recognizesCommand;
    using sioNetwork::get_dstats_for_command;
};

// --------------------------------------------------------------------------------
// Every command sioNetwork recognizes (via the shared NDevice dispatch table)
// must have a real DSTATS direction. If someone adds a command -- in the base,
// in a mixin, wherever -- and forgets to teach get_dstats_for_command() about
// it, this is what catches that instead of a confused BASIC program on real
// hardware discovering it via XIO.
// --------------------------------------------------------------------------------

TEST_CASE("every recognized command has a DSTATS direction")
{
    TestableSioNetwork dev;

    for (int cmd = 0; cmd <= 0xFF; ++cmd)
    {
        auto command = static_cast<fujiCommandID_t>(cmd);

        if (!dev.recognizesCommand(command))
            continue;

        char buf[8];
        std::snprintf(buf, sizeof(buf), "0x%02X", (uint8_t) command);
        INFO("command = " << buf);
        CHECK(dev.get_dstats_for_command(command) != SIO_DIRECTION::INVALID);
    }
}

TEST_CASE("an unrecognized command reports SIO_DIRECTION_INVALID")
{
    TestableSioNetwork dev;

    // Pick a command id that should never be valid. If NDevice ever grows to
    // recognize every single byte value (unlikely, but not impossible), this
    // assumption breaks and the test itself needs a different unused id.
    constexpr fujiCommandID_t unknown_command = static_cast<fujiCommandID_t>(0);
    REQUIRE_FALSE(dev.recognizesCommand(unknown_command));

    CHECK(dev.get_dstats_for_command(unknown_command) == SIO_DIRECTION::INVALID);
}

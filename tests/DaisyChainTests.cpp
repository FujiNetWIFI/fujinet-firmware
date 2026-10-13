// lib/bus/DaisyChain: disk swap rotates device IDs among the mounted disks (#1387).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <concepts>
#include <vector>

#include "DaisyChain.h"

class virtualDevice {}; // DaisyChain only ever holds pointers to it
class FakeDisk : public virtualDevice {};

static fujiDeviceID_t disk_id(int slot) { return FUJI_DEVICEID::DISK + slot; }

// n disks in slots D1..Dn, rotated by `amount`; returns the slot each disk ended up in.
static std::vector<int> rotate(int n, int amount)
{
    DaisyChain chain;
    std::vector<FakeDisk> disks(n);
    std::vector<FakeDisk *> mounted;
    for (int i = 0; i < n; i++)
    {
        chain.addDevice(&disks[i], disk_id(i));
        mounted.push_back(&disks[i]);
    }
    chain.rotateDevices(mounted, amount);

    std::vector<int> slot(n);
    for (int i = 0; i < n; i++)
        slot[i] = (int)*chain.fujiIDForDevice(&disks[i]) - (int)FUJI_DEVICEID::DISK;
    return slot;
}

TEST_CASE("swap moves the second disk into D1: for every mounted count")
{
    for (int n = 2; n <= 8; n++)
    {
        std::vector<int> slot = rotate(n, -1);
        CHECK_MESSAGE(slot[1] == 0, "n=", n);     // D2's disk is now D1:
        CHECK_MESSAGE(slot[0] == n - 1, "n=", n); // the first slot gets the last ID
    }
}

TEST_CASE("positive amounts move disks up and wrap")
{
    std::vector<int> slot = rotate(3, 1);
    CHECK(slot[0] == 1);
    CHECK(slot[1] == 2);
    CHECK(slot[2] == 0);
}

TEST_CASE("a full turn or a zero amount changes nothing")
{
    for (int amount : {0, 3, -3, 6})
    {
        std::vector<int> slot = rotate(3, amount);
        CHECK(slot == std::vector<int>{0, 1, 2});
    }
}

TEST_CASE("the ID lookup follows the rotation")
{
    DaisyChain chain;
    FakeDisk d[3];
    std::vector<FakeDisk *> mounted = {&d[0], &d[1], &d[2]};
    for (int i = 0; i < 3; i++)
        chain.addDevice(&d[i], disk_id(i));
    chain.rotateDevices(mounted, -1);
    CHECK(chain.deviceWithFujiID(disk_id(0)) == &d[1]);
    CHECK(chain.deviceWithFujiID(disk_id(2)) == &d[0]);
}

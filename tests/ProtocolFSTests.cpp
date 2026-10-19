#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "FS.h"
#include "samlib.h"
#include "fnTcpClient.h"

// TCP.cpp is linked only because Protocol.cpp casts to it; its harness wants this.
FakePeer fake_peer;

// utils.cpp's speech helper is never called here; the stub keeps SAM out of the link.
int sam(int argc, char **argv) { return 0; }

// A file system that mounts, then fails to open what it was asked for.
class OpenFailingFS : public NetworkProtocolFS
{
public:
    std::string rx, tx, sp;
    int mounts = 0;
    int umounts = 0;

    OpenFailingFS() : NetworkProtocolFS(&rx, &tx, &sp) {}

protected:
    fujiError_t mount(PeoplesUrlParser *) override { mounts++; return FUJI_ERROR::NONE; }
    // Clears the error, so the open has to keep its own.
    fujiError_t umount() override
    {
        umounts++;
        error = NDEV_STATUS::SUCCESS;
        return FUJI_ERROR::NONE;
    }
    fujiError_t open_file_handle() override
    {
        error = NDEV_STATUS::FILE_NOT_FOUND;
        return FUJI_ERROR::UNSPECIFIED;
    }
    fujiError_t open_dir_handle() override
    {
        error = NDEV_STATUS::FILE_NOT_FOUND;
        return FUJI_ERROR::UNSPECIFIED;
    }
    void resolve() override {}
    fujiError_t stat() override { return FUJI_ERROR::NONE; }
    void fserror_to_error() override {}
    fujiError_t read_file_handle(uint8_t *, unsigned short) override { return FUJI_ERROR::UNSPECIFIED; }
    fujiError_t read_dir_entry(char *, unsigned short) override { return FUJI_ERROR::UNSPECIFIED; }
    fujiError_t close_file_handle() override { return FUJI_ERROR::NONE; }
    fujiError_t close_dir_handle() override { return FUJI_ERROR::NONE; }
    fujiError_t write_file_handle(uint8_t *, unsigned short) override { return FUJI_ERROR::UNSPECIFIED; }
};

// The caller deletes a protocol whose open failed without closing it (#1837).
TEST_CASE("a failed file open unmounts and keeps the open's error")
{
    OpenFailingFS fs;
    PeoplesUrlParser url("TEST://host/missing.txt");

    CHECK(fs.open(&url, ACCESS_MODE::READ, NETPROTO_TRANS::NONE) != FUJI_ERROR::NONE);
    CHECK(fs.mounts == 1);
    CHECK(fs.umounts == 1);
    CHECK(fs.error == NDEV_STATUS::FILE_NOT_FOUND);
}

TEST_CASE("a failed directory open unmounts and keeps the open's error")
{
    OpenFailingFS fs;
    PeoplesUrlParser url("TEST://host/missing/");

    CHECK(fs.open(&url, ACCESS_MODE::DIRECTORY, NETPROTO_TRANS::NONE) != FUJI_ERROR::NONE);
    CHECK(fs.mounts == 1);
    CHECK(fs.umounts == 1);
    CHECK(fs.error == NDEV_STATUS::FILE_NOT_FOUND);
}

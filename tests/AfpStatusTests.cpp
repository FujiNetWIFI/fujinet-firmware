#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cerrno>

#include "afpStatus.h"

TEST_CASE("no error is success")
{
    CHECK(afp_errno_to_status(0) == NDEV_STATUS::SUCCESS);
}

TEST_CASE("file errors")
{
    CHECK(afp_errno_to_status(-ENOENT) == NDEV_STATUS::FILE_NOT_FOUND);
    CHECK(afp_errno_to_status(-EEXIST) == NDEV_STATUS::FILE_EXISTS);
    CHECK(afp_errno_to_status(-ENOSPC) == NDEV_STATUS::NO_SPACE_ON_DEVICE);
    CHECK(afp_errno_to_status(-EACCES) == NDEV_STATUS::ACCESS_DENIED);
    CHECK(afp_errno_to_status(-EPERM) == NDEV_STATUS::ACCESS_DENIED);
    CHECK(afp_errno_to_status(-EROFS) == NDEV_STATUS::ACCESS_DENIED);
}

TEST_CASE("a path the volume cannot take is a bad devicespec")
{
    CHECK(afp_errno_to_status(-EISDIR) == NDEV_STATUS::INVALID_DEVICESPEC);
    CHECK(afp_errno_to_status(-ENOTDIR) == NDEV_STATUS::INVALID_DEVICESPEC);
    CHECK(afp_errno_to_status(-EINVAL) == NDEV_STATUS::INVALID_DEVICESPEC);
    CHECK(afp_errno_to_status(-ENAMETOOLONG) == NDEV_STATUS::INVALID_DEVICESPEC);
}

TEST_CASE("why a login failed")
{
    CHECK(afp_errno_to_status(-EHOSTUNREACH) == NDEV_STATUS::NETWORK_UNREACHABLE);
    CHECK(afp_errno_to_status(-ENETUNREACH) == NDEV_STATUS::NETWORK_UNREACHABLE);
    CHECK(afp_errno_to_status(-ECONNREFUSED) == NDEV_STATUS::CONNECTION_REFUSED);
    CHECK(afp_errno_to_status(-ETIMEDOUT) == NDEV_STATUS::SOCKET_TIMEOUT);
    CHECK(afp_errno_to_status(-ENOTCONN) == NDEV_STATUS::NOT_CONNECTED);
}

TEST_CASE("anything else is a general error")
{
    CHECK(afp_errno_to_status(-EIO) == NDEV_STATUS::GENERAL);
    CHECK(afp_errno_to_status(-EBUSY) == NDEV_STATUS::GENERAL);
    CHECK(afp_errno_to_status(-9999) == NDEV_STATUS::GENERAL);
}

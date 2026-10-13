#include "afpStatus.h"

#include <cerrno>

nDevStatus_t afp_errno_to_status(int error)
{
    switch (-error)
    {
    case 0:
        return NDEV_STATUS::SUCCESS;
    case ENOENT:
        return NDEV_STATUS::FILE_NOT_FOUND;
    case EEXIST:
        return NDEV_STATUS::FILE_EXISTS;
    case EACCES:
    case EPERM:
    case EROFS:
        return NDEV_STATUS::ACCESS_DENIED;
    case ENOSPC:
        return NDEV_STATUS::NO_SPACE_ON_DEVICE;
    case EISDIR:
    case ENOTDIR:
    case EINVAL:
    case ENAMETOOLONG:
        return NDEV_STATUS::INVALID_DEVICESPEC;
    case EHOSTUNREACH:
    case ENETUNREACH:
        return NDEV_STATUS::NETWORK_UNREACHABLE;
    case ECONNREFUSED:
        return NDEV_STATUS::CONNECTION_REFUSED;
    case ETIMEDOUT:
        return NDEV_STATUS::SOCKET_TIMEOUT;
    case ENOTCONN:
        return NDEV_STATUS::NOT_CONNECTED;
    default:
        return NDEV_STATUS::GENERAL;
    }
}

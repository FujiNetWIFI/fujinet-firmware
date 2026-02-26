/**
 * Error codes to return
 */

#ifndef STATUS_ERROR_CODES_H
#define STATUS_ERROR_CODES_H

#include <stdint.h>

typedef enum class NDEV_STATUS : uint8_t {
    SUCCESS                        = 1,   //  Success
    WRITE_ONLY                     = 131, //  IOCB Write Only
    INVALID_COMMAND                = 132, //  IOCB Invalid Command
    READ_ONLY                      = 135, //  IOCB Read Only
    END_OF_FILE                    = 136, //  End of file
    GENERAL_TIMEOUT                = 138, //  General timeout
    GENERAL                        = 144, //  A fatal error
    NOT_IMPLEMENTED                = 146, //  Command not implemented
    FILE_EXISTS                    = 151, //  File exists (directory)
    NO_SPACE_ON_DEVICE             = 162, //  No space left on device
    INVALID_DEVICESPEC             = 165, //  An invalid devicespec was given
    ACCESS_DENIED                  = 167, //  Access denied
    FILE_NOT_FOUND                 = 170, //  Network error, file not found
    CONNECTION_REFUSED             = 200, //  A connection was either refused or not possible
    NETWORK_UNREACHABLE            = 201, //  Network unreachable
    SOCKET_TIMEOUT                 = 202, //  Network Socket Timeout
    NETWORK_DOWN                   = 203, //  Network Down
    CONNECTION_RESET               = 204, //  Connection was reset
    CONNECTION_ALREADY_IN_PROGRESS = 205, //  Connection already in progress
    ADDRESS_IN_USE                 = 206, //  Address in use
    NOT_CONNECTED                  = 207, //  Not connected.
    SERVER_NOT_RUNNING             = 208, //  Server not running (server returned NULL)
    NO_CONNECTION_WAITING          = 209, //  No connection waiting
    SERVICE_NOT_AVAILABLE          = 210, //  Service not available
    CONNECTION_ABORTED             = 211, //  Connection aborted
    INVALID_USERNAME_OR_PASSWORD   = 212, //  Invalid username or password.
    COULD_NOT_PARSE_JSON           = 213, //  Could not parse JSON
    CLIENT_GENERAL                 = 214, //  General client error
    SERVER_GENERAL                 = 215, //  General Server error
    COULD_NOT_ALLOCATE_BUFFERS     = 255, //  Could not allocate buffers
} nDevStatus_t;

#endif /* STATUS_ERROR_CODES */

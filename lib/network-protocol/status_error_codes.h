/**
 * Error codes to return
 */

#ifndef STATUS_ERROR_CODES_H
#define STATUS_ERROR_CODES_H

/**
 * Attempted to use connection while not open
 */
#define NETWORK_ERROR_NOT_CONNECTED 133

/**
 * A fatal error
 */
#define NETWORK_ERROR_GENERAL 144

/**
 * An invalid devicespec was given
 */
#define NETWORK_ERROR_INVALID_DEVICESPEC 165

/**
 * A connection was either refused or not possible
 */
#define NETWORK_ERROR_CONNECTION_REFUSED 200

/**
 * Network unreachable
 */
#define NETWORK_ERROR_NETWORK_UNREACHABLE 201

/**
 * Network Socket Timeout
 */
#define NETWORK_ERROR_SOCKET_TIMEOUT 202

/**
 * Network Down
 */
#define NETWORK_ERROR_NETWORK_DOWN 203

/**
 * Connection was reset
 */
#define NETWORK_ERROR_CONNECTION_RESET 204

/**
 * Could not allocate buffers
 */
#define NETWORK_ERROR_COULD_NOT_ALLOCATE_BUFFERS 255

#endif /* STATUS_ERROR_CODES */
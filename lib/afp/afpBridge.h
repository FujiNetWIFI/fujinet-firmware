#ifndef AFP_BRIDGE_H
#define AFP_BRIDGE_H

/* The C face of afpfs-ng for the rest of FujiNet.  afpBridge.c is the only
 * file that includes the library's own headers; everything else sees these
 * two types as opaque. */

#ifdef __cplusplus
extern "C" {
#endif

struct afp_server;
struct afp_volume;

typedef void (*afp_bridge_log_fn)(const char * message);
typedef void (*afp_bridge_gone_fn)(struct afp_volume * volume);

/* Starts the library once.  gone is called whenever a volume is unmounted,
 * including when the server ends the session.  Returns 0, or -1. */
int afp_bridge_start(afp_bridge_log_fn log, afp_bridge_gone_fn gone);

/* The login for this server and user, made if there is not one yet.
 * NULL on failure, with *error -EHOSTUNREACH when the name does not resolve
 * and -EACCES otherwise: the library does not tell a refused login from a
 * server that never answers. */
struct afp_server * afp_bridge_login(const char * host, int port,
	const char * user, const char * password, int * error);

int afp_bridge_volume_count(struct afp_server * server);
const char * afp_bridge_volume_name(struct afp_server * server, int index);
struct afp_volume * afp_bridge_find_volume(struct afp_server * server,
	const char * name);

/* Returns 0 on success. */
int afp_bridge_attach(struct afp_volume * volume);

/* Unmounts.  The library logs its server out with the last volume. */
void afp_bridge_detach(struct afp_volume * volume);

/* Logs out a server that has nothing attached; otherwise does nothing. */
void afp_bridge_release(struct afp_server * server);

#ifdef __cplusplus
}
#endif

#endif /* AFP_BRIDGE_H */

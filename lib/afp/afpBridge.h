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
 * NULL on failure, with *error a negative errno: -EHOSTUNREACH for a name
 * that does not resolve, the connection error if it could not connect, and
 * -EACCES for a refused login. */
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

/* An open file, or one entry of a directory listing. */
struct afp_file_info;

enum {
	AFP_BRIDGE_READ,	/* must exist */
	AFP_BRIDGE_WRITE,	/* created if missing, truncated if not */
	AFP_BRIDGE_APPEND,	/* created if missing */
	AFP_BRIDGE_UPDATE	/* must exist; read and write */
};

/* Paths are relative to the volume and start with '/'.  Unless it says
 * otherwise each of these returns 0 or a negative errno. */

int afp_bridge_stat(struct afp_volume * volume, const char * path,
	int * is_dir, unsigned long long * size);

int afp_bridge_open(struct afp_volume * volume, const char * path, int mode,
	struct afp_file_info ** file);
int afp_bridge_close(struct afp_volume * volume, const char * path,
	struct afp_file_info * file);

/* Bytes transferred, or a negative errno.  A read may be shorter than asked
 * without being at the end; only 0 is end of file.  afpfs-ng does not yet
 * report a write the server refused: it returns the full length. */
int afp_bridge_read(struct afp_volume * volume, const char * path,
	struct afp_file_info * file, void * buf, unsigned int len,
	unsigned long long offset);
int afp_bridge_write(struct afp_volume * volume, const char * path,
	struct afp_file_info * file, const void * buf, unsigned int len,
	unsigned long long offset);

/* A listing is a chain of entries, freed as a whole. */
int afp_bridge_list(struct afp_volume * volume, const char * path,
	struct afp_file_info ** entries);
const char * afp_bridge_entry_name(const struct afp_file_info * entry);
int afp_bridge_entry_is_dir(const struct afp_file_info * entry);
unsigned long long afp_bridge_entry_size(const struct afp_file_info * entry);
struct afp_file_info * afp_bridge_entry_next(const struct afp_file_info * entry);
void afp_bridge_list_free(struct afp_file_info * entries);

int afp_bridge_remove(struct afp_volume * volume, const char * path);
int afp_bridge_rename(struct afp_volume * volume, const char * from,
	const char * to);
int afp_bridge_mkdir(struct afp_volume * volume, const char * path);
int afp_bridge_rmdir(struct afp_volume * volume, const char * path);

#ifdef __cplusplus
}
#endif

#endif /* AFP_BRIDGE_H */

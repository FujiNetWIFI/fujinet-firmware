#include <string.h>

#include "afp.h"
#include "midlevel.h"

/* The mounted volume of a server prepared by cs_fake_mounted_login(), with
 * the protocol version the path checks need. */
static struct afp_volume * volume_of(struct afp_server * s)
{
	static struct afp_versions afp32={"AFP3.2",32};

	s->using_version=&afp32;
	return &s->volumes[0];
}

int dl_creat(struct afp_server * s, const char * path)
{
	return ml_creat(volume_of(s),path,0644);
}

int dl_unlink(struct afp_server * s, const char * path)
{
	return ml_unlink(volume_of(s),path);
}

int dl_mkdir(struct afp_server * s, const char * path)
{
	return ml_mkdir(volume_of(s),path,0755);
}

int dl_rmdir(struct afp_server * s, const char * path)
{
	return ml_rmdir(volume_of(s),path);
}

int dl_rename(struct afp_server * s, const char * from, const char * to)
{
	return ml_rename(volume_of(s),from,to);
}

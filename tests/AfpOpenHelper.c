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

int oh_open(struct afp_server * s, const char * path, int flags, void ** handle)
{
	struct afp_file_info * fp=(struct afp_file_info *) 1;
	int ret=ml_open(volume_of(s),path,flags,&fp);

	*handle=fp;
	return ret;
}

int oh_close(struct afp_server * s, const char * path, void * handle)
{
	return ml_close(volume_of(s),path,handle);
}

int oh_has_open_forks(struct afp_server * s)
{
	return volume_of(s)->open_forks!=NULL;
}

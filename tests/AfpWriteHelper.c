#include <string.h>

#include "afp.h"
#include "midlevel.h"

/* Readies the mounted volume of a server prepared by cs_fake_mounted_login()
 * for writes of at most quantum bytes per request. */
void wr_prepare(struct afp_server * s, unsigned int quantum, int locking)
{
	static struct afp_versions afp32={"AFP3.2",32};

	s->using_version=&afp32;
	s->tx_quantum=quantum;
	if (locking)
		s->volumes[0].extra_flags&=~VOLUME_EXTRA_FLAGS_NO_LOCKING;
	else
		s->volumes[0].extra_flags|=VOLUME_EXTRA_FLAGS_NO_LOCKING;
}

/* One ml_write() at offset 0 through an already open fork. */
int wr_write(struct afp_server * s, const char * data, unsigned int len)
{
	struct afp_file_info fp;

	memset(&fp,0,sizeof(fp));
	fp.forkid=7;
	return ml_write(&s->volumes[0],"/file",data,len,0,&fp,0,0);
}

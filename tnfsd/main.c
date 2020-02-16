/* The MIT License
 *
 * Copyright (c) 2010 Dylan Smith
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * The main()
 *
 * */

#include <stdio.h>
#include <stdlib.h>

#include "datagram.h"
#include "session.h"
#include "directory.h"
#include "errortable.h"
#include "chroot.h"

/* declare the main() - it won't be used elsewhere so I'll not bother
 * with putting it in a .h file */
int main(int argc, char **argv);

int main(int argc, char **argv)
{
	if(argc < 2)
	{
#ifdef ENABLE_CHROOT
		fprintf(stderr, "Usage: tnfsd <root dir> [-c <username>]\n");
#else
		fprintf(stderr, "Usage: tnfsd <root dir>\n");
#endif
		exit(-1);
	}

#ifdef ENABLE_CHROOT
	if(argc == 4)
	{
		/* chroot into the specified directory and drop privs */
		chroot_tnfs(argv[3], argv[1]);
		if(tnfs_setroot("/") < 0)
		{
			fprintf(stderr, "Unable to chdir to /...\n");
			exit(-1);
		}
	}
	else if(tnfs_setroot(argv[1]) < 0)
	{
#else
	if(tnfs_setroot(argv[1]) < 0)
	{
#endif
		fprintf(stderr, "Invalid root directory\n");
		exit(-1);
	}

	tnfs_init();		/* initialize structures etc. */
	tnfs_init_errtable();	/* initialize error lookup table */
	tnfs_sockinit();	/* initialize communications */
	tnfs_mainloop();	/* run */
	return 0;
}


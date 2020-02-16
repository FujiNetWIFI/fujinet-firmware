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
 * Drops root privileges and chroots to the given directory.
 *
 * */

#ifdef ENABLE_CHROOT
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <pwd.h>

#include "chroot.h"

void chroot_tnfs(const char *user, const char *newroot)
{
	struct passwd *entry;

	/* Get the passed user's UID and change this process's UID
	 * to this user */
	entry=getpwnam(user);
	if(entry == NULL)
	{
		perror("getpwnam");
		exit(-1);
	}

	/* Do the chroot */
	if(chroot(newroot) == -1)
	{
		perror("chroot");
		exit(-1);
	}

	/* Finally drop privileges */
	if(setuid(entry->pw_uid) == -1)
	{
		perror("setuid");
		exit(-1);
	}
}

#endif


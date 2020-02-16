#ifndef CHROOT_H
#define CHROOT_H

#ifdef ENABLE_CHROOT
/* Allows the tnfsd to assume a new uid and root */
void chroot_tnfs(const char *user, const char *newroot);
#endif

#endif


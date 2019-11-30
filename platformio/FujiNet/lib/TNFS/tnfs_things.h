#ifndef _TNFS_THINGS_H
#define _TNFS_THINGS_H

void tnfs_mount(const char *host, uint16_t port);
void tnfs_open();
void tnfs_read();
void tnfs_seek(uint32_t offset);

#endif //_TNFS_THINGS_H

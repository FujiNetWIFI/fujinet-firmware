/* pico_lfs.h
   Copyright (C) 2024 Timo Kokkonen <tjko@iki.fi>

   SPDX-License-Identifier: GPL-3.0-or-later

   This file is part of pico-lfs Library.

   pico-lfs Library is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   pico-lfs Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with pico-lfs Library. If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef _PICO_LFS_H_
#define _PICO_LFS_H_

#include "lfs.h"

#include "pico/mutex.h"

#ifdef __cplusplus
extern "C"
{
#endif

struct pico_lfs_context {
	struct lfs_config cfg;
	uint32_t base;
#ifdef LFS_THREADSAFE
	recursive_mutex_t mutex;
#endif
#ifdef LIB_PICO_MULTICORE
	bool  multicore_lockout_enabled;
#endif
};


/* Initialize LFS configuration. This allocates memory for the
   the configuration. pico_lfs_destroy() can be used to free
   the configuration if needed.

   Parameters:

   offset = offset in flash memory (must be aligned on 4K page size)
   size = size of the filesystem (must be multiple of 4K page size)
*/
struct lfs_config* pico_lfs_init(size_t offset, size_t size);

/* Release FLS configuration. lfs_unmount() should be called before
   calling this.
*/
void pico_lfs_destroy(struct lfs_config *cfg);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _PICO_LFS_H_ */

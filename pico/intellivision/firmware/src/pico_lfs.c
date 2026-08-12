/* pico_lfs.c
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

#include <stdio.h>

#include "hardware/flash.h"
#include "pico/multicore.h"
#include "pico/mutex.h"
#include "lfs.h"

#include "pico_lfs.h"

static int pico_block_device_read(const struct lfs_config *c, lfs_block_t block,
				lfs_off_t off, void *buffer, lfs_size_t size)
{
	struct pico_lfs_context *ctx = (struct pico_lfs_context*)c->context;

	/* Sanity check */
	LFS_ASSERT(block < c->block_count); /* Do not read past end of flash region. */
	LFS_ASSERT(off + size <= c->block_size); /* Read must be within a block. */

	/* Read from flash via the uncached XIP window */
	memcpy(buffer, (void*)XIP_NOCACHE_NOALLOC_BASE + ctx->base + block * c->block_size + off, size);

	return LFS_ERR_OK;
}

static int pico_block_device_prog(const struct lfs_config *c, lfs_block_t block,
				lfs_off_t off, const void *buffer, lfs_size_t size)
{
	struct pico_lfs_context *ctx = (struct pico_lfs_context*)c->context;
	uint32_t ints;

	/* Sanity check */
	LFS_ASSERT(block < c->block_count); /* Do not write past end of flash region. */
	LFS_ASSERT(off % c->prog_size == 0); /* Flash address must be aligned to flash page */
	LFS_ASSERT(size % c->prog_size == 0); /* Bytes to program must be multiple of flash page size. */
	LFS_ASSERT(off + size <= c->block_size); /* Write must be within a block. */

	/* Program flash block */
	ints = save_and_disable_interrupts();
	flash_range_program(ctx->base + block * c->block_size + off, buffer, size);
	restore_interrupts(ints);

	return LFS_ERR_OK;
}

static int pico_block_device_erase(const struct lfs_config *c, lfs_block_t block)
{
	struct pico_lfs_context *ctx = (struct pico_lfs_context*)c->context;
	uint32_t ints;

	/* Sanity check */
	LFS_ASSERT(block < c->block_count);

	/* Erase flash block */
	ints = save_and_disable_interrupts();
	flash_range_erase(ctx->base + block * c->block_size, c->block_size);
	restore_interrupts(ints);

	return LFS_ERR_OK;
}

static int pico_block_device_sync(const struct lfs_config *c)
{
	return LFS_ERR_OK;
}

#ifdef LFS_THREADSAFE
static int pico_block_device_lock(const struct lfs_config *c)
{
	struct pico_lfs_context *ctx = (struct pico_lfs_context*)c->context;

	recursive_mutex_enter_blocking(&ctx->mutex);

	return LFS_ERR_OK;
}

static int pico_block_device_unlock(const struct lfs_config *c)
{
	struct pico_lfs_context *ctx = (struct pico_lfs_context*)c->context;

	recursive_mutex_exit(&ctx->mutex);

	return LFS_ERR_OK;
}
#endif


struct lfs_config* pico_lfs_init(size_t offset, size_t size)
{
	struct pico_lfs_context *ctx;
	struct lfs_config *c;

	if (!size)
		return NULL;

	/* Offset and size must align with flash block size. */
	if (offset % FLASH_SECTOR_SIZE != 0 || size % FLASH_SECTOR_SIZE != 0)
		return NULL;

	if (!(ctx = calloc(1, sizeof(struct pico_lfs_context))))
		return NULL;

	ctx->base = (uint32_t)offset;
#ifdef LFS_THREADSAFE
	recursive_mutex_init(&ctx->mutex);
	LFS_DEBUG("LFS_THREADSAFE enabled");
#endif
#ifdef LIB_PICO_MULTICORE
	ctx->multicore_lockout_enabled = true;
	LFS_DEBUG("LIB_PICO_MULTICORE enabled");
#endif

	c = &ctx->cfg;
	c->context = ctx;

	/* Callbacks to handle I/O */
	c->read  = pico_block_device_read;
	c->prog  = pico_block_device_prog;
	c->erase = pico_block_device_erase;
	c->sync  = pico_block_device_sync;
#ifdef LFS_THREADSAFE
	c->lock = pico_block_device_lock;
	c->unlock = pico_block_device_unlock;
#endif

	/* Configure lfs settings */
	c->read_size = 1;
	c->prog_size = FLASH_PAGE_SIZE;
        c->block_size = FLASH_SECTOR_SIZE;
        c->block_count = size / FLASH_SECTOR_SIZE;
	c->block_cycles = 300;
	c->cache_size = FLASH_PAGE_SIZE * 4;
	c->lookahead_size = 32;

	return c;
}

void pico_lfs_destroy(struct lfs_config *c)
{
	if (!c)
		return;

	memset(c, 0, sizeof(struct lfs_config));
	free(c);
}

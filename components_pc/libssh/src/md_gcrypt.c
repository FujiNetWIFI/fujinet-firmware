/*
 * This file is part of the SSH Library
 *
 * Copyright (c) 2009 by Aris Adamantiadis
 * Copyright (C) 2016 g10 Code GmbH
 *
 * The SSH Library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * The SSH Library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with the SSH Library; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA 02111-1307, USA.
 */

#include "config.h"

#include "libssh/crypto.h"
#include "libssh/wrapper.h"

#include <gcrypt.h>

SHACTX
sha1_init(void)
{
    SHACTX ctx = NULL;
    gcry_md_open(&ctx, GCRY_MD_SHA1, 0);

    return ctx;
}

void
sha1_update(SHACTX c, const void *data, size_t len)
{
    gcry_md_write(c, data, len);
}

void
sha1_final(unsigned char *md, SHACTX c)
{
    gcry_md_final(c);
    memcpy(md, gcry_md_read(c, 0), SHA_DIGEST_LEN);
    gcry_md_close(c);
}

void
sha1(const unsigned char *digest, size_t len, unsigned char *hash)
{
    gcry_md_hash_buffer(GCRY_MD_SHA1, hash, digest, len);
}

SHA256CTX
sha256_init(void)
{
    SHA256CTX ctx = NULL;
    gcry_md_open(&ctx, GCRY_MD_SHA256, 0);

    return ctx;
}

void
sha256_update(SHACTX c, const void *data, size_t len)
{
    gcry_md_write(c, data, len);
}

void
sha256_final(unsigned char *md, SHACTX c)
{
    gcry_md_final(c);
    memcpy(md, gcry_md_read(c, 0), SHA256_DIGEST_LEN);
    gcry_md_close(c);
}

void
sha256(const unsigned char *digest, size_t len, unsigned char *hash)
{
    gcry_md_hash_buffer(GCRY_MD_SHA256, hash, digest, len);
}

SHA384CTX
sha384_init(void)
{
    SHA384CTX ctx = NULL;
    gcry_md_open(&ctx, GCRY_MD_SHA384, 0);

    return ctx;
}

void
sha384_update(SHACTX c, const void *data, size_t len)
{
    gcry_md_write(c, data, len);
}

void
sha384_final(unsigned char *md, SHACTX c)
{
    gcry_md_final(c);
    memcpy(md, gcry_md_read(c, 0), SHA384_DIGEST_LEN);
    gcry_md_close(c);
}

void
sha384(const unsigned char *digest, size_t len, unsigned char *hash)
{
    gcry_md_hash_buffer(GCRY_MD_SHA384, hash, digest, len);
}

SHA512CTX
sha512_init(void)
{
    SHA512CTX ctx = NULL;
    gcry_md_open(&ctx, GCRY_MD_SHA512, 0);

    return ctx;
}

void
sha512_update(SHACTX c, const void *data, size_t len)
{
    gcry_md_write(c, data, len);
}

void
sha512_final(unsigned char *md, SHACTX c)
{
    gcry_md_final(c);
    memcpy(md, gcry_md_read(c, 0), SHA512_DIGEST_LEN);
    gcry_md_close(c);
}

void
sha512(const unsigned char *digest, size_t len, unsigned char *hash)
{
    gcry_md_hash_buffer(GCRY_MD_SHA512, hash, digest, len);
}

MD5CTX
md5_init(void)
{
    MD5CTX c = NULL;
    gcry_md_open(&c, GCRY_MD_MD5, 0);

    return c;
}

void
md5_update(MD5CTX c, const void *data, size_t len)
{
    gcry_md_write(c, data, len);
}

void
md5_final(unsigned char *md, MD5CTX c)
{
    gcry_md_final(c);
    memcpy(md, gcry_md_read(c, 0), MD5_DIGEST_LEN);
    gcry_md_close(c);
}

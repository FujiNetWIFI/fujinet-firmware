/*
 * ndif_blocks_test.c - standalone spot check for ndif_read_blocks()
 * (lib/stuffit/ndif.h), driven by tests/ndif_test.sh as part of
 * ndif_sample_tests.
 *
 * Opens one entry of a StuffIt archive (data fork to a tmpfile,
 * resource fork to memory - same two sit_extract() calls unsit.c's -i
 * mode makes) via ndif_open(), then for a handful of deterministically
 * (srand(42)) chosen block ranges calls ndif_read_blocks() and compares
 * the bytes it returns against the corresponding byte range of an
 * already-decoded reference raw image file (produced independently,
 * e.g. by `unsit -i` or the ndif2raw oracle - see ndif_test.sh). This
 * exercises ndif.c's random-access path (ndif_extract()'s own
 * in-order, whole-image path is covered separately by the byte-for-byte
 * comparison against ndif2raw that ndif_test.sh does with `unsit -i`).
 *
 * A standalone program rather than another unsit.c mode because it
 * needs its own bounded random-sampling loop rather than a single
 * one-shot extract; it links the same lib/stuffit sources unsit does
 * (see tests/CMakeLists.txt) to avoid depending on unsit's internals.
 *
 * usage: ndif_blocks_test archive.sit entrypath reference_raw_file
 */
#include "stuffit/stuffit.h"
#include "stuffit/ndif.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SPOT_CHECKS   5
#define SPOT_NBLOCKS  8u
#define NDIF_TEST_BLOCK_SIZE 512u

static int file_sink(const uint8_t *buf, size_t n, void *ctx)
{
    FILE *out = (FILE *)ctx;
    return fwrite(buf, 1, n, out) != n;
}

typedef struct { uint8_t *buf; size_t off; size_t cap; } mem_sink_ctx;

static int mem_sink(const uint8_t *buf, size_t n, void *ctx)
{
    mem_sink_ctx *c = (mem_sink_ctx *)ctx;
    if (c->off + n > c->cap) return 1;
    memcpy(c->buf + c->off, buf, n);
    c->off += n;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: %s archive.sit entrypath reference_raw_file\n", argv[0]);
        return 2;
    }
    const char *archive_path = argv[1];
    const char *wantpath = argv[2];
    const char *ref_path = argv[3];

    FILE *archf = fopen(archive_path, "rb");
    if (!archf) {
        fprintf(stderr, "ndif_blocks_test: cannot open %s\n", archive_path);
        return 1;
    }

    sit_archive ar;
    int rc = sit_open(&ar, archf, NULL);
    if (rc != SIT_OK) {
        fprintf(stderr, "ndif_blocks_test: sit_open failed: %s\n", sit_strerror(rc));
        fclose(archf);
        return 1;
    }

    sit_entry e;
    int found = 0;
    while ((rc = sit_next_entry(&ar, &e)) == 1) {
        if (strcmp(e.path, wantpath) == 0) { found = 1; break; }
    }
    if (rc < 0 || !found) {
        fprintf(stderr, "ndif_blocks_test: entry not found or parse error: %s\n", wantpath);
        sit_close(&ar);
        fclose(archf);
        return 1;
    }

    FILE *datatmp = tmpfile();
    if (!datatmp) {
        fprintf(stderr, "ndif_blocks_test: tmpfile() failed\n");
        sit_close(&ar);
        fclose(archf);
        return 1;
    }
    rc = sit_extract(&ar, &e, SIT_FORK_DATA, file_sink, datatmp, NULL);
    if (rc != SIT_OK) {
        fprintf(stderr, "ndif_blocks_test: data fork extract failed: %s\n", sit_strerror(rc));
        fclose(datatmp);
        sit_close(&ar);
        fclose(archf);
        return 1;
    }
    rewind(datatmp);

    uint8_t *rsrcbuf = (uint8_t *)malloc(e.rsrc_len);
    if (!rsrcbuf) {
        fprintf(stderr, "ndif_blocks_test: out of memory (%u bytes)\n", e.rsrc_len);
        fclose(datatmp);
        sit_close(&ar);
        fclose(archf);
        return 1;
    }
    mem_sink_ctx rctx = { rsrcbuf, 0, e.rsrc_len };
    rc = sit_extract(&ar, &e, SIT_FORK_RSRC, mem_sink, &rctx, NULL);
    if (rc != SIT_OK) {
        fprintf(stderr, "ndif_blocks_test: resource fork extract failed: %s\n", sit_strerror(rc));
        free(rsrcbuf);
        fclose(datatmp);
        sit_close(&ar);
        fclose(archf);
        return 1;
    }

    ndif_image img;
    rc = ndif_open(&img, datatmp, rsrcbuf, e.rsrc_len, &sit_libc_allocator);
    if (rc != NDIF_OK) {
        fprintf(stderr, "ndif_blocks_test: ndif_open failed: %s\n", ndif_strerror(rc));
        free(rsrcbuf);
        fclose(datatmp);
        sit_close(&ar);
        fclose(archf);
        return 1;
    }

    FILE *reff = fopen(ref_path, "rb");
    if (!reff) {
        fprintf(stderr, "ndif_blocks_test: cannot open reference file %s\n", ref_path);
        ndif_close(&img);
        free(rsrcbuf);
        fclose(datatmp);
        sit_close(&ar);
        fclose(archf);
        return 1;
    }

    int rv = 0;
    if (img.block_count <= SPOT_NBLOCKS) {
        fprintf(stderr, "ndif_blocks_test: image too small for spot check (%u blocks)\n", img.block_count);
        rv = 1;
    } else {
        srand(42);
        int failures = 0;
        uint8_t got[SPOT_NBLOCKS * NDIF_TEST_BLOCK_SIZE];
        uint8_t want[SPOT_NBLOCKS * NDIF_TEST_BLOCK_SIZE];
        uint32_t max_block = img.block_count - SPOT_NBLOCKS;

        for (int i = 0; i < SPOT_CHECKS; i++) {
            uint32_t block = (uint32_t)(((double)rand() / ((double)RAND_MAX + 1.0)) * ((double)max_block + 1.0));

            rc = ndif_read_blocks(&img, block, SPOT_NBLOCKS, got);
            if (rc != NDIF_OK) {
                fprintf(stderr, "ndif_blocks_test: FAIL: ndif_read_blocks(block=%u, n=%u): %s\n",
                        block, SPOT_NBLOCKS, ndif_strerror(rc));
                failures++;
                continue;
            }

            if (fseek(reff, (long)block * (long)NDIF_TEST_BLOCK_SIZE, SEEK_SET) != 0) {
                fprintf(stderr, "ndif_blocks_test: FAIL: fseek on reference file failed (block=%u)\n", block);
                failures++;
                continue;
            }
            size_t rn = fread(want, 1, sizeof(want), reff);
            if (rn != sizeof(want)) {
                fprintf(stderr, "ndif_blocks_test: FAIL: short read on reference file at block %u\n", block);
                failures++;
                continue;
            }

            if (memcmp(got, want, sizeof(got)) != 0) {
                fprintf(stderr, "ndif_blocks_test: FAIL: block %u (%u blocks) differs from reference\n",
                        block, SPOT_NBLOCKS);
                failures++;
            } else {
                printf("ndif_blocks_test: OK: %s block %u (%u blocks) matches reference\n",
                       wantpath, block, SPOT_NBLOCKS);
            }
        }

        if (failures) {
            fprintf(stderr, "ndif_blocks_test: %d/%d spot check(s) failed for %s\n", failures, SPOT_CHECKS, wantpath);
            rv = 1;
        }
    }

    fclose(reff);
    ndif_close(&img);
    free(rsrcbuf);
    fclose(datatmp);
    sit_close(&ar);
    fclose(archf);
    return rv;
}

/*
 * unsit.c - host command-line tool for lib/stuffit/, exercising the
 * public sit_open/sit_next_entry/sit_extract/sit_close API (and, for
 * .hqx input, binhex.h's hqx_open/hqx_data_fork/hqx_close).
 *
 * Usage:
 *   unsit -l archive.sit[.hqx]                 list entries, both forks
 *   unsit -x archive.sit[.hqx] path outfile     extract one entry's data fork
 *   unsit -xr archive.sit[.hqx] path outfile    extract one entry's resource fork
 *   unsit -a archive.sit[.hqx] outdir           extract every entry into outdir
 *   unsit -i archive.sit[.hqx] path outfile     unstuff+undiskimage: decode
 *                                               entry's NDIF (Disk Copy 6)
 *                                               image to a raw disk image
 *
 * A BinHex-wrapped archive (.hqx, or any file starting with the BinHex
 * ASCII banner regardless of extension) is unwrapped transparently in
 * every mode: unsit always tries hqx_open() first and only falls back
 * to opening the file directly as a bare .sit when hqx_open() reports
 * SIT_E_FORMAT (i.e. no BinHex banner found at all).
 *
 * -i is lib/stuffit/ndif.c's test harness: it extracts one entry's data
 * fork to a temp file and its resource fork to memory (same
 * sit_next_entry()/sit_extract() calls as -x/-xr), then feeds both to
 * ndif_open()/ndif_extract() to decode the NDIF image inside - the
 * StuffIt archive's own compression is unrelated to and independent of
 * NDIF's chunk compression, so both layers run back to back.
 */
#include "stuffit/stuffit.h"
#include "stuffit/binhex.h"
#include "stuffit/ndif.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#include <direct.h> // _mkdir
#define mkdir(dir, mode) _mkdir(dir)
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

static const char *method_name(int m)
{
    switch (m) {
        case 0:  return "none";
        case 1:  return "RLE";
        case 2:  return "Compress";
        case 3:  return "Huffman";
        case 5:  return "LZAH";
        case 6:  return "FixedHuffman";
        case 8:  return "MW";
        case 13: return "LZ+Huffman";
        case 14: return "Installer";
        case 15: return "Arsenic";
        default: return "unknown";
    }
}

static int file_sink(const uint8_t *buf, size_t n, void *ctx)
{
    FILE *out = (FILE *)ctx;
    return fwrite(buf, 1, n, out) != n;
}

/* sink for do_ndif_extract()'s resource-fork extraction: `cap` is
 * known exactly ahead of time (sit_entry's rsrc_len), so this just
 * refuses to overrun rather than growing. */
typedef struct { uint8_t *buf; size_t off; size_t cap; } mem_sink_ctx;

static int mem_sink(const uint8_t *buf, size_t n, void *ctx)
{
    mem_sink_ctx *c = (mem_sink_ctx *)ctx;
    if (c->off + n > c->cap) return 1;
    memcpy(c->buf + c->off, buf, n);
    c->off += n;
    return 0;
}

/* mkdir -p equivalent, only for the plain '/'-separated relative paths
 * sit_entry produces. */
static int mkdir_p(const char *dir)
{
    char tmp[1024];
    size_t len = strlen(dir);
    if (len == 0 || len >= sizeof(tmp)) return -1;
    memcpy(tmp, dir, len + 1);

    for (size_t i = 1; i < len; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            if (mkdir(tmp, 0777) != 0 && errno != EEXIST) return -1;
            tmp[i] = '/';
        }
    }
    if (mkdir(tmp, 0777) != 0 && errno != EEXIST) return -1;
    return 0;
}

static void split_dir(const char *path, char *dirbuf, size_t dirbufsz)
{
    const char *slash = strrchr(path, '/');
    if (!slash) { dirbuf[0] = '\0'; return; }
    size_t n = (size_t)(slash - path);
    if (n >= dirbufsz) n = dirbufsz - 1;
    memcpy(dirbuf, path, n);
    dirbuf[n] = '\0';
}

/* ------------------------------------------------------------------- */
/* input handling: opens argv's archive, transparently unwrapping a     */
/* BinHex (.hqx) layer if present, and yields an already-sit_open()'d   */
/* archive. Everything opened here is torn down by unsit_close_input(). */
/* ------------------------------------------------------------------- */

typedef struct {
    FILE *archf;      /* the file named on the command line */
    FILE *innerf;      /* fmemopen()'d over hqx.data, only when hqx_used */
    hqx_file hqx;       /* only valid when hqx_used */
    int hqx_used;
} unsit_input;

static int unsit_open_input(const char *path, unsit_input *in, sit_archive *ar)
{
    memset(in, 0, sizeof(*in));

    in->archf = fopen(path, "rb");
    if (!in->archf) return SIT_E_IO;

    int hrc = hqx_open(in->archf, NULL, &in->hqx);
    if (hrc == SIT_OK) {
        in->hqx_used = 1;
        in->innerf = hqx_data_fork(&in->hqx);
        if (!in->innerf) {
            hqx_close(&in->hqx);
            fclose(in->archf);
            in->archf = NULL;
            return SIT_E_IO;
        }
        return sit_open(ar, in->innerf, NULL);
    }

    if (hrc != SIT_E_FORMAT) {
        /* Looked like BinHex (banner found) but failed to decode - a
         * real error, not "try it as a bare .sit instead". */
        fclose(in->archf);
        in->archf = NULL;
        return hrc;
    }

    /* No BinHex banner at all - open directly as a bare .sit/.sea. */
    if (fseek(in->archf, 0, SEEK_SET) != 0) {
        fclose(in->archf);
        in->archf = NULL;
        return SIT_E_IO;
    }
    return sit_open(ar, in->archf, NULL);
}

static void unsit_close_input(unsit_input *in, sit_archive *ar)
{
    sit_close(ar);
    if (in->hqx_used) {
        if (in->innerf) fclose(in->innerf);
        hqx_close(&in->hqx);
    }
    if (in->archf) fclose(in->archf);
}

/* ------------------------------------------------------------------- */
/* commands                                                              */
/* ------------------------------------------------------------------- */

static int do_list(sit_archive *ar)
{
    sit_entry e;
    int count = 0;
    int rc;
    while ((rc = sit_next_entry(ar, &e)) == 1) {
        printf("%-48s data:%-12s %10u %10u %04x%s",
               e.path, method_name(e.data_method), e.data_len, e.data_comp_len,
               e.data_crc, (e.flags & SIT_FLAG_ENCRYPTED) ? " [encrypted]" : "");

        if (e.rsrc_len || e.rsrc_comp_len) {
            printf("  rsrc:%-12s %10u %10u %04x%s",
                   method_name(e.rsrc_method), e.rsrc_len, e.rsrc_comp_len,
                   e.rsrc_crc, (e.flags & SIT_FLAG_RSRC_ENCRYPTED) ? " [encrypted]" : "");
        }
        printf("\n");
        count++;
    }

    if (rc < 0) {
        fprintf(stderr, "unsit: parse error after %d entries: %s\n", count, sit_strerror(rc));
        return 1;
    }
    return 0;
}

static int do_extract_one(sit_archive *ar, const char *wantpath, const char *outfile, sit_fork which)
{
    sit_entry e;
    int found = 0;
    int rc;
    while ((rc = sit_next_entry(ar, &e)) == 1) {
        if (strcmp(e.path, wantpath) == 0) { found = 1; break; }
    }
    if (rc < 0) {
        fprintf(stderr, "unsit: parse error: %s\n", sit_strerror(rc));
        return 1;
    }
    if (!found) {
        fprintf(stderr, "unsit: no such entry: %s\n", wantpath);
        return 1;
    }

    FILE *out = fopen(outfile, "wb");
    if (!out) {
        fprintf(stderr, "unsit: cannot create %s: %s\n", outfile, strerror(errno));
        return 1;
    }

    rc = sit_extract(ar, &e, which, file_sink, out, NULL);
    fclose(out);

    if (rc != SIT_OK) {
        fprintf(stderr, "unsit: extract failed for %s (%s fork): %s\n", wantpath,
                which == SIT_FORK_RSRC ? "resource" : "data", sit_strerror(rc));
        return 1;
    }
    return 0;
}

/* Extracts every entry's data fork, plus - purely as a testing/
 * inspection convention, not anything lib/stuffit or any real tool
 * imposes - a same-path ".rsrc" sidecar for any entry that also has a
 * resource fork (outdir/Sub/File for the data fork, outdir/Sub/File.rsrc
 * for the resource fork). */
static int do_extract_all(sit_archive *ar, const char *outdir)
{
    if (mkdir_p(outdir) != 0) {
        fprintf(stderr, "unsit: cannot create %s: %s\n", outdir, strerror(errno));
        return 1;
    }

    int extracted = 0, skipped = 0;
    sit_entry e;
    int rc;
    while ((rc = sit_next_entry(ar, &e)) == 1) {
        int has_data = e.data_len != 0 || e.data_comp_len != 0;
        int has_rsrc = e.rsrc_len != 0 || e.rsrc_comp_len != 0;

        if (!has_data && !has_rsrc) { skipped++; continue; }

        char fullpath[2048];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", outdir, e.path);

        char dirbuf[2048];
        split_dir(fullpath, dirbuf, sizeof(dirbuf));
        if (dirbuf[0] && mkdir_p(dirbuf) != 0) {
            fprintf(stderr, "unsit: cannot create directory %s: %s\n", dirbuf, strerror(errno));
            skipped++;
            continue;
        }

        if (has_data) {
            if (e.flags & SIT_FLAG_ENCRYPTED) {
                fprintf(stderr, "unsit: skipping encrypted data fork: %s\n", e.path);
                skipped++;
            } else {
                FILE *out = fopen(fullpath, "wb");
                if (!out) {
                    fprintf(stderr, "unsit: cannot create %s: %s\n", fullpath, strerror(errno));
                    skipped++;
                } else {
                    int erc = sit_extract(ar, &e, SIT_FORK_DATA, file_sink, out, NULL);
                    fclose(out);
                    if (erc != SIT_OK) {
                        fprintf(stderr, "unsit: skipping %s data fork (method %s): %s\n",
                                e.path, method_name(e.data_method), sit_strerror(erc));
                        remove(fullpath);
                        skipped++;
                    } else {
                        extracted++;
                    }
                }
            }
        }

        if (has_rsrc) {
            char rsrcpath[2064];
            snprintf(rsrcpath, sizeof(rsrcpath), "%s.rsrc", fullpath);

            if (e.flags & SIT_FLAG_RSRC_ENCRYPTED) {
                fprintf(stderr, "unsit: skipping encrypted resource fork: %s\n", e.path);
                skipped++;
            } else {
                FILE *out = fopen(rsrcpath, "wb");
                if (!out) {
                    fprintf(stderr, "unsit: cannot create %s: %s\n", rsrcpath, strerror(errno));
                    skipped++;
                } else {
                    int erc = sit_extract(ar, &e, SIT_FORK_RSRC, file_sink, out, NULL);
                    fclose(out);
                    if (erc != SIT_OK) {
                        fprintf(stderr, "unsit: skipping %s resource fork (method %s): %s\n",
                                e.path, method_name(e.rsrc_method), sit_strerror(erc));
                        remove(rsrcpath);
                        skipped++;
                    } else {
                        extracted++;
                    }
                }
            }
        }
    }

    if (rc < 0) {
        fprintf(stderr, "unsit: parse error after %d extracted: %s\n", extracted, sit_strerror(rc));
        return 1;
    }

    fprintf(stderr, "unsit: extracted %d, skipped %d\n", extracted, skipped);
    return 0;
}

/* Finds entry `wantpath`, extracts its data fork to a tmpfile() (ndif_open
 * wants a seekable FILE*) and its resource fork to an exactly-sized malloc'd
 * buffer (rsrc_len is already known from sit_entry, so no growable buffer is
 * needed), then decodes the NDIF image inside via ndif_open()/ndif_extract()
 * into outfile. */
static int do_ndif_extract(sit_archive *ar, const char *wantpath, const char *outfile)
{
    sit_entry e;
    int found = 0;
    int rc;
    while ((rc = sit_next_entry(ar, &e)) == 1) {
        if (strcmp(e.path, wantpath) == 0) { found = 1; break; }
    }
    if (rc < 0) {
        fprintf(stderr, "unsit: parse error: %s\n", sit_strerror(rc));
        return 1;
    }
    if (!found) {
        fprintf(stderr, "unsit: no such entry: %s\n", wantpath);
        return 1;
    }
    if (e.rsrc_len == 0) {
        fprintf(stderr, "unsit: %s has no resource fork (needed for its NDIF 'bcem' block map)\n", wantpath);
        return 1;
    }

    FILE *datatmp = tmpfile();
    if (!datatmp) {
        fprintf(stderr, "unsit: cannot create temp file for %s data fork: %s\n", wantpath, strerror(errno));
        return 1;
    }

    rc = sit_extract(ar, &e, SIT_FORK_DATA, file_sink, datatmp, NULL);
    if (rc != SIT_OK) {
        fprintf(stderr, "unsit: extract failed for %s (data fork): %s\n", wantpath, sit_strerror(rc));
        fclose(datatmp);
        return 1;
    }
    rewind(datatmp);

    uint8_t *rsrcbuf = (uint8_t *)malloc(e.rsrc_len);
    if (!rsrcbuf) {
        fprintf(stderr, "unsit: out of memory for %s resource fork (%u bytes)\n", wantpath, e.rsrc_len);
        fclose(datatmp);
        return 1;
    }

    mem_sink_ctx rctx = { rsrcbuf, 0, e.rsrc_len };
    rc = sit_extract(ar, &e, SIT_FORK_RSRC, mem_sink, &rctx, NULL);
    if (rc != SIT_OK) {
        fprintf(stderr, "unsit: extract failed for %s (resource fork): %s\n", wantpath, sit_strerror(rc));
        free(rsrcbuf);
        fclose(datatmp);
        return 1;
    }

    ndif_image img;
    rc = ndif_open(&img, datatmp, rsrcbuf, e.rsrc_len, &sit_libc_allocator);
    if (rc != NDIF_OK) {
        fprintf(stderr, "unsit: ndif_open failed for %s: %s\n", wantpath, ndif_strerror(rc));
        free(rsrcbuf);
        fclose(datatmp);
        return 1;
    }

    FILE *out = fopen(outfile, "wb");
    if (!out) {
        fprintf(stderr, "unsit: cannot create %s: %s\n", outfile, strerror(errno));
        ndif_close(&img);
        free(rsrcbuf);
        fclose(datatmp);
        return 1;
    }

    rc = ndif_extract(&img, file_sink, out);
    fclose(out);
    ndif_close(&img);
    free(rsrcbuf);
    fclose(datatmp);

    if (rc != NDIF_OK) {
        fprintf(stderr, "unsit: ndif_extract failed for %s: %s\n", wantpath, ndif_strerror(rc));
        return 1;
    }
    return 0;
}

static void usage(const char *argv0)
{
    fprintf(stderr,
        "usage:\n"
        "  %s -l archive.sit[.hqx]\n"
        "  %s -x archive.sit[.hqx] entrypath outfile\n"
        "  %s -xr archive.sit[.hqx] entrypath outfile\n"
        "  %s -a archive.sit[.hqx] outdir\n"
        "  %s -i archive.sit[.hqx] entrypath outfile   (decode entry's NDIF image)\n",
        argv0, argv0, argv0, argv0, argv0);
}

int main(int argc, char **argv)
{
    if (argc < 3) { usage(argv[0]); return 2; }

    const char *mode = argv[1];
    const char *archive = argv[2];

    int wantargc;
    if (strcmp(mode, "-l") == 0) wantargc = 3;
    else if (strcmp(mode, "-x") == 0 || strcmp(mode, "-xr") == 0 || strcmp(mode, "-i") == 0) wantargc = 5;
    else if (strcmp(mode, "-a") == 0) wantargc = 4;
    else { usage(argv[0]); return 2; }

    if (argc != wantargc) { usage(argv[0]); return 2; }

    unsit_input in;
    sit_archive ar;
    int rc = unsit_open_input(archive, &in, &ar);
    if (rc != SIT_OK) {
        fprintf(stderr, "unsit: open failed for %s: %s\n", archive, sit_strerror(rc));
        if (in.archf) fclose(in.archf);
        return 1;
    }

    if (strcmp(mode, "-l") == 0) {
        rc = do_list(&ar);
    } else if (strcmp(mode, "-x") == 0) {
        rc = do_extract_one(&ar, argv[3], argv[4], SIT_FORK_DATA);
    } else if (strcmp(mode, "-xr") == 0) {
        rc = do_extract_one(&ar, argv[3], argv[4], SIT_FORK_RSRC);
    } else if (strcmp(mode, "-i") == 0) {
        rc = do_ndif_extract(&ar, argv[3], argv[4]);
    } else {
        rc = do_extract_all(&ar, argv[3]);
    }

    unsit_close_input(&in, &ar);
    return rc;
}

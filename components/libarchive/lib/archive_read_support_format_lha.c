/*-
 * Copyright (c) 2008-2014 Michihiro NAKAJIMA
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "archive_platform.h"

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include "archive.h"
#include "archive_entry.h"
#include "archive_entry_locale.h"
#include "archive_private.h"
#include "archive_read_private.h"
#include "archive_time_private.h"
#include "archive_endian.h"


#define MAXMATCH		256	/* Maximum match length. */
#define MINMATCH		3	/* Minimum match length. */
/*
 * Literal table format:
 * +0              +256                      +510
 * +---------------+-------------------------+
 * | literal code  |       match length      |
 * |   0 ... 255   |  MINMATCH ... MAXMATCH  |
 * +---------------+-------------------------+
 *  <---          LT_BITLEN_SIZE         --->
 */
/* Literal table size. */
#define LT_BITLEN_SIZE		(UCHAR_MAX + 1 + MAXMATCH - MINMATCH + 1)
/* Position table size.
 * Note: this used for both position table and pre literal table.*/
#define PT_BITLEN_SIZE		(3 + 16)

/*
 * -lh1- parameters.
 *
 * -lh1- is LHarc 1.x's method: a 4KiB LZSS window whose literal/length
 * alphabet is coded with an ADAPTIVE ("dynamic") Huffman tree that both
 * sides update after every symbol, and whose match positions come from a
 * FIXED Huffman table rather than one stored in the stream. It therefore
 * shares nothing with -lh5-/-lh6-/-lh7- above the LZSS layer: there are no
 * blocks, no transmitted code lengths, and no end marker - the uncompressed
 * size recorded in the header is the only terminator.
 *
 * The tree is the one described in dhuf.c of 'LHa for UNIX' (start_c_dyn,
 * swap_inc, reconst, update_c), reimplemented here as a resumable decoder.
 */
#define LZH1_MAXMATCH	60
/* Literal/length alphabet size: 256 literals + lengths MINMATCH..60. */
#define LZH1_NC		(UCHAR_MAX + 1 + LZH1_MAXMATCH - MINMATCH + 1)	/* 314 */
/* Node indices run 0 .. LZH1_NC*2-2; the root is 0. */
#define LZH1_TREESIZE	(LZH1_NC * 2)					/* 628 */
#define LZH1_ROOT	0
/* Position symbols: the high 6 bits of a 12-bit distance. */
#define LZH1_NP		(1 << (12 - 6))					/* 64 */
/*
 * Block ids are handed out from stock[] and used as edge[] indices. Every
 * node can end up in a block of its own, so up to LZH1_TREESIZE-1 ids are
 * live at once on top of the two the initial tree reserves: size both
 * arrays with that slack rather than bounding avail at run time.
 */
#define LZH1_STOCK_SIZE	(LZH1_TREESIZE + 2)

/*
 * Decoding status; ds->state holds one of these. ST_GET_LITERAL is the
 * boundary lzh_decode() dispatches on: below it a block header is being
 * read, at or above it compressed data is being decoded.
 */
#define ST_RD_BLOCK		0
#define ST_RD_PT_1		1
#define ST_RD_PT_2		2
#define ST_RD_PT_3		3
#define ST_RD_PT_4		4
#define ST_RD_LITERAL_1		5
#define ST_RD_LITERAL_2		6
#define ST_RD_LITERAL_3		7
#define ST_RD_POS_DATA_1	8
#define ST_GET_LITERAL		9
#define ST_GET_POS_1		10
#define ST_GET_POS_2		11
#define ST_COPY_DATA		12
/* -lh1- only. */
#define ST_LH1_GET_LITERAL	13
#define ST_LH1_TREE		14
#define ST_LH1_GET_POS_1	15
#define ST_LH1_GET_POS_2	16

/*
 * -lh1- adaptive Huffman tree over the literal/length alphabet.
 *
 * Allocated only for -lh1- entries; it is ~8KiB and every other method
 * leaves it NULL. Note there are deliberately no position-tree arrays:
 * -lh1- positions use the fixed table below, and only -lh2- (unsupported)
 * codes positions adaptively.
 *
 * child[] holds an internal node index when positive and ~symbol when
 * negative; node[] is the reverse map from symbol to leaf index.
 */
struct lzh_dyn {
	int16_t		 child[LZH1_TREESIZE];
	uint16_t	 parent[LZH1_TREESIZE];
	uint16_t	 freq[LZH1_TREESIZE];
	uint16_t	 block[LZH1_TREESIZE];
	uint16_t	 edge[LZH1_STOCK_SIZE];
	uint16_t	 stock[LZH1_STOCK_SIZE];
	uint16_t	 node[LZH1_NC];
	int		 avail;
	/* Node reached so far by a tree walk suspended for want of input. */
	int		 walk;
	/* Fixed position table: 8 bits of lookahead resolve a symbol
	 * outright, since no code in it is longer than 8 bits. */
	unsigned char	 pt_tbl[1 << 8];
	unsigned char	 pt_len[LZH1_NP];
};

struct lzh_dec {
	/* Decoding status. */
	int     		 state;

	/*
	 * Window to see last 8Ki(lh5),32Ki(lh6),64Ki(lh7) bytes of decoded
	 * data.
	 */
	int			 w_size;
	int			 w_mask;
	/* Window buffer, which is a loop buffer. */
	unsigned char		*w_buff;
	/* The insert position to the window. */
	int			 w_pos;
	/* The position where we can copy decoded code from the window. */
	int     		 copy_pos;
	/* The length how many bytes we can copy decoded code from
	 * the window. */
	int     		 copy_len;

	/*
	 * Bit stream reader.
	 */
	struct lzh_br {
#define CACHE_TYPE		uint64_t
#define CACHE_BITS		(8 * sizeof(CACHE_TYPE))
	 	/* Cache buffer. */
		CACHE_TYPE	 cache_buffer;
		/* Indicates how many bits avail in cache_buffer. */
		int		 cache_avail;
	} br;

	/*
	 * Huffman coding.
	 */
	struct huffman {
		int		 len_size;
		int		 len_avail;
		int		 len_bits;
		int		 freq[17];
		unsigned char	*bitlen;

		/*
		 * Use a index table. It's faster than searching a huffman
		 * coding tree, which is a binary tree. But a use of a large
		 * index table causes L1 cache read miss many times.
		 */
#define HTBL_BITS	10
		int		 max_bits;
		int		 shift_bits;
		int		 tbl_bits;
		int		 tree_used;
		int		 tree_avail;
		/* Direct access table. */
		uint16_t	*tbl;
		/* Binary tree table for extra bits over the direct access. */
		struct htree_t {
			uint16_t left;
			uint16_t right;
		}		*tree;
	}			 lt, pt;

	int			 blocks_avail;
	int			 pos_pt_len_size;
	int			 pos_pt_len_bits;
	int			 literal_pt_len_size;
	int			 literal_pt_len_bits;
	int			 reading_position;
	int			 loop;
	int			 error;

	/* The state a finished match returns to: ST_GET_LITERAL, or
	 * ST_LH1_GET_LITERAL when decoding -lh1-. */
	int			 literal_state;
	/* -lh1- only: NULL for every other method. */
	struct lzh_dyn		*dyn;
	/* -lh1- only: bytes of this entry still to emit. -lh1- has no end
	 * marker, so this is what ends the entry. */
	int64_t			 out_left;
};

struct lzh_stream {
	const unsigned char	*next_in;
	int			 avail_in;
	int64_t			 total_in;
	const unsigned char	*ref_ptr;
	int			 avail_out;
	int64_t			 total_out;
	struct lzh_dec		*ds;
};

struct lha {
	/* entry_bytes_remaining is the number of bytes we expect.	    */
	int64_t                  entry_offset;
	int64_t                  entry_bytes_remaining;
	int64_t			 entry_unconsumed;
	uint16_t		 entry_crc_calculated;
 
	size_t			 header_size;	/* header size		    */
	unsigned char		 level;		/* header level		    */
	char			 method[3];	/* compress type	    */
	int64_t			 compsize;	/* compressed data size	    */
	int64_t			 origsize;	/* original file size	    */
	int			 setflag;
#define BIRTHTIME_IS_SET	1
#define ATIME_IS_SET		2
#define UNIX_MODE_IS_SET	4
#define CRC_IS_SET		8
	int64_t			 birthtime;
	uint32_t		 birthtime_tv_nsec;
	int64_t			 mtime;
	uint32_t		 mtime_tv_nsec;
	int64_t			 atime;
	uint32_t		 atime_tv_nsec;
	mode_t			 mode;
	int64_t			 uid;
	int64_t			 gid;
	struct archive_string 	 uname;
	struct archive_string 	 gname;
	uint16_t		 header_crc;
	uint16_t		 crc;
	/* dirname and filename could be in different codepages */
	struct archive_string_conv *sconv_dir;
	struct archive_string_conv *sconv_fname;
	struct archive_string_conv *opt_sconv;
	/*
	 * "lha:sfx" option. The SFX scan is otherwise only entered for a
	 * DOS/PE executable ("MZ"), which no Commodore self-extracting
	 * archive is - a .sfx from a CBM archive is a PRG, so it begins with
	 * a two-byte load address and its LHA header sits a few KB in, behind
	 * the extractor stub. The scan is opt-in rather than unconditional
	 * because it reads up to 20 KB ahead, and lha is registered
	 * speculatively for every unknown extension.
	 */
	char			 opt_sfx;

	struct archive_string 	 dirname;
	struct archive_string 	 filename;
	struct archive_wstring	 ws;

	unsigned char		 dos_attr;

	/* Flag to mark progress that an archive was read their first header.*/
	char			 found_first_header;
	/* Flag to mark that indicates an empty directory. */
	char			 directory;

	/* Flags to mark progress of decompression. */
	char			 decompress_init;
	char			 end_of_entry;
	char			 end_of_entry_cleanup;
	char			 entry_is_compressed;

	char			 format_name[64];

	struct lzh_stream	 strm;
};

/*
 * LHA header common member offset.
 */
#define H_METHOD_OFFSET	2	/* Compress type. */
#define H_ATTR_OFFSET	19	/* DOS attribute. */
#define H_LEVEL_OFFSET	20	/* Header Level.  */
#define H_SIZE		22	/* Minimum header size. */

static int      archive_read_format_lha_bid(struct archive_read *, int);
static int      archive_read_format_lha_options(struct archive_read *,
		    const char *, const char *);
static int	archive_read_format_lha_read_header(struct archive_read *,
		    struct archive_entry *);
static int	archive_read_format_lha_read_data(struct archive_read *,
		    const void **, size_t *, int64_t *);
static int	archive_read_format_lha_read_data_skip(struct archive_read *);
static int	archive_read_format_lha_cleanup(struct archive_read *);

static void	lha_replace_path_separator(struct lha *,
		    struct archive_entry *);
static int	lha_read_file_header_0(struct archive_read *, struct lha *);
static int	lha_read_file_header_1(struct archive_read *, struct lha *);
static int	lha_read_file_header_2(struct archive_read *, struct lha *);
static int	lha_read_file_header_3(struct archive_read *, struct lha *);
static int	lha_read_file_extended_header(struct archive_read *,
		    struct lha *, uint16_t *, int, uint64_t, size_t *);
static size_t	lha_check_header_format(const void *);
static int	lha_level1_header_is_plausible(const unsigned char *);
static int	lha_skip_sfx(struct archive_read *);
static unsigned char	lha_calcsum(unsigned char, const void *,
		    int, size_t);
static int	lha_parse_linkname(struct archive_wstring *,
		    struct archive_wstring *);
static int	lha_read_data_none(struct archive_read *, const void **,
		    size_t *, int64_t *);
static int	lha_read_data_lzh(struct archive_read *, const void **,
		    size_t *, int64_t *);
static void	lha_crc16_init(void);
static uint16_t lha_crc16(uint16_t, const void *, size_t);
static int	lzh_decode_init(struct lzh_stream *, const char *, int64_t);
static void	lzh_decode_free(struct lzh_stream *);
static void	lzh_dyn_init_pt(struct lzh_dyn *);
static void	lzh_dyn_start(struct lzh_dyn *);
static void	lzh_dyn_reconst(struct lzh_dyn *, int, int);
static int	lzh_dyn_swap_inc(struct lzh_dyn *, int);
static void	lzh_dyn_update(struct lzh_dyn *, int);
static int	lzh_decode(struct lzh_stream *, int);
static int	lzh_br_fillup(struct lzh_stream *, struct lzh_br *);
static int	lzh_huffman_init(struct huffman *, size_t, int);
static void	lzh_huffman_free(struct huffman *);
static int	lzh_read_pt_bitlen(struct lzh_stream *, int start, int end);
static int	lzh_make_fake_table(struct huffman *, uint16_t);
static int	lzh_make_huffman_table(struct huffman *);
static inline int lzh_decode_huffman(struct huffman *, unsigned);
static int	lzh_decode_huffman_tree(struct huffman *, unsigned, int);


int
archive_read_support_format_lha(struct archive *_a)
{
	struct archive_read *a = (struct archive_read *)_a;
	struct lha *lha;
	int r;

	archive_check_magic(_a, ARCHIVE_READ_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_read_support_format_lha");

	lha = calloc(1, sizeof(*lha));
	if (lha == NULL) {
		archive_set_error(&a->archive, ENOMEM,
		    "Can't allocate lha data");
		return (ARCHIVE_FATAL);
	}
	archive_string_init(&lha->ws);

	r = __archive_read_register_format(a,
	    lha,
	    "lha",
	    archive_read_format_lha_bid,
	    archive_read_format_lha_options,
	    archive_read_format_lha_read_header,
	    archive_read_format_lha_read_data,
	    archive_read_format_lha_read_data_skip,
	    NULL,
	    archive_read_format_lha_cleanup,
	    NULL,
	    NULL);

	if (r != ARCHIVE_OK)
		free(lha);
	return (ARCHIVE_OK);
}

static size_t
lha_check_header_format(const void *h)
{
	const unsigned char *p = h;
	size_t next_skip_bytes;

	switch (p[H_METHOD_OFFSET+3]) {
	/*
	 * "-lh0-" ... "-lh7-" "-lhd-"
	 * "-lzs-" "-lz5-"
	 */
	case '0': case '1': case '2': case '3':
	case '4': case '5': case '6': case '7':
	case 'd':
	case 's':
		next_skip_bytes = 4;

		/* b0 == 0 means the end of an LHa archive file.	*/
		if (p[0] == 0)
			break;
		if (p[H_METHOD_OFFSET] != '-' || p[H_METHOD_OFFSET+1] != 'l'
		    ||  p[H_METHOD_OFFSET+4] != '-')
			break;

		if (p[H_METHOD_OFFSET+2] == 'h') {
			/* "-lh?-" */
			if (p[H_METHOD_OFFSET+3] == 's')
				break;
			if (p[H_LEVEL_OFFSET] == 0)
				return (0);
			if (p[H_LEVEL_OFFSET] <= 3 && p[H_ATTR_OFFSET] == 0x20)
				return (0);
			/*
			 * The attribute byte is only MS-DOS's. Amiga LhA puts
			 * AmigaOS protection bits there instead, so a real
			 * Amiga level-1 archive is rejected by the test above
			 * before a single entry is read. Accept a level-1
			 * header that is self-consistent instead of one that
			 * merely looks like DOS wrote it — see
			 * lha_level1_header_is_plausible(). Nothing downstream
			 * reads the attribute byte: lha->dos_attr comes from
			 * extended headers only.
			 *
			 * Level 0 is already accepted with no such check at
			 * all, and levels 2 and 3 have no name length in the
			 * first 22 bytes to check against, so they keep the
			 * 0x20 requirement.
			 */
			if (p[H_LEVEL_OFFSET] == 1
			    && lha_level1_header_is_plausible(p))
				return (0);
		}
		if (p[H_METHOD_OFFSET+2] == 'z') {
			/* LArc extensions: -lzs-,-lz4- and -lz5- */
			if (p[H_LEVEL_OFFSET] != 0)
				break;
			if (p[H_METHOD_OFFSET+3] == 's'
			    || p[H_METHOD_OFFSET+3] == '4'
			    || p[H_METHOD_OFFSET+3] == '5')
				return (0);
		}
		break;
	case 'h': next_skip_bytes = 1; break;
	case 'z': next_skip_bytes = 1; break;
	case 'l': next_skip_bytes = 2; break;
	case '-': next_skip_bytes = 3; break;
	default : next_skip_bytes = 4; break;
	}

	return (next_skip_bytes);
}

static int
archive_read_format_lha_bid(struct archive_read *a, int best_bid)
{
	const char *p;
	const void *buff;
	ssize_t bytes_avail, offset, window;
	size_t next;
	struct lha *lha;

	/* If there's already a better bid than we can ever
	   make, don't bother testing. */
	if (best_bid > 30)
		return (-1);

	if ((p = __archive_read_ahead(a, H_SIZE, NULL)) == NULL)
		return (-1);

	if (lha_check_header_format(p) == 0)
		return (30);

	/* choose_format() points a->format at the slot being bid, so the
	 * format's own data is reachable here. */
	lha = (struct lha *)(a->format->data);

	if ((p[0] == 'M' && p[1] == 'Z') || (lha != NULL && lha->opt_sfx)) {
		/* PE file, or a caller-declared self-extracting archive */
		offset = 0;
		window = 4096;
		while (offset < (1024 * 20)) {
			buff = __archive_read_ahead(a, offset + window,
			    &bytes_avail);
			if (buff == NULL) {
				/* Remaining bytes are less than window. */
				window >>= 1;
				if (window < (H_SIZE + 3))
					return (0);
				continue;
			}
			p = (const char *)buff + offset;
			while (p + H_SIZE < (const char *)buff + bytes_avail) {
				if ((next = lha_check_header_format(p)) == 0)
					return (30);
				p += next;
			}
			offset = p - (const char *)buff;
		}
	}
	return (0);
}

static int
archive_read_format_lha_options(struct archive_read *a,
    const char *key, const char *val)
{
	struct lha *lha;
	int ret = ARCHIVE_FAILED;

	lha = (struct lha *)(a->format->data);
	if (strcmp(key, "hdrcharset")  == 0) {
		if (val == NULL || val[0] == 0)
			archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
			    "lha: hdrcharset option needs a character-set name");
		else {
			lha->opt_sconv =
			    archive_string_conversion_from_charset(
				&a->archive, val, 0);
			if (lha->opt_sconv != NULL)
				ret = ARCHIVE_OK;
			else
				ret = ARCHIVE_FATAL;
		}
		return (ret);
	}
	if (strcmp(key, "sfx") == 0) {
		/* A bare "lha:sfx" arrives here with val == "1". */
		lha->opt_sfx = (val != NULL && val[0] != 0);
		return (ARCHIVE_OK);
	}

	/* Note: The "warn" return is just to inform the options
	 * supervisor that we didn't handle it.  It will generate
	 * a suitable error if no one used this option. */
	return (ARCHIVE_WARN);
}

static int
lha_skip_sfx(struct archive_read *a)
{
	const void *h;
	const char *p, *q;
	size_t next, skip;
	ssize_t bytes, window;

	window = 4096;
	for (;;) {
		h = __archive_read_ahead(a, window, &bytes);
		if (h == NULL) {
			/* Remaining bytes are less than window. */
			window >>= 1;
			if (window < (H_SIZE + 3))
				goto fatal;
			continue;
		}
		if (bytes < H_SIZE)
			goto fatal;
		p = h;
		q = p + bytes;

		/*
		 * Scan ahead until we find something that looks
		 * like the lha header.
		 */
		while (p + H_SIZE < q) {
			if ((next = lha_check_header_format(p)) == 0) {
				skip = p - (const char *)h;
				__archive_read_consume(a, skip);
				return (ARCHIVE_OK);
			}
			p += next;
		}
		skip = p - (const char *)h;
		__archive_read_consume(a, skip);
	}
fatal:
	archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
	    "Couldn't find out LHa header");
	return (ARCHIVE_FATAL);
}

static int
truncated_error(struct archive_read *a)
{
	archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
	    "Truncated LHa header");
	return (ARCHIVE_FATAL);
}

static int
archive_read_format_lha_read_header(struct archive_read *a,
    struct archive_entry *entry)
{
	struct archive_wstring linkname;
	struct archive_wstring pathname;
	struct lha *lha;
	const unsigned char *p;
	const char *signature;
	int err;
	struct archive_mstring conv_buffer;
	const wchar_t *conv_buffer_p;

	lha_crc16_init();

	a->archive.archive_format = ARCHIVE_FORMAT_LHA;
	if (a->archive.archive_format_name == NULL)
		a->archive.archive_format_name = "lha";

	lha = (struct lha *)(a->format->data);
	lha->decompress_init = 0;
	lha->end_of_entry = 0;
	lha->end_of_entry_cleanup = 0;
	lha->entry_unconsumed = 0;

	if ((p = __archive_read_ahead(a, H_SIZE, NULL)) == NULL) {
		/*
		 * LHa archiver added 0 to the tail of its archive file as
		 * the mark of the end of the archive.
		 */
		signature = __archive_read_ahead(a, sizeof(signature[0]), NULL);
		if (signature == NULL || signature[0] == 0)
			return (ARCHIVE_EOF);
		return (truncated_error(a));
	}

	signature = (const char *)p;
	if (lha->found_first_header == 0 &&
	    ((signature[0] == 'M' && signature[1] == 'Z') || lha->opt_sfx)) {
                /* This is an executable?  Must be self-extracting... 	*/
		err = lha_skip_sfx(a);
		if (err < ARCHIVE_WARN)
			return (err);

		if ((p = __archive_read_ahead(a, sizeof(*p), NULL)) == NULL)
			return (truncated_error(a));
		signature = (const char *)p;
	}
	/* signature[0] == 0 means the end of an LHa archive file. */
	if (signature[0] == 0)
		return (ARCHIVE_EOF);

	/*
	 * Check the header format and method type.
	 */
	if (lha_check_header_format(p) != 0) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Bad LHa file");
		return (ARCHIVE_FATAL);
	}

	/* We've found the first header. */
	lha->found_first_header = 1;
	/* Set a default value and common data */
	lha->header_size = 0;
	lha->level = p[H_LEVEL_OFFSET];
	lha->method[0] = p[H_METHOD_OFFSET+1];
	lha->method[1] = p[H_METHOD_OFFSET+2];
	lha->method[2] = p[H_METHOD_OFFSET+3];
	if (memcmp(lha->method, "lhd", 3) == 0)
		lha->directory = 1;
	else
		lha->directory = 0;
	if (memcmp(lha->method, "lh0", 3) == 0 ||
	    memcmp(lha->method, "lz4", 3) == 0)
		lha->entry_is_compressed = 0;
	else
		lha->entry_is_compressed = 1;

	lha->compsize = 0;
	lha->origsize = 0;
	lha->setflag = 0;
	lha->birthtime = 0;
	lha->birthtime_tv_nsec = 0;
	lha->mtime = 0;
	lha->mtime_tv_nsec = 0;
	lha->atime = 0;
	lha->atime_tv_nsec = 0;
	lha->mode = (lha->directory)? 0777 : 0666;
	lha->uid = 0;
	lha->gid = 0;
	archive_string_empty(&lha->dirname);
	archive_string_empty(&lha->filename);
	lha->dos_attr = 0;
	if (lha->opt_sconv != NULL) {
		lha->sconv_dir = lha->opt_sconv;
		lha->sconv_fname = lha->opt_sconv;
	} else {
		lha->sconv_dir = NULL;
		lha->sconv_fname = NULL;
	}

	switch (p[H_LEVEL_OFFSET]) {
	case 0:
		err = lha_read_file_header_0(a, lha);
		break;
	case 1:
		err = lha_read_file_header_1(a, lha);
		break;
	case 2:
		err = lha_read_file_header_2(a, lha);
		break;
	case 3:
		err = lha_read_file_header_3(a, lha);
		break;
	default:
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Unsupported LHa header level %d", p[H_LEVEL_OFFSET]);
		err = ARCHIVE_FATAL;
		break;
	}
	if (err < ARCHIVE_WARN)
		return (err);


	if (!lha->directory && archive_strlen(&lha->filename) == 0)
		/* The filename has not been set */
		return (truncated_error(a));

	/*
	 * Make a pathname from a dirname and a filename, after converting to Unicode.
	 * This is because codepages might differ between dirname and filename.
	*/
	archive_string_init(&pathname);
	archive_string_init(&linkname);
	archive_string_init(&conv_buffer.aes_mbs);
	archive_string_init(&conv_buffer.aes_mbs_in_locale);
	archive_string_init(&conv_buffer.aes_utf8);
	archive_string_init(&conv_buffer.aes_wcs);
	if (0 != archive_mstring_copy_mbs_len_l(&conv_buffer, lha->dirname.s, lha->dirname.length, lha->sconv_dir)) {
		archive_set_error(&a->archive,
			ARCHIVE_ERRNO_FILE_FORMAT,
			"Pathname cannot be converted "
			"from %s to Unicode.",
			archive_string_conversion_charset_name(lha->sconv_dir));
		err = ARCHIVE_FATAL;
	} else if (0 != archive_mstring_get_wcs(&a->archive, &conv_buffer, &conv_buffer_p))
		err = ARCHIVE_FATAL;
	if (err == ARCHIVE_FATAL) {
		archive_mstring_clean(&conv_buffer);
		archive_wstring_free(&pathname);
		archive_wstring_free(&linkname);
		return (err);
	}
	archive_wstring_copy(&pathname, &conv_buffer.aes_wcs);

	archive_string_empty(&conv_buffer.aes_mbs);
	archive_string_empty(&conv_buffer.aes_mbs_in_locale);
	archive_string_empty(&conv_buffer.aes_utf8);
	archive_wstring_empty(&conv_buffer.aes_wcs);
	if (0 != archive_mstring_copy_mbs_len_l(&conv_buffer, lha->filename.s, lha->filename.length, lha->sconv_fname)) {
		archive_set_error(&a->archive,
			ARCHIVE_ERRNO_FILE_FORMAT,
			"Pathname cannot be converted "
			"from %s to Unicode.",
			archive_string_conversion_charset_name(lha->sconv_fname));
		err = ARCHIVE_FATAL;
	}
	else if (0 != archive_mstring_get_wcs(&a->archive, &conv_buffer, &conv_buffer_p))
		err = ARCHIVE_FATAL;
	if (err == ARCHIVE_FATAL) {
		archive_mstring_clean(&conv_buffer);
		archive_wstring_free(&pathname);
		archive_wstring_free(&linkname);
		return (err);
	}
	archive_wstring_concat(&pathname, &conv_buffer.aes_wcs);
	archive_mstring_clean(&conv_buffer);

	if ((lha->mode & AE_IFMT) == AE_IFLNK) {
		/*
	 	 * Extract the symlink-name if it's included in the pathname.
	 	 */
		if (!lha_parse_linkname(&linkname, &pathname)) {
			/* We couldn't get the symlink-name. */
			archive_set_error(&a->archive,
		    	    ARCHIVE_ERRNO_FILE_FORMAT,
			    "Unknown symlink-name");
			archive_wstring_free(&pathname);
			archive_wstring_free(&linkname);
			return (ARCHIVE_FAILED);
		}
	} else {
		/*
		 * Make sure a file-type is set.
		 * The mode has been overridden if it is in the extended data.
		 */
		lha->mode = (lha->mode & ~AE_IFMT) |
		    ((lha->directory)? AE_IFDIR: AE_IFREG);
	}
	if ((lha->setflag & UNIX_MODE_IS_SET) == 0 &&
	    (lha->dos_attr & 1) != 0)
		lha->mode &= ~(0222);/* read only. */

	/*
	 * Set basic file parameters.
	 */
	archive_entry_copy_pathname_w(entry, pathname.s);
	archive_wstring_free(&pathname);
	if (archive_strlen(&linkname) > 0) {
		archive_entry_copy_symlink_w(entry, linkname.s);
	} else
		archive_entry_set_symlink(entry, NULL);
	archive_wstring_free(&linkname);
	/*
	 * When a header level is 0, there is a possibility that
	 * a pathname and a symlink has '\' character, a directory
	 * separator in DOS/Windows. So we should convert it to '/'.
	 */
	if (lha->level == 0)
		lha_replace_path_separator(lha, entry);

	archive_entry_set_mode(entry, lha->mode);
	archive_entry_set_uid(entry, lha->uid);
	archive_entry_set_gid(entry, lha->gid);
	if (archive_strlen(&lha->uname) > 0)
		archive_entry_set_uname(entry, lha->uname.s);
	if (archive_strlen(&lha->gname) > 0)
		archive_entry_set_gname(entry, lha->gname.s);
	if (lha->setflag & BIRTHTIME_IS_SET) {
		archive_entry_set_birthtime(entry, lha->birthtime,
		    lha->birthtime_tv_nsec);
		archive_entry_set_ctime(entry, lha->birthtime,
		    lha->birthtime_tv_nsec);
	} else {
		archive_entry_unset_birthtime(entry);
		archive_entry_unset_ctime(entry);
	}
	archive_entry_set_mtime(entry, lha->mtime, lha->mtime_tv_nsec);
	if (lha->setflag & ATIME_IS_SET)
		archive_entry_set_atime(entry, lha->atime,
		    lha->atime_tv_nsec);
	else
		archive_entry_unset_atime(entry);
	if (lha->directory || archive_entry_symlink(entry) != NULL)
		archive_entry_unset_size(entry);
	else
		archive_entry_set_size(entry, lha->origsize);

	/*
	 * Prepare variables used to read a file content.
	 */
	lha->entry_bytes_remaining = lha->compsize;
	if (lha->entry_bytes_remaining < 0) {
		archive_set_error(&a->archive,
		    ARCHIVE_ERRNO_FILE_FORMAT,
		    "Invalid LHa entry size");
		return (ARCHIVE_FATAL);
	}
	lha->entry_offset = 0;
	lha->entry_crc_calculated = 0;

	/*
	 * This file does not have a content.
	 */
	if (lha->directory || lha->compsize == 0)
		lha->end_of_entry = 1;

	snprintf(lha->format_name, sizeof(lha->format_name), "lha -%c%c%c-",
	    lha->method[0], lha->method[1], lha->method[2]);
	a->archive.archive_format_name = lha->format_name;

	return (err);
}

/*
 * Replace a DOS path separator '\' by a character '/'.
 * Some multi-byte character set have  a character '\' in its second byte.
 */
static void
lha_replace_path_separator(struct lha *lha, struct archive_entry *entry)
{
	const wchar_t *wp;
	size_t i;

	if ((wp = archive_entry_pathname_w(entry)) != NULL) {
		archive_wstrcpy(&(lha->ws), wp);
		for (i = 0; i < archive_strlen(&(lha->ws)); i++) {
			if (lha->ws.s[i] == L'\\')
				lha->ws.s[i] = L'/';
		}
		archive_entry_copy_pathname_w(entry, lha->ws.s);
	}

	if ((wp = archive_entry_symlink_w(entry)) != NULL) {
		archive_wstrcpy(&(lha->ws), wp);
		for (i = 0; i < archive_strlen(&(lha->ws)); i++) {
			if (lha->ws.s[i] == L'\\')
				lha->ws.s[i] = L'/';
		}
		archive_entry_copy_symlink_w(entry, lha->ws.s);
	}
}

/*
 * Header 0 format
 *
 * +0              +1         +2               +7                  +11
 * +---------------+----------+----------------+-------------------+
 * |header size(*1)|header sum|compression type|compressed size(*2)|
 * +---------------+----------+----------------+-------------------+
 *                             <---------------------(*1)----------*
 *
 * +11               +15       +17       +19            +20              +21
 * +-----------------+---------+---------+--------------+----------------+
 * |uncompressed size|time(DOS)|date(DOS)|attribute(DOS)|header level(=0)|
 * +-----------------+---------+---------+--------------+----------------+
 * *--------------------------------(*1)---------------------------------*
 *
 * +21             +22       +22+(*3)   +22+(*3)+2       +22+(*3)+2+(*4)
 * +---------------+---------+----------+----------------+------------------+
 * |name length(*3)|file name|file CRC16|extra header(*4)|  compressed data |
 * +---------------+---------+----------+----------------+------------------+
 *                  <--(*3)->                             <------(*2)------>
 * *----------------------(*1)-------------------------->
 *
 */
#define H0_HEADER_SIZE_OFFSET	0
#define H0_HEADER_SUM_OFFSET	1
#define H0_COMP_SIZE_OFFSET	7
#define H0_ORIG_SIZE_OFFSET	11
#define H0_DOS_TIME_OFFSET	15
#define H0_NAME_LEN_OFFSET	21
#define H0_FILE_NAME_OFFSET	22
#define H0_FIXED_SIZE		24
static int
lha_read_file_header_0(struct archive_read *a, struct lha *lha)
{
	const unsigned char *p;
	int extdsize, namelen;
	unsigned char headersum, sum_calculated;

	if ((p = __archive_read_ahead(a, H0_FIXED_SIZE, NULL)) == NULL)
		return (truncated_error(a));
	lha->header_size = p[H0_HEADER_SIZE_OFFSET] + 2;
	headersum = p[H0_HEADER_SUM_OFFSET];
	lha->compsize = archive_le32dec(p + H0_COMP_SIZE_OFFSET);
	lha->origsize = archive_le32dec(p + H0_ORIG_SIZE_OFFSET);
	lha->mtime = dos_to_unix(archive_le32dec(p + H0_DOS_TIME_OFFSET));
	namelen = p[H0_NAME_LEN_OFFSET];
	extdsize = (int)lha->header_size - H0_FIXED_SIZE - namelen;
	if ((namelen > 221 || extdsize < 0) && extdsize != -2) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Invalid LHa header");
		return (ARCHIVE_FATAL);
	}
	if ((p = __archive_read_ahead(a, lha->header_size, NULL)) == NULL)
		return (truncated_error(a));

	archive_strncpy(&lha->filename, p + H0_FILE_NAME_OFFSET, namelen);
	/* When extdsize == -2, A CRC16 value is not present in the header. */
	if (extdsize >= 0) {
		lha->crc = archive_le16dec(p + H0_FILE_NAME_OFFSET + namelen);
		lha->setflag |= CRC_IS_SET;
	}
	sum_calculated = lha_calcsum(0, p, 2, lha->header_size - 2);

	/* Read an extended header */
	if (extdsize > 0) {
		/* This extended data is set by 'LHa for UNIX' only.
		 * Maybe fixed size.
		 */
		p += H0_FILE_NAME_OFFSET + namelen + 2;
		if (p[0] == 'U' && extdsize == 12) {
			/* p[1] is a minor version. */
			lha->mtime = archive_le32dec(&p[2]);
			lha->mode = archive_le16dec(&p[6]);
			lha->uid = archive_le16dec(&p[8]);
			lha->gid = archive_le16dec(&p[10]);
			lha->setflag |= UNIX_MODE_IS_SET;
		}
	}
	__archive_read_consume(a, lha->header_size);

	if (sum_calculated != headersum) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
		    "LHa header sum error");
		return (ARCHIVE_FATAL);
	}

	return (ARCHIVE_OK);
}

/*
 * Header 1 format
 *
 * +0              +1         +2               +7            +11
 * +---------------+----------+----------------+-------------+
 * |header size(*1)|header sum|compression type|skip size(*2)|
 * +---------------+----------+----------------+-------------+
 *                             <---------------(*1)----------*
 *
 * +11               +15       +17       +19            +20              +21
 * +-----------------+---------+---------+--------------+----------------+
 * |uncompressed size|time(DOS)|date(DOS)|attribute(DOS)|header level(=1)|
 * +-----------------+---------+---------+--------------+----------------+
 * *-------------------------------(*1)----------------------------------*
 *
 * +21             +22       +22+(*3)   +22+(*3)+2  +22+(*3)+3  +22+(*3)+3+(*4)
 * +---------------+---------+----------+-----------+-----------+
 * |name length(*3)|file name|file CRC16|  creator  |padding(*4)|
 * +---------------+---------+----------+-----------+-----------+
 *                  <--(*3)->
 * *----------------------------(*1)----------------------------*
 *
 * +22+(*3)+3+(*4)  +22+(*3)+3+(*4)+2     +22+(*3)+3+(*4)+2+(*5)
 * +----------------+---------------------+------------------------+
 * |next header size| extended header(*5) |     compressed data    |
 * +----------------+---------------------+------------------------+
 * *------(*1)-----> <--------------------(*2)-------------------->
 */
#define H1_HEADER_SIZE_OFFSET	0
#define H1_HEADER_SUM_OFFSET	1
#define H1_COMP_SIZE_OFFSET	7
#define H1_ORIG_SIZE_OFFSET	11
#define H1_DOS_TIME_OFFSET	15
#define H1_NAME_LEN_OFFSET	21
#define H1_FILE_NAME_OFFSET	22
#define H1_FIXED_SIZE		27

/*
 * Is this a level-1 header, judged on its own structure rather than on the
 * MS-DOS attribute byte? Used to bid on archives whose writer put something
 * else in that byte, which is what Amiga LhA does.
 *
 * Only the first H_SIZE bytes may be read here — that is all the bidder and
 * the SFX scan guarantee — so this checks exactly what those bytes can prove:
 * the header must be long enough to hold its own fixed part plus the name,
 * and the name length must be one lha_read_file_header_1() would accept.
 * Note `header size' is measured from offset 2, hence the +2.
 *
 * A false positive costs nothing: lha_read_file_header_1() verifies the
 * header checksum and fails the read.
 */
static int
lha_level1_header_is_plausible(const unsigned char *p)
{

	if (p[H1_NAME_LEN_OFFSET] > 230)
		return (0);
	return ((size_t)p[H1_HEADER_SIZE_OFFSET] + 2
	    >= H1_FIXED_SIZE + (size_t)p[H1_NAME_LEN_OFFSET]);
}

static int
lha_read_file_header_1(struct archive_read *a, struct lha *lha)
{
	const unsigned char *p;
	size_t extdsize;
	int i, err, err2;
	int namelen, padding;
	unsigned char headersum, sum_calculated;

	err = ARCHIVE_OK;

	if ((p = __archive_read_ahead(a, H1_FIXED_SIZE, NULL)) == NULL)
		return (truncated_error(a));

	lha->header_size = p[H1_HEADER_SIZE_OFFSET] + 2;
	headersum = p[H1_HEADER_SUM_OFFSET];
	/* Note: An extended header size is included in a compsize. */
	lha->compsize = archive_le32dec(p + H1_COMP_SIZE_OFFSET);
	lha->origsize = archive_le32dec(p + H1_ORIG_SIZE_OFFSET);
	lha->mtime = dos_to_unix(archive_le32dec(p + H1_DOS_TIME_OFFSET));
	namelen = p[H1_NAME_LEN_OFFSET];
	/* Calculate a padding size. The result will be normally 0 only(?) */
	padding = ((int)lha->header_size) - H1_FIXED_SIZE - namelen;

	if (namelen > 230 || padding < 0)
		goto invalid;

	if ((p = __archive_read_ahead(a, lha->header_size, NULL)) == NULL)
		return (truncated_error(a));

	for (i = 0; i < namelen; i++) {
		if (p[i + H1_FILE_NAME_OFFSET] == 0xff)
			goto invalid;/* Invalid filename. */
	}
	archive_strncpy(&lha->filename, p + H1_FILE_NAME_OFFSET, namelen);
	lha->crc = archive_le16dec(p + H1_FILE_NAME_OFFSET + namelen);
	lha->setflag |= CRC_IS_SET;

	sum_calculated = lha_calcsum(0, p, 2, lha->header_size - 2);
	/* Consume used bytes but not include `next header size' data
	 * since it will be consumed in lha_read_file_extended_header(). */
	__archive_read_consume(a, lha->header_size - 2);

	/* Read extended headers */
	err2 = lha_read_file_extended_header(a, lha, NULL, 2,
	    (uint64_t)(lha->compsize + 2), &extdsize);
	if (err2 < ARCHIVE_WARN)
		return (err2);
	if (err2 < err)
		err = err2;
	/* Get a real compressed file size. */
	lha->compsize -= extdsize - 2;

	if (lha->compsize < 0)
		goto invalid;	/* Invalid compressed file size */

	if (sum_calculated != headersum) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
		    "LHa header sum error");
		return (ARCHIVE_FATAL);
	}
	return (err);
invalid:
	archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
	    "Invalid LHa header");
	return (ARCHIVE_FATAL);
}

/*
 * Header 2 format
 *
 * +0              +2               +7                  +11               +15
 * +---------------+----------------+-------------------+-----------------+
 * |header size(*1)|compression type|compressed size(*2)|uncompressed size|
 * +---------------+----------------+-------------------+-----------------+
 *  <--------------------------------(*1)---------------------------------*
 *
 * +15               +19          +20              +21        +23         +24
 * +-----------------+------------+----------------+----------+-----------+
 * |data/time(time_t)| 0x20 fixed |header level(=2)|file CRC16|  creator  |
 * +-----------------+------------+----------------+----------+-----------+
 * *---------------------------------(*1)---------------------------------*
 *
 * +24              +26                 +26+(*3)      +26+(*3)+(*4)
 * +----------------+-------------------+-------------+-------------------+
 * |next header size|extended header(*3)| padding(*4) |  compressed data  |
 * +----------------+-------------------+-------------+-------------------+
 * *--------------------------(*1)-------------------> <------(*2)------->
 *
 */
#define H2_HEADER_SIZE_OFFSET	0
#define H2_COMP_SIZE_OFFSET	7
#define H2_ORIG_SIZE_OFFSET	11
#define H2_TIME_OFFSET		15
#define H2_CRC_OFFSET		21
#define H2_FIXED_SIZE		24
static int
lha_read_file_header_2(struct archive_read *a, struct lha *lha)
{
	const unsigned char *p;
	size_t extdsize;
	int err, padding;
	uint16_t header_crc;

	if ((p = __archive_read_ahead(a, H2_FIXED_SIZE, NULL)) == NULL)
		return (truncated_error(a));

	lha->header_size =archive_le16dec(p + H2_HEADER_SIZE_OFFSET);
	lha->compsize = archive_le32dec(p + H2_COMP_SIZE_OFFSET);
	lha->origsize = archive_le32dec(p + H2_ORIG_SIZE_OFFSET);
	lha->mtime = archive_le32dec(p + H2_TIME_OFFSET);
	lha->crc = archive_le16dec(p + H2_CRC_OFFSET);
	lha->setflag |= CRC_IS_SET;

	if (lha->header_size < H2_FIXED_SIZE) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Invalid LHa header size");
		return (ARCHIVE_FATAL);
	}

	header_crc = lha_crc16(0, p, H2_FIXED_SIZE);
	__archive_read_consume(a, H2_FIXED_SIZE);

	/* Read extended headers */
	err = lha_read_file_extended_header(a, lha, &header_crc, 2,
		  lha->header_size - H2_FIXED_SIZE, &extdsize);
	if (err < ARCHIVE_WARN)
		return (err);

	/* Calculate a padding size. The result will be normally 0 or 1. */
	padding = (int)lha->header_size - (int)(H2_FIXED_SIZE + extdsize);
	if (padding > 0) {
		if ((p = __archive_read_ahead(a, padding, NULL)) == NULL)
			return (truncated_error(a));
		header_crc = lha_crc16(header_crc, p, padding);
		__archive_read_consume(a, padding);
	}

	if (header_crc != lha->header_crc) {
#ifndef DONT_FAIL_ON_CRC_ERROR
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "LHa header CRC error");
		return (ARCHIVE_FATAL);
#endif
	}
	return (err);
}

/*
 * Header 3 format
 *
 * +0           +2               +7                  +11               +15
 * +------------+----------------+-------------------+-----------------+
 * | 0x04 fixed |compression type|compressed size(*2)|uncompressed size|
 * +------------+----------------+-------------------+-----------------+
 *  <-------------------------------(*1)-------------------------------*
 *
 * +15               +19          +20              +21        +23         +24
 * +-----------------+------------+----------------+----------+-----------+
 * |date/time(time_t)| 0x20 fixed |header level(=3)|file CRC16|  creator  |
 * +-----------------+------------+----------------+----------+-----------+
 * *--------------------------------(*1)----------------------------------*
 *
 * +24             +28              +32                 +32+(*3)
 * +---------------+----------------+-------------------+-----------------+
 * |header size(*1)|next header size|extended header(*3)| compressed data |
 * +---------------+----------------+-------------------+-----------------+
 * *------------------------(*1)-----------------------> <------(*2)----->
 *
 */
#define H3_FIELD_LEN_OFFSET	0
#define H3_COMP_SIZE_OFFSET	7
#define H3_ORIG_SIZE_OFFSET	11
#define H3_TIME_OFFSET		15
#define H3_CRC_OFFSET		21
#define H3_HEADER_SIZE_OFFSET	24
#define H3_FIXED_SIZE		28
static int
lha_read_file_header_3(struct archive_read *a, struct lha *lha)
{
	const unsigned char *p;
	size_t extdsize;
	int err;
	uint16_t header_crc;

	if ((p = __archive_read_ahead(a, H3_FIXED_SIZE, NULL)) == NULL)
		return (truncated_error(a));

	if (archive_le16dec(p + H3_FIELD_LEN_OFFSET) != 4)
		goto invalid;
	lha->header_size = archive_le32dec(p + H3_HEADER_SIZE_OFFSET);
	lha->compsize = archive_le32dec(p + H3_COMP_SIZE_OFFSET);
	lha->origsize = archive_le32dec(p + H3_ORIG_SIZE_OFFSET);
	lha->mtime = archive_le32dec(p + H3_TIME_OFFSET);
	lha->crc = archive_le16dec(p + H3_CRC_OFFSET);
	lha->setflag |= CRC_IS_SET;

	if (lha->header_size < H3_FIXED_SIZE + 4)
		goto invalid;
	header_crc = lha_crc16(0, p, H3_FIXED_SIZE);
	__archive_read_consume(a, H3_FIXED_SIZE);

	/* Reject rediculously large header */
	if (lha->header_size > 65536) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "LHa header size too large");
		return (ARCHIVE_FATAL);
	}

	/* Read extended headers */
	err = lha_read_file_extended_header(a, lha, &header_crc, 4,
		  lha->header_size - H3_FIXED_SIZE, &extdsize);
	if (err < ARCHIVE_WARN)
		return (err);

	if (header_crc != lha->header_crc) {
#ifndef DONT_FAIL_ON_CRC_ERROR
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "LHa header CRC error");
		return (ARCHIVE_FATAL);
#endif
	}
	return (err);
invalid:
	archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
	    "Invalid LHa header");
	return (ARCHIVE_FATAL);
}

/*
 * Extended header format
 *
 * +0             +2        +3  -- used in header 1 and 2
 * +0             +4        +5  -- used in header 3
 * +--------------+---------+-------------------+--------------+--
 * |ex-header size|header id|        data       |ex-header size| .......
 * +--------------+---------+-------------------+--------------+--
 *  <-------------( ex-header size)------------> <-- next extended header --*
 *
 * If the ex-header size is zero, it is the make of the end of extended
 * headers.
 *
 */
static int
lha_read_file_extended_header(struct archive_read *a, struct lha *lha,
    uint16_t *crc, int sizefield_length, uint64_t limitsize, size_t *total_size)
{
	const void *h;
	const unsigned char *extdheader;
	size_t	extdsize;
	size_t	datasize;
	unsigned int i;
	unsigned char extdtype;

#define EXT_HEADER_CRC		0x00		/* Header CRC and information*/
#define EXT_FILENAME		0x01		/* Filename 		    */
#define EXT_DIRECTORY		0x02		/* Directory name	    */
#define EXT_DOS_ATTR		0x40		/* MS-DOS attribute	    */
#define EXT_TIMESTAMP		0x41		/* Windows time stamp	    */
#define EXT_FILESIZE		0x42		/* Large file size	    */
#define EXT_TIMEZONE		0x43		/* Time zone		    */
#define EXT_UTF16_FILENAME	0x44		/* UTF-16 filename 	    */
#define EXT_UTF16_DIRECTORY	0x45		/* UTF-16 directory name    */
#define EXT_CODEPAGE		0x46		/* Codepage		    */
#define EXT_UNIX_MODE		0x50		/* File permission	    */
#define EXT_UNIX_GID_UID	0x51		/* gid,uid		    */
#define EXT_UNIX_GNAME		0x52		/* Group name		    */
#define EXT_UNIX_UNAME		0x53		/* User name		    */
#define EXT_UNIX_MTIME		0x54		/* Modified time	    */
#define EXT_OS2_NEW_ATTR	0x7f		/* new attribute(OS/2 only) */
#define EXT_NEW_ATTR		0xff		/* new attribute	    */

	*total_size = sizefield_length;

	for (;;) {
		/* Read an extended header size. */
		if ((h =
		    __archive_read_ahead(a, sizefield_length, NULL)) == NULL)
			return (truncated_error(a));
		/* Check if the size is the zero indicates the end of the
		 * extended header. */
		if (sizefield_length == sizeof(uint16_t))
			extdsize = archive_le16dec(h);
		else
			extdsize = archive_le32dec(h);
		if (extdsize == 0) {
			/* End of extended header */
			if (crc != NULL)
				*crc = lha_crc16(*crc, h, sizefield_length);
			__archive_read_consume(a, sizefield_length);
			return (ARCHIVE_OK);
		}

		/* Sanity check to the extended header size. */
		if (((uint64_t)*total_size + extdsize) > limitsize ||
		    extdsize <= (size_t)sizefield_length)
			goto invalid;

		/* Read the extended header. */
		if ((h = __archive_read_ahead(a, extdsize, NULL)) == NULL)
			return (truncated_error(a));
		*total_size += extdsize;

		extdheader = (const unsigned char *)h;
		/* Get the extended header type. */
		extdtype = extdheader[sizefield_length];
		/* Calculate an extended data size. */
		datasize = extdsize - (1 + sizefield_length);
		/* Skip an extended header size field and type field. */
		extdheader += sizefield_length + 1;

		if (crc != NULL && extdtype != EXT_HEADER_CRC)
			*crc = lha_crc16(*crc, h, extdsize);
		switch (extdtype) {
		case EXT_HEADER_CRC:
			/* We only use a header CRC. Following data will not
			 * be used. */
			if (datasize >= 2) {
				lha->header_crc = archive_le16dec(extdheader);
				if (crc != NULL) {
					static const char zeros[2] = {0, 0};
					*crc = lha_crc16(*crc, h,
					    extdsize - datasize);
					/* CRC value itself as zero */
					*crc = lha_crc16(*crc, zeros, 2);
					*crc = lha_crc16(*crc,
					    extdheader+2, datasize - 2);
				}
			}
			break;
		case EXT_FILENAME:
			if (datasize == 0) {
				/* maybe directory header */
				archive_string_empty(&lha->filename);
				break;
			}
			if (extdheader[0] == '\0')
				goto invalid;
			archive_strncpy(&lha->filename,
			    (const char *)extdheader, datasize);
			break;
		case EXT_UTF16_FILENAME:
			if (datasize == 0) {
				/* maybe directory header */
				archive_string_empty(&lha->filename);
				break;
			} else if (datasize & 1) {
				/* UTF-16 characters take always 2 or 4 bytes */
				goto invalid;
			}
			if (extdheader[0] == '\0')
				goto invalid;
			archive_string_empty(&lha->filename);
			archive_array_append(&lha->filename,
				(const char *)extdheader, datasize);
			/* Setup a string conversion for a filename. */
			lha->sconv_fname =
			    archive_string_conversion_from_charset(&a->archive,
			        "UTF-16LE", 1);
			if (lha->sconv_fname == NULL)
				return (ARCHIVE_FATAL);
			break;
		case EXT_DIRECTORY:
			if (datasize == 0 || extdheader[0] == '\0')
				/* no directory name data. exit this case. */
				goto invalid;

			archive_strncpy(&lha->dirname,
		  	    (const char *)extdheader, datasize);
			/*
			 * Convert directory delimiter from 0xFF
			 * to '/' for local system.
	 		 */
			for (i = 0; i < lha->dirname.length; i++) {
				if ((unsigned char)lha->dirname.s[i] == 0xFF)
					lha->dirname.s[i] = '/';
			}
			/* Is last character directory separator? */
			if (lha->dirname.s[lha->dirname.length-1] != '/')
				/* invalid directory data */
				goto invalid;
			break;
		case EXT_UTF16_DIRECTORY:
			/* UTF-16 characters take always 2 or 4 bytes */
			if (datasize == 0 || (datasize & 1) ||
			    extdheader[0] == '\0') {
				/* no directory name data. exit this case. */
				goto invalid;
			}

			archive_string_empty(&lha->dirname);
			archive_array_append(&lha->dirname,
				(const char *)extdheader, datasize);
			lha->sconv_dir =
			    archive_string_conversion_from_charset(&a->archive,
			        "UTF-16LE", 1);
			if (lha->sconv_dir == NULL)
				return (ARCHIVE_FATAL);
			else {
				/*
				 * Convert directory delimiter from 0xFFFF
				 * to '/' for local system.
				 */
				uint16_t dirSep;
				uint16_t d = 1;
				if (archive_be16dec(&d) == 1)
					dirSep = 0x2F00;
				else
					dirSep = 0x002F;

				/* UTF-16LE character */
				uint16_t *utf16name =
				    (uint16_t *)lha->dirname.s;
				for (i = 0; i < lha->dirname.length / 2; i++) {
					if (utf16name[i] == 0xFFFF) {
						utf16name[i] = dirSep;
					}
				}
				/* Is last character directory separator? */
				if (utf16name[lha->dirname.length / 2 - 1] !=
				    dirSep) {
					/* invalid directory data */
					goto invalid;
				}
			}
			break;
		case EXT_DOS_ATTR:
			if (datasize == 2)
				lha->dos_attr = (unsigned char)
				    (archive_le16dec(extdheader) & 0xff);
			break;
		case EXT_TIMESTAMP:
			if (datasize == (sizeof(uint64_t) * 3)) {
				ntfs_to_unix(archive_le64dec(extdheader),
					&lha->birthtime,
				    &lha->birthtime_tv_nsec);
				extdheader += sizeof(uint64_t);
				ntfs_to_unix(archive_le64dec(extdheader),
					&lha->mtime,
				    &lha->mtime_tv_nsec);
				extdheader += sizeof(uint64_t);
				ntfs_to_unix(archive_le64dec(extdheader),
					&lha->atime,
				    &lha->atime_tv_nsec);
				lha->setflag |= BIRTHTIME_IS_SET |
				    ATIME_IS_SET;
			}
			break;
		case EXT_FILESIZE:
			if (datasize == sizeof(uint64_t) * 2) {
				lha->compsize = archive_le64dec(extdheader);
				extdheader += sizeof(uint64_t);
				lha->origsize = archive_le64dec(extdheader);
				if (lha->compsize < 0 || lha->origsize < 0)
					goto invalid;
			}
			break;
		case EXT_CODEPAGE:
			/* Get an archived filename charset from codepage.
			 * This overwrites the charset specified by
			 * hdrcharset option. */
			if (datasize == sizeof(uint32_t)) {
				struct archive_string cp;
				const char *charset;

				archive_string_init(&cp);
				switch (archive_le32dec(extdheader)) {
				case 65001: /* UTF-8 */
					charset = "UTF-8";
					break;
				default:
					archive_string_sprintf(&cp, "CP%d",
					    (int)archive_le32dec(extdheader));
					charset = cp.s;
					break;
				}
				lha->sconv_dir =
				    archive_string_conversion_from_charset(
					&(a->archive), charset, 1);
				lha->sconv_fname =
				    archive_string_conversion_from_charset(
					&(a->archive), charset, 1);
				archive_string_free(&cp);
				if (lha->sconv_dir == NULL)
					return (ARCHIVE_FATAL);
				if (lha->sconv_fname == NULL)
					return (ARCHIVE_FATAL);
			}
			break;
		case EXT_UNIX_MODE:
			if (datasize == sizeof(uint16_t)) {
				lha->mode = archive_le16dec(extdheader);
				lha->setflag |= UNIX_MODE_IS_SET;
			}
			break;
		case EXT_UNIX_GID_UID:
			if (datasize == (sizeof(uint16_t) * 2)) {
				lha->gid = archive_le16dec(extdheader);
				lha->uid = archive_le16dec(extdheader+2);
			}
			break;
		case EXT_UNIX_GNAME:
			if (datasize > 0)
				archive_strncpy(&lha->gname,
				    (const char *)extdheader, datasize);
			break;
		case EXT_UNIX_UNAME:
			if (datasize > 0)
				archive_strncpy(&lha->uname,
				    (const char *)extdheader, datasize);
			break;
		case EXT_UNIX_MTIME:
			if (datasize == sizeof(uint32_t))
				lha->mtime = archive_le32dec(extdheader);
			break;
		case EXT_OS2_NEW_ATTR:
			/* This extended header is OS/2 depend. */
			if (datasize == 16) {
				lha->dos_attr = (unsigned char)
				    (archive_le16dec(extdheader) & 0xff);
				lha->mode = archive_le16dec(extdheader+2);
				lha->gid = archive_le16dec(extdheader+4);
				lha->uid = archive_le16dec(extdheader+6);
				lha->birthtime = archive_le32dec(extdheader+8);
				lha->atime = archive_le32dec(extdheader+12);
				lha->setflag |= UNIX_MODE_IS_SET
				    | BIRTHTIME_IS_SET | ATIME_IS_SET;
			}
			break;
		case EXT_NEW_ATTR:
			if (datasize == 20) {
				lha->mode = (mode_t)archive_le32dec(extdheader);
				lha->gid = archive_le32dec(extdheader+4);
				lha->uid = archive_le32dec(extdheader+8);
				lha->birthtime = archive_le32dec(extdheader+12);
				lha->atime = archive_le32dec(extdheader+16);
				lha->setflag |= UNIX_MODE_IS_SET
				    | BIRTHTIME_IS_SET | ATIME_IS_SET;
			}
			break;
		case EXT_TIMEZONE:		/* Not supported */
			break;
		default:
			break;
		}

		__archive_read_consume(a, extdsize);
	}
invalid:
	archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
	    "Invalid extended LHa header");
	return (ARCHIVE_FATAL);
}

static int
lha_end_of_entry(struct archive_read *a)
{
	struct lha *lha = (struct lha *)(a->format->data);
	int r = ARCHIVE_EOF;

	if (!lha->end_of_entry_cleanup) {
		if ((lha->setflag & CRC_IS_SET) &&
		    lha->crc != lha->entry_crc_calculated) {
			archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
			    "LHa data CRC error");
			r = ARCHIVE_WARN;
		}

		/* End-of-entry cleanup done. */
		lha->end_of_entry_cleanup = 1;
	}
	return (r);
}

static int
archive_read_format_lha_read_data(struct archive_read *a,
    const void **buff, size_t *size, int64_t *offset)
{
	struct lha *lha = (struct lha *)(a->format->data);
	int r;

	if (lha->entry_unconsumed) {
		/* Consume as much as the decompressor actually used. */
		__archive_read_consume(a, lha->entry_unconsumed);
		lha->entry_unconsumed = 0;
	}
	if (lha->end_of_entry) {
		*offset = lha->entry_offset;
		*size = 0;
		*buff = NULL;
		return (lha_end_of_entry(a));
	}

	if (lha->entry_is_compressed)
		r =  lha_read_data_lzh(a, buff, size, offset);
	else
		/* No compression. */
		r =  lha_read_data_none(a, buff, size, offset);
	return (r);
}

/*
 * Read a file content in no compression.
 *
 * Returns ARCHIVE_OK if successful, ARCHIVE_FATAL otherwise, sets
 * lha->end_of_entry if it consumes all of the data.
 */
static int
lha_read_data_none(struct archive_read *a, const void **buff,
    size_t *size, int64_t *offset)
{
	struct lha *lha = (struct lha *)(a->format->data);
	ssize_t bytes_avail;

	if (lha->entry_bytes_remaining == 0) {
		*buff = NULL;
		*size = 0;
		*offset = lha->entry_offset;
		lha->end_of_entry = 1;
		return (ARCHIVE_OK);
	}
	/*
	 * Note: '1' here is a performance optimization.
	 * Recall that the decompression layer returns a count of
	 * available bytes; asking for more than that forces the
	 * decompressor to combine reads by copying data.
	 */
	*buff = __archive_read_ahead(a, 1, &bytes_avail);
	if (bytes_avail <= 0) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Truncated LHa file data");
		return (ARCHIVE_FATAL);
	}
	if (bytes_avail > lha->entry_bytes_remaining)
		bytes_avail = (ssize_t)lha->entry_bytes_remaining;
	lha->entry_crc_calculated =
	    lha_crc16(lha->entry_crc_calculated, *buff, bytes_avail);
	*size = bytes_avail;
	*offset = lha->entry_offset;
	lha->entry_offset += bytes_avail;
	lha->entry_bytes_remaining -= bytes_avail;
	if (lha->entry_bytes_remaining == 0)
		lha->end_of_entry = 1;
	lha->entry_unconsumed = bytes_avail;
	return (ARCHIVE_OK);
}

/*
 * Read a file content in LZHUFF encoding.
 *
 * Returns ARCHIVE_OK if successful, returns ARCHIVE_WARN if compression is
 * unsupported, ARCHIVE_FATAL otherwise, sets lha->end_of_entry if it consumes
 * all of the data.
 */
static int
lha_read_data_lzh(struct archive_read *a, const void **buff,
    size_t *size, int64_t *offset)
{
	struct lha *lha = (struct lha *)(a->format->data);
	ssize_t bytes_avail;
	int r;

	/* If we haven't yet read any data, initialize the decompressor. */
	if (!lha->decompress_init) {
		r = lzh_decode_init(&(lha->strm), lha->method, lha->origsize);
		switch (r) {
		case ARCHIVE_OK:
			break;
		case ARCHIVE_FAILED:
        		/* Unsupported compression. */
			*buff = NULL;
			*size = 0;
			*offset = 0;
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_FILE_FORMAT,
			    "Unsupported lzh compression method -%c%c%c-",
			    lha->method[0], lha->method[1], lha->method[2]);
			/* We know compressed size; just skip it. */
			archive_read_format_lha_read_data_skip(a);
			return (ARCHIVE_WARN);
		default:
			archive_set_error(&a->archive, ENOMEM,
			    "Couldn't allocate memory "
			    "for lzh decompression");
			return (ARCHIVE_FATAL);
		}
		/* We've initialized decompression for this stream. */
		lha->decompress_init = 1;
		lha->strm.avail_out = 0;
		lha->strm.total_out = 0;
	}

	/*
	 * Note: '1' here is a performance optimization.
	 * Recall that the decompression layer returns a count of
	 * available bytes; asking for more than that forces the
	 * decompressor to combine reads by copying data.
	 */
	lha->strm.next_in = __archive_read_ahead(a, 1, &bytes_avail);
	if (bytes_avail <= 0) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Truncated LHa file body");
		return (ARCHIVE_FATAL);
	}
	if (bytes_avail > lha->entry_bytes_remaining)
		bytes_avail = (ssize_t)lha->entry_bytes_remaining;

	lha->strm.avail_in = (int)bytes_avail;
	lha->strm.total_in = 0;
	lha->strm.avail_out = 0;

	r = lzh_decode(&(lha->strm), bytes_avail == lha->entry_bytes_remaining);
	switch (r) {
	case ARCHIVE_OK:
		break;
	case ARCHIVE_EOF:
		lha->end_of_entry = 1;
		break;
	default:
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
		    "Bad lzh data");
		return (ARCHIVE_FAILED);
	}
	lha->entry_unconsumed = lha->strm.total_in;
	lha->entry_bytes_remaining -= lha->strm.total_in;

	if (lha->end_of_entry && lha->entry_bytes_remaining > 0) {
		/*
		 * -lh1- ends when it has emitted `origsize' bytes rather than
		 * when its input runs out, so compressed bytes can be left
		 * over. Nothing else would ever consume them:
		 * read_data_skip() consumes entry_unconsumed and then returns
		 * early because the entry is finished, so the NEXT header
		 * would be read from the wrong offset.
		 *
		 * Usually there is nothing to do here - the bit reader fills a
		 * 64-bit cache greedily, so it has already taken the encoder's
		 * few trailing flush bytes. It matters when the two sizes
		 * genuinely disagree: an entry whose origsize is 0, or a
		 * header that under-reports it.
		 *
		 * Not reachable for -lh5-/-lh6-/-lh7-, whose end of entry IS
		 * input exhaustion.
		 */
		lha->entry_unconsumed += lha->entry_bytes_remaining;
		lha->entry_bytes_remaining = 0;
	}

	if (lha->strm.avail_out) {
		*offset = lha->entry_offset;
		*size = lha->strm.avail_out;
		*buff = lha->strm.ref_ptr;
		lha->entry_crc_calculated =
		    lha_crc16(lha->entry_crc_calculated, *buff, *size);
		lha->entry_offset += *size;
	} else {
		*offset = lha->entry_offset;
		*size = 0;
		*buff = NULL;
		if (lha->end_of_entry)
			return (lha_end_of_entry(a));
	}
	return (ARCHIVE_OK);
}

/*
 * Skip a file content.
 */
static int
archive_read_format_lha_read_data_skip(struct archive_read *a)
{
	struct lha *lha;
	int64_t bytes_skipped;

	lha = (struct lha *)(a->format->data);

	if (lha->entry_unconsumed) {
		/* Consume as much as the decompressor actually used. */
		__archive_read_consume(a, lha->entry_unconsumed);
		lha->entry_unconsumed = 0;
	}

	/* if we've already read to end of data, we're done. */
	if (lha->end_of_entry_cleanup)
		return (ARCHIVE_OK);

	/*
	 * If the length is at the beginning, we can skip the
	 * compressed data much more quickly.
	 */
	bytes_skipped = __archive_read_consume(a, lha->entry_bytes_remaining);
	if (bytes_skipped < 0)
		return (ARCHIVE_FATAL);

	/* This entry is finished and done. */
	lha->end_of_entry_cleanup = lha->end_of_entry = 1;
	return (ARCHIVE_OK);
}

static int
archive_read_format_lha_cleanup(struct archive_read *a)
{
	struct lha *lha = (struct lha *)(a->format->data);

	lzh_decode_free(&(lha->strm));
	archive_string_free(&(lha->dirname));
	archive_string_free(&(lha->filename));
	archive_string_free(&(lha->uname));
	archive_string_free(&(lha->gname));
	archive_wstring_free(&(lha->ws));
	free(lha);
	(a->format->data) = NULL;
	return (ARCHIVE_OK);
}

/*
 * 'LHa for UNIX' utility has archived a symbolic-link name after
 * a pathname with '|' character.
 * This function extracts the symbolic-link name from the pathname.
 *
 * example.
 *   1. a symbolic-name is 'aaa/bb/cc'
 *   2. a filename is 'xxx/bbb'
 *  then an archived pathname is 'xxx/bbb|aaa/bb/cc'
 */
static int
lha_parse_linkname(struct archive_wstring *linkname,
    struct archive_wstring *pathname)
{
	wchar_t *	linkptr;
	size_t 	symlen;

	linkptr = wcschr(pathname->s, L'|');
	if (linkptr != NULL) {
		symlen = wcslen(linkptr + 1);
		archive_wstrncpy(linkname, linkptr+1, symlen);

		*linkptr = 0;
		pathname->length = wcslen(pathname->s);

		return (1);
	}
	return (0);
}

static unsigned char
lha_calcsum(unsigned char sum, const void *pp, int offset, size_t size)
{
	unsigned char const *p = (unsigned char const *)pp;

	p += offset;
	for (;size > 0; --size)
		sum += *p++;
	return (sum);
}

static uint16_t crc16tbl[2][256];
static void
lha_crc16_init(void)
{
	unsigned int i;
	static int crc16init = 0;

	if (crc16init)
		return;
	crc16init = 1;

	for (i = 0; i < 256; i++) {
		unsigned int j;
		uint16_t crc = (uint16_t)i;
		for (j = 8; j; j--)
			crc = (crc >> 1) ^ ((crc & 1) * 0xA001);
		crc16tbl[0][i] = crc;
	}

	for (i = 0; i < 256; i++) {
		crc16tbl[1][i] = (crc16tbl[0][i] >> 8)
			^ crc16tbl[0][crc16tbl[0][i] & 0xff];
	}
}

static uint16_t
lha_crc16(uint16_t crc, const void *pp, size_t len)
{
	const unsigned char *p = (const unsigned char *)pp;
	const uint16_t *buff;
	const union {
		uint32_t i;
		char c[4];
	} u = { 0x01020304 };

	if (len == 0)
		return crc;

	/* Process unaligned address. */
	if (((uintptr_t)p) & (uintptr_t)0x1) {
		crc = (crc >> 8) ^ crc16tbl[0][(crc ^ *p++) & 0xff];
		len--;
	}
	buff = (const uint16_t *)p;
	/*
	 * Modern C compiler such as GCC does not unroll automatically yet
	 * without unrolling pragma, and Clang is so. So we should
	 * unroll this loop for its performance.
	 */
	for (;len >= 8; len -= 8) {
		/* This if statement expects compiler optimization will
		 * remove the statement which will not be executed. */
#undef bswap16
#ifndef __has_builtin
#define __has_builtin(x) 0
#endif
#if defined(_MSC_VER) && _MSC_VER >= 1400  /* Visual Studio */
#  define bswap16(x) _byteswap_ushort(x)
#elif defined(__GNUC__) && ((__GNUC__ == 4 && __GNUC_MINOR__ >= 8) || __GNUC__ > 4)
/* GCC 4.8 and later has __builtin_bswap16() */
#  define bswap16(x) __builtin_bswap16(x)
#elif defined(__clang__) && __has_builtin(__builtin_bswap16)
/* Newer clang versions have __builtin_bswap16() */
#  define bswap16(x) __builtin_bswap16(x)
#else
#  define bswap16(x) ((((x) >> 8) & 0xff) | ((x) << 8))
#endif
#define CRC16W	do { 	\
		if(u.c[0] == 1) { /* Big endian */		\
			crc ^= bswap16(*buff); buff++;		\
		} else						\
			crc ^= *buff++;				\
		crc = crc16tbl[1][crc & 0xff] ^ crc16tbl[0][crc >> 8];\
} while (0)
		CRC16W;
		CRC16W;
		CRC16W;
		CRC16W;
#undef CRC16W
#undef bswap16
	}

	p = (const unsigned char *)buff;
	for (;len; len--) {
		crc = (crc >> 8) ^ crc16tbl[0][(crc ^ *p++) & 0xff];
	}
	return crc;
}

/*
 * Build -lh1-'s fixed position table.
 *
 * The distances a -lh1- match can name are 12 bits wide; the top 6 bits are
 * Huffman coded with a table both sides agree on in advance, and the low 6
 * bits follow raw. The table is canonical over the code lengths below, whose
 * Kraft sum is exactly 1 - so no code is longer than 8 bits and one 8-bit
 * lookahead always resolves a symbol. (This is ready_made(0) of shuf.c,
 * expanded here into the lookahead table it is only ever used through.)
 */
static void
lzh_dyn_init_pt(struct lzh_dyn *d)
{
	/* Symbol counts for code lengths 3, 4, 5, 6, 7 and 8. */
	static const unsigned char runs[] = { 1, 3, 8, 12, 24, 16 };
	unsigned int i, n, e, len, sym, idx;

	sym = idx = 0;
	for (i = 0; i < sizeof(runs); i++) {
		len = i + 3;
		for (n = runs[i]; n > 0; n--) {
			d->pt_len[sym] = (unsigned char)len;
			for (e = 1U << (8 - len); e > 0; e--)
				d->pt_tbl[idx++] = (unsigned char)sym;
			sym++;
		}
	}
}

/*
 * Build -lh1-'s initial literal/length tree: every symbol has frequency 1,
 * leaves fill the high half of the node array and internal nodes are folded
 * down to the root at index 0. Nodes of equal frequency are grouped into
 * "blocks" so that a frequency increment only has to swap a node with its
 * block leader; edge[] names each block's leader and stock[] hands out block
 * ids. Called per entry - the tree is not carried between entries.
 */
static void
lzh_dyn_start(struct lzh_dyn *d)
{
	int i, j;
	unsigned int f;

	for (i = 0; i < LZH1_STOCK_SIZE; i++)
		d->stock[i] = (uint16_t)i;
	for (i = 0; i < LZH1_TREESIZE; i++)
		d->block[i] = 0;
	for (i = 0, j = LZH1_NC * 2 - 2; i < LZH1_NC; i++, j--) {
		d->freq[j] = 1;
		d->child[j] = (int16_t)~i;
		d->node[i] = (uint16_t)j;
		d->block[j] = 1;
	}
	d->avail = 2;
	d->edge[1] = LZH1_NC - 1;
	d->walk = 0;
	i = LZH1_NC * 2 - 2;
	while (j >= 0) {
		f = (unsigned int)d->freq[i] + d->freq[i - 1];
		d->freq[j] = (uint16_t)f;
		d->child[j] = (int16_t)i;
		d->parent[i] = d->parent[i - 1] = (uint16_t)j;
		if (f == d->freq[j + 1])
			d->edge[d->block[j] = d->block[j + 1]] = (uint16_t)j;
		else
			d->edge[d->block[j] = d->stock[d->avail++]] =
			    (uint16_t)j;
		i -= 2;
		j--;
	}
}

/*
 * Halve every frequency and rebuild the tree, which -lh1- does whenever the
 * root's frequency would overflow 15 bits. Both sides do it at the same
 * symbol count, so it is part of the bit stream's meaning, not an
 * optimisation. (reconst() of dhuf.c.)
 */
static void
lzh_dyn_reconst(struct lzh_dyn *d, int start, int end)
{
	int i, j, k, l, b = 0;
	unsigned int f, g;

	/* Collect the leaves, halving their frequencies, and release the
	 * blocks they were grouped into. */
	for (i = j = start; i < end; i++) {
		if ((k = d->child[i]) < 0) {
			d->freq[j] = (uint16_t)((d->freq[i] + 1) / 2);
			d->child[j] = (int16_t)k;
			j++;
		}
		if (d->edge[b = d->block[i]] == i)
			d->stock[--d->avail] = (uint16_t)b;
	}
	/* Rebuild the internal nodes over them, keeping frequency order. */
	j--;
	i = end - 1;
	l = end - 2;
	while (i >= start) {
		while (i >= l) {
			d->freq[i] = d->freq[j];
			d->child[i] = d->child[j];
			i--, j--;
		}
		f = (unsigned int)d->freq[l] + d->freq[l + 1];
		for (k = start; f < d->freq[k]; k++)
			;
		while (j >= k) {
			d->freq[i] = d->freq[j];
			d->child[i] = d->child[j];
			i--, j--;
		}
		d->freq[i] = (uint16_t)f;
		d->child[i] = (int16_t)(l + 1);
		i--;
		l -= 2;
	}
	/* Re-derive the parent/leaf maps and regroup equal frequencies. */
	f = 0;
	for (i = start; i < end; i++) {
		if ((j = d->child[i]) < 0)
			d->node[~j] = (uint16_t)i;
		else
			d->parent[j] = d->parent[j - 1] = (uint16_t)i;
		if ((g = d->freq[i]) == f)
			d->block[i] = (uint16_t)b;
		else {
			d->edge[b = d->stock[d->avail++]] = (uint16_t)i;
			d->block[i] = (uint16_t)b;
			f = g;
		}
	}
}

/*
 * Increment node p's frequency, moving it ahead of its equal-frequency
 * block first so the node array stays sorted, and return its parent.
 * (swap_inc() of dhuf.c.)
 */
static int
lzh_dyn_swap_inc(struct lzh_dyn *d, int p)
{
	int b, q, r, s;

	b = d->block[p];
	q = d->edge[b];
	if (q != p) {
		/* Swap p with its block leader; only the leader may leave
		 * the block. */
		r = d->child[p];
		s = d->child[q];
		d->child[p] = (int16_t)s;
		d->child[q] = (int16_t)r;
		if (r >= 0)
			d->parent[r] = d->parent[r - 1] = (uint16_t)q;
		else
			d->node[~r] = (uint16_t)q;
		if (s >= 0)
			d->parent[s] = d->parent[s - 1] = (uint16_t)p;
		else
			d->node[~s] = (uint16_t)p;
		p = q;
	} else if (b != d->block[p + 1]) {
		/* p is alone in its block: it can be incremented in place,
		 * and merged into the block above if it catches up. */
		if (++d->freq[p] == d->freq[p - 1]) {
			d->stock[--d->avail] = (uint16_t)b;
			d->block[p] = d->block[p - 1];
		}
		return (d->parent[p]);
	}
	/* p is the block leader now: move the block's edge past it, then
	 * either join the block above or start a block of its own. */
	d->edge[b]++;
	if (++d->freq[p] == d->freq[p - 1])
		d->block[p] = d->block[p - 1];
	else
		d->edge[d->block[p] = d->stock[d->avail++]] = (uint16_t)p;
	return (d->parent[p]);
}

/*
 * Account for one decoded symbol: walk from its leaf to the root
 * incrementing frequencies, rebuilding the tree first if the root is about
 * to overflow. (update_c() of dhuf.c.)
 */
static void
lzh_dyn_update(struct lzh_dyn *d, int c)
{
	int q;

	if (d->freq[LZH1_ROOT] == 0x8000)
		lzh_dyn_reconst(d, 0, LZH1_NC * 2 - 1);
	d->freq[LZH1_ROOT]++;
	q = d->node[c];
	do {
		q = lzh_dyn_swap_inc(d, q);
	} while (q != LZH1_ROOT);
}

/*
 * Initialize LZHUF decoder.
 *
 * Returns ARCHIVE_OK if initialization was successful.
 * Returns ARCHIVE_FAILED if method is unsupported.
 * Returns ARCHIVE_FATAL if initialization failed; memory allocation
 * error occurred.
 */
static int
lzh_decode_init(struct lzh_stream *strm, const char *method, int64_t origsize)
{
	struct lzh_dec *ds;
	int w_bits, w_size;

	if (strm->ds == NULL) {
		strm->ds = calloc(1, sizeof(*strm->ds));
		if (strm->ds == NULL)
			return (ARCHIVE_FATAL);
	}
	ds = strm->ds;
	ds->error = ARCHIVE_FAILED;
	if (method == NULL || method[0] != 'l' || method[1] != 'h')
		return (ARCHIVE_FAILED);
	switch (method[2]) {
	case '1':
		w_bits = 12;/* 4KiB for window */
		break;
	case '5':
		w_bits = 13;/* 8KiB for window */
		break;
	case '6':
		w_bits = 15;/* 32KiB for window */
		break;
	case '7':
		w_bits = 16;/* 64KiB for window */
		break;
	default:
		return (ARCHIVE_FAILED);/* Not supported. */
	}
	ds->error = ARCHIVE_FATAL;
	/* Expand a window size up to 128 KiB for decompressing process
	 * performance whatever its original window size is. */
	ds->w_size = 1U << 17;
	ds->w_mask = ds->w_size -1;
	if (ds->w_buff == NULL) {
		ds->w_buff = malloc(ds->w_size);
		if (ds->w_buff == NULL)
			return (ARCHIVE_FATAL);
	}
	w_size = 1U << w_bits;
	/*
	 * A match may reach a full window back, and at w_pos == 0 a distance
	 * of w_size selects ds->w_size - w_size - 1: one byte BELOW the fill.
	 * The original decoders answer 0x20 there (their window is a circular
	 * buffer of exactly w_size bytes, pre-filled with spaces), so fill
	 * that byte too.
	 */
	memset(ds->w_buff + ds->w_size - w_size - 1, 0x20, w_size + 1);
	ds->w_pos = 0;
	ds->state = 0;
	ds->br.cache_buffer = 0;
	ds->br.cache_avail = 0;
	if (method[2] == '1') {
		/*
		 * -lh1- decodes straight from the first bit: no block header,
		 * no transmitted tables. The tree and the fixed position table
		 * are all the state it needs, and `origsize' is what ends it.
		 */
		if (ds->dyn == NULL) {
			ds->dyn = calloc(1, sizeof(*ds->dyn));
			if (ds->dyn == NULL)
				return (ARCHIVE_FATAL);
			lzh_dyn_init_pt(ds->dyn);
		}
		lzh_dyn_start(ds->dyn);
		ds->out_left = origsize;
		ds->literal_state = ST_LH1_GET_LITERAL;
		ds->state = ST_LH1_GET_LITERAL;
		ds->error = 0;
		return (ARCHIVE_OK);
	}
	ds->literal_state = ST_GET_LITERAL;
	ds->pos_pt_len_size = w_bits + 1;
	ds->pos_pt_len_bits = (w_bits == 15 || w_bits == 16)? 5: 4;
	ds->literal_pt_len_size = PT_BITLEN_SIZE;
	ds->literal_pt_len_bits = 5;

	if (lzh_huffman_init(&(ds->lt), LT_BITLEN_SIZE, 16)
	    != ARCHIVE_OK)
		return (ARCHIVE_FATAL);
	ds->lt.len_bits = 9;
	if (lzh_huffman_init(&(ds->pt), PT_BITLEN_SIZE, 16)
	    != ARCHIVE_OK)
		return (ARCHIVE_FATAL);
	ds->error = 0;

	return (ARCHIVE_OK);
}

/*
 * Release LZHUF decoder.
 */
static void
lzh_decode_free(struct lzh_stream *strm)
{

	if (strm->ds == NULL)
		return;
	free(strm->ds->w_buff);
	free(strm->ds->dyn);
	lzh_huffman_free(&(strm->ds->lt));
	lzh_huffman_free(&(strm->ds->pt));
	free(strm->ds);
	strm->ds = NULL;
}

/*
 * Bit stream reader.
 */
/* Check that the cache buffer has enough bits. */
#define lzh_br_has(br, n)	((br)->cache_avail >= n)
/* Get compressed data by bit. */
#define lzh_br_bits(br, n)				\
	(((uint16_t)((br)->cache_buffer >>		\
		((br)->cache_avail - (n)))) & cache_masks[n])
#define lzh_br_bits_forced(br, n)			\
	(((uint16_t)((br)->cache_buffer <<		\
		((n) - (br)->cache_avail))) & cache_masks[n])
/* Read ahead to make sure the cache buffer has enough compressed data we
 * will use.
 *  True  : completed, there is enough data in the cache buffer.
 *  False : we met that strm->next_in is empty, we have to get following
 *          bytes. */
#define lzh_br_read_ahead_0(strm, br, n)	\
	(lzh_br_has(br, (n)) || lzh_br_fillup(strm, br))
/*  True  : the cache buffer has some bits as much as we need.
 *  False : there are no enough bits in the cache buffer to be used,
 *          we have to get following bytes if we could. */
#define lzh_br_read_ahead(strm, br, n)	\
	(lzh_br_read_ahead_0((strm), (br), (n)) || lzh_br_has((br), (n)))

/* Notify how many bits we consumed. */
#define lzh_br_consume(br, n)	((br)->cache_avail -= (n))
#define lzh_br_unconsume(br, n)	((br)->cache_avail += (n))

static const uint16_t cache_masks[] = {
	0x0000, 0x0001, 0x0003, 0x0007,
	0x000F, 0x001F, 0x003F, 0x007F,
	0x00FF, 0x01FF, 0x03FF, 0x07FF,
	0x0FFF, 0x1FFF, 0x3FFF, 0x7FFF,
	0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF
};

/*
 * Shift away used bits in the cache data and fill it up with following bits.
 * Call this when cache buffer does not have enough bits you need.
 *
 * Returns 1 if the cache buffer is full.
 * Returns 0 if the cache buffer is not full; input buffer is empty.
 */
static int
lzh_br_fillup(struct lzh_stream *strm, struct lzh_br *br)
{
	int n = CACHE_BITS - br->cache_avail;

	for (;;) {
		const int x = n >> 3;
		if (strm->avail_in >= x) {
			switch (x) {
			case 8:
				br->cache_buffer =
				    ((uint64_t)strm->next_in[0]) << 56 |
				    ((uint64_t)strm->next_in[1]) << 48 |
				    ((uint64_t)strm->next_in[2]) << 40 |
				    ((uint64_t)strm->next_in[3]) << 32 |
				    ((uint32_t)strm->next_in[4]) << 24 |
				    ((uint32_t)strm->next_in[5]) << 16 |
				    ((uint32_t)strm->next_in[6]) << 8 |
				     (uint32_t)strm->next_in[7];
				strm->next_in += 8;
				strm->avail_in -= 8;
				br->cache_avail += 8 * 8;
				return (1);
			case 7:
				br->cache_buffer =
		 		   (br->cache_buffer << 56) |
				    ((uint64_t)strm->next_in[0]) << 48 |
				    ((uint64_t)strm->next_in[1]) << 40 |
				    ((uint64_t)strm->next_in[2]) << 32 |
				    ((uint64_t)strm->next_in[3]) << 24 |
				    ((uint64_t)strm->next_in[4]) << 16 |
				    ((uint64_t)strm->next_in[5]) << 8 |
				     (uint64_t)strm->next_in[6];
				strm->next_in += 7;
				strm->avail_in -= 7;
				br->cache_avail += 7 * 8;
				return (1);
			case 6:
				br->cache_buffer =
		 		   (br->cache_buffer << 48) |
				    ((uint64_t)strm->next_in[0]) << 40 |
				    ((uint64_t)strm->next_in[1]) << 32 |
				    ((uint64_t)strm->next_in[2]) << 24 |
				    ((uint64_t)strm->next_in[3]) << 16 |
				    ((uint64_t)strm->next_in[4]) << 8 |
				     (uint64_t)strm->next_in[5];
				strm->next_in += 6;
				strm->avail_in -= 6;
				br->cache_avail += 6 * 8;
				return (1);
			case 0:
				/* We have enough compressed data in
				 * the cache buffer.*/
				return (1);
			default:
				break;
			}
		}
		if (strm->avail_in == 0) {
			/* There is not enough compressed data to fill up the
			 * cache buffer. */
			return (0);
		}
		br->cache_buffer =
		   (br->cache_buffer << 8) | *strm->next_in++;
		strm->avail_in--;
		br->cache_avail += 8;
		n -= 8;
	}
}

/*
 * Decode LZHUF.
 *
 * 1. Returns ARCHIVE_OK if output buffer or input buffer are empty.
 *    Please set available buffer and call this function again.
 * 2. Returns ARCHIVE_EOF if decompression has been completed.
 * 3. Returns ARCHIVE_FAILED if an error occurred; compressed data
 *    is broken or you do not set 'last' flag properly.
 * 4. 'last' flag is very important, you must set 1 to the flag if there
 *    is no input data. The lha compressed data format does not provide how
 *    to know the compressed data is really finished.
 *    Note: lha command utility check if the total size of output bytes is
 *    reached the uncompressed size recorded in its header. it does not mind
 *    that the decoding process is properly finished.
 *    GNU ZIP can decompress another compressed file made by SCO LZH compress.
 *    it handles EOF as null to fill read buffer with zero until the decoding
 *    process meet 2 bytes of zeros at reading a size of a next chunk, so the
 *    zeros are treated as the mark of the end of the data although the zeros
 *    is dummy, not the file data.
 */
static int	lzh_read_blocks(struct lzh_stream *, int);
static int	lzh_decode_blocks(struct lzh_stream *, int);

static int
lzh_decode(struct lzh_stream *strm, int last)
{
	struct lzh_dec *ds = strm->ds;
	int avail_in;
	int r;

	if (ds->error)
		return (ds->error);

	avail_in = strm->avail_in;
	do {
		if (ds->state < ST_GET_LITERAL)
			r = lzh_read_blocks(strm, last);
		else
			r = lzh_decode_blocks(strm, last);
	} while (r == 100);
	strm->total_in += avail_in - strm->avail_in;
	return (r);
}

static void
lzh_emit_window(struct lzh_stream *strm, size_t s)
{
	strm->ref_ptr = strm->ds->w_buff;
	strm->avail_out = (int)s;
	strm->total_out += s;
}

static int
lzh_read_blocks(struct lzh_stream *strm, int last)
{
	struct lzh_dec *ds = strm->ds;
	struct lzh_br *br = &(ds->br);
	int c = 0, i;
	unsigned rbits;

	for (;;) {
		switch (ds->state) {
		case ST_RD_BLOCK:
			/*
			 * Read a block number indicates how many blocks
			 * we will handle. The block is composed of a
			 * literal and a match, sometimes a literal only
			 * in particular, there are no reference data at
			 * the beginning of the decompression.
			 */
			if (!lzh_br_read_ahead_0(strm, br, 16)) {
				if (!last)
					/* We need following data. */
					return (ARCHIVE_OK);
				if (lzh_br_has(br, 8)) {
					/*
					 * It seems there are extra bits.
					 *  1. Compressed data is broken.
					 *  2. `last' flag does not properly
					 *     set.
					 */
					goto failed;
				}
				if (ds->w_pos > 0) {
					lzh_emit_window(strm, ds->w_pos);
					ds->w_pos = 0;
					return (ARCHIVE_OK);
				}
				/* End of compressed data; we have completely
				 * handled all compressed data. */
				return (ARCHIVE_EOF);
			}
			ds->blocks_avail = lzh_br_bits(br, 16);
			if (ds->blocks_avail == 0)
				goto failed;
			lzh_br_consume(br, 16);
			/*
			 * Read a literal table compressed in huffman
			 * coding.
			 */
			ds->pt.len_size = ds->literal_pt_len_size;
			ds->pt.len_bits = ds->literal_pt_len_bits;
			ds->reading_position = 0;
			/* FALL THROUGH */
		case ST_RD_PT_1:
			/* Note: ST_RD_PT_1, ST_RD_PT_2 and ST_RD_PT_4 are
			 * used in reading both a literal table and a
			 * position table. */
			if (!lzh_br_read_ahead(strm, br, ds->pt.len_bits)) {
				if (last)
					goto failed;/* Truncated data. */
				ds->state = ST_RD_PT_1;
				return (ARCHIVE_OK);
			}
			ds->pt.len_avail = lzh_br_bits(br, ds->pt.len_bits);
			lzh_br_consume(br, ds->pt.len_bits);
			/* FALL THROUGH */
		case ST_RD_PT_2:
			if (ds->pt.len_avail == 0) {
				/* There is no bitlen. */
				if (!lzh_br_read_ahead(strm, br,
				    ds->pt.len_bits)) {
					if (last)
						goto failed;/* Truncated data.*/
					ds->state = ST_RD_PT_2;
					return (ARCHIVE_OK);
				}
				if (!lzh_make_fake_table(&(ds->pt),
				    lzh_br_bits(br, ds->pt.len_bits)))
					goto failed;/* Invalid data. */
				lzh_br_consume(br, ds->pt.len_bits);
				if (ds->reading_position)
					ds->state = ST_GET_LITERAL;
				else
					ds->state = ST_RD_LITERAL_1;
				break;
			} else if (ds->pt.len_avail > ds->pt.len_size)
				goto failed;/* Invalid data. */
			ds->loop = 0;
			memset(ds->pt.freq, 0, sizeof(ds->pt.freq));
			if (ds->pt.len_avail < 3 ||
			    ds->pt.len_size == ds->pos_pt_len_size) {
				ds->state = ST_RD_PT_4;
				break;
			}
			/* FALL THROUGH */
		case ST_RD_PT_3:
			ds->loop = lzh_read_pt_bitlen(strm, ds->loop, 3);
			if (ds->loop < 3) {
				if (ds->loop < 0 || last)
					goto failed;/* Invalid data. */
				/* Not completed, get following data. */
				ds->state = ST_RD_PT_3;
				return (ARCHIVE_OK);
			}
			/* There are some null in bitlen of the literal. */
			if (!lzh_br_read_ahead(strm, br, 2)) {
				if (last)
					goto failed;/* Truncated data. */
				ds->state = ST_RD_PT_3;
				return (ARCHIVE_OK);
			}
			c = lzh_br_bits(br, 2);
			lzh_br_consume(br, 2);
			if (c > ds->pt.len_avail - 3)
				goto failed;/* Invalid data. */
			for (i = 3; c-- > 0 ;)
				ds->pt.bitlen[i++] = 0;
			ds->loop = i;
			/* FALL THROUGH */
		case ST_RD_PT_4:
			ds->loop = lzh_read_pt_bitlen(strm, ds->loop,
			    ds->pt.len_avail);
			if (ds->loop < ds->pt.len_avail) {
				if (ds->loop < 0 || last)
					goto failed;/* Invalid data. */
				/* Not completed, get following data. */
				ds->state = ST_RD_PT_4;
				return (ARCHIVE_OK);
			}
			if (!lzh_make_huffman_table(&(ds->pt)))
				goto failed;/* Invalid data */
			if (ds->reading_position) {
				ds->state = ST_GET_LITERAL;
				break;
			}
			/* FALL THROUGH */
		case ST_RD_LITERAL_1:
			if (!lzh_br_read_ahead(strm, br, ds->lt.len_bits)) {
				if (last)
					goto failed;/* Truncated data. */
				ds->state = ST_RD_LITERAL_1;
				return (ARCHIVE_OK);
			}
			ds->lt.len_avail = lzh_br_bits(br, ds->lt.len_bits);
			lzh_br_consume(br, ds->lt.len_bits);
			/* FALL THROUGH */
		case ST_RD_LITERAL_2:
			if (ds->lt.len_avail == 0) {
				/* There is no bitlen. */
				if (!lzh_br_read_ahead(strm, br,
				    ds->lt.len_bits)) {
					if (last)
						goto failed;/* Truncated data.*/
					ds->state = ST_RD_LITERAL_2;
					return (ARCHIVE_OK);
				}
				if (!lzh_make_fake_table(&(ds->lt),
				    lzh_br_bits(br, ds->lt.len_bits)))
					goto failed;/* Invalid data */
				lzh_br_consume(br, ds->lt.len_bits);
				ds->state = ST_RD_POS_DATA_1;
				break;
			} else if (ds->lt.len_avail > ds->lt.len_size)
				goto failed;/* Invalid data */
			ds->loop = 0;
			memset(ds->lt.freq, 0, sizeof(ds->lt.freq));
			/* FALL THROUGH */
		case ST_RD_LITERAL_3:
			i = ds->loop;
			while (i < ds->lt.len_avail) {
				if (!lzh_br_read_ahead(strm, br,
				    ds->pt.max_bits)) {
					if (last)
						goto failed;/* Truncated data.*/
					ds->loop = i;
					ds->state = ST_RD_LITERAL_3;
					return (ARCHIVE_OK);
				}
				rbits = lzh_br_bits(br, ds->pt.max_bits);
				c = lzh_decode_huffman(&(ds->pt), rbits);
				if (c > 2) {
					/* Note: 'c' will never be more than
					 * eighteen since it's limited by
					 * PT_BITLEN_SIZE, which is being set
					 * to ds->pt.len_size through
					 * ds->literal_pt_len_size. */
					lzh_br_consume(br, ds->pt.bitlen[c]);
					c -= 2;
					ds->lt.freq[c]++;
					ds->lt.bitlen[i++] = c;
				} else if (c == 0) {
					lzh_br_consume(br, ds->pt.bitlen[c]);
					ds->lt.bitlen[i++] = 0;
				} else {
					/* c == 1 or c == 2 */
					int n = (c == 1)?4:9;
					if (!lzh_br_read_ahead(strm, br,
					     ds->pt.bitlen[c] + n)) {
						if (last) /* Truncated data. */
							goto failed;
						ds->loop = i;
						ds->state = ST_RD_LITERAL_3;
						return (ARCHIVE_OK);
					}
					lzh_br_consume(br, ds->pt.bitlen[c]);
					c = lzh_br_bits(br, n);
					lzh_br_consume(br, n);
					c += (n == 4)?3:20;
					if (i + c > ds->lt.len_avail)
						goto failed;/* Invalid data */
					memset(&(ds->lt.bitlen[i]), 0, c);
					i += c;
				}
			}
			if (i > ds->lt.len_avail ||
			    !lzh_make_huffman_table(&(ds->lt)))
				goto failed;/* Invalid data */
			/* FALL THROUGH */
		case ST_RD_POS_DATA_1:
			/*
			 * Read a position table compressed in huffman
			 * coding.
			 */
			ds->pt.len_size = ds->pos_pt_len_size;
			ds->pt.len_bits = ds->pos_pt_len_bits;
			ds->reading_position = 1;
			ds->state = ST_RD_PT_1;
			break;
		case ST_GET_LITERAL:
			return (100);
		}
	}
failed:
	return (ds->error = ARCHIVE_FAILED);
}

static int
lzh_decode_blocks(struct lzh_stream *strm, int last)
{
	struct lzh_dec *ds = strm->ds;
	struct lzh_br bre = ds->br;
	struct huffman *lt = &(ds->lt);
	struct huffman *pt = &(ds->pt);
	unsigned char *w_buff = ds->w_buff;
	unsigned char *lt_bitlen = lt->bitlen;
	unsigned char *pt_bitlen = pt->bitlen;
	int blocks_avail = ds->blocks_avail, c = 0;
	int copy_len = ds->copy_len, copy_pos = ds->copy_pos;
	int w_pos = ds->w_pos, w_mask = ds->w_mask, w_size = ds->w_size;
	int lt_max_bits = lt->max_bits, pt_max_bits = pt->max_bits;
	int state = ds->state;
	int literal_state = ds->literal_state;

	for (;;) {
		switch (state) {
		case ST_GET_LITERAL:
			for (;;) {
				if (blocks_avail == 0) {
					/* We have decoded all blocks.
					 * Let's handle next blocks. */
					ds->state = ST_RD_BLOCK;
					ds->br = bre;
					ds->blocks_avail = 0;
					ds->w_pos = w_pos;
					ds->copy_pos = 0;
					return (100);
				}

				/* lzh_br_read_ahead() always tries to fill the
				 * cache buffer up. In specific situation we
				 * are close to the end of the data, the cache
				 * buffer will not be full and thus we have to
				 * determine if the cache buffer has some bits
				 * as much as we need after lzh_br_read_ahead()
				 * failed. */
				if (!lzh_br_read_ahead(strm, &bre,
				    lt_max_bits)) {
					if (!last)
						goto next_data;
					/* Remaining bits are less than
					 * maximum bits(lt.max_bits) but maybe
					 * it still remains as much as we need,
					 * so we should try to use it with
					 * dummy bits. */
					c = lzh_decode_huffman(lt,
					      lzh_br_bits_forced(&bre,
					        lt_max_bits));
					lzh_br_consume(&bre, lt_bitlen[c]);
					if (!lzh_br_has(&bre, 0))
						goto failed;/* Over read. */
				} else {
					c = lzh_decode_huffman(lt,
					      lzh_br_bits(&bre, lt_max_bits));
					lzh_br_consume(&bre, lt_bitlen[c]);
				}
				blocks_avail--;
				if ((unsigned int)c > UCHAR_MAX)
					/* Current block is a match data. */
					break;
				/*
				 * 'c' is exactly a literal code.
				 */
				/* Save a decoded code to reference it
				 * afterward. */
				w_buff[w_pos] = c;
				if (++w_pos >= w_size) {
					w_pos = 0;
					lzh_emit_window(strm, w_size);
					goto next_data;
				}
			}
			/* 'c' is the length of a match pattern we have
			 * already extracted, which has be stored in
			 * window(ds->w_buff). */
			copy_len = c - (UCHAR_MAX + 1) + MINMATCH;
			/* FALL THROUGH */
		case ST_GET_POS_1:
			/*
			 * Get a reference position. 
			 */
			if (!lzh_br_read_ahead(strm, &bre, pt_max_bits)) {
				if (!last) {
					state = ST_GET_POS_1;
					ds->copy_len = copy_len;
					goto next_data;
				}
				copy_pos = lzh_decode_huffman(pt,
				    lzh_br_bits_forced(&bre, pt_max_bits));
				lzh_br_consume(&bre, pt_bitlen[copy_pos]);
				if (!lzh_br_has(&bre, 0))
					goto failed;/* Over read. */
			} else {
				copy_pos = lzh_decode_huffman(pt,
				    lzh_br_bits(&bre, pt_max_bits));
				lzh_br_consume(&bre, pt_bitlen[copy_pos]);
			}
			/* FALL THROUGH */
		case ST_GET_POS_2:
			if (copy_pos > 1) {
				/* We need an additional adjustment number to
				 * the position. */
				int p = copy_pos - 1;
				if (!lzh_br_read_ahead(strm, &bre, p)) {
					if (last)
						goto failed;/* Truncated data.*/
					state = ST_GET_POS_2;
					ds->copy_len = copy_len;
					ds->copy_pos = copy_pos;
					goto next_data;
				}
				copy_pos = (1 << p) + lzh_br_bits(&bre, p);
				lzh_br_consume(&bre, p);
			}
			/* The position is actually a distance from the last
			 * code we had extracted and thus we have to convert
			 * it to a position of the window. */
			copy_pos = (w_pos - copy_pos - 1) & w_mask;
			/* FALL THROUGH */
		case ST_COPY_DATA:
			/*
			 * Copy `copy_len' bytes as extracted data from
			 * the window into the output buffer.
			 */
			for (;;) {
				int l;

				l = copy_len;
				if (copy_pos > w_pos) {
					if (l > w_size - copy_pos)
						l = w_size - copy_pos;
				} else {
					if (l > w_size - w_pos)
						l = w_size - w_pos;
				}
				if ((copy_pos + l < w_pos)
				    || (w_pos + l < copy_pos)) {
					/* No overlap. */
					memcpy(w_buff + w_pos,
					    w_buff + copy_pos, l);
				} else {
					const unsigned char *s;
					unsigned char *d;
					int li;

					d = w_buff + w_pos;
					s = w_buff + copy_pos;
					for (li = 0; li < l-1;) {
						d[li] = s[li];li++;
						d[li] = s[li];li++;
					}
					if (li < l)
						d[li] = s[li];
				}
				w_pos += l;
				if (w_pos == w_size) {
					w_pos = 0;
					lzh_emit_window(strm, w_size);
					if (copy_len <= l)
						state = literal_state;
					else {
						state = ST_COPY_DATA;
						ds->copy_len = copy_len - l;
						ds->copy_pos =
						    (copy_pos + l) & w_mask;
					}
					goto next_data;
				}
				if (copy_len <= l)
					/* A copy of current pattern ended. */
					break;
				copy_len -= l;
				copy_pos = (copy_pos + l) & w_mask;
			}
			state = literal_state;
			break;

		/*
		 * -lh1- decoding. It joins the shared LZSS machinery at
		 * ST_COPY_DATA above; everything before that differs.
		 */
		case ST_LH1_GET_LITERAL:
			if (ds->out_left <= 0) {
				/* The whole entry is decoded. -lh1- has no end
				 * marker, so the byte count is the terminator
				 * and whatever bits follow are the encoder's
				 * flush. */
				ds->br = bre;
				ds->state = state;
				ds->w_pos = 0;
				if (w_pos > 0)
					lzh_emit_window(strm, w_pos);
				return (ARCHIVE_EOF);
			}
			ds->dyn->walk = ds->dyn->child[LZH1_ROOT];
			state = ST_LH1_TREE;
			/* FALL THROUGH */
		case ST_LH1_TREE:
			/*
			 * Walk the adaptive tree a bit at a time. A code has
			 * no length bound the way a table-driven one does, so
			 * the walk suspends on the node it reached rather than
			 * asking for a fixed number of bits up front.
			 */
			while (ds->dyn->walk > 0) {
				if (!lzh_br_read_ahead(strm, &bre, 1)) {
					if (last)
						goto failed;/* Truncated data. */
					state = ST_LH1_TREE;
					goto next_data;
				}
				ds->dyn->walk = ds->dyn->child[ds->dyn->walk
				    - lzh_br_bits(&bre, 1)];
				lzh_br_consume(&bre, 1);
			}
			c = ~ds->dyn->walk;
			if ((unsigned int)c >= LZH1_NC)
				goto failed;/* Invalid data. */
			lzh_dyn_update(ds->dyn, c);
			if (c <= UCHAR_MAX) {
				/* A literal. */
				w_buff[w_pos] = (unsigned char)c;
				ds->out_left--;
				state = ST_LH1_GET_LITERAL;
				if (++w_pos >= w_size) {
					w_pos = 0;
					lzh_emit_window(strm, w_size);
					goto next_data;
				}
				break;
			}
			copy_len = c - (UCHAR_MAX + 1) + MINMATCH;
			if (copy_len > ds->out_left)
				/* Only reachable on damaged data: a well
				 * formed entry's last match ends exactly on
				 * origsize. Clamping keeps the emitted size
				 * equal to the size the header promised. */
				copy_len = (int)ds->out_left;
			ds->out_left -= copy_len;
			/* FALL THROUGH */
		case ST_LH1_GET_POS_1:
			/* High 6 bits of the distance, Huffman coded with the
			 * fixed table. */
			if (!lzh_br_read_ahead(strm, &bre, 8)) {
				if (!last) {
					state = ST_LH1_GET_POS_1;
					ds->copy_len = copy_len;
					goto next_data;
				}
				c = ds->dyn->pt_tbl[
				    lzh_br_bits_forced(&bre, 8)];
				lzh_br_consume(&bre, ds->dyn->pt_len[c]);
				if (!lzh_br_has(&bre, 0))
					goto failed;/* Over read. */
			} else {
				c = ds->dyn->pt_tbl[lzh_br_bits(&bre, 8)];
				lzh_br_consume(&bre, ds->dyn->pt_len[c]);
			}
			copy_pos = c << 6;
			/* FALL THROUGH */
		case ST_LH1_GET_POS_2:
			/* Low 6 bits, raw. */
			if (!lzh_br_read_ahead(strm, &bre, 6)) {
				if (last)
					goto failed;/* Truncated data. */
				state = ST_LH1_GET_POS_2;
				ds->copy_len = copy_len;
				ds->copy_pos = copy_pos;
				goto next_data;
			}
			copy_pos |= lzh_br_bits(&bre, 6);
			lzh_br_consume(&bre, 6);
			/* Same conversion from distance to window position the
			 * shared path makes. */
			copy_pos = (w_pos - copy_pos - 1) & w_mask;
			state = ST_COPY_DATA;
			break;
		}
	}
failed:
	return (ds->error = ARCHIVE_FAILED);
next_data:
	ds->br = bre;
	ds->blocks_avail = blocks_avail;
	ds->state = state;
	ds->w_pos = w_pos;
	return (ARCHIVE_OK);
}

static int
lzh_huffman_init(struct huffman *hf, size_t len_size, int tbl_bits)
{
	int bits;

	if (hf->bitlen == NULL) {
		hf->bitlen = malloc(len_size * sizeof(hf->bitlen[0]));
		if (hf->bitlen == NULL)
			return (ARCHIVE_FATAL);
	}
	if (hf->tbl == NULL) {
		if (tbl_bits < HTBL_BITS)
			bits = tbl_bits;
		else
			bits = HTBL_BITS;
		hf->tbl = malloc(((size_t)1 << bits) * sizeof(hf->tbl[0]));
		if (hf->tbl == NULL)
			return (ARCHIVE_FATAL);
	}
	if (hf->tree == NULL && tbl_bits > HTBL_BITS) {
		hf->tree_avail = 1 << (tbl_bits - HTBL_BITS + 4);
		hf->tree = malloc(hf->tree_avail * sizeof(hf->tree[0]));
		if (hf->tree == NULL)
			return (ARCHIVE_FATAL);
	}
	hf->len_size = (int)len_size;
	hf->tbl_bits = tbl_bits;
	return (ARCHIVE_OK);
}

static void
lzh_huffman_free(struct huffman *hf)
{
	free(hf->bitlen);
	free(hf->tbl);
	free(hf->tree);
}

static const char bitlen_tbl[0x400] = {
	 7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
	 7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
	 7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
	 7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
	 7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
	 7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
	 7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
	 7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
	 7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
	 7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
	 7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
	 7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
	 7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
	 7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
	 7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
	 7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
	 7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
	 7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
	 7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
	 7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
	 7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
	 7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
	 7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
	 7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
	 7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
	 7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
	 7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
	 7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
	 7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
	 7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
	 7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
	 7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
	 8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
	 8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
	 8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
	 8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
	 8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
	 8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
	 8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
	 8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
	 8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
	 8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
	 8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
	 8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
	 8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
	 8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
	 8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
	 8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
	 9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
	 9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
	 9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
	 9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
	 9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
	 9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
	 9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
	 9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
	10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
	10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
	10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
	10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
	11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
	11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
	12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
	13, 13, 13, 13, 13, 13, 13, 13, 14, 14, 14, 14, 15, 15, 16,  0
};
static int
lzh_read_pt_bitlen(struct lzh_stream *strm, int start, int end)
{
	struct lzh_dec *ds = strm->ds;
	struct lzh_br *br = &(ds->br);
	int c, i;

	for (i = start; i < end; ) {
		/*
		 *  bit pattern     the number we need
		 *     000           ->  0
		 *     001           ->  1
		 *     010           ->  2
		 *     ...
		 *     110           ->  6
		 *     1110          ->  7
		 *     11110         ->  8
		 *     ...
		 *     1111111111110 ->  16
		 */
		if (!lzh_br_read_ahead(strm, br, 3))
			return (i);
		if ((c = lzh_br_bits(br, 3)) == 7) {
			if (!lzh_br_read_ahead(strm, br, 13))
				return (i);
			c = bitlen_tbl[lzh_br_bits(br, 13) & 0x3FF];
			if (c)
				lzh_br_consume(br, c - 3);
			else
				return (-1);/* Invalid data. */
		} else
			lzh_br_consume(br, 3);
		ds->pt.bitlen[i++] = c;
		ds->pt.freq[c]++;
	}
	return (i);
}

static int
lzh_make_fake_table(struct huffman *hf, uint16_t c)
{
	if (c >= hf->len_size)
		return (0);
	hf->tbl[0] = c;
	hf->max_bits = 0;
	hf->shift_bits = 0;
	hf->bitlen[hf->tbl[0]] = 0;
	return (1);
}

/*
 * Make a huffman coding table.
 */
static int
lzh_make_huffman_table(struct huffman *hf)
{
	uint16_t *tbl;
	const unsigned char *bitlen;
	int bitptn[17], weight[17];
	int i, maxbits = 0, ptn, tbl_size, w;
	int diffbits, len_avail;

	/*
	 * Initialize bit patterns.
	 */
	ptn = 0;
	for (i = 1, w = 1 << 15; i <= 16; i++, w >>= 1) {
		bitptn[i] = ptn;
		weight[i] = w;
		if (hf->freq[i]) {
			ptn += hf->freq[i] * w;
			maxbits = i;
		}
	}
	if (ptn != 0x10000 || maxbits > hf->tbl_bits)
		return (0);/* Invalid */

	hf->max_bits = maxbits;

	/*
	 * Cut out extra bits which we won't house in the table.
	 * This preparation reduces the same calculation in the for-loop
	 * making the table.
	 */
	if (maxbits < 16) {
		int ebits = 16 - maxbits;
		for (i = 1; i <= maxbits; i++) {
			bitptn[i] >>= ebits;
			weight[i] >>= ebits;
		}
	}
	if (maxbits > HTBL_BITS) {
		unsigned htbl_max;
		uint16_t *p;

		diffbits = maxbits - HTBL_BITS;
		for (i = 1; i <= HTBL_BITS; i++) {
			bitptn[i] >>= diffbits;
			weight[i] >>= diffbits;
		}
		htbl_max = bitptn[HTBL_BITS] +
		    weight[HTBL_BITS] * hf->freq[HTBL_BITS];
		p = &(hf->tbl[htbl_max]);
		while (p < &hf->tbl[1U<<HTBL_BITS])
			*p++ = 0;
	} else
		diffbits = 0;
	hf->shift_bits = diffbits;

	/*
	 * Make the table.
	 */
	tbl_size = 1 << HTBL_BITS;
	tbl = hf->tbl;
	bitlen = hf->bitlen;
	len_avail = hf->len_avail;
	hf->tree_used = 0;
	for (i = 0; i < len_avail; i++) {
		uint16_t *p;
		int len, cnt;
		uint16_t bit;
		int extlen;
		struct htree_t *ht;

		if (bitlen[i] == 0)
			continue;
		/* Get a bit pattern */
		len = bitlen[i];
		ptn = bitptn[len];
		cnt = weight[len];
		if (len <= HTBL_BITS) {
			/* Calculate next bit pattern */
			if ((bitptn[len] = ptn + cnt) > tbl_size)
				return (0);/* Invalid */
			/* Update the table */
			p = &(tbl[ptn]);
			if (cnt > 7) {
				uint16_t *pc;

				cnt -= 8;
				pc = &p[cnt];
				pc[0] = (uint16_t)i;
				pc[1] = (uint16_t)i;
				pc[2] = (uint16_t)i;
				pc[3] = (uint16_t)i;
				pc[4] = (uint16_t)i;
				pc[5] = (uint16_t)i;
				pc[6] = (uint16_t)i;
				pc[7] = (uint16_t)i;
				if (cnt > 7) {
					cnt -= 8;
					memcpy(&p[cnt], pc,
						8 * sizeof(uint16_t));
					pc = &p[cnt];
					while (cnt > 15) {
						cnt -= 16;
						memcpy(&p[cnt], pc,
							16 * sizeof(uint16_t));
					}
				}
				if (cnt)
					memcpy(p, pc, cnt * sizeof(uint16_t));
			} else {
				while (cnt > 1) {
					p[--cnt] = (uint16_t)i;
					p[--cnt] = (uint16_t)i;
				}
				if (cnt)
					p[--cnt] = (uint16_t)i;
			}
			continue;
		}

		/*
		 * A bit length is too big to be housed to a direct table,
		 * so we use a tree model for its extra bits.
		 */
		bitptn[len] = ptn + cnt;
		bit = 1U << (diffbits -1);
		extlen = len - HTBL_BITS;
		
		p = &(tbl[ptn >> diffbits]);
		if (*p == 0) {
			*p = len_avail + hf->tree_used;
			ht = &(hf->tree[hf->tree_used++]);
			if (hf->tree_used > hf->tree_avail)
				return (0);/* Invalid */
			ht->left = 0;
			ht->right = 0;
		} else {
			if (*p < len_avail ||
			    *p >= (len_avail + hf->tree_used))
				return (0);/* Invalid */
			ht = &(hf->tree[*p - len_avail]);
		}
		while (--extlen > 0) {
			if (ptn & bit) {
				if (ht->left < len_avail) {
					ht->left = len_avail + hf->tree_used;
					ht = &(hf->tree[hf->tree_used++]);
					if (hf->tree_used > hf->tree_avail)
						return (0);/* Invalid */
					ht->left = 0;
					ht->right = 0;
				} else {
					ht = &(hf->tree[ht->left - len_avail]);
				}
			} else {
				if (ht->right < len_avail) {
					ht->right = len_avail + hf->tree_used;
					ht = &(hf->tree[hf->tree_used++]);
					if (hf->tree_used > hf->tree_avail)
						return (0);/* Invalid */
					ht->left = 0;
					ht->right = 0;
				} else {
					ht = &(hf->tree[ht->right - len_avail]);
				}
			}
			bit >>= 1;
		}
		if (ptn & bit) {
			if (ht->left != 0)
				return (0);/* Invalid */
			ht->left = (uint16_t)i;
		} else {
			if (ht->right != 0)
				return (0);/* Invalid */
			ht->right = (uint16_t)i;
		}
	}
	return (1);
}

static int
lzh_decode_huffman_tree(struct huffman *hf, unsigned rbits, int c)
{
	struct htree_t *ht;
	int extlen;

	ht = hf->tree;
	extlen = hf->shift_bits;
	while (c >= hf->len_avail) {
		c -= hf->len_avail;
		if (extlen-- <= 0 || c >= hf->tree_used)
			return (0);
		if (rbits & (1U << extlen))
			c = ht[c].left;
		else
			c = ht[c].right;
	}
	return (c);
}

static inline int
lzh_decode_huffman(struct huffman *hf, unsigned rbits)
{
	int c;
	/*
	 * At first search an index table for a bit pattern.
	 * If it fails, search a huffman tree for.
	 */
	c = hf->tbl[rbits >> hf->shift_bits];
	if (c < hf->len_avail || hf->len_avail == 0)
		return (c);
	/* This bit pattern needs to be found out at a huffman tree. */
	return (lzh_decode_huffman_tree(hf, rbits, c));
}

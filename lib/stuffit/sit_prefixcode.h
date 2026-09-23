/*
 * sit_prefixcode.h - shared binary prefix-code (Huffman-style) tree
 * helper: an explicit node array, plus two non-recursive ways to
 * build it and one non-recursive way to decode with it.
 *
 * Ported from the tree-manipulation parts of XADPrefixCode.m/.h, The
 * Unarchiver (https://github.com/MacPaw/XADMaster), LGPL v2.1+ -
 * specifically: the plain (non-table-accelerated) bit-by-bit decode
 * given commented-out at the bottom of XADPrefixCode.m
 * (CSInputNextSymbolUsingCode); the self-describing tree parser used
 * by XADStuffItHuffmanHandle.m's parseTree (converted here from two
 * recursive calls per internal node into an explicit pending-work
 * stack); and the canonical from-code-lengths tree builder used via
 * -initWithLengths:numberOfSymbols:maximumLength:shortestCodeIsZeros:
 * (already non-recursive nested for-loops in the original; the
 * per-node XADPrefixCode Realloc is replaced here by one upfront
 * sit_allocator allocation sized by the caller).
 *
 * NOT ported: the table-accelerated _makeTable/_makeTableLE fast path
 * (a pure performance optimization that no caller here needs, and
 * whose MakeTable helper is itself recursive), and the "repeatAt"
 * mid-code-reuse feature of -addValue:forCodeWithHighBitFirst:
 * length:repeatAt: (dead code for every real caller in this project -
 * -initWithLengths: always calls the plain, non-repeating add path,
 * since repeatpos there is always passed as `length`, which the
 * reference's own repeat-position check always treats as "no repeat
 * requested").
 */
#ifndef FN_SIT_PREFIXCODE_H
#define FN_SIT_PREFIXCODE_H

#include <stdint.h>
#include "stuffit.h"
#include "sit_bitreader.h"

/* One tree node: left/right child index (>=0), or SIT_PC_EMPTY (-1)
 * for a branch not yet built ("open"). A leaf is a node whose left
 * and right fields are equal AND <= -2 (see sit_pc_decode_bits() /
 * the private encode/decode helpers in sit_prefixcode.c) - a node
 * fresh out of sit_pc_init()/pc_new_node() has left==right==-1, which
 * is deliberately NOT a valid leaf encoding, so "not yet built" and
 * "leaf" are never ambiguous. Node 0 is always the tree root. */
typedef struct {
    int32_t left;
    int32_t right;
} sit_pc_node;

#define SIT_PC_EMPTY (-1)

typedef struct {
    sit_pc_node *nodes;  /* [capacity], owned, via the sit_allocator passed to sit_pc_init */
    int count;            /* nodes[0..count) are in use */
    int capacity;          /* allocated size of nodes[] */
} sit_prefix_code;

/* Allocates nodes[capacity] through alloc and sets up an empty root
 * (node 0). capacity must be large enough for every node the caller's
 * build will need - a full binary tree with at most N distinct leaf
 * values needs at most 2N-1 nodes total, so any capacity >= 2*N is
 * always safe. Returns SIT_OK, SIT_E_INVAL (capacity<1), or
 * SIT_E_NOMEM. */
int sit_pc_init(sit_prefix_code *pc, int capacity, const sit_allocator *alloc);

/* Frees pc->nodes (safe to call on a zero-initialized or
 * already-freed sit_prefix_code - it is a no-op when pc->nodes is
 * already NULL). */
void sit_pc_free(sit_prefix_code *pc, const sit_allocator *alloc);

/* Build a tree exactly as XADStuffItHuffmanHandle.m's parseTree does:
 * read 1 bit (MSB-first); 1 means the next 8 bits (MSB-first) are a
 * literal byte value, ending a leaf at the current node; 0 means the
 * current node is internal, with its zero-branch parsed immediately,
 * followed by its one-branch. The reference's two-recursive-calls-
 * per-internal-node algorithm is replaced here by an explicit stack
 * of pending "still need to build this node's one-branch" entries,
 * bounded by SIT_PC_MAX_TREE_DEPTH (comfortably more than any
 * reachable depth for a byte alphabet, whose maximum Huffman code
 * length is under 256). Returns SIT_OK or SIT_E_LIMIT (tree bigger
 * than `capacity`, or nested deeper than the pending-stack bound -
 * which is also what a pathologically truncated tree description
 * runs into, since it never closes the pending stack; the shared
 * sit_bitreader.h's own greedy read-ahead makes its `error` flag
 * unreliable this close to end-of-stream - see sit_bitreader.h's own
 * comment on sit_br_fill - so it is deliberately not checked here). */
#define SIT_PC_MAX_TREE_DEPTH 512
int sit_pc_build_from_bitstream(sit_prefix_code *pc, sit_bitreader *br);

/* Insert one (value, code, length) canonical entry - code is given
 * MSB (high-bit) first, matching the reference's plain (non-repeating)
 * -addValue:forCodeWithHighBitFirst:length:. Returns SIT_OK,
 * SIT_E_LIMIT (capacity exhausted) or SIT_E_CORRUPT (code
 * collides with, or is a prefix of, another already-inserted code). */
int sit_pc_add_symbol(sit_prefix_code *pc, int value, uint32_t code_msb_first, int length);

/* Reverses the low `length` bits of code (used to turn a low-bit-
 * first constant, such as XADStuffIt13Handle.m's MetaCodes[], into
 * the high-bit-first form sit_pc_add_symbol wants - matches
 * XADPrefixCode.m's static ReverseN()). */
static inline uint32_t sit_pc_reverse_bits(uint32_t code, int length)
{
    uint32_t v = 0;
    for (int i = 0; i < length; i++) {
        v = (v << 1) | (code & 1u);
        code >>= 1;
    }
    return v;
}

/* Build a canonical Huffman tree from an array of `numsymbols` code
 * lengths (0, negative, or >maxlength = symbol unused/unencodable),
 * assigning consecutive MSB-first codes in order of increasing length
 * then symbol index, starting at code 0 for the first (shortest)
 * length that appears - i.e. always the "shortestCodeIsZeros:YES"
 * path of -initWithLengths:numberOfSymbols:maximumLength:
 * shortestCodeIsZeros: (the shortestCodeIsZeros:NO path is unused by
 * every caller in this project and is not implemented), with the
 * repeat-code feature elided as dead code (see file header). Returns
 * SIT_OK, SIT_E_LIMIT, or SIT_E_CORRUPT (from sit_pc_add_symbol). */
int sit_pc_build_from_lengths(sit_prefix_code *pc, const int *lengths,
                               int numsymbols, int maxlength);

/* Generic single-bit source for sit_pc_decode_bits: returns 0 or 1,
 * or a negative value if no more bits are available (propagated as
 * SIT_E_IO). */
typedef int (*sit_pc_bit_fn)(void *ctx);

/* Bit-by-bit decode of one symbol, walking from the root - the
 * "commented-out simple" CSInputNextSymbolUsingCode(LE) from
 * XADPrefixCode.m; the table-accelerated fast path is not ported (see
 * file header). Returns the decoded value (>=0), SIT_E_CORRUPT if the
 * walk reaches a branch that was never built (an invalid code in the
 * bitstream), or SIT_E_IO if getbit reports it has run out of input. */
int sit_pc_decode_bits(sit_prefix_code *pc, sit_pc_bit_fn getbit, void *ctx);

/* Convenience wrapper over the shared MSB-first sit_bitreader, for
 * method 3 (sit_huffman.c), which needs no other bit source. Note
 * sit_br_getbit() never itself reports EOF via a negative return (it
 * pads with zero bits and sets br->error instead - see
 * sit_bitreader.h), so this wrapper cannot surface SIT_E_IO; a
 * genuinely truncated method-3 stream is instead almost always caught
 * as SIT_E_CORRUPT (an open branch) by the tree walk itself. */
int sit_pc_decode(sit_prefix_code *pc, sit_bitreader *br);

#endif /* FN_SIT_PREFIXCODE_H */

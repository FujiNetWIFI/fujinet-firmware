/*
 * sit_prefixcode.c - see sit_prefixcode.h.
 * Ported from XADPrefixCode.m/.h and (for the self-describing tree
 * shape) XADStuffItHuffmanHandle.m's parseTree, The Unarchiver
 * (https://github.com/MacPaw/XADMaster), LGPL v2.1+.
 */
#include "sit_prefixcode.h"

int sit_pc_init(sit_prefix_code *pc, int capacity, const sit_allocator *alloc)
{
    if (capacity < 1) return SIT_E_INVAL;

    pc->nodes = alloc->alloc((size_t)capacity * sizeof(sit_pc_node), alloc->ctx);
    if (!pc->nodes) return SIT_E_NOMEM;

    pc->capacity = capacity;
    pc->count = 1;
    pc->nodes[0].left = SIT_PC_EMPTY;
    pc->nodes[0].right = SIT_PC_EMPTY;
    return SIT_OK;
}

void sit_pc_free(sit_prefix_code *pc, const sit_allocator *alloc)
{
    if (pc->nodes) alloc->free(pc->nodes, alloc->ctx);
    pc->nodes = NULL;
    pc->count = 0;
    pc->capacity = 0;
}

static int pc_new_node(sit_prefix_code *pc)
{
    if (pc->count >= pc->capacity) return -1;
    int n = pc->count++;
    pc->nodes[n].left = SIT_PC_EMPTY;
    pc->nodes[n].right = SIT_PC_EMPTY;
    return n;
}

static int pc_is_leaf(const sit_prefix_code *pc, int node)
{
    int32_t l = pc->nodes[node].left;
    int32_t r = pc->nodes[node].right;
    return l == r && l <= -2;
}

static int pc_leaf_value(const sit_prefix_code *pc, int node)
{
    return (int)(-(pc->nodes[node].left) - 2);
}

static void pc_set_leaf(sit_prefix_code *pc, int node, int value)
{
    int32_t enc = -(int32_t)value - 2;
    pc->nodes[node].left = enc;
    pc->nodes[node].right = enc;
}

/* ------------------------------------------------------------------ */
/* Self-describing tree parse (method 3), iterative.                   */
/* ------------------------------------------------------------------ */
int sit_pc_build_from_bitstream(sit_prefix_code *pc, sit_bitreader *br)
{
    /* Note: br->error is deliberately NOT checked here. sit_bitreader.h's
     * sit_br_fill() greedily tops up its accumulator to 25+ bits on
     * every underflow regardless of how many bits the caller actually
     * asked for, so br->error commonly (and correctly, per that
     * header's own documentation) becomes set once we are within
     * about 3 bytes of the end of a perfectly well-formed stream, well
     * before the tree description or message data has actually been
     * exhausted. Treating it as fatal here would spuriously reject
     * valid small inputs (as a synthetic self-test caught). Genuine
     * truncation/corruption is instead caught structurally: a
     * pathologically incomplete tree description either never closes
     * (runs into SIT_E_LIMIT via the capacity/stack bounds below) or
     * produces a tree that later decode calls detect via an open
     * branch (SIT_E_CORRUPT, see sit_pc_decode_bits). */
    /* pending "build node N's one-branch": 512 ints (2KB) - kept as
     * function-static storage rather than a stack local so a
     * constrained caller's stack frame here stays small. Safe because
     * this function is not reentrant/recursive (one self-describing
     * tree is fully parsed, sp reset to 0, before any later call could
     * reuse it) and every slot actually read (indices below sp) was
     * written earlier in this same call. */
    static int pending[SIT_PC_MAX_TREE_DEPTH];
    int sp = 0;
    int curr = 0; /* root, already allocated by sit_pc_init */

    for (;;) {
        int bit = sit_br_getbit(br);

        if (bit == 1) {
            int val = (int)sit_br_getbits(br, 8);

            pc_set_leaf(pc, curr, val);

            if (sp == 0) break; /* whole tree finished */

            int parent = pending[--sp];
            int nn = pc_new_node(pc);
            if (nn < 0) return SIT_E_LIMIT;
            pc->nodes[parent].right = nn;
            curr = nn;
        } else {
            int zero = pc_new_node(pc);
            if (zero < 0) return SIT_E_LIMIT;
            pc->nodes[curr].left = zero;

            if (sp >= SIT_PC_MAX_TREE_DEPTH) return SIT_E_LIMIT;
            pending[sp++] = curr;
            curr = zero;
        }
    }
    return SIT_OK;
}

/* ------------------------------------------------------------------ */
/* Canonical from-code-lengths tree build.                              */
/* ------------------------------------------------------------------ */
int sit_pc_add_symbol(sit_prefix_code *pc, int value, uint32_t code_msb_first, int length)
{
    if (length <= 0 || length > 32) return SIT_E_INVAL;

    int lastnode = 0;
    for (int bitpos = length - 1; bitpos >= 0; bitpos--) {
        int bit = (int)((code_msb_first >> bitpos) & 1u);

        if (pc_is_leaf(pc, lastnode)) return SIT_E_CORRUPT; /* "Prefix found" */

        int32_t *branch = bit ? &pc->nodes[lastnode].right : &pc->nodes[lastnode].left;
        if (*branch == SIT_PC_EMPTY) {
            int nn = pc_new_node(pc);
            if (nn < 0) return SIT_E_LIMIT;
            branch = bit ? &pc->nodes[lastnode].right : &pc->nodes[lastnode].left;
            *branch = nn;
        }
        lastnode = *branch;
    }

    if (!(pc->nodes[lastnode].left == SIT_PC_EMPTY && pc->nodes[lastnode].right == SIT_PC_EMPTY))
        return SIT_E_CORRUPT; /* "Prefix found": lastnode already in use */

    pc_set_leaf(pc, lastnode, value);
    return SIT_OK;
}

int sit_pc_build_from_lengths(sit_prefix_code *pc, const int *lengths,
                               int numsymbols, int maxlength)
{
    uint32_t code = 0;
    int symbolsleft = numsymbols;

    for (int length = 1; length <= maxlength; length++) {
        for (int i = 0; i < numsymbols; i++) {
            if (lengths[i] != length) continue;

            int rc = sit_pc_add_symbol(pc, i, code, length);
            if (rc != SIT_OK) return rc;

            code++;
            if (--symbolsleft == 0) return SIT_OK;
        }
        code <<= 1;
    }
    return SIT_OK;
}

/* ------------------------------------------------------------------ */
/* Decode.                                                              */
/* ------------------------------------------------------------------ */
int sit_pc_decode_bits(sit_prefix_code *pc, sit_pc_bit_fn getbit, void *ctx)
{
    int node = 0;
    int steps = 0;

    while (!pc_is_leaf(pc, node)) {
        if (++steps > pc->capacity) return SIT_E_CORRUPT; /* cannot exceed tree depth */

        int bit = getbit(ctx);
        if (bit < 0) return SIT_E_IO;

        int32_t branch = bit ? pc->nodes[node].right : pc->nodes[node].left;
        if (branch == SIT_PC_EMPTY) return SIT_E_CORRUPT;
        node = (int)branch;
    }
    return pc_leaf_value(pc, node);
}

static int pc_msb_bit_adapter(void *ctx)
{
    return sit_br_getbit((sit_bitreader *)ctx);
}

int sit_pc_decode(sit_prefix_code *pc, sit_bitreader *br)
{
    return sit_pc_decode_bits(pc, pc_msb_bit_adapter, br);
}

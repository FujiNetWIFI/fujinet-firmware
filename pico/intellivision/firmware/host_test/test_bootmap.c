// Host-buildable test driver for bootmap.c -- no RP2040/pico-sdk involved.
// Feeds real cartridge files through the same calls dbc_inbound_handler()
// makes, in the same 512-byte chunks MediaTypeROM::push_stream() sends, then
// commits the resulting plan into an mm_map_t and dumps what the bus would
// actually see. This is the regression gate for the network boot path: the
// cart itself only needs flashing once the output here is right.
//
// Build and run (from this directory):
//   gcc -Wall -Wextra -I../include -o /tmp/test_bootmap test_bootmap.c ../src/bootmap.c ../src/memory.c ../src/utils.c
//   /tmp/test_bootmap "$HOME/tnfs/INTV/sample roms/"*.bin "$HOME/tnfs/INTV/sample roms/"*.rom
//
// --rom-words N picks the board profile (default 220160 = fujicard's
// 1024*215; pass 100352 for pirto_ii_default's 1024*98).
//
// Expected verdicts for the sample set, on the fujicard profile:
//
//   DK Arcade.bin              OK   4 segs, no paging
//   pensate.bin                OK   2 segs, [vars] metadata ignored
//   Unlucky Pony.bin           OK   2 segs
//   frantic 4.bin              OK  17 segs, 11 paged, jlp=1
//   Cloudfire.bin              OK  24 segs, 13 paged, jlp=3 jlpflash=2 ecs=1
//   Pacmanthology.bin          OK  30 segs, 27 paged, jlp=1; 3 segs shadowed
//                                   away by the JLP window, 27 committed
//   Missile Domination.rom     OK   3 segs + RAM $8000-$81FF
//   Maria.rom                  OK   5 segs incl. $2100-$2FFF
//   Space Intruders (2026) 3.rom OK 4 segs incl. $2100-$2FFF
//   unlucky_pony_2024.rom      OK   4 segs incl. $2000-$2FFF
//
// On the pirto_ii_default profile Cloudfire and Pacmanthology instead
// report TOOBIG -- that board's cart.ROM[] cannot hold them.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "bootmap.h"
#include "memory.h"

#define PUSH_CHUNK 512          // DISK_SECTORBUF_SIZE on the ESP32 side
#define STAGE_BASE 0x2600u      // FUJI_STAGE_BASE
#define RAM_WORDS  0x2800u      // RAMSIZE

static uint16_t *rom_buf;
static uint32_t  rom_words = 1024 * 215;

extern mm_map_t m;              // the live map, defined in memory.c

static const char *err_name(int e)
{
    switch (e) {
    case 0:                       return "OK";
    case FUJI_BOOT_ERR_REJECTED:  return "REJECTED";
    case FUJI_BOOT_ERR_TRUNCATED: return "TRUNCATED";
    case FUJI_BOOT_ERR_NOMAP:     return "NOMAP";
    case FUJI_BOOT_ERR_MAILBOX:   return "MAILBOX";
    case FUJI_BOOT_ERR_CFGBAD:    return "CFGBAD";
    case FUJI_BOOT_ERR_RAM:       return "RAM";
    case FUJI_BOOT_ERR_TOOBIG:    return "TOOBIG";
    case FUJI_BOOT_ERR_BANKSW:    return "BANKSW";
    default:                      return "?";
    }
}

// Reads a whole file. Returns NULL (without complaint) if it isn't there --
// a missing .cfg sibling is the normal case for a .rom.
static uint8_t *slurp(const char *path, long *len_out)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(len > 0 ? (size_t)len : 1);
    if (len > 0 && fread(buf, 1, (size_t)len, f) != (size_t)len) {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    *len_out = len;
    return buf;
}

// Same-basename .cfg sibling, trying both cases like MediaTypeROM::mount().
static uint8_t *load_cfg_sibling(const char *rompath, long *len_out, char *namebuf,
                                 size_t namesz)
{
    snprintf(namebuf, namesz, "%s", rompath);
    char *base = namebuf;
    for (char *p = namebuf; *p; p++)
        if (*p == '/')
            base = p + 1;
    char *dot = strrchr(base, '.');
    if (!dot)
        return NULL;

    strcpy(dot, ".cfg");
    uint8_t *d = slurp(namebuf, len_out);
    if (d)
        return d;
    strcpy(dot, ".CFG");
    return slurp(namebuf, len_out);
}

// Walks the committed map through mm_lookup(), coalescing runs that share a
// kind and stay contiguous in the source. This is what the bus sees, not
// what the plan said -- which is the point.
static void dump_coverage(void)
{
    static const char *kn[] = { "----", "ROM ", "RAM8", "RAM16" };

    for (int page = 0; page < MM_NUM_PLANES; page++) {
        if (page > 0 && memcmp(m.map[page], m.map[0], MM_NUM_BLOCKS) == 0)
            continue;   // identical to the fixed plane -- nothing paged here

        char header[32];
        snprintf(header, sizeof(header), "  page %X   :", page);
        bool printed = false;

        uint32_t a = 0;
        while (a <= 0xFFFF) {
            uint32_t off;
            mm_kind_t k = mm_lookup(&m, (uint16_t)a, (uint8_t)page, &off);
            if (k == MM_NONE) {
                a++;
                continue;
            }
            uint32_t start = a, start_off = off;
            while (a + 1 <= 0xFFFF) {
                uint32_t noff;
                mm_kind_t nk = mm_lookup(&m, (uint16_t)(a + 1), (uint8_t)page, &noff);
                if (nk != k || noff != off + 1)
                    break;
                a++;
                off = noff;
            }
            printf("%s $%04X-$%04X %s @%05X\n", printed ? "             " : header,
                   start, a, kn[k], start_off);
            printed = true;
            a++;
        }
        if (!printed)
            printf("%s (nothing mapped)\n", header);
    }
}

static int run_one(const char *path)
{
    long romlen = 0;
    uint8_t *rom = slurp(path, &romlen);
    if (!rom) {
        printf("=== %s\n  CANNOT READ\n", path);
        return 1;
    }

    const char *base = strrchr(path, '/');
    printf("=== %s\n", base ? base + 1 : path);

    bootmap_init(rom_buf, rom_words, STAGE_BASE, RAM_WORDS);

    char cfgname[1024];
    long cfglen = 0;
    uint8_t *cfg = load_cfg_sibling(path, &cfglen, cfgname, sizeof(cfgname));
    if (cfg) {
        const char *cb = strrchr(cfgname, '/');
        printf("  cfg      : %s (%ld bytes)\n", cb ? cb + 1 : cfgname, cfglen);
        bootmap_cfg_begin();
        for (long i = 0; i < cfglen; i += PUSH_CHUNK) {
            long n = cfglen - i < PUSH_CHUNK ? cfglen - i : PUSH_CHUNK;
            bootmap_cfg_data(cfg + i, (uint16_t)n);
        }
        bootmap_cfg_end(false);
        free(cfg);
    } else {
        printf("  cfg      : (none)\n");
    }

    printf("  rom      : %ld bytes, %ld words\n", romlen, romlen / 2);

    int err = bootmap_rom_begin((uint32_t)romlen);
    if (!err) {
        for (long i = 0; i < romlen; i += PUSH_CHUNK) {
            long n = romlen - i < PUSH_CHUNK ? romlen - i : PUSH_CHUNK;
            bootmap_rom_data(rom + i, (uint16_t)n);
        }
    }
    free(rom);

    bm_plan_t *plan;
    int end_err = bootmap_rom_end(&plan);
    if (!err)
        err = end_err;

    printf("  result   : %s\n", err_name(err));
    if (err)
        return 1;

    printf("  vars     : jlp=%d jlpflash=%d ecs=%d voice=%d paging=%s\n",
           plan->jlp, plan->jlpflash, plan->ecs, plan->voice, plan->paging ? "yes" : "no");

    // fujinet.c's apply_boot_mapping() does this before committing: the JLP
    // RAM window shadows any cartridge ROM mapped underneath it.
    if (plan->jlp || plan->jlpflash) {
        int dropped = bootmap_shadow_range(plan, 0x8000, 0x9FFF);
        if (dropped)
            printf("  shadowed : %d segment(s) dropped under the JLP window\n", dropped);
    }

    printf("  segments : %d\n", plan->nseg);
    for (int i = 0; i < plan->nseg; i++) {
        printf("    [%02d] $%04X", i, plan->seg[i].cpu);
        if (plan->seg[i].page != MM_NO_PAGE)
            printf(" page %X", plan->seg[i].page);
        else
            printf("       ");
        printf("  rom[%05X..%05X]  %u words\n", plan->seg[i].rom_off, plan->seg[i].rom_end,
               (unsigned)(plan->seg[i].rom_end - plan->seg[i].rom_off + 1));
    }
    printf("  ram      : %d\n", plan->nram);
    for (int i = 0; i < plan->nram; i++)
        printf("    [%02d] $%04X-$%04X  RAM %u\n", i, plan->ram[i].lo, plan->ram[i].hi,
               plan->ram[i].width);

    // Commit exactly the way apply_boot_mapping() does.
    int commit = 0;
    mm_init(&m);
    for (int i = 0; i < plan->nseg; i++) {
        int r = mm_add(&m, plan->seg[i].rom_off, plan->seg[i].rom_end,
                       plan->seg[i].cpu, plan->seg[i].page);
        if (r < 0 && !commit)
            commit = r;
    }
    for (int i = 0; i < plan->nram; i++) {
        int r = mm_add_ram(&m, plan->ram[i].lo, plan->ram[i].hi, plan->ram[i].width);
        if (r < 0 && !commit)
            commit = r;
    }
    if (mm_finalize(&m) < 0 && !commit)
        commit = -99;

    if (commit) {
        printf("  commit   : FAILED (mm err %d)\n", commit);
        return 1;
    }
    printf("  commit   : ok (entries=%d splits=%u ram_words=%u)\n",
           m.count, m.n_splits, (unsigned)m.ram_words);
    dump_coverage();
    return 0;
}

int main(int argc, char **argv)
{
    int first = 1;
    if (argc > 2 && strcmp(argv[1], "--rom-words") == 0) {
        rom_words = (uint32_t)strtoul(argv[2], NULL, 0);
        first = 3;
    }
    if (first >= argc) {
        fprintf(stderr, "usage: %s [--rom-words N] <rom|bin>...\n", argv[0]);
        return 2;
    }

    rom_buf = calloc(rom_words, sizeof(uint16_t));
    if (!rom_buf) {
        fprintf(stderr, "out of memory for a %u-word cart.ROM[]\n", (unsigned)rom_words);
        return 2;
    }
    printf("cart.ROM[] = %u words, stage base $%04X\n\n", (unsigned)rom_words, STAGE_BASE);

    int failures = 0;
    for (int i = first; i < argc; i++) {
        failures += run_one(argv[i]);
        printf("\n");
    }

    printf("%d file(s), %d without a usable map\n", argc - first, failures);
    free(rom_buf);
    return 0;
}

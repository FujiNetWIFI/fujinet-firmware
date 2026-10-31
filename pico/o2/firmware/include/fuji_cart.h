/* fuji_cart.h -- the cartridge-side mailbox. core1 rings mailbox writes for
 * core0 to run; reads need nothing, as the mailbox is painted into rom_table[].
 */

#ifndef FUJI_CART_H
#define FUJI_CART_H

#include <stdbool.h>
#include <stdint.h>

#include "o2app.h"

/* Holds one transaction (~6 register writes plus up to 256 payload bytes); the
 * console cannot start another until it sees ACKSEQ. */
#define FUJI_RING_LEN 512

typedef struct {
    volatile uint16_t buf[FUJI_RING_LEN];   /* (addr << 8) | data */
    volatile uint16_t head;                 /* written by core1 only */
    volatile uint16_t tail;                 /* written by core0 only */
    volatile bool     overflow;
} fuji_ring_t;

extern fuji_ring_t fuji_ring;

/* The tables core1 serves a legacy image from, and the one it serves now.
 * rom_table[] is the baked CONFIG's and new_rom_table[] the first push's; a
 * push lands in whichever is not running, so the running one is never rewritten. */
extern unsigned char rom_table[8][4096];
extern unsigned char new_rom_table[8][4096];
extern unsigned char (*fuji_rom)[4096];
extern volatile bool fuji_boot_armed;

/* Does the RUNNING image own the mailbox page? A game that does not claim it
 * keeps its own bytes there, and nothing may be published over them. */
extern volatile bool fuji_mailbox_active;

/* The image waiting for the console's next reset: the table it is laid out in
 * (an APPBANK image keeps only its mailbox bytes there) and whether it claims
 * the mailbox. core1 makes both the running image's in fuji_cart_swap(). */
extern unsigned char (*volatile fuji_stage_rom)[4096];
extern volatile bool fuji_stage_mailbox;

/* Protocol 2: core0 fills fuji_app_next and arms it before the boot swap; core1
 * copies it to fuji_app, which it serves, at the $400 fetch that swaps the
 * tables. The baked CONFIG, when it is an APPBANK image, is fuji_app from boot. */
extern o2app_t fuji_app;
extern o2app_t fuji_app_next;
extern volatile bool fuji_app_armed;
extern volatile bool fuji_app_live;

/* core1 bus loop: must stay a compare and two stores. */
static inline void fuji_cart_note_write(uint8_t addr, uint8_t data)
{
    uint16_t head = fuji_ring.head;
    uint16_t next = (uint16_t)((head + 1u) % FUJI_RING_LEN);

    if (next == fuji_ring.tail) {
        fuji_ring.overflow = true;      /* core0 fell behind; drop, don't wrap */
        return;
    }
    fuji_ring.buf[head] = (uint16_t)(((uint16_t)addr << 8) | data);
    fuji_ring.head = next;
}

/* core1, at the console's fetch of $400 with an image staged: the staged
 * image becomes the running one. Field by field, no call: this runs inside the
 * ~1 us the byte at $400 is due in, from RAM. The mailbox flag goes first so
 * core0, which reads the table pointer before the flag, never sees the new
 * game's table with the old flag. */
static inline __attribute__((always_inline)) void fuji_cart_swap(void)
{
    fuji_mailbox_active = fuji_stage_mailbox;
    __asm__ volatile ("" ::: "memory");
    if (fuji_app_armed) {
        fuji_app.image = fuji_app_next.image;
        fuji_app.npages = fuji_app_next.npages;
        fuji_app.pagebase = fuji_app_next.pagebase;
        fuji_app.pagenum = fuji_app_next.pagenum;
        fuji_app.regsel = fuji_app_next.regsel;
        fuji_app.poke = fuji_app_next.poke;
        fuji_app.win = fuji_app_next.win;
    }
    fuji_app_live = fuji_app_armed;
    fuji_rom = fuji_stage_rom;
    fuji_boot_armed = false;
    __asm__ volatile ("" ::: "memory");
}

/* core0 */
bool fuji_cart_next_write(uint8_t *addr, uint8_t *data);
void fuji_cart_poke(unsigned prog_addr, uint8_t value);
void fuji_cart_paint(void);
void fuji_cart_init(void);

/* The table a push is laid out in: the one that is not running. */
unsigned char (*fuji_cart_stage_table(void))[4096];

/* An image is laid out in `table` and about to be armed. If it claims the
 * mailbox, its page takes over the running image's status bytes ($F20-$F2F, so
 * ACKSEQ and the boot state carry across) and every later poke reaches both. */
void fuji_cart_stage_boot(unsigned char (*table)[4096], bool claims);

#endif /* FUJI_CART_H */

#include <string.h>

#include "fuji_cart.h"
#include "fuji_mailbox.h"

fuji_ring_t fuji_ring;
volatile bool fuji_mailbox_active = true;

extern unsigned char rom_table[8][4096];
extern unsigned char new_rom_table[8][4096];

/* Set once an image is staged. core1 owns the live-table pointer and swaps it
 * to new_rom_table at the boot fetch, so from that moment on this is the table
 * a published byte has to reach -- but core0 cannot read that pointer safely
 * (core1 writes it, and nothing makes the load ordered or unhoistable). It
 * publishes into both tables instead: rom_table is dead memory after the swap,
 * and eight extra stores off the bus loop cost nothing. */
static bool publish_staged;

bool fuji_cart_next_write(uint8_t *addr, uint8_t *data)
{
    uint16_t tail = fuji_ring.tail;
    uint16_t entry;

    if (tail == fuji_ring.head)
        return false;

    entry = fuji_ring.buf[tail];
    fuji_ring.tail = (uint16_t)((tail + 1u) % FUJI_RING_LEN);
    *addr = (uint8_t)(entry >> 8);
    *data = (uint8_t)entry;
    return true;
}

/* Publish one mailbox byte. It goes into every bank because the console may be
 * running with any of them selected, and the mailbox has to answer regardless
 * of which. This is why serving the mailbox costs the bus loop nothing: by the
 * time core1 sees the read, the byte is already ROM as far as it is concerned. */
void fuji_cart_poke(unsigned prog_addr, uint8_t value)
{
    int b;

    if (!fuji_mailbox_active)
        return;                 /* a booted game owns this page now */
    for (b = 0; b < 8; b++) {
        rom_table[b][prog_addr & 0xFFF] = value;
        if (publish_staged)
            new_rom_table[b][prog_addr & 0xFFF] = value;
    }
}

/* Called when an image is armed: o2map_apply has just written it over the
 * staged table's mailbox page, so a mailbox that survives the boot has to be
 * published into that table too, from here to the end of the session. */
void fuji_cart_stage_boot(void)
{
    publish_staged = true;
}

void fuji_cart_paint(void)
{
    unsigned a;

    if (!fuji_mailbox_active)
        return;
    for (a = FN_R_ACKSEQ; a <= 0xFFF; a++)
        fuji_cart_poke(a, 0);
    fuji_cart_poke(FN_R_MAGIC0, 'F');
    fuji_cart_poke(FN_R_MAGIC1, 'N');
    fuji_cart_poke(FN_R_PROTO_VER, 1);
}

void fuji_cart_init(void)
{
    memset((void *)&fuji_ring, 0, sizeof fuji_ring);
    fuji_mailbox_active = true;
    publish_staged = false;
    fuji_cart_paint();
}

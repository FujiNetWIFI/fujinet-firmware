#include <string.h>

#include "fuji_cart.h"
#include "fuji_mailbox.h"

fuji_ring_t fuji_ring;
volatile bool fuji_mailbox_active = true;

o2app_t fuji_app;
volatile bool fuji_app_armed;
volatile bool fuji_app_live;

extern unsigned char rom_table[8][4096];
extern unsigned char new_rom_table[8][4096];

/* core1 swaps to new_rom_table at the boot fetch and core0 cannot read that
 * pointer safely, so once an image is staged, publish into both tables. */
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

/* Into every bank, since the console may have any selected; core1 then serves
 * it as plain ROM. */
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
    fuji_cart_poke(FN_R_PROTO_VER, FN_PROTO_VER);
}

void fuji_cart_init(void)
{
    memset((void *)&fuji_ring, 0, sizeof fuji_ring);
    fuji_mailbox_active = true;
    publish_staged = false;
    fuji_cart_paint();
}

#include <string.h>

#include "fuji_cart.h"
#include "fuji_mailbox.h"

fuji_ring_t fuji_ring;
volatile bool fuji_mailbox_active = true;

extern unsigned char rom_table[8][4096];

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
    for (b = 0; b < 8; b++)
        rom_table[b][prog_addr & 0xFFF] = value;
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
    fuji_cart_paint();
}

#include <string.h>

#include "fuji_cart.h"
#include "fuji_mailbox.h"

fuji_ring_t fuji_ring;
volatile bool fuji_mailbox_active = true;

o2app_t fuji_app;
o2app_t fuji_app_next;
volatile bool fuji_app_armed;
volatile bool fuji_app_live;

unsigned char (*volatile fuji_stage_rom)[4096];
volatile bool fuji_stage_mailbox;

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

/* Into every bank of the table core1 serves, since the console may have any
 * selected; core1 then serves it as plain ROM. While an image is staged that
 * claims the mailbox its table gets the byte too, so it takes over a current
 * page. A staged game that does not claim it is never written to: those bytes
 * are the game's. core1 may swap between the loads below, which is harmless:
 * the table read first is written either way, and the flag is read after it,
 * as fuji_cart_swap() publishes them in the opposite order. */
void fuji_cart_poke(unsigned prog_addr, uint8_t value)
{
    unsigned char (*running)[4096] = fuji_rom;
    unsigned char (*staged)[4096] = fuji_stage_rom;
    unsigned bank;

    if (!fuji_mailbox_active)
        return;                 /* the running game owns this page */
    for (bank = 0; bank < 8; bank++)
        running[bank][prog_addr & 0xFFF] = value;
    if (staged != NULL && staged != running && fuji_stage_mailbox)
        for (bank = 0; bank < 8; bank++)
            staged[bank][prog_addr & 0xFFF] = value;
}

unsigned char (*fuji_cart_stage_table(void))[4096]
{
    /* Armed already means the other table holds an image the console has not
     * taken yet; a newer push replaces it there. */
    if (fuji_boot_armed && fuji_stage_rom != NULL)
        return fuji_stage_rom;
    return fuji_rom == rom_table ? new_rom_table : rom_table;
}

void fuji_cart_stage_boot(unsigned char (*table)[4096], bool claims)
{
    unsigned char (*running)[4096] = fuji_rom;
    unsigned bank;

    if (claims && table != running) {
        /* A fresh page that carries over the status bytes: the console has
         * not seen the end of the MOUNT_IMAGE that staged this, and the new
         * image's first sequence number comes from ACKSEQ. */
        for (bank = 0; bank < 8; bank++) {
            memset(&table[bank][FN_R_ACKSEQ], 0, 0x1000 - FN_R_ACKSEQ);
            memcpy(&table[bank][FN_R_ACKSEQ], &running[0][FN_R_ACKSEQ],
                   FN_R_DATA - FN_R_ACKSEQ);
        }
    }
    fuji_stage_rom = table;
    fuji_stage_mailbox = claims;
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
    fuji_stage_rom = NULL;
    fuji_stage_mailbox = false;
    fuji_cart_paint();
}

#include <string.h>

#include "fuji_cart.h"
#include "fuji_mailbox.h"

fuji_ring_t fuji_ring;
volatile bool fuji_mailbox_active = true;

uint8_t fuji_window[0x2000];
uint8_t fuji_staged[0x2000];
uint8_t *volatile fuji_rom = fuji_window;
volatile bool fuji_boot_armed = false;
volatile bool fuji_staged_claims = false;

static bool have_staged;

bool fuji_cart_next_read(uint16_t *offset)
{
    uint16_t tail = fuji_ring.tail;

    if (tail == fuji_ring.head)
        return false;

    *offset = fuji_ring.buf[tail];
    fuji_ring.tail = (uint16_t)((tail + 1u) % FUJI_RING_LEN);
    return true;
}

/* Publish one mailbox byte. core0 cannot read the fuji_rom pointer safely
 * (core1 writes it, and nothing orders the load), so it publishes into both
 * windows: whichever core1 is serving gets the byte, and the other is
 * either dead memory or a staged image that wants the same publishes. A
 * staged image that does NOT claim the mailbox keeps its bytes pristine --
 * and the mailbox stays live on the current window until the swap actually
 * happens, so the client still sees BOOT_READY. (Deactivating at stage
 * time, as the o2 firmware port does, drops that publish and strands the
 * client at the progress screen.) */
void fuji_cart_poke(unsigned offset, uint8_t value)
{
    if (!fuji_mailbox_active)
        return;                 /* a booted game owns these pages now */
    fuji_window[offset & 0x1FFF] = value;
    if (have_staged && fuji_staged_claims)
        fuji_staged[offset & 0x1FFF] = value;
}

/* Take a committed image, already laid out as a full window by astromap.
 * Arming is separate and client-driven (FN_REG_BOOTLOCK): a stray can never
 * swap what was never armed, and nothing is overwritten under the running
 * client either way. */
void fuji_cart_stage(const uint8_t window[0x2000], bool claims)
{
    memcpy(fuji_staged, window, sizeof fuji_staged);
    fuji_staged_claims = claims;
    have_staged = true;
}

void fuji_cart_init(void)
{
    memset((void *)&fuji_ring, 0, sizeof fuji_ring);
    fuji_mailbox_active = true;
    fuji_boot_armed = false;
    fuji_staged_claims = false;
    have_staged = false;
    fuji_rom = fuji_window;
}

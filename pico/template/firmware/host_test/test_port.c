/* test_port.c -- the cart's fuji_port_ops_t, driven as fujimail drives it. */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "fujinet.h"
#include "fujimail.h"
#include "fuji_port.h"
#include "fuji_cart.h"
#include "fuji_mailbox.h"

/* ---- stand-ins for pico/common and core1's side ---- */

static const fuji_port_ops_t *ops;
static tmplmap_plan_t staged;
static int stages;
static uint8_t staged_first;

fuji_ring_t fuji_ring;
volatile bool fuji_boot_armed;

void fuji_port_init(const fuji_port_ops_t *o) { ops = o; }
void fujimail_read_hotspot(uint16_t offset) { (void)offset; }
bool fuji_cart_next_read(uint16_t *offset) { (void)offset; return false; }
void fuji_cart_poke(unsigned offset, uint8_t value) { (void)offset; (void)value; }

void fuji_cart_stage(const uint8_t *image, const tmplmap_plan_t *plan)
{
    staged = *plan;
    staged_first = image[0];
    stages++;
}

/* One push, in the 512-byte chunks the ESP32 sends; got is fujimail's count. */
static uint8_t push(uint32_t open_size, uint32_t len)
{
    static uint8_t chunk[512];
    uint32_t off, got = 0;

    memset(chunk, 0x5A, sizeof chunk);
    assert(ops->rom_open(open_size) == 0);
    for (off = 0; off < len; off += sizeof chunk) {
        unsigned n = len - off < sizeof chunk ? len - off : sizeof chunk;

        ops->rom_write(chunk, n);
        got += n;
    }
    return ops->rom_close(got, false, NULL);
}

int main(void)
{
    fuji_service_init();
    assert(ops && ops->poke == fuji_cart_poke && ops->own_reg == NULL);

    assert(push(1000, 1000) == 0);
    assert(stages == 1 && staged.size == 1000 && staged_first == 0x5A);

    assert(ops->rom_open(TMPLMAP_WINDOW + 1) == FN_BOOT_ERR_TOOBIG);   /* refused at open */

    /* an OPEN with no size: the gate can't refuse it, so close must */
    assert(push(0, TMPLMAP_WINDOW + 100) == FN_BOOT_ERR_TOOBIG);
    assert(stages == 1);
    assert(push(0, TMPLMAP_WINDOW) == 0);
    assert(stages == 2 && staged.size == TMPLMAP_WINDOW);

    assert(ops->rom_open(100) == 0);
    assert(ops->rom_close(0, false, NULL) == FN_BOOT_ERR_TRUNCATED);    /* nothing came */
    assert(ops->rom_open(100) == 0);
    assert(ops->rom_close(50, true, NULL) == 0 && stages == 2);         /* aborted */

    ops->arm_swap();
    assert(fuji_boot_armed);

    printf("test_port: all passed\n");
    return 0;
}

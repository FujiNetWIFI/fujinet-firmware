/* test_swap.c -- a swapped-in client's mailbox. core1's swap lines are copied here, since
 * coleco_cart.c needs the pico-sdk; the rest is the firmware's own code.
 */

#include <stdio.h>
#include <string.h>

#include "colmap.h"
#include "fuji_cart.h"
#include "fuji_mailbox.h"
#include "fuji_port.h"
#include "fujimail.h"
#include "fuji_store.h"
#include "fujinet.h"

static int failures;

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        if (!(cond)) {                                                        \
            printf("FAIL %s:%d: ", __func__, __LINE__);                       \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
            failures++;                                                       \
        }                                                                     \
    } while (0)

static uint8_t client[COLMAP_WINDOW];

/* The bus and the store are not under test; fujimail needs a poke and a link_up. */
static bool link_down(void)
{
    return false;
}

static const fujimail_port_t test_port = { .poke = fuji_cart_poke, .link_up = link_down };

void fuji_port_init(const fuji_port_ops_t *ops)
{
    (void)ops;
    fujimail_init(&test_port);
}

uint8_t fuji_store_open(uint32_t size)
{
    (void)size;
    return 0;
}

void fuji_store_write(const uint8_t *chunk, unsigned len)
{
    (void)chunk;
    (void)len;
}

const uint8_t *fuji_store_close(bool aborted, uint32_t *kept)
{
    (void)aborted;
    *kept = 0;
    return NULL;
}

static void core1_swap(void)
{
    fuji_live = fuji_next;
    if (fuji_live.base == fuji_win[fuji_live_win ^ 1u])
        fuji_live_win ^= 1u;
    fuji_boot_armed = false;
    fuji_have_staged = false;
    fuji_mailbox_active = fuji_staged_claims;
    if (fuji_mailbox_active)
        fuji_cart_note_read(FUJI_RING_SWAP);
}

static void test_swap_paints_mailbox(void)
{
    colmap_plan_t plan;
    const uint8_t *live;

    fuji_cart_init();
    fuji_service_init();
    memset(client, 0xFF, sizeof client);
    client[0] = 0xAA;
    client[1] = 0x55;
    CHECK(colmap_plan(client, sizeof client, COLMAP_KIND_AUTO, &plan) == COLMAP_OK,
          "plan");
    plan.mailbox_ok = true;
    fuji_cart_stage(client, &plan);
    fuji_boot_armed = true;

    core1_swap();
    fuji_mailbox_service();

    live = fuji_win[fuji_live_win];
    CHECK(live[FN_R_MAGIC0] == 'F' && live[FN_R_MAGIC1] == 'N',
          "magic %02X %02X", live[FN_R_MAGIC0], live[FN_R_MAGIC1]);
    CHECK(live[FN_R_PROTO_VER] == FN_PROTO_VER, "proto %02X", live[FN_R_PROTO_VER]);
    CHECK(live[FN_R_ACKSEQ] == 0, "ackseq %02X", live[FN_R_ACKSEQ]);
}

static void test_unclaimed_swap_leaves_ring_empty(void)
{
    colmap_plan_t plan;
    uint16_t offset;

    fuji_cart_init();
    fuji_service_init();
    memset(client, 0xFF, sizeof client);
    CHECK(colmap_plan(client, sizeof client, COLMAP_KIND_AUTO, &plan) == COLMAP_OK,
          "plan");
    plan.mailbox_ok = false;
    fuji_cart_stage(client, &plan);
    fuji_boot_armed = true;

    core1_swap();
    CHECK(!fuji_cart_next_read(&offset), "a non-claiming client got a swap mark");
}

int main(void)
{
    test_swap_paints_mailbox();
    test_unclaimed_swap_leaves_ring_empty();
    if (failures != 0) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}

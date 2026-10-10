/* fuji_audio.c -- POKEY sound into a PWM; see fuji_audio.h.
 *
 * Two DMA channels take turns feeding the PWM's compare register at 31.4 kHz
 * (the POKEY clock / 57), paced by a DMA timer; core0 renders the half that
 * is not playing. The PWM itself runs at 833 kHz, far above the board's
 * 10.6 kHz RC corner, so the filter removes the carrier. Register writes
 * apply at the synth's current time, so their timing is good to one buffer
 * (8 ms), which is what a music driver running once a frame needs.
 *
 * PROVISIONAL until a scope says otherwise: the output level against the
 * TIA's on the edge's audio input, hence the full-scale constant below.
 */

#include <string.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pwm.h"

#include "a78_cart.h"
#include "fuji_audio.h"
#include "fuji_cart.h"
#include "pokey.h"

#define CLOCKS_PER_SAMPLE 57u
#define HALF              256u
#define SAMPLE_DIV        (200000000u / (1789773u / CLOCKS_PER_SAMPLE))
#define FULL_SCALE        60u              /* four channels at volume 15 */
#define PWM_TOP           (FULL_SCALE * 4u - 1u)

static pokey_t pk;
static uint16_t buf[2][HALF] __attribute__((aligned(4)));
static int dma[2];
static int timer;
static unsigned slice;

static void render(unsigned half)
{
    static uint8_t lv[HALF];
    unsigned i;

    pokey_render(&pk, lv, HALF, CLOCKS_PER_SAMPLE);
    for (i = 0; i < HALF; i++)
        buf[half][i] = (uint16_t)(lv[i] * 4u);
}

void fuji_audio_init(void)
{
    pwm_config pc = pwm_get_default_config();
    unsigned i;

    pokey_init(&pk);
    slice = pwm_gpio_to_slice_num(AUDIO_PIN);
    pwm_config_set_wrap(&pc, PWM_TOP);
    pwm_init(slice, &pc, true);
    gpio_set_function(AUDIO_PIN, GPIO_FUNC_PWM);

    memset(buf, 0, sizeof buf);
    timer = dma_claim_unused_timer(true);
    dma_timer_set_fraction((unsigned)timer, 1, SAMPLE_DIV);
    dma[0] = dma_claim_unused_channel(true);
    dma[1] = dma_claim_unused_channel(true);
    for (i = 0; i < 2; i++) {
        dma_channel_config c = dma_channel_get_default_config((unsigned)dma[i]);

        channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);
        channel_config_set_dreq(&c, dma_get_timer_dreq((unsigned)timer));
        channel_config_set_chain_to(&c, (unsigned)dma[i ^ 1]);
        /* channel B's half of the compare register */
        dma_channel_configure((unsigned)dma[i], &c,
                              (uint16_t *)&pwm_hw->slice[slice].cc + 1,
                              buf[i], HALF, false);
    }
    dma_channel_start((unsigned)dma[0]);
}

void fuji_audio_write(unsigned reg, uint8_t v, uint32_t cycle)
{
    (void)cycle;
    pokey_write(&pk, reg, v, pk.t);
}

void fuji_audio_reset(void)
{
    pokey_init(&pk);
}

void fuji_audio_service(void)
{
    static bool played[2];
    unsigned i;

    /* a channel seen playing and now idle has finished: refill and re-arm
     * it, and the chain from the other starts it again */
    for (i = 0; i < 2; i++) {
        unsigned ch = (unsigned)dma[i];

        if (dma_channel_is_busy(ch)) {
            played[i] = true;
        } else if (played[i]) {
            played[i] = false;
            render(i);
            dma_channel_set_read_addr(ch, buf[i], false);
            dma_channel_set_trans_count(ch, HALF, false);
        }
    }
    /* the bus clock keeps RANDOM moving between renders; its offset from the
     * synth's clock only shifts which poly bits are read */
    fuji_pokey_rd[POKEY_RANDOM] = pokey_read(&pk, POKEY_RANDOM, fuji_bus_cycle);
    fuji_pokey_rd[POKEY_KBCODE] = 0xFF;
    fuji_pokey_rd[POKEY_IRQST] = 0xFF;
    fuji_pokey_rd[POKEY_SKSTAT] = 0xFF;
}

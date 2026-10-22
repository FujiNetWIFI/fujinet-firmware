/* test_fuji_port.c -- what fuji_port hands fujimail, against stand-ins for the USB link. */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "fuji_port.h"
#include "fujimail.h"
#include "fujibus_usb.h"
#include "fuji_mailbox_proto.h"

/* ---- stand-ins for fujimail and the USB link ---- */

static const fujimail_port_t *port;
static fujibus_inbound_fn inbound;
static int bootsels;

void fujimail_init(const fujimail_port_t *p) { port = p; }
bool fujimail_inbound(const fb_reply_t *frame) { (void)frame; return false; }
void fujibus_set_inbound_handler(fujibus_inbound_fn h) { inbound = h; }
bool fujibus_link_up(void) { return true; }
void fuji_wait_ms_pumped(uint32_t ms) { (void)ms; }

fb_status_t fujibus_transact(uint8_t device, uint8_t command, const fb_param_t *params,
                             unsigned nparams, const uint8_t *payload, uint16_t payload_len,
                             uint32_t timeout_ms, fb_reply_t *reply)
{
    (void)device; (void)command; (void)params; (void)nparams; (void)payload;
    (void)payload_len; (void)timeout_ms; (void)reply;
    return FB_OK;
}

void fujibus_send_bare(uint8_t device, uint8_t command, const uint8_t *payload,
                       uint16_t payload_len)
{
    (void)device; (void)command; (void)payload; (void)payload_len;
}

void reset_usb_boot(uint32_t gpio_activity_pin_mask, uint32_t disable_interface_mask)
{
    assert(gpio_activity_pin_mask == 0 && disable_interface_mask == 0);
    bootsels++;
}

/* ---- the cart ---- */

static uint32_t opened, wrote, closed_got;
static bool closed_aborted;
static char closed_mapper[32];
static int closes, arms, regs;
static uint8_t refuse;

static void poke(unsigned offset, uint8_t value) { (void)offset; (void)value; }

static uint8_t rom_open(uint32_t size)
{
    opened = size;
    return refuse;
}

static void rom_write(const uint8_t *chunk, unsigned len)
{
    (void)chunk;
    wrote += len;
}

static uint8_t rom_close(uint32_t got, bool aborted, const char *mapper)
{
    closes++;
    closed_got = got;
    closed_aborted = aborted;
    strcpy(closed_mapper, mapper ? mapper : "(none)");
    return 7;
}

static void arm_swap(void) { arms++; }
static void own_reg(uint8_t reg, uint8_t value) { (void)value; regs += reg; }

static const fuji_port_ops_t ops = {
    .poke      = poke,
    .rom_open  = rom_open,
    .rom_write = rom_write,
    .rom_close = rom_close,
    .arm_swap  = arm_swap,
    .own_reg   = own_reg,
};

static void push_cfg(const char *text, bool aborted)
{
    assert(port->stream_open(FN_STREAM_CFG, (uint32_t)strlen(text)) == 0);
    port->stream_write(FN_STREAM_CFG, (const uint8_t *)text, (unsigned)strlen(text));
    assert(port->stream_close(FN_STREAM_CFG, (uint32_t)strlen(text), aborted) == 0);
}

static void push_rom(bool aborted)
{
    static const uint8_t img[100];

    assert(port->stream_open(FN_STREAM_ROM, sizeof img) == 0);
    port->stream_write(FN_STREAM_ROM, img, sizeof img);
    assert(port->stream_close(FN_STREAM_ROM, sizeof img, aborted) == 7);
}

int main(void)
{
    fuji_port_init(&ops);
    assert(port && inbound == fujimail_inbound);
    assert(port->poke == poke && port->arm_swap == arm_swap && port->own_reg == own_reg);
    assert(port->link_up == fujibus_link_up && port->transact == fujibus_transact);
    assert(port->send_bare == fujibus_send_bare && port->wait_link_ms == fuji_wait_ms_pumped);
    assert(port->on_txn == NULL && port->on_dbc == NULL);
    port->bootsel();
    assert(bootsels == 1);

    /* a .cfg never reaches the cart; its mapper rides on the next image only */
    push_cfg("name=x\nmapper=m1\n", false);
    assert(opened == 0 && wrote == 0 && closes == 0);
    push_rom(false);
    assert(opened == 100 && wrote == 100 && closed_got == 100 && !closed_aborted);
    assert(strcmp(closed_mapper, "m1") == 0);
    push_rom(false);
    assert(strcmp(closed_mapper, "(none)") == 0);

    /* an aborted image still closes, and still uses up the mapper */
    push_cfg("mapper=m2\n", false);
    push_rom(true);
    assert(closed_aborted && strcmp(closed_mapper, "m2") == 0);
    push_rom(false);
    assert(strcmp(closed_mapper, "(none)") == 0);

    /* an aborted .cfg names nothing */
    push_cfg("mapper=m3\n", true);
    push_rom(false);
    assert(strcmp(closed_mapper, "(none)") == 0);

    /* the cart's refusal is fujimail's */
    refuse = 0x42;
    assert(port->stream_open(FN_STREAM_ROM, 5) == 0x42);
    refuse = 0;

    port->arm_swap();
    port->own_reg(0x14, 1);
    assert(arms == 1 && regs == 0x14);

    printf("test_fuji_port: all passed\n");
    return 0;
}

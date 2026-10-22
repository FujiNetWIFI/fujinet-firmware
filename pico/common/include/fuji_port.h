/* fuji_port.h -- fujimail on a cartridge. The USB link to the ESP32-S3, BOOTSEL and the
 * .cfg sibling are bound here, so a cart supplies only what its hardware does with the
 * mailbox and a pushed image. Core0 only.
 */

#ifndef FUJI_PORT_H
#define FUJI_PORT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    /* Publish one mailbox byte into the served window. */
    void (*poke)(unsigned offset, uint8_t value);

    /* The image's push stream, as fujimail_port_t's stream_* for FN_STREAM_ROM;
     * rom_close also gets the mapper the .cfg sibling named, or NULL. */
    uint8_t (*rom_open)(uint32_t size);
    void (*rom_write)(const uint8_t *chunk, unsigned len);
    uint8_t (*rom_close)(uint32_t got, bool aborted, const char *mapper);

    void (*arm_swap)(void);

    /* Optional: a write to one of the platform's registers (FN_REG_PLATFORM up). */
    void (*own_reg)(uint8_t reg, uint8_t value);
} fuji_port_ops_t;

/* Starts fujimail on the USB link and hands it the link's inbound frames. */
void fuji_port_init(const fuji_port_ops_t *ops);

#endif /* FUJI_PORT_H */

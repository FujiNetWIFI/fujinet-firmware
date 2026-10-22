/* fuji_cfg.h -- the .cfg sibling a DBC push sends before an image (FN_STREAM_CFG). Only a
 * line starting `mapper=` matters: it names the mapper for the next image. The MAME devices
 * use this too, so the emulator reads a .cfg exactly as the cart does.
 */

#ifndef FUJI_CFG_H
#define FUJI_CFG_H

#include <stdbool.h>
#include <stdint.h>

#define FUJI_CFG_TEXT_MAX   256
#define FUJI_CFG_MAPPER_MAX 24

typedef struct {
    char text[FUJI_CFG_TEXT_MAX];
    unsigned len;
    char mapper[FUJI_CFG_MAPPER_MAX];
} fuji_cfg_t;

void fuji_cfg_open(fuji_cfg_t *cfg);
void fuji_cfg_write(fuji_cfg_t *cfg, const uint8_t *chunk, unsigned len);

/* Reads the mapper= line; an aborted stream leaves no mapper. */
void fuji_cfg_close(fuji_cfg_t *cfg, bool aborted);

/* The mapper the next image should use, or NULL. */
const char *fuji_cfg_mapper(const fuji_cfg_t *cfg);

/* Forgets it once the image it was for has been planned. */
void fuji_cfg_clear(fuji_cfg_t *cfg);

#endif /* FUJI_CFG_H */

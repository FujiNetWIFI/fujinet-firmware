/* fuji_cfg.c -- the .cfg sibling's mapper= line; see fuji_cfg.h. */

#include <string.h>

#include "fuji_cfg.h"

void fuji_cfg_open(fuji_cfg_t *cfg)
{
    cfg->len = 0;
    cfg->text[0] = '\0';
}

void fuji_cfg_write(fuji_cfg_t *cfg, const uint8_t *chunk, unsigned len)
{
    if (cfg->len + len >= sizeof cfg->text)
        len = sizeof cfg->text - 1 - cfg->len;
    memcpy(cfg->text + cfg->len, chunk, len);
    cfg->len += len;
    cfg->text[cfg->len] = '\0';
}

void fuji_cfg_close(fuji_cfg_t *cfg, bool aborted)
{
    const char *p = cfg->text;

    cfg->mapper[0] = '\0';
    if (aborted)
        return;
    while (p && *p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
            p++;
        if (strncmp(p, "mapper=", 7) == 0) {
            unsigned n = 0;

            p += 7;
            while (*p && *p != '\r' && *p != '\n' && *p != ' ' && n < sizeof cfg->mapper - 1)
                cfg->mapper[n++] = *p++;
            cfg->mapper[n] = '\0';
            return;
        }
        p = strchr(p, '\n');
    }
}

const char *fuji_cfg_mapper(const fuji_cfg_t *cfg)
{
    return cfg->mapper[0] ? cfg->mapper : NULL;
}

void fuji_cfg_clear(fuji_cfg_t *cfg)
{
    cfg->mapper[0] = '\0';
}

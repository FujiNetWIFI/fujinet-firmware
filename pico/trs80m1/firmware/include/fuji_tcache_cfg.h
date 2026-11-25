/* fuji_tcache_cfg.h -- the Model I card's track cache: four drives of single-density
 * tracks, up to 18 256-byte sectors each, and the write protect from DISK_STATUS. */
#ifndef FUJI_TCACHE_CFG_H
#define FUJI_TCACHE_CFG_H

#define FUJI_TC_DRIVES  4
#define FUJI_TC_SECTOR  256
#define FUJI_TC_SPT_MAX 18
#ifndef FUJI_TC_SLOTS
#define FUJI_TC_SLOTS 32
#endif
#define FUJI_TC_PROBE_STATUS 1

#endif /* FUJI_TCACHE_CFG_H */

/* fuji_tcache_cfg.h -- the FDC+'s track cache: eight drives of up to 32 Altair
 * 137-byte sectors a track. */
#ifndef FUJI_TCACHE_CFG_H
#define FUJI_TCACHE_CFG_H

#define FUJI_TC_DRIVES  8
#define FUJI_TC_SECTOR  137
#define FUJI_TC_SPT_MAX 32
#ifndef FUJI_TC_SLOTS
#define FUJI_TC_SLOTS 24
#endif

#endif /* FUJI_TCACHE_CFG_H */

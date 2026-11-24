/* fuji_tcache_cfg.h -- the cache test_tcache.c and test_iolink.c run: four drives of up
 * to 18 256-byte sectors a track, in six slots. */
#ifndef FUJI_TCACHE_CFG_H
#define FUJI_TCACHE_CFG_H

#define FUJI_TC_DRIVES  4
#define FUJI_TC_SECTOR  256
#define FUJI_TC_SPT_MAX 18
#define FUJI_TC_SLOTS   6

#endif /* FUJI_TCACHE_CFG_H */

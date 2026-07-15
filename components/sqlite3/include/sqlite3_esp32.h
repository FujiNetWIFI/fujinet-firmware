#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/*
 * ESP32-specific SQLite helpers.
 *
 * With SQLITE_OMIT_AUTOINIT=1 the caller is responsible for initialization.
 * Call sqlite3_esp32_init() once before any sqlite3_open().
 *
 * PSRAM allocator swap:
 *   FTS5 builds accumulate a large token hash made of many small (<512-byte)
 *   allocations that fall below CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=512 and
 *   land in internal DRAM, exhausting the 125-byte DMA slots the SDMMC driver
 *   needs for every block transfer.  sqlite3_esp32_psram_malloc_enter() swaps
 *   the global SQLite allocator to heap_caps PSRAM for the duration; call
 *   sqlite3_esp32_psram_malloc_exit() to restore the system allocator.
 *   No DB connections may be open when either function is called.
 */

/* Initialize SQLite: configure PSRAM page cache and call sqlite3_initialize().
 * Returns 1 if the page cache is in PSRAM, 0 if DRAM fallback (no PSRAM). */
int  sqlite3_esp32_init(void);

/* Swap the global SQLite allocator to PSRAM (heap_caps).
 * Internally calls sqlite3_shutdown() / sqlite3_initialize(). */
void sqlite3_esp32_psram_malloc_enter(void);

/* Restore the system allocator saved during sqlite3_esp32_init(). */
void sqlite3_esp32_psram_malloc_exit(void);

#ifdef __cplusplus
}
#endif

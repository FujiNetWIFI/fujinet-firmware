/*
//   Minty - MultiCart for Mattel Intellivision by Gennaro Tortone 2025
//
//   based on PiRTO II Flash MultiCART by Andrea Ottaviani 2024
//   parts of code are directly from the A8PicoCart project by Robin Edwards 2023
*/

#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#include <stdio.h>
#include <string.h>
#include <stddef.h>

#include "flash_fs.h"

// Implements 512 byte FAT sectors on 4096 byte flash sectors.
// Doesn't really implement wear levelling so not for heavy use but should be
// fine for the intended use case.
//
// The FAT sector -> flash sector map is NOT held in RAM: it lives in the first
// FS_MAP_ENTRIES flash sectors and is read directly through the XIP window.
//
// Power loss resistance is built on two mechanisms:
//
//  - a write ahead log: every map update is appended to a dedicated flash
//    sector before it is considered committed. The RAM journal is just the
//    in-memory image of that log, rebuilt by replaying it at mount time. The
//    map sectors themselves are rewritten only when the log fills up
//    (checkpoint), not on every sync.
//
//  - a shadow sector: every destructive operation (rewriting a map page,
//    erasing a data sector that still holds live FAT sectors) first stores its
//    result in a shadow sector and records an intent. If power is lost half
//    way through, the intent is found at mount time and the operation is
//    simply replayed. Once completed, a DONE record retires the intent.
//
// On-flash layout of the storage area, in 4096 byte sectors:
//
//   0 .. FS_MAP_ENTRIES-1            sector map
//   FS_SHADOW_DATA_SECTOR            shadow content (4096 bytes)
//   FS_SHADOW_HDR_SECTOR             shadow intent records (16 slots)
//   FS_LOG_FIRST_SECTOR .. +N-1      write ahead log
//   FS_RESERVED_SECTORS ..           data

// bumped: the on-flash layout now reserves extra sectors, an old filesystem
// must be recreated
#define MAGIC_8_BYTES "MNTYFS40"

// the storage geometry (FS_MAP_ENTRIES, FS_LOG_SECTORS, the sector indices of
// the shadow and log areas, NUM_FAT_SECTORS) is derived in flash_fs.h
_Static_assert(FLASH_SECTOR_SIZE == FS_FLASH_SECTOR_SIZE,
               "flash_fs.h sector size does not match the SDK");
_Static_assert(FLASH_PAGE_SIZE == FS_FLASH_PAGE_SIZE,
               "flash_fs.h page size does not match the SDK");

#define FS_LOG_PAGES_PER_SECTOR (FLASH_SECTOR_SIZE / FLASH_PAGE_SIZE)
#define FS_LOG_RECS_PER_PAGE (FLASH_PAGE_SIZE / 8)
#define FS_LOG_PAGES (FS_LOG_SECTORS * FS_LOG_PAGES_PER_SECTOR)
#define FS_LOG_CAPACITY (FS_LOG_PAGES * FS_LOG_RECS_PER_PAGE)

// the RAM journal must hold every distinct entry the log can contain, so that
// replaying a completely full log at mount time can never overflow it
#define FS_MAP_JOURNAL_SIZE FS_LOG_CAPACITY

#define LOG_REC_MAGIC 0x4C52u   // 'LR'

typedef struct {
   uint16_t fat_sector;
   uint16_t map_entry;
   uint16_t magic;
   uint16_t crc;                // crc16 of the first 6 bytes
} log_record;

_Static_assert(sizeof(log_record) == 8, "unexpected log_record padding");

#define SHADOW_MAGIC 0x53484431u        // 'SHD1'
#define SHADOW_TYPE_MAP_PAGE 1u
#define SHADOW_TYPE_DATA_COPY 2u
#define SHADOW_TYPE_DONE 3u
#define SHADOW_HDR_SLOTS (FLASH_SECTOR_SIZE / FLASH_PAGE_SIZE)

typedef struct {
   uint32_t magic;
   uint32_t type;
   uint32_t target_sector;
   uint32_t preserve_bitmap;
   uint32_t content_crc;        // crc32 of the 4096 bytes in the shadow sector
   uint32_t hdr_crc;            // crc32 of everything above
} shadow_hdr;

typedef struct {
   uint16_t fat_sector;
   uint16_t map_entry;
} map_update;

// journal of map updates present in the log but not yet folded into the map,
// kept sorted by fat_sector so lookups are O(log n) and updates belonging to
// the same map page are contiguous
static map_update map_journal[FS_MAP_JOURNAL_SIZE];
static uint16_t map_journal_count = 0;

// staging buffer for the log page being filled, and the page it will land on
static uint8_t log_page_buf[FLASH_PAGE_SIZE];
static uint16_t log_page_used = 0;
static uint16_t log_next_page = 0;

// nesting depth of a filesystem mutation; the autosync timer skips its tick
// while this is non zero. Only ever touched from thread context, so a plain
// counter is enough: the timer callback runs while that code is suspended and
// therefore observes either 0 (not yet inside) or non zero (inside).
static volatile uint8_t fs_busy = 0;

static repeating_timer_t sync_timer;
static bool sync_timer_running = false;

static inline void fs_enter(void) {
   fs_busy++;
}

static inline void fs_exit(void) {
   fs_busy--;
}

static uint16_t shadow_slot_next = 0;

// shared 4K scratch buffer, used by the checkpoint and by
// flash_erase_with_copy_sector(); those two never run nested, so a single
// buffer is enough and keeps it off the stack
static uint8_t sector_buf[FLASH_SECTOR_SIZE];

// one bit per 512 byte slot, one byte per 4096 byte flash sector
uint8_t used_bitmap[NUM_FLASH_SECTORS];

uint16_t write_sector = 0;      // which flash sector we are writing to
uint8_t write_sector_bitmap = 0;        // 1 for each free 512 byte page on the sector

// each sector entry in the sector map is:
//  13 bits of sector (indexing 8192 4k flash sectors)
//   3 bits of offset (0->7 512 byte FAT sectors in each 4k flash sector)
uint16_t getMapSector(uint16_t mapEntry) {
   return (mapEntry & 0xFFF8) >> 3;
}
uint8_t getMapOffset(uint16_t mapEntry) {
   return mapEntry & 0x7;
}
uint16_t makeMapEntry(uint16_t sector, uint8_t offset) {
   return (sector << 3) | offset;
};

// forward declns
void flash_read_sector(uint16_t sector, uint8_t offset, void *buffer, uint16_t size);
void flash_erase_sector(uint16_t sector);
void flash_write_sector(uint16_t sector, uint8_t offset, const void *buffer, uint16_t size);
void flash_erase_with_copy_sector(uint16_t sector, uint8_t preserve_bitmap);
static void flash_write_page(uint16_t sector, uint16_t page, const void *buffer);
int64_t write_fs_map(void);

/* Helpers */

// direct pointer into the memory mapped flash; reads never need a buffer
static inline const uint8_t *fs_xip_ptr(uint16_t sector, uint32_t offset) {
   return (const uint8_t *) (XIP_BASE + HW_FLASH_STORAGE_BASE +
                             (uint32_t) sector * FLASH_SECTOR_SIZE + offset);
}

static uint32_t crc32_calc(const void *data, size_t len) {
   const uint8_t *p = (const uint8_t *) data;
   uint32_t crc = 0xFFFFFFFF;

   while (len--) {
      crc ^= *p++;
      for (int k = 0; k < 8; k++)
         crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t) (-(int32_t) (crc & 1)));
   }
   return ~crc;
}

static inline uint16_t crc16_calc(const void *data, size_t len) {
   return (uint16_t) crc32_calc(data, len);
}

/* Shadow sector: makes a destructive operation restartable */

static bool shadow_hdr_valid(const shadow_hdr *h) {
   if (h->magic != SHADOW_MAGIC)
      return false;
   return h->hdr_crc == crc32_calc(h, offsetof(shadow_hdr, hdr_crc));
}

static const shadow_hdr *shadow_hdr_slot(uint16_t slot) {
   return (const shadow_hdr *) fs_xip_ptr(FS_SHADOW_HDR_SECTOR, (uint32_t) slot * FLASH_PAGE_SIZE);
}

static void shadow_hdr_write(uint32_t type, uint32_t target, uint32_t preserve, uint32_t content_crc) {
   uint8_t page[FLASH_PAGE_SIZE];
   shadow_hdr *h = (shadow_hdr *) page;

   // an erased header sector means "nothing pending", which is exactly what a
   // DONE record encodes, so recycling here is always safe
   if (shadow_slot_next >= SHADOW_HDR_SLOTS) {
      flash_erase_sector(FS_SHADOW_HDR_SECTOR);
      shadow_slot_next = 0;
      if (type == SHADOW_TYPE_DONE)
         return;
   }

   memset(page, 0xFF, sizeof(page));
   h->magic = SHADOW_MAGIC;
   h->type = type;
   h->target_sector = target;
   h->preserve_bitmap = preserve;
   h->content_crc = content_crc;
   h->hdr_crc = crc32_calc(h, offsetof(shadow_hdr, hdr_crc));

   flash_write_page(FS_SHADOW_HDR_SECTOR, shadow_slot_next, page);
   shadow_slot_next++;
}

// stage the result of a destructive operation and record the intent; from this
// point on the operation can be replayed after a power loss
static void shadow_begin(uint32_t type, uint16_t target, uint8_t preserve, const uint8_t *content) {
   // two slots are needed: the intent and the matching DONE record
   if (shadow_slot_next + 2 > SHADOW_HDR_SLOTS) {
      flash_erase_sector(FS_SHADOW_HDR_SECTOR);
      shadow_slot_next = 0;
   }

   flash_erase_sector(FS_SHADOW_DATA_SECTOR);
   flash_write_sector(FS_SHADOW_DATA_SECTOR, 0, content, FLASH_SECTOR_SIZE);

   // the intent is written last, so a valid intent implies valid content
   shadow_hdr_write(type, target, preserve, crc32_calc(content, FLASH_SECTOR_SIZE));
}

static void shadow_end(void) {
   shadow_hdr_write(SHADOW_TYPE_DONE, 0, 0, 0);
}

// rewrite the preserved 512 byte slots of a data sector from a 4K image
static void data_copy_apply(uint16_t sector, uint8_t preserve_bitmap, const uint8_t *content) {
   flash_erase_sector(sector);
   for (int i = 0; i < 8; i++) {
      if (preserve_bitmap & (1 << i))
         flash_write_sector(sector, i, content + (i * 512), 512);
   }
}

// replay an interrupted destructive operation, if any; must run before
// anything else reads the map
static void shadow_recover(void) {
   const shadow_hdr *found = NULL;
   shadow_hdr intent;
   uint16_t slot;

   shadow_slot_next = 0;
   for (slot = 0; slot < SHADOW_HDR_SLOTS; slot++) {
      const shadow_hdr *h = shadow_hdr_slot(slot);

      if (!shadow_hdr_valid(h))
         break;
      found = h;
      shadow_slot_next = (uint16_t) (slot + 1);
   }

   if (found == NULL)
      return;                   // nothing was ever staged

   // the intent lives in flash, take a RAM copy before touching the flash
   memcpy(&intent, found, sizeof(intent));

   if (intent.type == SHADOW_TYPE_DONE)
      return;                   // nothing was in flight

   if (crc32_calc(fs_xip_ptr(FS_SHADOW_DATA_SECTOR, 0), FLASH_SECTOR_SIZE) != intent.content_crc) {
      printf("shadow_recover() - shadow content is corrupt, intent dropped\n");
      shadow_end();
      return;
   }

   printf("shadow_recover() - replaying intent type %lu on sector %lu\n",
          (unsigned long) intent.type, (unsigned long) intent.target_sector);

   // the source of a program operation MUST live in RAM: the XIP window is
   // switched off while the flash is busy, so reading the shadow sector in
   // place would stall the bus
   memcpy(sector_buf, fs_xip_ptr(FS_SHADOW_DATA_SECTOR, 0), FLASH_SECTOR_SIZE);

   if (intent.type == SHADOW_TYPE_MAP_PAGE) {
      flash_erase_sector((uint16_t) intent.target_sector);
      flash_write_sector((uint16_t) intent.target_sector, 0, sector_buf, FLASH_SECTOR_SIZE);
   } else if (intent.type == SHADOW_TYPE_DATA_COPY) {
      data_copy_apply((uint16_t) intent.target_sector, (uint8_t) intent.preserve_bitmap, sector_buf);
   }

   shadow_end();
}

/* Sector map accessors */

// byte offset of a map entry from the beginning of the storage area
static inline uint32_t map_entry_offset(uint16_t fat_sector) {
   return FS_MAP_HEADER_SIZE + ((uint32_t) fat_sector * 2);
}

// flash sector (0 .. FS_MAP_ENTRIES-1) holding the entry of fat_sector
static inline uint16_t map_entry_page(uint16_t fat_sector) {
   return (uint16_t) (map_entry_offset(fat_sector) / FLASH_SECTOR_SIZE);
}

// read a map entry straight from flash; the offset is always even, so the
// 16 bit access is properly aligned
static inline uint16_t map_read_flash(uint16_t fat_sector) {
   const volatile uint16_t *p = (const volatile uint16_t *)
      (XIP_BASE + HW_FLASH_STORAGE_BASE + map_entry_offset(fat_sector));

   return *p;
}

// binary search in the journal; returns the index of fat_sector or -1, and
// stores the insertion point in *pos when pos is not NULL
static int map_journal_find(uint16_t fat_sector, uint16_t *pos) {
   uint16_t lo = 0, hi = map_journal_count;

   while (lo < hi) {
      uint16_t mid = (uint16_t) ((lo + hi) / 2);

      if (map_journal[mid].fat_sector < fat_sector)
         lo = (uint16_t) (mid + 1);
      else
         hi = mid;
   }

   if (pos)
      *pos = lo;

   if (lo < map_journal_count && map_journal[lo].fat_sector == fat_sector)
      return (int) lo;

   return -1;
}

// current value of a map entry: pending update if any, flash content otherwise
static uint16_t map_get_entry(uint16_t fat_sector) {
   int idx = map_journal_find(fat_sector, NULL);

   if (idx >= 0)
      return map_journal[idx].map_entry;

   return map_read_flash(fat_sector);
}

// record a pending value; the caller guarantees there is room
static void map_journal_set(uint16_t fat_sector, uint16_t map_entry) {
   uint16_t pos;
   int idx = map_journal_find(fat_sector, &pos);

   if (idx >= 0) {              // already pending: overwrite in place, no growth
      map_journal[idx].map_entry = map_entry;
      return;
   }

   memmove(&map_journal[pos + 1], &map_journal[pos],
           (size_t) (map_journal_count - pos) * sizeof(map_update));
   map_journal[pos].fat_sector = fat_sector;
   map_journal[pos].map_entry = map_entry;
   map_journal_count++;
}

/* Write ahead log */

static void log_record_build(log_record *r, uint16_t fat_sector, uint16_t map_entry) {
   r->fat_sector = fat_sector;
   r->map_entry = map_entry;
   r->magic = LOG_REC_MAGIC;
   r->crc = crc16_calc(r, offsetof(log_record, crc));
}

static bool log_record_valid(const log_record *r) {
   if (r->magic != LOG_REC_MAGIC)
      return false;
   return r->crc == crc16_calc(r, offsetof(log_record, crc));
}

static void log_page_reset(void) {
   memset(log_page_buf, 0xFF, sizeof(log_page_buf));
   log_page_used = 0;
}

// program the staged log page; unused record slots stay erased, but the page
// cannot be reused afterwards so we always move on to the next one
static int64_t log_flush_page(void) {
   uint16_t sector, page;

   if (log_page_used == 0)
      return 0;

   sector = (uint16_t) (FS_LOG_FIRST_SECTOR + log_next_page / FS_LOG_PAGES_PER_SECTOR);
   page = (uint16_t) (log_next_page % FS_LOG_PAGES_PER_SECTOR);
   flash_write_page(sector, page, log_page_buf);

   log_next_page++;
   log_page_reset();
   return FLASH_PAGE_SIZE;
}

static void log_erase(void) {
   for (int i = 0; i < FS_LOG_SECTORS; i++)
      flash_erase_sector((uint16_t) (FS_LOG_FIRST_SECTOR + i));
   log_next_page = 0;
   log_page_reset();
}

static bool log_is_full(void) {
   return log_next_page >= FS_LOG_PAGES;
}

static void log_append(uint16_t fat_sector, uint16_t map_entry) {
   log_record *r = &((log_record *) log_page_buf)[log_page_used];

   log_record_build(r, fat_sector, map_entry);
   log_page_used++;

   if (log_page_used == FS_LOG_RECS_PER_PAGE)
      log_flush_page();
}

// rebuild the journal and the allocation bitmap by replaying the log on top of
// the map; records are applied in order so intermediate allocations are freed
// instead of being leaked
static void log_replay(void) {
   uint16_t p, i;

   for (p = 0; p < FS_LOG_PAGES; p++) {
      uint16_t sector = (uint16_t) (FS_LOG_FIRST_SECTOR + p / FS_LOG_PAGES_PER_SECTOR);
      uint32_t offset = (uint32_t) (p % FS_LOG_PAGES_PER_SECTOR) * FLASH_PAGE_SIZE;
      const log_record *recs = (const log_record *) fs_xip_ptr(sector, offset);
      uint16_t valid = 0;

      for (i = 0; i < FS_LOG_RECS_PER_PAGE; i++) {
         uint16_t old_entry;

         // a sync flushes a partially filled page, so a hole ends this page
         // but not the log
         if (!log_record_valid(&recs[i]))
            break;

         old_entry = map_get_entry(recs[i].fat_sector);
         if (old_entry)
            used_bitmap[getMapSector(old_entry)] &= ~(1 << getMapOffset(old_entry));

         map_journal_set(recs[i].fat_sector, recs[i].map_entry);
         used_bitmap[getMapSector(recs[i].map_entry)] |= (1 << getMapOffset(recs[i].map_entry));
         valid++;
      }

      if (valid == 0) {         // blank page: this is the end of the log
         log_next_page = p;
         log_page_reset();
         return;
      }
      // pages are filled in order and cannot be programmed twice, so a
      // partially used page simply pushes the next write to the following one
   }

   log_next_page = FS_LOG_PAGES;
   log_page_reset();
}

void debug_print_in_use() {
   // just shows first 1meg
   printf("IN USE-----------------------------------\n");
   for (int i = 0; i < 16; i++) {
      printf("%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
         used_bitmap[i * 16 + 0], used_bitmap[i * 16 + 1], used_bitmap[i * 16 + 2], used_bitmap[i * 16 + 3],
         used_bitmap[i * 16 + 4], used_bitmap[i * 16 + 5], used_bitmap[i * 16 + 6], used_bitmap[i * 16 + 7],
         used_bitmap[i * 16 + 8], used_bitmap[i * 16 + 9], used_bitmap[i * 16 + 10], used_bitmap[i * 16 + 11],
         used_bitmap[i * 16 + 12], used_bitmap[i * 16 + 13], used_bitmap[i * 16 + 14], used_bitmap[i * 16 + 15]);
   }
   printf("END--------------------------------------\n");
}

// checkpoint: fold the journal into the map sectors, then drop the log.
// Every map page is rewritten through the shadow sector, so an interrupted
// checkpoint is completed on the next mount. The log is erased last: if we die
// half way, replaying it simply redoes the folds already applied.
int64_t write_fs_map() {
   uint64_t nb = 0;
   uint16_t i = 0;

   fs_enter();

   if (map_journal_count == 0) {
      int64_t r = log_flush_page();

      fs_exit();
      return r;
   }

   while (i < map_journal_count) {
      uint16_t page = map_entry_page(map_journal[i].fat_sector);
      uint32_t page_base = (uint32_t) page * FLASH_SECTOR_SIZE;

      flash_read_sector(page, 0, sector_buf, FLASH_SECTOR_SIZE);

      while (i < map_journal_count && map_entry_page(map_journal[i].fat_sector) == page) {
         uint32_t off = map_entry_offset(map_journal[i].fat_sector) - page_base;

         sector_buf[off] = (uint8_t) (map_journal[i].map_entry & 0xFF);
         sector_buf[off + 1] = (uint8_t) (map_journal[i].map_entry >> 8);
         i++;
      }

      shadow_begin(SHADOW_TYPE_MAP_PAGE, page, 0, sector_buf);
      flash_erase_sector(page);
      flash_write_sector(page, 0, sector_buf, FLASH_SECTOR_SIZE);
      shadow_end();

      nb += FLASH_SECTOR_SIZE;
   }

   log_erase();
   map_journal_count = 0;

   fs_exit();
   return (int64_t) nb;
}

uint16_t getNextWriteSector() {
   static uint16_t search_start_pos = 0;
   int i;

   if (write_sector == 0 || write_sector_bitmap == 0) { // first try to find a completely free sector
      for (i = 0; i < NUM_FLASH_SECTORS; i++) {
         if (used_bitmap[(i + search_start_pos) % NUM_FLASH_SECTORS] == 0)
            break;
      }
      if (i < NUM_FLASH_SECTORS) {
         write_sector = (i + search_start_pos) % NUM_FLASH_SECTORS;
         write_sector_bitmap = 0xFF;
         flash_erase_sector(write_sector);
      } else {                  // no completely free sector, just return the first sector with space
         for (i = 0; i < NUM_FLASH_SECTORS; i++) {
            if (used_bitmap[(i + search_start_pos) % NUM_FLASH_SECTORS] != 0xFF)
               break;
         }
         if (i == NUM_FLASH_SECTORS) {  // every slot is taken
            printf("getNextWriteSector() - storage area is full\n");
            return 0;
         }
         write_sector = (i + search_start_pos) % NUM_FLASH_SECTORS;
         write_sector_bitmap = ~used_bitmap[write_sector];
         flash_erase_with_copy_sector(write_sector, used_bitmap[write_sector]);
      }
      search_start_pos = (i + search_start_pos) % NUM_FLASH_SECTORS;
   }
   // if we get here, then at least one 512 byte page is free on the write_sector
   for (i = 0; i < 8; i++) {
      if (write_sector_bitmap & (1 << i))
         break;
   }
   if (i == 8) {                // should be unreachable: FS_SPARE_SECTORS keeps
      printf("getNextWriteSector() - no free slot\n");   // slots in reserve
      return 0;
   }
   // mark the offset used
   write_sector_bitmap &= ~(1 << i);
   return makeMapEntry(write_sector, i);
}

// seed the allocation bitmap from the map in flash; the log is applied on top
// of it afterwards by log_replay()
void init_used_bitmap() {
   memset(used_bitmap, 0, NUM_FLASH_SECTORS);
   for (int i = 0; i < FS_RESERVED_SECTORS; i++)
      used_bitmap[i] = 0xFF;    // map, shadow and log sectors are never allocated

   for (int i = 0; i < NUM_FAT_SECTORS; i++) {
      uint16_t mapEntry = map_read_flash((uint16_t) i);

      if (mapEntry)
         used_bitmap[getMapSector(mapEntry)] |= (1 << getMapOffset(mapEntry));
   }
   write_sector = 0;
   write_sector_bitmap = 0;
}

int flash_fs_mount() {
   map_journal_count = 0;
   log_next_page = 0;
   log_page_reset();

   // finish any operation that was interrupted by a power loss before looking
   // at the map, it may be the thing that needs repairing
   shadow_recover();

   if (memcmp(fs_xip_ptr(0, 0), MAGIC_8_BYTES, FS_MAP_HEADER_SIZE) != 0) {
      printf("flash_fs_mount() - magic bytes not found\n");
      return 1;
   }

   init_used_bitmap();
   log_replay();
   //debug_print_in_use();

   flash_fs_autosync_start();
   return 0;
}

void flash_fs_create() {
   map_journal_count = 0;

   // page 0 carries the header, all the map entries are zeroed
   memset(sector_buf, 0, FLASH_SECTOR_SIZE);
   memcpy(sector_buf, MAGIC_8_BYTES, FS_MAP_HEADER_SIZE);

   for (int i = 0; i < FS_MAP_ENTRIES; i++) {
      flash_erase_sector((uint16_t) i);
      flash_write_sector((uint16_t) i, 0, sector_buf, FLASH_SECTOR_SIZE);
      if (i == 0)
         memset(sector_buf, 0, FS_MAP_HEADER_SIZE);     // header only on the first page
   }

   flash_erase_sector(FS_SHADOW_DATA_SECTOR);
   flash_erase_sector(FS_SHADOW_HDR_SECTOR);
   shadow_slot_next = 0;
   log_erase();

   init_used_bitmap();

   flash_fs_autosync_start();
}

// make everything written so far durable. The map sectors are untouched: the
// log already holds the pending updates, so this costs a single 256 byte page
// program.
int64_t flash_fs_sync() {
   int64_t nb;

   fs_enter();
   nb = log_flush_page();
   fs_exit();
   return nb;
}

// Fires every FS_SYNC_INTERVAL_MS from the default alarm pool, so a staged log
// page never sits in RAM longer than that. Runs in interrupt context, which is
// safe for two reasons: the flash program itself executes from RAM with
// interrupts disabled, and the tick is skipped whenever thread context is in
// the middle of a filesystem mutation.
//
// This assumes core1 never fetches code or data from flash. Everything it runs
// must be __not_in_flash_func, and every table it touches must live in RAM:
// a const array left in .rodata is an XIP read and would stall core1 for the
// duration of the program cycle.
static bool sync_timer_cb(repeating_timer_t *t) {
   (void) t;

   if (fs_busy || log_page_used == 0)
      return true;              // try again on the next tick

   log_flush_page();
   return true;
}

// Started automatically by flash_fs_mount() and flash_fs_create(); calling it
// again is harmless.
bool flash_fs_autosync_start(void) {
   if (sync_timer_running)
      return true;

   // negative period: the interval is measured from the start of the callback
   sync_timer_running = add_repeating_timer_ms(-(int32_t) FS_SYNC_INTERVAL_MS,
                                               sync_timer_cb, NULL, &sync_timer);
   if (!sync_timer_running)
      printf("flash_fs_autosync_start() - no alarm slot available\n");

   return sync_timer_running;
}

// Only needed to release the alarm slot, or to leave the log clean before a
// deliberate shutdown; the final flush is done here.
void flash_fs_autosync_stop(void) {
   if (!sync_timer_running)
      return;

   cancel_repeating_timer(&sync_timer);
   sync_timer_running = false;
   flash_fs_sync();
}

void flash_fs_read_FAT_sector(uint16_t fat_sector, void *buffer) {
   uint16_t mapEntry = map_get_entry(fat_sector);

   if (mapEntry)
      flash_read_sector(getMapSector(mapEntry), getMapOffset(mapEntry), buffer, 512);
   else
      memset(buffer, 0, 512);
   return;
}

void flash_fs_write_FAT_sector(uint16_t fat_sector, const void *buffer) {
   uint16_t old_entry, new_entry;

   fs_enter();

   // make room before touching anything; a checkpoint here is a safe point
   if (map_journal_count >= FS_MAP_JOURNAL_SIZE || log_is_full())
      write_fs_map();

   old_entry = map_get_entry(fat_sector);
   new_entry = getNextWriteSector();
   if (new_entry == 0) {        // out of space: leave the previous copy in place
      fs_exit();
      return;
   }

   // data goes down first: until the log record is committed the map still
   // points at the previous copy, which is still marked allocated and has
   // therefore not been overwritten
   flash_write_sector(getMapSector(new_entry), getMapOffset(new_entry), buffer, 512);

   log_append(fat_sector, new_entry);
   map_journal_set(fat_sector, new_entry);

   if (old_entry)
      used_bitmap[getMapSector(old_entry)] &= ~(1 << getMapOffset(old_entry));
   used_bitmap[getMapSector(new_entry)] |= (1 << getMapOffset(new_entry));

   fs_exit();
}

bool flash_fs_verify_FAT_sector(uint16_t fat_sector, const void *buffer) {
   uint8_t read_buf[512];

   flash_fs_read_FAT_sector(fat_sector, read_buf);
   if (memcmp(buffer, read_buf, 512) == 0)
      return true;
   return false;
}

/* Low level flash functions */

void flash_read_sector(uint16_t sector, uint8_t offset, void *buffer, uint16_t size) {
   //printf("[FS] READ: %d, %d (%d)\n", sector, offset, size);
   memcpy(buffer, fs_xip_ptr(sector, (uint32_t) offset * 512), size);
}

void flash_erase_sector(uint16_t sector) {
//  printf("[FS] ERASE: %d\n", sector);
   uint32_t fs_start = HW_FLASH_STORAGE_BASE;
   uint32_t offset = fs_start + (sector * FLASH_SECTOR_SIZE);
   uint32_t ints = save_and_disable_interrupts();

   flash_range_erase(offset, FLASH_SECTOR_SIZE);
   restore_interrupts(ints);
}

void flash_write_sector(uint16_t sector, uint8_t offset, const void *buffer, uint16_t size) {
//  printf("[FS] WRITE: %d, %d (%d)\n", sector, offset, size);
   uint32_t fs_start = HW_FLASH_STORAGE_BASE;
   uint32_t addr = fs_start + (sector * FLASH_SECTOR_SIZE) + (offset * 512);
   uint32_t ints = save_and_disable_interrupts();

   flash_range_program(addr, (const uint8_t *) buffer, size);
   restore_interrupts(ints);
}

// program a single 256 byte flash page, the smallest unit the SDK accepts
static void flash_write_page(uint16_t sector, uint16_t page, const void *buffer) {
   uint32_t addr = HW_FLASH_STORAGE_BASE + (sector * FLASH_SECTOR_SIZE) + (page * FLASH_PAGE_SIZE);
   uint32_t ints = save_and_disable_interrupts();

   flash_range_program(addr, (const uint8_t *) buffer, FLASH_PAGE_SIZE);
   restore_interrupts(ints);
}

void flash_erase_with_copy_sector(uint16_t sector, uint8_t preserve_bitmap) {
//  printf("[FS] ERASE with COPY: %d\n", sector);
   flash_read_sector(sector, 0, sector_buf, FLASH_SECTOR_SIZE);

   // the sector still holds live FAT sectors, so stage it before erasing
   shadow_begin(SHADOW_TYPE_DATA_COPY, sector, preserve_bitmap, sector_buf);
   data_copy_apply(sector, preserve_bitmap, sector_buf);
   shadow_end();
}

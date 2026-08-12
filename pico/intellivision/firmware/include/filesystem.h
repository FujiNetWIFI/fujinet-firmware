
#ifndef FILESYSTEM_H
#define FILESYSTEM_H

typedef struct {
   unsigned int id;
   char isDir;
   char filename[64];            // limit filename to 64 chars
} SCREEN_ENTRY;                  // 69 bytes per entry, 512 entries max = 34Kb (40kB allocated)

typedef struct {
   unsigned int section;
   char line[10][20];
} INFO_ENTRY;                   // 204 bytes per page (20400 bytes allocated allowing for 100 pages) 

#define MAX_INFO_PAGES 100

int entry_compare(const void *p1, const void *p2);
char *get_filename_ext(char *filename);
int is_rom_file(char *filename);
int is_valid_file(char *filename);
int read_directory(char *path, SCREEN_ENTRY *dst);
int load_file(char *filename);
int collect_info_by_id(unsigned int id, char *path, INFO_ENTRY *info_entries);
int get_file_from_id(unsigned int id, char *path);
int load_file_by_id(unsigned int id, char *path);

#endif

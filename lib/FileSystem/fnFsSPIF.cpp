#include <esp_vfs.h>
#include <esp_spiffs.h>
#include <errno.h>

#include "fnFsSPIF.h"
#include "../../include/debug.h"

#define SPIFFS_MAXPATH 512

// Our global SPIFFS interface
FileSystemSPIFFS fnSPIFFS;

bool FileSystemSPIFFS::dir_open(const char * path, const char * pattern, uint16_t diropts)
{
    // We ignore sorting options since we don't expect user browsing on SPIFFS
    char * fpath = _make_fullpath(path);
    _dir = opendir(fpath);
    free(fpath);
    return(_dir != nullptr);
}

fsdir_entry * FileSystemSPIFFS::dir_read()
{
    if(_dir == nullptr)
        return nullptr;

    struct dirent *d;
    d = readdir(_dir);
    if(d != nullptr)
    {
        strlcpy(_direntry.filename, d->d_name, sizeof(_direntry.filename));

        _direntry.isDir = (d->d_type & DT_DIR) ? true : false;

        _direntry.size = 0;
        _direntry.modified_time = 0;

        // isDir will always be false - SPIFFS doesn't store directories ("dir/name" is really just "name_part1/name_part2")
        // timestamps aren't stored when files are uploaded during firmware deployment
        char * fpath = _make_fullpath(_direntry.filename);
        struct stat s;
        if(stat(fpath, &s) == 0)
        {
            _direntry.size = s.st_size;
            _direntry.modified_time = s.st_mtime;
        }
        #ifdef DEBUG
            // Debug_printf("stat \"%s\" errno %d\n", fpath, errno);
        #endif
        return &_direntry;
    }
    return nullptr;
}

void FileSystemSPIFFS::dir_close()
{
    closedir(_dir);
    _dir = nullptr;
}

uint16_t FileSystemSPIFFS::dir_tell()
{
    return 0;
}

bool FileSystemSPIFFS::dir_seek(uint16_t)
{
    return false;
}

FILE * FileSystemSPIFFS::file_open(const char* path, const char* mode)
{
    char * fpath = _make_fullpath(path);
    FILE * result = fopen(fpath, mode);
    free(fpath);
    return result;
}

bool FileSystemSPIFFS::exists(const char* path)
{
    char * fpath = _make_fullpath(path);
    struct stat st;
    int i = stat(fpath, &st);
#ifdef DEBUG
    //Debug_printf("FileSystemSPIFFS::exists returned %d on \"%s\" (%s)\n", i, path, fpath);
#endif
    free(fpath);
    return (i == 0);
}

bool FileSystemSPIFFS::remove(const char* path)
{
    char * fpath = _make_fullpath(path);
    int i = ::remove(fpath);
#ifdef DEBUG
    Debug_printf("FileSystemSPIFFS::remove returned %d on \"%s\" (%s)\n", i, path, fpath);
#endif
    free(fpath);
    return (i == 0);
}

bool FileSystemSPIFFS::rename(const char* pathFrom, const char* pathTo)
{
    char * spath = _make_fullpath(pathFrom);
    char * dpath = _make_fullpath(pathTo);
    int i = ::rename(spath, dpath);
#ifdef DEBUG
    Debug_printf("FileSystemSPIFFS::rename returned %d on \"%s\" -> \"%s\" (%s -> %s)\n", i, pathFrom, pathTo, spath, dpath);
#endif
    free(spath);
    free(dpath);
    return (i == 0);
}

uint64_t FileSystemSPIFFS::total_bytes()
{
    size_t total = 0, used = 0;
	esp_spiffs_info(NULL, &total, &used);
    return (uint64_t)total;
}

uint64_t FileSystemSPIFFS::used_bytes()
{
    size_t total = 0, used = 0;
	esp_spiffs_info(NULL, &total, &used);
    return (uint64_t)used;
}

bool FileSystemSPIFFS::start()
{
    if(_started)
        return true;

    // Set our basepath
    strlcpy(_basepath, "/spiffs", sizeof(_basepath));

    esp_vfs_spiffs_conf_t conf = {
      .base_path = _basepath,
      .partition_label = NULL,
      .max_files = 10, // from SPIFFS.h
      .format_if_mount_failed = false
    };
    
    esp_err_t e = esp_vfs_spiffs_register(&conf);

    if (e != ESP_OK)
    {
        #ifdef DEBUG
        Debug_printf("Failed to mount SPIFFS partition, err = %d\n", e);
        #endif
        _started = false;
    }
    else
    {
        _started = true;
    #ifdef DEBUG        
        Debug_println("SPIFFS mounted.");
        /*
        size_t total = 0, used = 0;
        esp_spiffs_info(NULL, &total, &used);
        Debug_printf("  partition size: %u, used: %u, free: %u\n", total, used, total-used);
        */
    #endif
    }

    return _started;
}

// ============================================================================

// fnmatch defines
#define	FNM_NOMATCH     1       // Match failed.
#define	FNM_NOESCAPE	0x01	// Disable backslash escaping.
#define	FNM_PATHNAME	0x02	// Slash must be matched by slash.
#define	FNM_PERIOD		0x04	// Period must be matched by period.
#define	FNM_LEADING_DIR	0x08	// Ignore /<tail> after Imatch.
#define	FNM_CASEFOLD	0x10	// Case insensitive search.
#define FNM_PREFIX_DIRS	0x20	// Directory prefixes of pattern match too.
#define	EOS	            '\0'

//-----------------------------------------------------------------------
static const char * rangematch(const char *pattern, char test, int flags)
{
  int negate, ok;
  char c, c2;

  /*
   * A bracket expression starting with an unquoted circumflex
   * character produces unspecified results (IEEE 1003.2-1992,
   * 3.13.2).  This implementation treats it like '!', for
   * consistency with the regular expression syntax.
   * J.T. Conklin (conklin@ngai.kaleida.com)
   */
  if ( (negate = (*pattern == '!' || *pattern == '^')) ) ++pattern;

  if (flags & FNM_CASEFOLD) test = tolower((unsigned char)test);

  for (ok = 0; (c = *pattern++) != ']';) {
    if (c == '\\' && !(flags & FNM_NOESCAPE)) c = *pattern++;
    if (c == EOS) return (NULL);

    if (flags & FNM_CASEFOLD) c = tolower((unsigned char)c);

    if (*pattern == '-' && (c2 = *(pattern+1)) != EOS && c2 != ']') {
      pattern += 2;
      if (c2 == '\\' && !(flags & FNM_NOESCAPE)) c2 = *pattern++;
      if (c2 == EOS) return (NULL);

      if (flags & FNM_CASEFOLD) c2 = tolower((unsigned char)c2);

      if ((unsigned char)c <= (unsigned char)test &&
          (unsigned char)test <= (unsigned char)c2) ok = 1;
    }
    else if (c == test) ok = 1;
  }
  return (ok == negate ? NULL : pattern);
}

//--------------------------------------------------------------------
static int fnmatch(const char *pattern, const char *string, int flags)
{
  const char *stringstart;
  char c, test;

  for (stringstart = string;;)
    switch (c = *pattern++) {
    case EOS:
      if ((flags & FNM_LEADING_DIR) && *string == '/') return (0);
      return (*string == EOS ? 0 : FNM_NOMATCH);
    case '?':
      if (*string == EOS) return (FNM_NOMATCH);
      if (*string == '/' && (flags & FNM_PATHNAME)) return (FNM_NOMATCH);
      if (*string == '.' && (flags & FNM_PERIOD) &&
          (string == stringstart ||
          ((flags & FNM_PATHNAME) && *(string - 1) == '/')))
              return (FNM_NOMATCH);
      ++string;
      break;
    case '*':
      c = *pattern;
      // Collapse multiple stars.
      while (c == '*') c = *++pattern;

      if (*string == '.' && (flags & FNM_PERIOD) &&
          (string == stringstart ||
          ((flags & FNM_PATHNAME) && *(string - 1) == '/')))
              return (FNM_NOMATCH);

      // Optimize for pattern with * at end or before /.
      if (c == EOS)
        if (flags & FNM_PATHNAME)
          return ((flags & FNM_LEADING_DIR) ||
                    strchr(string, '/') == NULL ?
                    0 : FNM_NOMATCH);
        else return (0);
      else if ((c == '/') && (flags & FNM_PATHNAME)) {
        if ((string = strchr(string, '/')) == NULL) return (FNM_NOMATCH);
        break;
      }

      // General case, use recursion.
      while ((test = *string) != EOS) {
        if (!fnmatch(pattern, string, flags & ~FNM_PERIOD)) return (0);
        if ((test == '/') && (flags & FNM_PATHNAME)) break;
        ++string;
      }
      return (FNM_NOMATCH);
    case '[':
      if (*string == EOS) return (FNM_NOMATCH);
      if ((*string == '/') && (flags & FNM_PATHNAME)) return (FNM_NOMATCH);
      if ((pattern = rangematch(pattern, *string, flags)) == NULL) return (FNM_NOMATCH);
      ++string;
      break;
    case '\\':
      if (!(flags & FNM_NOESCAPE)) {
        if ((c = *pattern++) == EOS) {
          c = '\\';
          --pattern;
        }
      }
      break;
      // FALLTHROUGH
    default:
      if (c == *string) {
      }
      else if ((flags & FNM_CASEFOLD) && (tolower((unsigned char)c) == tolower((unsigned char)*string))) {
      }
      else if ((flags & FNM_PREFIX_DIRS) && *string == EOS && ((c == '/' && string != stringstart) ||
    		  (string == stringstart+1 && *stringstart == '/')))
              return (0);
      else return (FNM_NOMATCH);
      string++;
      break;
    }
  // NOTREACHED
  return 0;
}

void FileSystemSPIFFS::list(char *path, char *match) 
{

    DIR *dir = NULL;
    struct dirent *ent;
    char type;
    char size[12];
    char tpath[255];
    char tbuffer[80];
    struct stat sb;
    struct tm *tm_info;
    char *lpath = NULL;
    int statok;

    printf("\nList of Directory [%s]\n", path);
    printf("-----------------------------------\n");
    // Open directory
    dir = opendir(path);
    if (!dir) {
        printf("Error opening directory\n");
        return;
    }

    // Read directory entries
    uint64_t total = 0;
    int nfiles = 0;
    printf("T  Size      Date/Time         Name\n");
    printf("-----------------------------------\n");
    while ((ent = readdir(dir)) != NULL) {
        sprintf(tpath, path);
        if (path[strlen(path)-1] != '/') strcat(tpath,"/");
        strcat(tpath,ent->d_name);
        tbuffer[0] = '\0';

        if ((match == NULL) || (fnmatch(match, tpath, (FNM_PERIOD)) == 0)) {
            // Get file stat
            statok = stat(tpath, &sb);

            if (statok == 0) {
                tm_info = localtime(&sb.st_mtime);
                strftime(tbuffer, 80, "%d/%m/%Y %R", tm_info);
            }
            else sprintf(tbuffer, "                ");

            if (ent->d_type == DT_REG) {
                type = 'f';
                nfiles++;
                if (statok) strcpy(size, "       ?");
                else {
                    total += sb.st_size;
                    if (sb.st_size < (1024*1024)) sprintf(size,"%8d", (int)sb.st_size);
                    else if ((sb.st_size/1024) < (1024*1024)) sprintf(size,"%6dKB", (int)(sb.st_size / 1024));
                    else sprintf(size,"%6dMB", (int)(sb.st_size / (1024 * 1024)));
                }
            }
            else {
                type = 'd';
                strcpy(size, "       -");
            }

            printf("%c  %s  %s  %s\r\n",
                type,
                size,
                tbuffer,
                ent->d_name
            );
        }
    }
    if (total) {
        printf("-----------------------------------\n");
    	if (total < (1024*1024)) printf("   %8d", (int)total);
    	else if ((total/1024) < (1024*1024)) printf("   %6dKB", (int)(total / 1024));
    	else printf("   %6dMB", (int)(total / (1024 * 1024)));
    	printf(" in %d file(s)\n", nfiles);
    }
    printf("-----------------------------------\n");

    closedir(dir);

    free(lpath);

	uint32_t tot=0, used=0;
    esp_spiffs_info(NULL, &tot, &used);
    printf("SPIFFS: free %d KB of %d KB\n", (tot-used) / 1024, tot / 1024);
    printf("-----------------------------------\n\n");
}
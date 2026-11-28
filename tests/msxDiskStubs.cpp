// Link-time stand-ins for what rs232Disk::write_blank(NewDiskTypedRequest) uses
// beyond the disk code: the system clock source and the flash filesystem's
// directory listing, which the tests never reach.

#include "fnSystem.h"
#include "utils.h"

SystemManager fnSystem;
SystemManager::SystemManager() {}
uint32_t SystemManager::random() { return 0; }

bool util_wildcard_match(const char *, const char *) { return false; }

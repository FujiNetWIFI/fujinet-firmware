#ifndef IECHOST_H
#define IECHOST_H

#include <string.h>

// Arduino-isms the vendored IECDevice library uses outside its bus timing code
#define PSTR(x) x
#define strncmp_P strncmp
#define strcmp_P  strcmp
#define min(x, y) ((x)<(y) ? (x) : (y))
#define max(x, y) ((x)>(y) ? (x) : (y))

#endif

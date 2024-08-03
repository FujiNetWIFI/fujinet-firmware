#ifndef _CBUFFER_H
#define _CBUFFER_H

#include <stdint.h>
#include <stdbool.h>

#define BUFFER_OK           0x00
#define BUFFER_FULL         0x0F
#define BUFFER_EMPTY        0x0E

//#define BUFFER_SIZE         1024

// Circular buffer struct
typedef struct {
   uint16_t size;
   uint16_t rdp;     // READ pointer
   uint16_t wrp;     // WRITE pointer
   uint16_t full;    // number of buffer full conditions
   char data[1024];   // vector of elements
} CBuffer;

//void cbuffer_alloc(CBuffer *cb, uint16_t size);
void cbuffer_init(CBuffer *cb);

bool cbuffer_isfull(CBuffer *cb);
bool cbuffer_isempty(CBuffer *cb);

uint8_t cbuffer_write(CBuffer *cb, char ch);
uint8_t cbuffer_read(CBuffer *cb, char *ch);

char *cbuffer_dumpdata(CBuffer *cb);

#endif

// EOF

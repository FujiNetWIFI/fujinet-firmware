#include "cbuffer.h"

#include <pico/stdlib.h>
#include <string.h>     // for memset

/*
void cbuffer_alloc(CBuffer *cb, uint16_t size) {

   cb->size = (size + 1); // include empty elem
   cb->data = (char *) malloc(sizeof (char) * cb->size);
}
*/

void cbuffer_init(CBuffer *cb) {

   memset(cb->data, '\0', cb->size);
   cb->rdp = cb->wrp = 0;
   cb->full = 0;
}

bool cbuffer_isempty(CBuffer *cb) {
   return (cb->wrp == cb->rdp);
}

bool cbuffer_isfull(CBuffer *cb) {
   return (((cb->wrp + 1) % cb->size) == cb->rdp);
}

uint8_t cbuffer_write(CBuffer *cb, char ch) {

   uint8_t retval = BUFFER_OK;

   cb->data[cb->wrp] = ch;
   cb->wrp = (cb->wrp + 1) % cb->size;

   if(cb->wrp == cb->rdp) {
      cb->rdp = (cb->rdp + 1) % cb->size; // full, overwrite
      retval = BUFFER_FULL;
      (cb->full)++;
   }

   return (retval);
}

uint8_t cbuffer_read(CBuffer *cb, char *ch) {

   if(cbuffer_isempty(cb))
      return BUFFER_EMPTY;

   (*ch) = cb->data[cb->rdp];
   cb->rdp = (cb->rdp + 1) % cb->size;

   return BUFFER_OK;
}

char *cbuffer_dumpdata(CBuffer *cb) {

   return (cb->data);
}

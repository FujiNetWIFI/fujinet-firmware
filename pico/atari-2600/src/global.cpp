#include "global.h"

uint8_t __not_in_flash() buffer[BUFFER_SIZE * 1024];
char __not_in_flash() http_request_header[512];

unsigned int cart_size_bytes;

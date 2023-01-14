#include "string_utils.h"

// Copy string to char buffer
void copyString(const std::string& input, char *dst, size_t dst_size)
{
    strncpy(dst, input.c_str(), dst_size - 1);
    dst[dst_size - 1] = '\0';
}
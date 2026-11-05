#include "media.h"

#include <strings.h>

const char *Media::matchExtension(const std::string &path,
                                  std::initializer_list<const char *> extensions)
{
    size_t end = path.find_first_of("?#");
    if (end == std::string::npos)
        end = path.size();

    size_t slash = path.find_last_of("/\\", end == 0 ? 0 : end - 1);
    size_t dot = path.rfind('.', end == 0 ? 0 : end - 1);
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return nullptr;

    std::string extension = path.substr(dot, end - dot);
    for (const char *candidate : extensions)
        if (strcasecmp(extension.c_str(), candidate) == 0)
            return candidate;
    return nullptr;
}

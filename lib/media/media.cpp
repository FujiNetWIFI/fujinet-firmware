#include "media.h"

#include <cctype>
#include <cstring>
#include <optional>
#include <strings.h>

#include "fujiHost.h"
#include "../../include/PSRAMMemoryResource.h"

// A media path is already host-prefixed, and fujiHost APIs prefix again.
static std::string strip_host_prefix(fujiHost *host, const std::string &path)
{
    const char *prefix = host->get_prefix();
    if (prefix == nullptr || prefix[0] == '\0')
        return path;

    size_t prefix_len = strlen(prefix);
    if (path.compare(0, prefix_len, prefix) != 0)
        return path; // not prefixed after all

    size_t start = prefix_len;
    // skip the separator util_concat_paths() inserted
    if (prefix[prefix_len - 1] != '/' && prefix[prefix_len - 1] != '\\' &&
        start < path.size() && (path[start] == '/' || path[start] == '\\'))
        start++;
    return path.substr(start);
}

// extension in the other case; nullopt if its letters mix cases.
static std::optional<std::string> other_case(const std::string &extension)
{
    bool hasLower = false, hasUpper = false;
    for (char letter : extension)
    {
        hasLower |= islower((unsigned char)letter) != 0;
        hasUpper |= isupper((unsigned char)letter) != 0;
    }
    if (hasLower == hasUpper)
        return std::nullopt;

    std::string swapped = extension;
    for (char &letter : swapped)
        letter = hasLower ? toupper((unsigned char)letter) : tolower((unsigned char)letter);
    return swapped;
}

std::optional<std::string> Media::matchExtension(
    const std::string &path, std::initializer_list<const char *> extensions)
{
    size_t end = path.find_first_of("?#");
    if (end == std::string::npos)
        end = path.size();

    size_t slash = path.find_last_of("/\\", end == 0 ? 0 : end - 1);
    size_t dot = path.rfind('.', end == 0 ? 0 : end - 1);
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return std::nullopt;

    std::string extension = path.substr(dot, end - dot);
    for (const char *candidate : extensions)
        if (strcasecmp(extension.c_str(), candidate) == 0)
            return candidate;
    return std::nullopt;
}

std::optional<std::string> Media::findSibling(fujiHost *host, const std::string &path,
                                              const char *extension)
{
    if (host == nullptr)
        return std::nullopt;

    std::string base = strip_host_prefix(host, path);
    size_t slash = base.find_last_of("/\\");
    size_t dot = base.rfind('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
        base.erase(dot);

    std::string candidate = base + extension;
    if (host->file_exists(candidate.c_str()))
        return candidate;

    std::optional<std::string> swapped = other_case(extension);
    if (!swapped.has_value())
        return std::nullopt;
    candidate = base + *swapped;
    if (!host->file_exists(candidate.c_str()))
        return std::nullopt;
    return candidate;
}

std::optional<std::pmr::vector<uint8_t>> Media::contentsOfHostFile(fujiHost *host,
                                                                   const std::string &path,
                                                                   size_t maxSize)
{
    if (host == nullptr)
        return std::nullopt;

    fnFile *fileh = host->fnfile_open(path.c_str(), nullptr, 0, "rb");
    if (fileh == nullptr)
        return std::nullopt;

    size_t fileSize = 0;
    if (host->file_size(fileh).to(&fileSize).is_error() || fileSize > maxSize)
    {
        fnio::fclose(fileh);
        return std::nullopt;
    }

    // Stateless: it only decides where the bytes go, PSRAM first on the ESP32
    static PSRAMMemoryResource psram;
    std::pmr::vector<uint8_t> contents(fileSize, &psram);
    size_t got = fnio::fread(contents.data(), 1, fileSize, fileh);
    fnio::fclose(fileh);
    if (got != fileSize)
        return std::nullopt;
    return contents;
}

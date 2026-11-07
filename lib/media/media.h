#ifndef MEDIA_H
#define MEDIA_H

#include "global_types.h"
#include "fnio.h"

#include <initializer_list>
#include <memory>
#include <memory_resource>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

class fujiHost;

// Root of the media hierarchy. No read()/write() here -- each family, such
// as ROMMedia, defines its own shape.
class Media
{
public:
    explicit Media(const std::string &path, fnFile *handle) : _path(path), _fileh(handle) {}
    virtual ~Media() = default;

    Media(const Media &) = delete;
    Media &operator=(const Media &) = delete;
    Media(Media &&) = delete;
    Media &operator=(Media &&) = delete;

    // Builds T and attach()es it from the start of the file; nullptr if that fails.
    template <typename T, typename... Args>
    static std::unique_ptr<T> open(const std::string &path, fnFile *handle, Args &&...args)
    {
        static_assert(std::is_base_of<Media, T>::value, "T must derive from Media");
        auto media = std::make_unique<T>(path, handle, std::forward<Args>(args)...);
        Media &base = *media; // attach() is protected in T
        if (fnio::fseek(handle, 0, SEEK_SET) != 0 || base.attach().is_error())
            return nullptr;
        return media;
    }

    // Destructors can't report failure; call this explicitly to catch a
    // failed flush/finalize before the object goes away.
    virtual error_is_true close() = 0;

    // Describes only the primary data; a format with a separate writable
    // channel (e.g. ROM save data) reports that capability itself.
    virtual bool isReadOnly() const = 0;

    // The entry of extensions that path ends in, ignoring case; nullopt if none.
    static std::optional<std::string> matchExtension(
        const std::string &path, std::initializer_list<const char *> extensions);

    // The same-named file beside path with extension, or in its other case; nullopt if none.
    static std::optional<std::string> findSibling(fujiHost *host, const std::string &path,
                                                  const char *extension);

    // The whole file at path, read through host; nullopt if it can't be read or
    // is over maxSize, which keeps a stray large file from exhausting the heap.
    static std::optional<std::pmr::vector<uint8_t>> contentsOfHostFile(fujiHost *host,
                                                                       const std::string &path,
                                                                       size_t maxSize);

protected:
    // Fallible per-format setup, called only from open(). Verifies and
    // prepares access to _path; does not read data into memory -- reads
    // happen on demand through each family's own read methods.
    virtual error_is_true attach() = 0;

    const std::string _path;
    fnFile *_fileh;
};

#ifdef BUILD_ATARI
# include "atari/diskType.h"
# include "atari/diskTypeAtr.h"
# include "atari/diskTypeAtx.h"
# include "atari/diskTypeXex.h"
#endif

#ifdef BUILD_RS232
# include "rs232/diskType.h"
# include "rs232/diskTypeAltair.h"
# include "rs232/diskTypeImg.h"
# include "rs232/diskTypeIMD.h"
#endif

#ifdef BUILD_IEC
# include "cbm/mediaType.h"
# include "cbm/BlankImage.h"
#endif

#ifdef BUILD_ADAM
# include "adam/mediaType.h"
# include "adam/mediaTypeDDP.h"
# include "adam/mediaTypeDSK.h"
# include "adam/mediaTypeROM.h"
#endif

#ifdef BUILD_LYNX
#include "lynx/mediaType.h"
#include "lynx/mediaTypeROM.h"
#endif

#ifdef BUILD_APPLE
# include "apple/mediaType.h"
# include "apple/mediaTypeDO.h"
# include "apple/mediaTypePO.h"
# include "apple/mediaType2MG.h"
# include "apple/mediaTypeWOZ.h"
# include "apple/mediaTypeDSK.h"
#endif

#ifdef BUILD_MAC
#include "mac/mediaType.h"
#include "mac/mediaTypeMOOF.h"
#include "mac/mediaTypeDCD.h"
#include "mac/mediaTypeFloppyImage.h"
#endif

#ifdef BUILD_S100
# include "adam/mediaType.h"
# include "adam/mediaTypeDSK.h"
#endif

#ifdef BUILD_RC2014
# include "rc2014/mediaType.h"
# include "rc2014/mediaTypeIMG.h"
#endif

#ifdef BUILD_COCO
# include "drivewire/mediaType.h"
# include "drivewire/mediaTypeDSK.h"
# include "drivewire/mediaTypeMRM.h"
# include "drivewire/mediaTypeVDK.h"
# include "drivewire/mediaTypeCASDSK.h"
#endif

#ifdef NEW_TARGET
# include "new/mediaType.h"
# include "new/mediaTypeDDP.h"
# include "new/mediaTypeDSK.h"
# include "new/mediaTypeROM.h"
#endif

#ifdef BUILD_CX16
#include "cx16/mediaType.h"
#endif

#endif // MEDIA_H
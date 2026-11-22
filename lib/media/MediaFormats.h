#ifndef MEDIAFORMATS_H
#define MEDIAFORMATS_H

#include "media.h"

#include <memory>
#include <string>
#include <type_traits>

// A format a platform accepts, and the platform's mediatype_t ID that a directory
// entry for it reports. Media never interprets the ID.
template <typename Format, auto MediaType>
struct MediaFormat
{
    static_assert(std::is_base_of_v<Media, Format>, "Format must derive from Media");
    using Type = Format;
    static constexpr auto mediaType = MediaType;
};

// Every format a platform accepts, in the order they're tried, so mounting and
// directory entries work from one list. Unknown is the mediatype_t ID for a file
// no format names.
template <auto Unknown, typename... Formats>
class MediaFormats
{
    static_assert(sizeof...(Formats) > 0, "MediaFormats needs at least one format");
    static_assert((std::is_same_v<decltype(Unknown),
                                  std::remove_cv_t<decltype(Formats::mediaType)>> && ...),
                  "every media type must be the same type as Unknown");

public:
    using MediaTypeID = decltype(Unknown);

    MediaFormats() = delete;

    // Tries each format derived from Base, in order, and returns the first whose
    // attach() accepts the file, or nullptr. A rejected format leaves handle
    // open, so a caller can try another Base with the same handle.
    template <typename Base, typename... Args>
    static std::unique_ptr<Base> open(const std::string &path, fnFile *handle,
                                      const Args &...args)
    {
        static_assert((std::is_base_of_v<Base, typename Formats::Type> || ...),
                      "no format derives from Base");
        std::unique_ptr<Base> media;
        ((media = tryOpen<Base, typename Formats::Type>(path, handle, args...)) || ...);
        return media;
    }

    // The media type of the first format whose extensions path ends in.
    static MediaTypeID entryType(const std::string &path)
    {
        MediaTypeID type = Unknown;
        ((Media::matchExtension(path, Formats::Type::extensions)
          && (type = Formats::mediaType, true)) || ...);
        return type;
    }

private:
    template <typename Base, typename Format, typename... Args>
    static std::unique_ptr<Base> tryOpen(const std::string &path, fnFile *handle,
                                         const Args &...args)
    {
        if constexpr (std::is_base_of_v<Base, Format>)
            return Media::open<Format>(path, handle, args...);
        else
            return nullptr;
    }
};

// Concatenates MediaFormats lists that share an Unknown, keeping their order, so
// a platform with several mounting classes can find a directory entry's type in
// all of them at once.
template <typename... Lists>
struct MediaFormatsJoin
{
    static_assert(sizeof...(Lists) == 0,
                  "every list must be a MediaFormats with the same Unknown");
};

template <auto Unknown, typename... Formats>
struct MediaFormatsJoin<MediaFormats<Unknown, Formats...>>
{
    using type = MediaFormats<Unknown, Formats...>;
};

template <auto Unknown, typename... First, typename... Second, typename... Rest>
struct MediaFormatsJoin<MediaFormats<Unknown, First...>, MediaFormats<Unknown, Second...>,
                        Rest...>
    : MediaFormatsJoin<MediaFormats<Unknown, First..., Second...>, Rest...>
{
};

template <typename... Lists>
using JoinedMediaFormats = typename MediaFormatsJoin<Lists...>::type;

#endif /* MEDIAFORMATS_H */

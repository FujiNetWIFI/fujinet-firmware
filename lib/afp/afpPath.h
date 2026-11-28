#ifndef AFP_PATH_H
#define AFP_PATH_H

#include <string>

struct AfpPath
{
    std::string volume; // first path component; empty when the URL names no volume
    std::string path;   // relative to the volume, "/" for its root; empty with no volume
};

// "/Volume/dir/file" -> {"Volume", "/dir/file"}
AfpPath afp_split_path(const std::string &url_path);

#endif /* AFP_PATH_H */

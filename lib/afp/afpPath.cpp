#include "afpPath.h"

AfpPath afp_split_path(const std::string &url_path)
{
    AfpPath out;

    size_t start = url_path.find_first_not_of('/');
    if (start == std::string::npos)
        return out;

    size_t slash = url_path.find('/', start);
    if (slash == std::string::npos)
    {
        out.volume = url_path.substr(start);
        out.path = "/";
        return out;
    }

    out.volume = url_path.substr(start, slash - start);
    out.path = url_path.substr(slash);
    while (out.path.size() > 1 && out.path.back() == '/')
        out.path.pop_back();
    return out;
}

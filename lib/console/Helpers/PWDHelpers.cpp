#include "PWDHelpers.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "string_utils.h"

#include "../Console.h"

namespace ESP32Console
{
    constexpr char *PWD_DEFAULT = (char*) "/";

    const char *console_getpwd()
    {
        char *pwd = getenv("PWD");
        if (pwd)
        { // If we have defined a PWD value, return it
            return pwd;
        }

        // Otherwise set a default one
        setenv("PWD", PWD_DEFAULT, 1);
        return PWD_DEFAULT;
    }

    const char *console_realpath(const char *path, char *resolvedPath)
    {
        std::string in = std::string(path);
        std::string pwd = std::string(console_getpwd());
        std::string result;
        // If path is not absolute we prepend our pwd
        if (!mstr::startsWith(in, "/"))
        {
            result = pwd + "/" + in;
        }
        else
        {
            result = in;
        }
        
        realpath(result.c_str(), resolvedPath);
        return resolvedPath;
    }

    int console_chdir(const char *path)
    {
        char buffer[PATH_MAX + 2];
        console_realpath(path, buffer);

        size_t buffer_len = strlen(buffer);
        //If path does not end with slash, add it.
        if(buffer[buffer_len - 1] != '/')
        {
            buffer[buffer_len] = '/';
            buffer[buffer_len + 1] = '\0';
        }

        setenv("PWD", buffer, 1);

        return 0;
    }

}
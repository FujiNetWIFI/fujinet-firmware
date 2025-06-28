#pragma once

namespace ESP32Console
{

    /**
     * @brief Returns the current console process working dir
     *
     * @return const char*
     */
    const char *console_getpwd();

    /**
     * @brief Resolves the given path using the console process working dir
     *
     * @return const char*
     */
    const char *console_realpath(const char *path, char *resolvedPath);

    int console_chdir(const char *path);

}
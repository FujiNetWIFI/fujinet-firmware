#pragma once

#include <string>

namespace ESP32Console
{
    /**
     * @brief Interpolate the given line using environment variables. $VAR and ${ENV} are replaced by the representive values of the environment variables.
     * 
     * @param in 
     * @return String
     */
    std::string interpolateLine(const char *in);
}
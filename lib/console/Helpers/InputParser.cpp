#include "InputParser.h"

#include <string>
#include "string_utils.h"

namespace ESP32Console
{
    std::string interpolateLine(const char *in_)
    {
        std::string in(in_);
        std::string out = in;

        // Add a space so this can be processed as a command
        mstr::replaceAll(out, "IMPROV", "improv ");

        // Add a space at end of line, this does not change anything for our consoleLine and makes parsing easier
        in = in + " ";

        // Interpolate each $ with the env variable if existing. If $ is the first character in a line it is not interpolated
        int var_index = 1;
        while ((var_index = in.find_first_of("$", var_index + 1)) > 0)
        {
            /**
             * Extract the possible env variable
             */
            int variable_start = var_index + 1;
            // If the char after $ is a { we look for an closing }. Otherwise we just look for an space
            char delimiter = ' ';
            if (in[variable_start] == '{')
            {
                // Our variable starts then at the character after ${
                variable_start++;
            }

            int variable_end = in.find_first_of(delimiter, variable_start + 1);
            // If delimiter not found look for next possible env variable
            if (variable_end == -1)
            {
                continue;
            }

            variable_end -= variable_start;
            std::string env_var = in.substr(variable_start, variable_end);
            mstr::trim(env_var);
            // Depending on whether this is an variable string, we have to include the next character
            std::string replace_target = in.substr(var_index, delimiter == '}' ? variable_end + 1 : variable_end);

            // Check if we have an env with this name, then replace it
            const char *value = getenv(env_var.c_str());
            if (value)
            {
                mstr::replaceAll(out, replace_target, value);
            }
        }

        return out.c_str();
    }
}
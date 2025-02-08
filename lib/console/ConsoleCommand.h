#pragma once

#include "./ConsoleCommandBase.h"

namespace ESP32Console
{

    /**
     * @brief A class for registering custom console commands via a passed function pointer.
     * 
     */
    class ConsoleCommand : public ConsoleCommandBase
    {
    protected:
        const char *hint_;

    public:
        /**
         * @brief Creates a new ConsoleCommand object with the given parameters.
         * 
         * @param command The name under which the command will be called (e.g. "ls"). Must not contain spaces.
         * @param func The pointer to the function which is run if this function is called. Takes two paramaters argc and argv, similar to a classical C program.
         * @param help The text shown in "help" output for this command. If set to empty string, then the command is not shown in help.
         * @param hint A hint explaining the parameters in help output
         */
        ConsoleCommand(const char *command, esp_console_cmd_func_t func, const char* help, const char* hint = "") { command_ = command; func_ = func; help_= help; hint_ = hint;};

        const esp_console_cmd_t toCommandStruct() const override
        {
            const esp_console_cmd_t cmd = {
                .command = command_,
                .help = help_,
                .hint = hint_,
                .func = func_,
                .argtable = nullptr
            };

            return cmd;
        }
    };
};
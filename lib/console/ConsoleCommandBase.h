#pragma once

#include "esp_console.h"

namespace ESP32Console {
    class ConsoleCommandBase
    {
        protected:
            const char *command_;
            const char *help_;
            esp_console_cmd_func_t func_;

        public:
            /**
            * @brief Get the command name
            * 
            * @return const char* 
            */
            const char* getCommand() { return command_; };

            const char* getHelp() { return help_; };

            virtual const esp_console_cmd_t toCommandStruct() const = 0;  
    };
};
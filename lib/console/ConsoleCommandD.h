#pragma once

#include "./ConsoleCommand.h"
#include "esp_console.h"
#include <string>
#include <functional>
#include <unordered_map>

#include <type_traits>

namespace ESP32Console
{
    using delegateFunc = std::function<int(int, char **)>;

    /**
     * @brief A class for registering custom console commands via delegate function element. The difference to ConsoleCommand is that you can pass a std::function object instead of a function pointer directly.
     * This allows for use of lambda functions. The disadvantage is that we need more heap, as we have to save the delegate function objects in a map.
     * 
     */
    class ConsoleCommandD : public ConsoleCommand
    {
    protected:
        delegateFunc delegateFn_;

        static int delegateResolver(int argc, char **argv);

    public:
        static std::unordered_map<std::string, delegateFunc> registry_;
        
        ConsoleCommandD(const char *command, delegateFunc func, const char* help, const char* hint = ""): ConsoleCommand(command, &delegateResolver, help, hint), delegateFn_(func) {};

        const esp_console_cmd_t toCommandStruct() const override
        {
            const esp_console_cmd_t cmd = {
                .command = command_,
                .help = help_,
                .hint = hint_,
                .func = func_,
                .argtable = nullptr
                };

            // When the command gets registered add it to our map, so we can access it later to resolve the delegated function call
            registry_.insert({std::string(command_), std::move(delegateFn_)});

            return cmd;
        }

        delegateFunc &getDelegateFunction() { return delegateFn_; }
    };

}
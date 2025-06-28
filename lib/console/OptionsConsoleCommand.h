#pragma once

#include "./ConsoleCommandBase.h"

#include "esp_console.h"

//This define is important, otherwise we get very high memory usage from regex
#define CXXOPTS_NO_REGEX 1
#define CXXOPTS_NO_RTTI 1
#include "cxxopts/cxxopts.hpp"
#include <functional>
#include <unordered_map>
#include <string>

namespace ESP32Console
{
    using cxxopts::Options;
    using cxxopts::ParseResult;
    using argParseFunc = std::function<int(int, char **, ParseResult&, Options&)>;

    class OptionsConsoleCommand : public ConsoleCommandBase
    {
    protected:
        argParseFunc delegateFn_;
        const char *hint_;
        const char *version_;

        static int delegateResolver(int argc, char **argv);

    public:
        Options options;
        static std::unordered_map<std::string, OptionsConsoleCommand> registry_;

        OptionsConsoleCommand(const char *command, argParseFunc func, const char *help, const char* version = nullptr, const char *hint = nullptr): options(command, help)
        {
            command_ = command;
            help_ = help;
            version_ = version;

            if (hint)
            {
                hint_ = hint;
            }
            else
            {
                    hint_ = "Use --help option of command for more info";
            }

            //Add an option 
            options.add_options()
                ("help", "Show help", cxxopts::value<bool>()->default_value("false"))
            ;

            if (version_)
            {
                options.add_options()
                    ("version", "Show version number of this command", cxxopts::value<bool>()->default_value("false"))
                ;
            }



            delegateFn_ = func;
            func_ = &delegateResolver;
        }

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
            registry_.insert({std::string(command_), std::move(*this)});

            return cmd;
        }

        argParseFunc &getDelegateFunction() { return delegateFn_; }

        const char* getVersion() const {return version_;};
    };
}
#include "./OptionsConsoleCommand.h"

#include "../../include/debug.h"

namespace ESP32Console
{
    std::unordered_map<std::string, OptionsConsoleCommand> OptionsConsoleCommand::registry_ = std::unordered_map<std::string, OptionsConsoleCommand>();

    int OptionsConsoleCommand::delegateResolver(int argc, char **argv)
    {
        // Retrieve ArgParserCommand from registry
        auto it = registry_.find(argv[0]);
        if (it == registry_.end())
        {
            Debug_printv("Could not resolve the delegated function call!");
            return 1;
        }

        auto command = it->second;

        int code = 0;

//        try
        {
            auto options = command.options;
            auto result = options.parse(argc, argv);

            //Print help on --help argument
            if (result.count("help")) {
                printf(options.help().c_str());
                printf("\r\n");
                return EXIT_SUCCESS;
            }

            if (command.getVersion() && result.count("version")) {
                printf("Version: %s\r\n", command.getVersion());
                return EXIT_SUCCESS;
            }

            return command.delegateFn_(argc, argv, result, options);
        }
        // catch (const std::exception &err)
        // {
        //     printf(err.what());
        //     printf("\r\n");
        //     return EXIT_FAILURE;
        // }
    }
}

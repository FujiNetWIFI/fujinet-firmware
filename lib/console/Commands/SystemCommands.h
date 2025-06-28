#pragma once

#include "../ConsoleCommand.h"

namespace ESP32Console::Commands
{
    const ConsoleCommand getSysInfoCommand();

    const ConsoleCommand getRestartCommand();

    const ConsoleCommand getMemInfoCommand();

    const ConsoleCommand getTaskInfoCommand();

    const ConsoleCommand getDateCommand();
};
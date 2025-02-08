#pragma once

#include "../ConsoleCommand.h"

namespace ESP32Console::Commands
{
    const ConsoleCommand getPingCommand();

    const ConsoleCommand getIpconfigCommand();

    const ConsoleCommand getScanCommand();

    const ConsoleCommand getConnectCommand();

    const ConsoleCommand getIMPROVCommand();
}
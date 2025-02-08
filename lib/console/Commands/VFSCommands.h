#pragma once

#include "../ConsoleCommand.h"

namespace ESP32Console::Commands
{
    const ConsoleCommand getCatCommand();

    const ConsoleCommand getPWDCommand();

    const ConsoleCommand getCDCommand();

    const ConsoleCommand getLsCommand();

    const ConsoleCommand getMvCommand();

    const ConsoleCommand getCPCommand();

    const ConsoleCommand getRMCommand();

    const ConsoleCommand getRMDirCommand();

    const ConsoleCommand getMKDirCommand();

    const ConsoleCommand getEditCommand();

    const ConsoleCommand getStatusCommand();

    const ConsoleCommand getMountCommand();

    const ConsoleCommand getCRC32Command();

    const ConsoleCommand getWgetCommand();
}
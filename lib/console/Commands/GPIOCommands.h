#pragma once

#include "../ConsoleCommand.h"

namespace ESP32Console::Commands
{
    const ConsoleCommand getPinModeCommand();

    const ConsoleCommand getDigitalWriteCommand();

    const ConsoleCommand getDigitalReadCommand();

    const ConsoleCommand getAnalogReadCommand();
}
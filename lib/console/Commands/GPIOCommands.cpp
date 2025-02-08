#include "GPIOCommands.h"

#include <hal/gpio_types.h>
#include <string>
#include <stdexcept>

#include "string_utils.h"

#define HIGH 1
#define LOW  0

static int _pinmode(int argc, char **argv)
{
    // if (argc != 3)
    // {
    //     printf("You have to pass a pin number and mode. Syntax: pinMode [GPIO] [MODE]\r\n");
    //     return 1;
    // }

    // char *pin_str = argv[1];
    // std::string mode_str = std::string(argv[2]);

    // unsigned long pin = 0;

    // try
    // {
    //     pin = std::stoul(pin_str);
    // }
    // catch (std::invalid_argument ex)
    // {
    //     fprintf(stderr, "Invalid argument for pin: %s\r\n", ex.what());
    //     return 1;
    // }

    // if (pin > 255 || !digitalPinIsValid(pin)) {
    //     fprintf(stderr, "%d is not a GPIO pin\r\n", pin);
    //     return 1;
    // }

    // gpio_mode_t mode = GPIO_MODE_INPUT;

    // if (mstr::equals(mode_str, "INPUT", false))
    // {
    //     mode = GPIO_MODE_INPUT;
    // }
    // else if (mstr::equals(mode_str, "OUTPUT"))
    // {
    //     mode = GPIO_MODE_OUTPUT;
    // }
    // else if (mstr::equals(mode_str, "INPUT_PULLUP"))
    // {
    //     mode = GPIO_MODE_INPUT | GPIO_PULLUP_ENABLE;
    // }
    // else if (mstr::equals(mode_str, "INPUT_PULLDOWN"))
    // {
    //     mode = GPIO_MODE_INPUT | GPIO_PULLDOWN_ENABLE;
    // }
    // else if (mstr::equals(mode_str, "OUTPUT_OPEN_DRAIN"))
    // {
    //     mode = GPIO_MODE_OUTPUT_OD;
    // }
    // else
    // {
    //     fprintf(stderr, "Invalid mode: Allowed modes are INPUT, OUTPUT, INPUT_PULLUP, INPUT_PULLDOWN, OUTPUT_OPEN_DRAIN\r\n");
    // }

    // pinMode(pin, mode);
    // printf("Mode set successful.\r\n");

    return 0;
}

static int _digitalWrite(int argc, char** argv)
{
    // if (argc != 3)
    // {
    //     printf("You have to pass an pin number and level. Syntax: digitalWrite [GPIO] [Level]\r\n");
    //     return 1;
    // }

    // char *pin_str = argv[1];
    // std::string mode_str = std::string(argv[2]);

    // unsigned long pin = 0;

    // try
    // {
    //     pin = std::stoul(pin_str);
    // }
    // catch (std::invalid_argument ex)
    // {
    //     fprintf(stderr, "Invalid argument for pin: %s\r\n", ex.what());
    //     return 1;
    // }

    // if (pin > 255 || !digitalPinCanOutput(pin)) {
    //     fprintf(stderr, "%d is not a GPIO pin\r\n", pin);
    //     return 1;
    // }

    // int mode = LOW;

    // if (mstr::equals(mode_str, "HIGH")  || mstr::equals(mode_str, "1"))
    // {
    //     mode = HIGH;
    // }
    // else if (mstr::equals(mode_str, "LOW") || mstr::equals(mode_str, "0"))
    // {
    //     mode = LOW;
    // } else
    // {
    //     fprintf(stderr, "Invalid mode: Allowed levels are HIGH, LOW, 0 and 1\r\n");
    // }

    // pinMode(pin, mode);
    // printf("Output set successful.\r\n");

    return 0;
}

static int _digitalRead(int argc, char** argv)
{
    // if (argc != 2)
    // {
    //     printf("You have to pass an pin number to read\r\n");
    //     return 1;
    // }

    // char *pin_str = argv[1];

    // unsigned long pin = 0;

    // try
    // {
    //     pin = std::stoul(pin_str);
    // }
    // catch (std::invalid_argument ex)
    // {
    //     fprintf(stderr, "Invalid argument for pin: %s\r\n", ex.what());
    //     return 1;
    // }

    // if (pin > 255 || !digitalPinCanOutput(pin)) {
    //     fprintf(stderr, "%d is not a GPIO pin\r\n", pin);
    //     return 1;
    // }

    // auto level = digitalRead(pin);

    // if(level == HIGH) {
    //     printf("HIGH\r\n");
    // } else if(level == LOW) {
    //     printf("LOW\r\n");
    // } else {
    //     fprintf(stderr, "Unknown state (%u) of pin %u!\r\n", level, pin);
    //     return 1;
    // }

    return 0;
}

static int _analogRead(int argc, char** argv)
{
    // if (argc != 2)
    // {
    //     printf("You have to pass an pin number to read\r\n");
    //     return 1;
    // }

    // char *pin_str = argv[1];

    // unsigned long pin = 0;

    // try
    // {
    //     pin = std::stoul(pin_str);
    // }
    // catch (std::invalid_argument ex)
    // {
    //     fprintf(stderr, "Invalid argument for pin: %s\r\n", ex.what());
    //     return 1;
    // }

    // if (pin > 255 || digitalPinToAnalogChannel(pin) == -1) {
    //     fprintf(stderr, "%d is not a ADC pin\r\n", pin);
    //     return 1;
    // }

    // auto value = analogReadMilliVolts(pin);
    
    // printf("%u mV\r\n", value);

    return 0;
}



namespace ESP32Console::Commands
{
    const ConsoleCommand getPinModeCommand()
    {
        return ConsoleCommand("pinMode", &_pinmode, "Changes the pinmode of an GPIO pin (similar to Arduino function)");
    }

    const ConsoleCommand getDigitalWriteCommand()
    {
        return ConsoleCommand("digitalWrite", &_digitalWrite, "Writes the state of an ouput pin (similar to Arduino function)");
    }

    const ConsoleCommand getDigitalReadCommand()
    {
        return ConsoleCommand("digitalRead", &_digitalRead, "Reads the state of an input pin (similar to Arduino function)");
    }

    const ConsoleCommand getAnalogReadCommand()
    {
        return ConsoleCommand("analogRead", &_analogRead, "Show the voltage at an analog pin in millivollts.");
    }
}
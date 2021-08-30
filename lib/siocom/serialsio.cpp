
#include "serialsio.h"
#include "fnSystem.h"

/* SIO PIN management
 * added SIO specific functionality to UARTManager for SIO port
 * see serialsio.h
 */

bool SerialSioPort::command_line()
{
    return (bool)fnSystem.digital_read(PIN_CMD);
}

bool SerialSioPort::motor_line()
{
    return (bool)fnSystem.digital_read(PIN_MTR);
}

void SerialSioPort::set_proceed_line(bool level)
{
    fnSystem.digital_write(PIN_PROC, level);
}

void SerialSioPort::set_interrupt_line(bool level)
{
    fnSystem.digital_write(PIN_INT, level);
}

// specific to SerialSioPort/UART
void SerialSioPort::setup()
{
    // INT PIN
    fnSystem.set_pin_mode(PIN_INT, gpio_mode_t::GPIO_MODE_OUTPUT_OD, SystemManager::pull_updown_t::PULL_UP);
    fnSystem.digital_write(PIN_INT, DIGI_HIGH);
    // PROC PIN
    fnSystem.set_pin_mode(PIN_PROC, gpio_mode_t::GPIO_MODE_OUTPUT_OD, SystemManager::pull_updown_t::PULL_UP);
    fnSystem.digital_write(PIN_PROC, DIGI_HIGH);
    // MTR PIN
    fnSystem.set_pin_mode(PIN_MTR, gpio_mode_t::GPIO_MODE_INPUT);
    // CMD PIN
    fnSystem.set_pin_mode(PIN_CMD, gpio_mode_t::GPIO_MODE_INPUT);
    // CKI PIN
    fnSystem.set_pin_mode(PIN_CKI, gpio_mode_t::GPIO_MODE_OUTPUT_OD);
    fnSystem.digital_write(PIN_CKI, DIGI_LOW);
    // CKO PIN
    fnSystem.set_pin_mode(PIN_CKO, gpio_mode_t::GPIO_MODE_INPUT);
}

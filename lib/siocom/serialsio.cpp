
#include "serialsio.h"
#include "fnSystem.h"

/* SIO PIN management
 * added SIO specific functionality to UARTManager for SIO port
 * see serialsio.h
 */

bool SerialSioPort::command_asserted()
{
    return ! (bool)fnSystem.digital_read(PIN_CMD); // command line is asserted with low voltage
}

bool SerialSioPort::motor_asserted()
{
    return (bool)fnSystem.digital_read(PIN_MTR);
}

void SerialSioPort::set_proceed(bool level)
{
    fnSystem.digital_write(PIN_PROC, level ? DIGI_LOW : DIGI_HIGH); // proceed line is asserted with low voltage
}

void SerialSioPort::set_interrupt(bool level)
{
    fnSystem.digital_write(PIN_INT, level ? DIGI_LOW : DIGI_HIGH); // interrupt line is asserted with low voltage
}

// specific to SerialSioPort/UART
void SerialSioPort::setup()
{
    // INT PIN
    fnSystem.set_pin_mode(PIN_INT, gpio_mode_t::GPIO_MODE_OUTPUT_OD, SystemManager::pull_updown_t::PULL_UP);
    set_interrupt(false);
    // PROC PIN
    fnSystem.set_pin_mode(PIN_PROC, gpio_mode_t::GPIO_MODE_OUTPUT_OD, SystemManager::pull_updown_t::PULL_UP);
    set_proceed(false);
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

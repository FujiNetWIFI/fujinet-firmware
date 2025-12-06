#include "ESP32UARTChannel.h"
#include "fnSystem.h"
#include "../../include/pinmap.h"
#include "../../include/debug.h"

#include <soc/uart_reg.h>
#include <hal/gpio_types.h>

#define MAX_FLUSH_WAIT_TICKS 200

// Serial "debug port"
ESP32UARTChannel fnDebugConsole;

void ESP32UARTChannel::begin(const ChannelConfig& conf)
{
    if (_uart_q)
    {
        end();
    }

    _uart_num = conf.device;
    read_timeout_ms = conf.read_timeout_ms;
    discard_timeout_ms = conf.discard_timeout_ms;
    Debug_printv("speed: %i", conf.uart_config.baud_rate);
    uart_param_config(_uart_num, &conf.uart_config);

    int tx, rx;
    if (_uart_num == 0)
    {
        rx = PIN_UART0_RX;
        tx = PIN_UART0_TX;
    }
    else if (_uart_num == 1)
    {
        rx = PIN_UART1_RX;
        tx = PIN_UART1_TX;
    }
    else if (_uart_num == 2)
    {
        rx = PIN_UART2_RX;
        tx = PIN_UART2_TX;
    }
    else
    {
        return;
    }

    uart_set_pin(_uart_num, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    if (conf.isInverted)
        uart_set_line_inverse(_uart_num, UART_SIGNAL_TXD_INV | UART_SIGNAL_RXD_INV);

    if (conf.uart_intr_config)
        uart_intr_config(_uart_num,conf.uart_intr_config);

    // Arduino default buffer size is 256
    int uart_buffer_size = UART_HW_FIFO_LEN(uart_num) * 2;
    int uart_queue_size = 10;
    int intr_alloc_flags = 0;

    // Install UART driver using an event queue here
    uart_driver_install(_uart_num, uart_buffer_size, 0, uart_queue_size, &_uart_q,
                        intr_alloc_flags);

    controlPins = conf.pins;

    if (controlPins.rts >= 0)
        fnSystem.set_pin_mode(controlPins.rts, gpio_mode_t::GPIO_MODE_INPUT);
    if (controlPins.cts >= 0)
    {
        fnSystem.set_pin_mode(controlPins.cts, gpio_mode_t::GPIO_MODE_OUTPUT);
        fnSystem.digital_write(controlPins.cts, DIGI_LOW);
    }

    if (controlPins.dtr >= 0)
        fnSystem.set_pin_mode(controlPins.dtr, gpio_mode_t::GPIO_MODE_INPUT);
    if (controlPins.dsr >= 0)
    {
        fnSystem.set_pin_mode(controlPins.dsr, gpio_mode_t::GPIO_MODE_OUTPUT);
        fnSystem.digital_write(controlPins.dsr, DIGI_LOW);
    }

    if (controlPins.dcd >= 0)
    {
        fnSystem.set_pin_mode(controlPins.dcd, gpio_mode_t::GPIO_MODE_OUTPUT);
        fnSystem.digital_write(controlPins.dcd, DIGI_HIGH);
    }

    if (controlPins.ri >= 0)
    {
        fnSystem.set_pin_mode(controlPins.ri, gpio_mode_t::GPIO_MODE_OUTPUT);
        fnSystem.digital_write(controlPins.ri, DIGI_HIGH);
    }

    return;
}

void ESP32UARTChannel::end()
{
    uart_driver_delete(_uart_num);
    if (_uart_q)
        vQueueDelete(_uart_q);
    _uart_q = NULL;
}

void ESP32UARTChannel::updateFIFO()
{
    uart_event_t event;

    while (xQueueReceive(_uart_q, &event, 1))
    {
        if (event.type == UART_DATA)
        {
            size_t old_len = _fifo.size();
            _fifo.resize(old_len + event.size);
            int result = uart_read_bytes(_uart_num, &_fifo[old_len], event.size, 0);
            if (result < 0)
                result = 0;
            _fifo.resize(old_len + result);
        }
    }

    return;
}

void ESP32UARTChannel::flushOutput()
{
    uart_wait_tx_done(_uart_num, MAX_FLUSH_WAIT_TICKS);
}

uint32_t ESP32UARTChannel::getBaudrate()
{
    uint32_t baud;
    uart_get_baudrate(_uart_num, &baud);
    return baud;
}

void ESP32UARTChannel::setBaudrate(uint32_t baud)
{
#ifdef DEBUG
    uint32_t before;
    uart_get_baudrate(_uart_num, &before);
#endif
    uart_set_baudrate(_uart_num, baud);
#ifdef DEBUG
    Debug_printf("set_baudrate change from %d to %d\r\n", before, baud);
#endif
}

size_t ESP32UARTChannel::dataOut(const void *buffer, size_t size)
{
    return uart_write_bytes(_uart_num, (const char *)buffer, size);
}

bool ESP32UARTChannel::getPin(int pin)
{
    if (pin < 0)
        return 0;
    return fnSystem.digital_read(pin) == DIGI_LOW;
}

void ESP32UARTChannel::setPin(int pin, bool state)
{
    if (pin >= 0)
        fnSystem.digital_write(pin, !state);
    return;
}

bool ESP32UARTChannel::getDTR()
{
    return getPin(controlPins.dtr);
}

void ESP32UARTChannel::setDSR(bool state)
{
    setPin(controlPins.dsr, state);
}

bool ESP32UARTChannel::getRTS()
{
    return getPin(controlPins.rts);
}

void ESP32UARTChannel::setCTS(bool state)
{
    setPin(controlPins.cts, state);
}

void ESP32UARTChannel::setDCD(bool state)
{
    setPin(controlPins.dcd, state);
}

void ESP32UARTChannel::setRI(bool state)
{
    setPin(controlPins.ri, state);
}

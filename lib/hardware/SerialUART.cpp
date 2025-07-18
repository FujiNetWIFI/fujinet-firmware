#include "SerialUART.h"
#include "fnSystem.h"
#include "../../include/pinmap.h"
#include "../../include/debug.h"

#include <soc/uart_reg.h>
#include <hal/gpio_types.h>

#define MAX_FLUSH_WAIT_TICKS 200

// Serial "debug port"
SerialUART fnDebugConsole;

void SerialUART::end()
{
    uart_driver_delete(_uart_num);
    if (_uart_q)
        vQueueDelete(_uart_q);
    _uart_q = NULL;
}

void SerialUART::begin(uart_port_t uart_num, const SerialUARTConfig& conf)
{
    if (_uart_q)
    {
        end();
    }

    _uart_num = uart_num;
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

    // Arduino default buffer size is 256
    int uart_buffer_size = UART_HW_FIFO_LEN(uart_num) * 2;
    int uart_queue_size = 10;
    int intr_alloc_flags = 0;

    // Install UART driver using an event queue here
    uart_driver_install(_uart_num, uart_buffer_size, 0, uart_queue_size, &_uart_q,
                        intr_alloc_flags);
}

void SerialUART::checkRXQueue()
{
    uart_event_t event;

    while (xQueueReceive(_uart_q, &event, 1))
    {
        Debug_printf("UART EVENT: %i size: %i\r\n", event.type, event.size);
        if (event.type == UART_DATA)
        {
            size_t old_len = fifo.size();
            fifo.resize(old_len + event.size);
            int result = uart_read_bytes(_uart_num, &fifo[old_len], event.size, 0);
            Debug_printf("UART READ: %i\n", result);
            if (result < 0)
                result = 0;
            fifo.resize(old_len + result);
        }
    }

    return;
}

void SerialUART::flush()
{
    uart_wait_tx_done(_uart_num, MAX_FLUSH_WAIT_TICKS);
}

uint32_t SerialUART::getBaudrate()
{
    uint32_t baud;
    uart_get_baudrate(_uart_num, &baud);
    return baud;
}

void SerialUART::setBaudrate(uint32_t baud)
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

size_t SerialUART::send(const void *buffer, size_t size)
{
    return uart_write_bytes(_uart_num, (const char *)buffer, size);
}

bool SerialUART::dtrState()
{
#ifdef FUJINET_OVER_USB
    return 0;
#else /* FUJINET_OVER_USB */
    return fnSystem.digital_read(PIN_RS232_DTR) == DIGI_LOW;
#endif /* FUJINET_OVER_USB */
}

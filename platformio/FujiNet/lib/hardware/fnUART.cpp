#include <string.h>
#include <cstdarg>
#include <esp_system.h>
#include <driver/uart.h>

#include "fnUART.h"

#define UART_DEBUG UART_NUM_0
#define UART_SIO   UART_NUM_2

// Number of RTOS ticks to wait for data in TX buffer to complete sending
#define MAX_FLUSH_WAIT_TICKS 200
#define MAX_READ_WAIT_TICKS 1000
#define MAX_WRITE_BYTE_TICKS 100
#define MAX_WRITE_BUFFER_TICKS 1000

// Don't really need these right now, but they could come in handy later
#define UART0_RX 3
#define UART0_TX 1
#define UART1_RX 9
#define UART1_TX 10
#define UART2_RX 16
#define UART2_TX 17

UARTManager fnUartDebug(UART_DEBUG);
//UARTManager fnUartSIO(UART_SIO);

// Constructor
UARTManager::UARTManager(uart_port_t uart_num) : _uart_num(uart_num), _uart_q(NULL) {}

void UARTManager::end()
{
    uart_driver_delete(_uart_num);
    if(_uart_q)
        free(_uart_q);
    _uart_q = NULL;
}

void UARTManager::begin(int baud)
{
    if(_uart_q) {
        end();
    }

    uart_config_t uart_config = 
    {
        .baud_rate = baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122, // No idea what this is for, but shouldn't matter if flow ctrl is disabled?
    };

    uart_param_config(UART_DEBUG, &uart_config);
    uart_set_pin(_uart_num, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    // Don't know how this compares to what Arduino normally does?
    int uart_buffer_size = 2048;
    int uart_queue_size = 10;
    int intr_alloc_flags = 0;

    // Install UART driver using an event queue here
    uart_driver_install(UART_DEBUG, uart_buffer_size, uart_buffer_size, uart_queue_size, &_uart_q, intr_alloc_flags);
}

/* Clears input buffer and flushes out transmit buffer waiting at most
   waiting MAX_FLUSH_WAIT_TICKS until all sends are completed
*/
void UARTManager::flush()
{
    uart_wait_tx_done(_uart_num, MAX_FLUSH_WAIT_TICKS);
}

/* Returns number of bytes available in receive buffer or -1 on error
*/
int UARTManager::available()
{
    size_t result;
    if(ESP_FAIL == uart_get_buffered_data_len(_uart_num, &result))
        return -1;
    return result;
}

/* NOT IMPLEMENTED
*/
int UARTManager::peek()
{
    return 0;
}

/* Changes baud rate
*/
void UARTManager::set_baudrate(uint32_t baud)
{
    uart_set_baudrate(_uart_num, baud);
}

/* Returns a single byte from the incoming stream if there's one waiting
*  otherwise -1
*/
int UARTManager::read(void)
{
    size_t result;
    if(ESP_FAIL == uart_get_buffered_data_len(_uart_num, &result))
        return -1;

    uint8_t byte;
    uart_read_bytes(_uart_num, &byte, 1, 0); // 0 wait since we already confirmed there's data in the buffer
    return byte;
}

/* Since the underlying Stream calls this Read() multiple times to get more than one
*  character for ReadBytes(), we override with a single call to uart_read_bytes
*/
size_t UARTManager::readBytes(char *buffer, size_t length)
{
    size_t result;
    if(ESP_FAIL == uart_get_buffered_data_len(_uart_num, &result))
        return -1;

    return uart_read_bytes(_uart_num, (uint8_t *)buffer, length, MAX_READ_WAIT_TICKS);
}

size_t UARTManager::write(uint8_t c)
{
    int z = uart_write_bytes(_uart_num, (const char *)&c, 1);
    uart_wait_tx_done(_uart_num, MAX_WRITE_BYTE_TICKS);
    return z;
}

size_t UARTManager::write(const uint8_t *buffer, size_t size)
{
    int z = uart_write_bytes(_uart_num, (const char *)buffer, size);
    uart_wait_tx_done(_uart_num, MAX_WRITE_BUFFER_TICKS);
    return z;
}

/*
void UARTManager::printf(const char * fmt...)
{
    va_list vargs;
    va_start(vargs, fmt);
    char * result;
    int z = vasprintf(&result, fmt, vargs);
    uart_write_bytes(UART_DEBUG, result, z);
    free(result);
    uart_wait_tx_done(_uart_num, MAX_WRITE_BUFFER_TICKS);
}
*/
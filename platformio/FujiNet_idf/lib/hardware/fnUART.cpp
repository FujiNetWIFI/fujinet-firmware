#include <string.h>
#include <cstdarg>
#include <esp_system.h>
#include <driver/uart.h>

#include "../../include/debug.h"
#include "fnUART.h"

#define UART_DEBUG UART_NUM_0
#define UART_SIO   UART_NUM_2

// Number of RTOS ticks to wait for data in TX buffer to complete sending
#define MAX_FLUSH_WAIT_TICKS 200
#define MAX_READ_WAIT_TICKS 1000
#define MAX_WRITE_BYTE_TICKS 100
#define MAX_WRITE_BUFFER_TICKS 1000

#define UART0_RX 3
#define UART0_TX 1
#define UART1_RX 9
#define UART1_TX 10
#ifdef BOARD_HAS_PSRAM
#define UART2_RX 33
#define UART2_TX 21
#else
#define UART2_RX 16
#define UART2_TX 17
#endif

// Only define these if the default Arduino global HardwareSerial objects aren't declared
#ifdef NO_GLOBAL_SERIAL
UARTManager fnUartDebug(UART_DEBUG);
UARTManager fnUartSIO(UART_SIO);
#else
#pragma GCC error "Arduino serial interfaces must be disabled with NO_GLOBAL_SERIAL!"
#endif

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
    if(_uart_q)
    {
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
    uart_param_config(_uart_num, &uart_config);

    int tx, rx;
    if(_uart_num == 0)
    {
        rx = UART0_RX;
        tx = UART0_TX;
    }
    else if(_uart_num == 1)
    {
        rx = UART1_RX;
        tx = UART1_TX;
    }
    else if (_uart_num == 2)
    {
        rx = UART2_RX;
        tx = UART2_TX;
    } else {
        return;
    }

    uart_set_pin(_uart_num, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    // Arduino default buffer size is 256
    int uart_buffer_size = 512;
    int uart_queue_size = 10;
    int intr_alloc_flags = 0;

    // Install UART driver using an event queue here
    //uart_driver_install(_uart_num, uart_buffer_size, uart_buffer_size, uart_queue_size, &_uart_q, intr_alloc_flags);
    uart_driver_install(_uart_num, uart_buffer_size, 0, uart_queue_size, NULL, intr_alloc_flags);
}

/* Discards anything in the input buffer
*/
void UARTManager::flush_input()
{
    uart_flush_input(_uart_num);
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
#ifdef DEBUG    
    uint32_t before;
    uart_get_baudrate(_uart_num, &before);
#endif    
    uart_set_baudrate(_uart_num, baud);
#ifdef DEBUG
    Debug_printf("set_baudrate change from %d to %d\n", before, baud);
#endif    
}

/* Returns a single byte from the incoming stream
*/
int UARTManager::read(void)
{
    uint8_t byte;
    int result = uart_read_bytes(_uart_num, &byte, 1, MAX_READ_WAIT_TICKS);
    if(result < 1)
    {
#ifdef DEBUG
        if(result == 0)
            Debug_println("### UART read() TIMEOUT ###");
        else
            Debug_printf("### UART read() ERROR %d ###\n", result);
#endif        
        return -1;
    } else
        return byte;
}

/* Since the underlying Stream calls this Read() multiple times to get more than one
*  character for ReadBytes(), we override with a single call to uart_read_bytes
*/
size_t UARTManager::readBytes(uint8_t *buffer, size_t length)
{
    int result = uart_read_bytes(_uart_num, buffer, length, MAX_READ_WAIT_TICKS);
#ifdef DEBUG
    if(result < length)
    {
        if(result < 0)
            Debug_printf("### UART readBytes() ERROR %d ###\n", result);
        else
            Debug_println("### UART readBytes() TIMEOUT ###");
    }
#endif        
    return result;    
}

size_t UARTManager::write(uint8_t c)
{
    int z = uart_write_bytes(_uart_num, (const char *)&c, 1);
    //uart_wait_tx_done(_uart_num, MAX_WRITE_BYTE_TICKS);
    return z;
}

size_t UARTManager::write(const uint8_t *buffer, size_t size)
{
    int z = uart_write_bytes(_uart_num, (const char *)buffer, size);
    //uart_wait_tx_done(_uart_num, MAX_WRITE_BUFFER_TICKS);
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
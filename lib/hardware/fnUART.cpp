#ifdef ESP_PLATFORM

// ESP UART code


#include "fnUART.h"

#include <soc/uart_reg.h>
#include <hal/gpio_types.h>

#include <cstring>

#include "../../include/pinmap.h"

#include "../../include/debug.h"

// Number of RTOS ticks to wait for data in TX buffer to complete sending
#define MAX_FLUSH_WAIT_TICKS 200
#define MAX_READ_WAIT_TICKS 200
#define MAX_WRITE_BYTE_TICKS 100
#define MAX_WRITE_BUFFER_TICKS 1000

// Serial "debug port"
UARTManager fnUartDebug(FN_UART_DEBUG);

// Serial "bus port" (CoCo uses fnDwCom - configurable serial or TCP (Becker) drivewire port)
#ifndef BUILD_COCO
UARTManager fnUartBUS(FN_UART_BUS);
#endif

// Constructor
UARTManager::UARTManager(uart_port_t uart_num) : _uart_num(uart_num), _uart_q(NULL) {}

void UARTManager::end()
{
    uart_driver_delete(_uart_num);
    if (_uart_q)
        vQueueDelete(_uart_q);
    _uart_q = NULL;
}

void UARTManager::begin(int baud)
{
    if (_uart_q)
    {
        end();
    }

    uart_config_t uart_config =
        {
            .baud_rate = baud,
            .data_bits = UART_DATA_8_BITS,
#ifdef BUILD_LYNX
            .parity = UART_PARITY_ODD,
#else
            .parity = UART_PARITY_DISABLE,
#endif /* BUILD_LYNX */
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .rx_flow_ctrl_thresh = 122, // No idea what this is for, but shouldn't matter if flow ctrl is disabled?
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
            .source_clk = UART_SCLK_DEFAULT
#else
            .use_ref_tick = false       // ?
#endif
        };

    // This works around an obscure hardware bug where resetting UART2 causes the TX to become corrupted
    // when the FIFO is reset by this function. Blame me for it -Thom
    // ... except on the Adam, which needs this to happen regardless. Go figure.
// #ifdef BUILD_ATARI
//     if (_uart_num == UART_SIO)
//     {
//         if (esp_reset_reason() != ESP_RST_SW)
//             uart_param_config(_uart_num, &uart_config);
//     }
//     else if (_uart_num == UART_DEBUG)
//     {
//         uart_param_config(_uart_num, &uart_config);
//     }
// #else
    uart_param_config(_uart_num, &uart_config); // now always gets called.
// #endif

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
#ifndef BUILD_RS232
    else if (_uart_num == 2)
    {
        rx = PIN_UART2_RX;
        tx = PIN_UART2_TX;
    }
#endif /* BUILD_RS232 */
    else
    {
        return;
    }

    uart_set_pin(_uart_num, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

#ifdef BUILD_ADAM
    if (_uart_num == 2)
        uart_set_line_inverse(_uart_num, UART_SIGNAL_TXD_INV | UART_SIGNAL_RXD_INV);
#endif /* BUILD_ADAM */

#ifdef BUILD_COCO
#ifndef PINMAP_COCO_CART
    if (_uart_num == 2)
        uart_set_line_inverse(_uart_num, UART_SIGNAL_TXD_INV | UART_SIGNAL_RXD_INV);
#endif
#endif /* BUILD_COCO */


    // Arduino default buffer size is 256
    int uart_buffer_size = 256;
    int uart_queue_size = 10;
    int intr_alloc_flags = 0;

    // Install UART driver using an event queue here
    // uart_driver_install(_uart_num, uart_buffer_size, uart_buffer_size, uart_queue_size, &_uart_q, intr_alloc_flags);
    uart_driver_install(_uart_num, uart_buffer_size, 0, uart_queue_size, NULL, intr_alloc_flags);

#ifdef BUILD_ADAM
    uart_intr_config_t uart_intr;
    uart_intr.intr_enable_mask = UART_RXFIFO_FULL_INT_ENA_M | UART_RXFIFO_TOUT_INT_ENA_M | UART_FRM_ERR_INT_ENA_M | UART_RXFIFO_OVF_INT_ENA_M | UART_BRK_DET_INT_ENA_M | UART_PARITY_ERR_INT_ENA_M;
    uart_intr.rxfifo_full_thresh = 1;        // UART_FULL_THRESH_DEFAULT,  //120 default!! aghh! need receive 120 chars before we see them
    uart_intr.rx_timeout_thresh = 10;        // UART_TOUT_THRESH_DEFAULT,  //10 works well for my short messages I need send/receive
    uart_intr.txfifo_empty_intr_thresh = 2; // UART_EMPTY_THRESH_DEFAULT
    uart_intr_config(_uart_num, &uart_intr);
#endif /* BUILD_ADAM */

#ifdef BUILD_LYNX
    uart_intr_config_t uart_intr;
    uart_intr.intr_enable_mask = UART_RXFIFO_FULL_INT_ENA_M | UART_RXFIFO_TOUT_INT_ENA_M | UART_FRM_ERR_INT_ENA_M | UART_RXFIFO_OVF_INT_ENA_M | UART_BRK_DET_INT_ENA_M | UART_PARITY_ERR_INT_ENA_M;
    uart_intr.rxfifo_full_thresh = 1;        // UART_FULL_THRESH_DEFAULT,  //120 default!! aghh! need receive 120 chars before we see them
    uart_intr.rx_timeout_thresh = 10;        // UART_TOUT_THRESH_DEFAULT,  //10 works well for my short messages I need send/receive
    uart_intr.txfifo_empty_intr_thresh = 2; // UART_EMPTY_THRESH_DEFAULT
    uart_intr_config(_uart_num, &uart_intr);
#endif /* BUILD_LYNX */

    // Set initialized.
    _initialized = true;
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
    if (ESP_FAIL == uart_get_buffered_data_len(_uart_num, &result))
        return -1;
    return result;
}

/* NOT IMPLEMENTED
 */
int UARTManager::peek()
{
    return 0;
}

/* Get current baud rate
*/
uint32_t UARTManager::get_baudrate()
{
    uint32_t baud;
    uart_get_baudrate(_uart_num, &baud);
    return baud;
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
    Debug_printf("set_baudrate change from %d to %d\r\n", before, baud);
#endif
}

/* Returns a single byte from the incoming stream
 */
int UARTManager::read(void)
{
    uint8_t byte;
    int result = uart_read_bytes(_uart_num, &byte, 1, MAX_READ_WAIT_TICKS);
    if (result < 1)
    {
#ifdef DEBUG
        if (result == 0)
            Debug_println("### UART read() TIMEOUT ###");
        else
            Debug_printf("### UART read() ERROR %d ###\r\n", result);
#endif
        return -1;
    }
    else
        return byte;
}

/* Since the underlying Stream calls this Read() multiple times to get more than one
 *  character for ReadBytes(), we override with a single call to uart_read_bytes
 */
size_t UARTManager::readBytes(uint8_t *buffer, size_t length)
{
    int result = uart_read_bytes(_uart_num, buffer, length, MAX_READ_WAIT_TICKS);
#ifdef DEBUG
    if (result < length)
    {
        if (result < 0)
        {
            Debug_printf("### UART readBytes() ERROR %d ###\r\n", result);
        }
        else
        {
            // Debug_println("### UART readBytes() TIMEOUT ###");
        }
    }
#endif
    return result;
}

size_t UARTManager::write(uint8_t c)
{
    int z = uart_write_bytes(_uart_num, (const char *)&c, 1);
    // uart_wait_tx_done(_uart_num, MAX_WRITE_BYTE_TICKS);
    return z;
}

size_t UARTManager::write(const uint8_t *buffer, size_t size)
{
    int z = uart_write_bytes(_uart_num, (const char *)buffer, size);
    // uart_wait_tx_done(_uart_num, MAX_WRITE_BUFFER_TICKS);
    return z;
}

size_t UARTManager::write(const char *str)
{
    int z = uart_write_bytes(_uart_num, str, strlen(str));
    return z;
}

size_t UARTManager::printf(const char *fmt...)
{
    char *result = nullptr;
    va_list vargs;

    if (!_initialized)
        return -1;

    va_start(vargs, fmt);

    int z = vasprintf(&result, fmt, vargs);

    if (z > 0)
        uart_write_bytes(_uart_num, result, z);

    va_end(vargs);

    if (result != nullptr)
        free(result);

    // uart_wait_tx_done(_uart_num, MAX_WRITE_BUFFER_TICKS);

    return z >= 0 ? z : 0;
}

size_t UARTManager::_print_number(unsigned long n, uint8_t base)
{
    char buf[8 * sizeof(long) + 1]; // Assumes 8-bit chars plus zero byte.
    char *str = &buf[sizeof(buf) - 1];

    if (!_initialized)
        return -1;

    *str = '\0';

    // prevent crash if called with base == 1
    if (base < 2)
        base = 10;

    do
    {
        unsigned long m = n;
        n /= base;
        char c = m - base * n;
        *--str = c < 10 ? c + '0' : c + 'A' - 10;
    } while (n);

    return write(str);
}

size_t UARTManager::print(const char *str)
{
    int z = strlen(str);

    if (!_initialized)
        return -1;

    return uart_write_bytes(_uart_num, str, z);
    ;
}

size_t UARTManager::print(const std::string &str)
{
    if (!_initialized)
        return -1;

    return print(str.c_str());
}

size_t UARTManager::print(int n, int base)
{
    if (!_initialized)
        return -1;

    return print((long)n, base);
}

size_t UARTManager::print(unsigned int n, int base)
{
    if (!_initialized)
        return -1;

    return print((unsigned long)n, base);
}

size_t UARTManager::print(long n, int base)
{
    if (!_initialized)
        return -1;

    if (base == 0)
    {
        return write(n);
    }
    else if (base == 10)
    {
        if (n < 0)
        {
            int t = print('-');
            n = -n;
            return _print_number(n, 10) + t;
        }
        return _print_number(n, 10);
    }
    else
    {
        return _print_number(n, base);
    }
}

size_t UARTManager::print(unsigned long n, int base)
{
    if (!_initialized)
        return -1;

    if (base == 0)
    {
        return write(n);
    }
    else
    {
        return _print_number(n, base);
    }
}

size_t UARTManager::println(const char *str)
{
    if (!_initialized)
        return -1;

    size_t n = print(str);
    n += println();
    return n;
}

size_t UARTManager::println(std::string str)
{
    if (!_initialized)
        return -1;

    size_t n = print(str);
    n += println();
    return n;
}

size_t UARTManager::println(int num, int base)
{
    if (!_initialized)
        return -1;

    size_t n = print(num, base);
    n += println();
    return n;
}

/*

size_t Print::print(const __FlashStringHelper *ifsh)
{
    return print(reinterpret_cast<const char *>(ifsh));
}


size_t Print::print(char c)
{
    return write(c);
}

size_t Print::print(unsigned char b, int base)
{
    return print((unsigned long) b, base);
}


size_t Print::print(double n, int digits)
{
    return printFloat(n, digits);
}

size_t Print::println(const __FlashStringHelper *ifsh)
{
    size_t n = print(ifsh);
    n += println();
    return n;
}

size_t Print::print(const Printable& x)
{
    return x.printTo(*this);
}

size_t Print::print(struct tm * timeinfo, const char * format)
{
    const char * f = format;
    if(!f){
        f = "%c";
    }
    char buf[64];
    size_t written = strftime(buf, 64, f, timeinfo);
    if(written == 0){
        return written;
    }
    return print(buf);
}

size_t Print::println(char c)
{
    size_t n = print(c);
    n += println();
    return n;
}

size_t Print::println(unsigned char b, int base)
{
    size_t n = print(b, base);
    n += println();
    return n;
}

size_t Print::println(unsigned int num, int base)
{
    size_t n = print(num, base);
    n += println();
    return n;
}

size_t Print::println(long num, int base)
{
    size_t n = print(num, base);
    n += println();
    return n;
}

size_t Print::println(unsigned long num, int base)
{
    size_t n = print(num, base);
    n += println();
    return n;
}

size_t Print::println(double num, int digits)
{
    size_t n = print(num, digits);
    n += println();
    return n;
}

size_t Print::println(const Printable& x)
{
    size_t n = print(x);
    n += println();
    return n;
}

size_t Print::println(struct tm * timeinfo, const char * format)
{
    size_t n = print(timeinfo, format);
    n += println();
    return n;
}
*/

#endif // ESP_PLATFORM

/* Basically a simplified copy of the ESP Arduino library in HardwareSerial.h/HardwareSerial.cpp
*/
#ifndef FNUART_H
#define FNUART_H

//#include "Stream.h"
#include <driver/uart.h>

class UARTManager
{
private:
    uart_port_t _uart_num;
    QueueHandle_t _uart_q;

    size_t _print_number(unsigned long n, uint8_t base);

public:
    UARTManager(uart_port_t uart_num);

    void begin(int baud);
    void end();
    void set_baudrate(uint32_t baud);

    int available();
    int peek();
    void flush();
    void flush_input();

    int read();
    size_t readBytes(uint8_t *buffer, size_t length);
    size_t readBytes(char *buffer, size_t length) { return readBytes((uint8_t *)buffer, length); };

    size_t write(uint8_t);
    size_t write(const uint8_t *buffer, size_t size);
    size_t write(const char *s);

    size_t write(unsigned long n) { return write((uint8_t)n); };
    size_t write(long n) { return write((uint8_t)n); };
    size_t write(unsigned int n) { return write((uint8_t)n); };
    size_t write(int n) { return write((uint8_t)n); };

    size_t printf(const char *format, ...);

    size_t println(const char *format, ...);
    size_t println() { return print("\r\n"); };
    size_t println(std::string str);
    size_t println(int num, int base = 10);

    size_t print(const char *format, ...);
    size_t print(std::string str);
    size_t print(int n, int base = 10);
    size_t print(unsigned int n, int base = 10);
    size_t print(long n, int base = 10);
    size_t print(unsigned long n, int base = 10);
};

// Only define these if the default Arduino global SerialX objects aren't declared
#ifdef NO_GLOBAL_SERIAL
extern UARTManager fnUartDebug;
extern UARTManager fnUartSIO;
#endif

#endif //FNUART_H

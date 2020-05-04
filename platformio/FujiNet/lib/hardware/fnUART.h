/* Basically a simplified copy of the ESP Arduino library in HardwareSerial.h/HardwareSerial.cpp
*/
#ifndef FNUART_H
#define FNUART_H


#include "Stream.h"
#include <driver/uart.h>

class UARTManager : public Stream
{
private:
    uart_port_t _uart_num;
    QueueHandle_t _uart_q;

public:
    UARTManager(uart_port_t uart_num);

    void begin(int baud);
    void end();
    void set_baudrate(uint32_t baud);

    int available() override;
    int peek() override;
    void flush() override;

    int read() override;
    size_t readBytes(uint8_t * buffer, size_t length) override;
    inline size_t readBytes(char * buffer, size_t length) override
    {
        return readBytes((uint8_t *)buffer, length);
    };

    size_t write(uint8_t) override;
    size_t write(const uint8_t *buffer, size_t size) override;

    inline size_t write(const char * s)
    {
        return write((uint8_t*) s, strlen(s));
    }
    inline size_t write(unsigned long n)
    {
        return write((uint8_t) n);
    }
    inline size_t write(long n)
    {
        return write((uint8_t) n);
    }
    inline size_t write(unsigned int n)
    {
        return write((uint8_t) n);
    }
    inline size_t write(int n)
    {
        return write((uint8_t) n);
    }

    // size_t printf(const char * format, ...) override;


    //uint32_t baudRate();
    //operator bool() const;
    //size_t setRxBufferSize(size_t);
    //void setDebugOutput(bool);

};

// Only define these if the default Arduino global SerialX objects aren't declared
//#ifdef NO_GLOBAL_SERIAL
extern UARTManager fnUartDebug;
extern UARTManager fnUartSIO;
//#endif

#endif //FNUART_H

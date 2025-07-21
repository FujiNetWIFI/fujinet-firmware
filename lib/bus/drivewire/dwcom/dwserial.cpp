#ifdef BUILD_COCO

#include "dwserial.h"

#include "../../include/debug.h"

void SerialDwPort::begin(int baud)
{
    Debug_printf("SerialDwPort: begin @ %d\n", baud);
    _uart.begin(FN_UART_BUS, SerialUARTConfig().baud(baud));
}

// read bytes into buffer
size_t SerialDwPort::read(uint8_t *buffer, size_t size)
{
    // for UARTManager there is separate read() and read(uint8_t *buffer, size_t size) ...
    // TODO is there any reason to do special call to UARTManager for single byte?
    if (size == 1)
    {
        int b = _uart.read(); // single byte from UART
        if (b < 0)
            return 0; // error, 0 bytes was read
        *buffer = b;
        return 1; // 1 byte was read
    }
    return _uart.read(buffer, size);
}

ssize_t SerialDwPort::write(const uint8_t *buffer, size_t size) 
{
    // for UART there is separate write(uint8_t b) and write(uint8_t *buffer, size_t size) ...
    // TODO is there any reason to do special call to UARTManager for single byte?
    if (size == 1)
    {
        return _uart.write(*buffer); // single byte to UART
    }
    return _uart.write(buffer, size);
}

#endif // BUILD_COCO

#ifndef SERIALINTERFACE_H
#define SERIALINTERFACE_H

#ifndef ESP_PLATFORM
#ifndef _WIN32
#define ITS_A_UNIX_SYSTEM_I_KNOW_THIS
#else /* _WIN32 */
#define HELLO_IM_A_PC
#endif /* _WIN32 */
#endif /* ESP_PLATFORM */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <string>

class SerialInterface
{
private:
    size_t _print_number(unsigned long n, uint8_t base);

protected:
    std::string _fifo;

    // Handled by SerialInterface, not implemented by subclass
    virtual size_t si_recv(void *buffer, size_t length);

    // Must be implemented by subclass
    virtual size_t si_send(const void *buffer, size_t length) = 0;
    virtual void update_fifo() = 0;

public:
    // begin() and arguments vary by subclass so not declared here
    virtual void end() = 0;

    virtual void flush() = 0;

    virtual uint32_t getBaudrate() = 0;
    virtual void setBaudrate(uint32_t baud) = 0;

    // Handled by SerialInterface, not implemented by subclass
    virtual size_t available();
    virtual void discardInput();

    /* Convenience methods, just wrappers for si_recv()/si_send() methods above */
    size_t read(void *buffer, size_t length);
    int read(void);

    size_t write(const void *buffer, size_t length);
    size_t write(uint8_t c);
    size_t write(const char *s);
    size_t write(unsigned long n);
    size_t write(long n);
    size_t write(unsigned int n);
    size_t write(int n);

    size_t printf(const char *format, ...);

    size_t println(const char *str);
    size_t println() { return print("\r\n"); };
    size_t println(std::string str);
    size_t println(int num, int base = 10);

    size_t print(const char *str);
    size_t print(const std::string &str);
    size_t print(int n, int base = 10);
    size_t print(unsigned int n, int base = 10);
    size_t print(long n, int base = 10);
    size_t print(unsigned long n, int base = 10);
};

#endif /* SERIALINTERFACE_H */

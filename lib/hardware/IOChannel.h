#ifndef IOCHANNEL_H
#define IOCHANNEL_H

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

#if defined(ESP_PLATFORM)
#include <esp_timer.h>
#define GET_TIMESTAMP() esp_timer_get_time()
#elif defined(ITS_A_UNIX_SYSTEM_I_KNOW_THIS)
#include <sys/time.h>
#define GET_TIMESTAMP() ({ struct timeval _tv; gettimeofday(&_tv, NULL); \
            _tv.tv_sec * ((uint64_t) 1000000) + _tv.tv_usec; })
#endif /* ESP_PLATFORM */

class IOChannel
{
private:
    size_t _print_number(unsigned long n, uint8_t base);

protected:
    std::string _fifo;
    uint32_t read_timeout_ms = 10;
    uint32_t discard_timeout_ms = 10;

    // Handled by IOChannel, not implemented by subclass
    size_t dataIn(void *buffer, size_t length);

    // Must be implemented by subclass
    virtual size_t dataOut(const void *buffer, size_t length) = 0;
    virtual void updateFIFO() = 0;

public:
    // begin() and arguments vary by subclass so not declared here
    virtual void end() = 0;

    virtual void flush() = 0;

    virtual uint32_t getBaudrate() = 0;
    virtual void setBaudrate(uint32_t baud) = 0;

    virtual bool getDTR() = 0;
    virtual void setDSR(bool state) = 0;
    virtual bool getRTS() = 0;
    virtual void setCTS(bool state) = 0;

    // Handled by IOChannel, not implemented by subclass
    size_t available();
    void discardInput();

    /* Convenience methods, just wrappers for dataIn()/dataOut() methods above */
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

#endif /* IOCHANNEL_H */

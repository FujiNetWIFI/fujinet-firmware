#ifndef IOCHANNEL_H
#define IOCHANNEL_H

#define IOCHANNEL_DEFAULT_TIMEOUT 100

#ifndef ESP_PLATFORM
#if defined(_WIN16) || defined(_WIN32) || defined(_WIN64) || defined(__WINDOWS__)
#define HELLO_IM_A_PC
#else /* ! (_WIN16 || _WIN32 || _WIN64 || __WINDOWS__ */
#define ITS_A_UNIX_SYSTEM_I_KNOW_THIS
#endif /* ! (_WIN16 || _WIN32 || _WIN64 || __WINDOWS__ */
#endif /* ESP_PLATFORM */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <string>

#ifdef ESP_PLATFORM
#include <esp_timer.h>
#define GET_TIMESTAMP() esp_timer_get_time()
#else /* ! ESP_PLATFORM */
#include <chrono>

inline uint64_t GET_TIMESTAMP() {
    auto now = std::chrono::steady_clock::now();
    auto micros =
        std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch());
    return micros.count();
}
#endif /* ESP_PLATFORM */

class IOChannel
{
private:
    size_t _print_number(unsigned long n, uint8_t base);

protected:
    std::string _fifo;
    uint32_t read_timeout_ms = IOCHANNEL_DEFAULT_TIMEOUT;
    uint32_t discard_timeout_ms = IOCHANNEL_DEFAULT_TIMEOUT;

    // Handled by IOChannel, not implemented by subclass
    size_t dataIn(void *buffer, size_t length);

    // Must be implemented by subclass
    virtual size_t dataOut(const void *buffer, size_t length) = 0;
    virtual void updateFIFO() = 0;

public:
    // begin() and arguments vary by subclass so not declared here
    virtual void end() = 0;

    virtual void flushOutput() = 0;

#ifdef RS232_THINGS
    virtual uint32_t getBaudrate() = 0;
    virtual void setBaudrate(uint32_t baud) = 0;

    virtual bool getDTR() = 0;
    virtual void setDSR(bool state) = 0;
    virtual bool getRTS() = 0;
    virtual void setCTS(bool state) = 0;
#endif /* RS232_THINGS */

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

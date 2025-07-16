#include "SerialInterface.h"

#include <stdarg.h>

size_t SerialInterface::read(void *buffer, size_t length)
{
    return recv(buffer, length);
}

int SerialInterface::read(void)
{
    uint8_t buf[1];
    int result = read(buf, 1);

    if (result < 1)
        return result;
    return buf[0];
}

size_t SerialInterface::write(const void *buffer, size_t length)
{
    return send(buffer, length);
}

size_t SerialInterface::write(uint8_t c)
{
    uint8_t buf[1];


    buf[0] = c;
    return write(buf, 1);
}

size_t SerialInterface::write(const char *s)
{
    return write(s, strlen(s));
}

size_t SerialInterface::write(unsigned long n)
{
    return write((uint8_t) n);
}

size_t SerialInterface::write(long n)
{
    return write((uint8_t) n);
}

size_t SerialInterface::write(unsigned int n)
{
    return write((uint8_t) n);
}

size_t SerialInterface::write(int n)
{
    return write((uint8_t) n);
}

size_t SerialInterface::printf(const char *fmt...)
{
    char *result = nullptr;
    va_list vargs;

    va_start(vargs, fmt);

    int z = vasprintf(&result, fmt, vargs);

    if (z > 0)
        write(result, z);

    va_end(vargs);

    if (result != nullptr)
        free(result);

    return z >= 0 ? z : 0;
}

size_t SerialInterface::_print_number(unsigned long n, uint8_t base)
{
    char buf[8 * sizeof(long) + 1]; // Assumes 8-bit chars plus zero byte.
    char *str = &buf[sizeof(buf) - 1];

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

size_t SerialInterface::print(const char *str)
{
    int z = strlen(str);

    return write(str, z);
    ;
}

size_t SerialInterface::print(const std::string &str)
{
    return print(str.c_str());
}

size_t SerialInterface::print(int n, int base)
{
    return print((long)n, base);
}

size_t SerialInterface::print(unsigned int n, int base)
{
    return print((unsigned long)n, base);
}

size_t SerialInterface::print(long n, int base)
{
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

size_t SerialInterface::print(unsigned long n, int base)
{
    if (base == 0)
    {
        return write(n);
    }
    else
    {
        return _print_number(n, base);
    }
}

size_t SerialInterface::println(const char *str)
{
    size_t n = print(str);
    n += println();
    return n;
}

size_t SerialInterface::println(std::string str)
{
    size_t n = print(str);
    n += println();
    return n;
}

size_t SerialInterface::println(int num, int base)
{
    size_t n = print(num, base);
    n += println();
    return n;
}


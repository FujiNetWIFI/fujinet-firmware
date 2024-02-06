#ifndef LOG_H_COMPATIBILITY
#define LOG_H_COMPATIBILITY

#include <cstdarg>
#include <cstdio>

// This is to provide a compatibility with SP over SLIP code copied over from AppleWin project
// and simply converts to vprintf

inline void LogFileOutput(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
}

#endif
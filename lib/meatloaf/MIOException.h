#ifndef MEATLOAF_EXCEPTION
#define MEATLOAF_EXCEPTION

#include <exception>

// truned off by default: https://github.com/platformio/platform-ststm32/issues/402

// PIO_FRAMEWORK_ARDUINO_ENABLE_EXCEPTIONS - whre do I set this?
// https://github.com/esp8266/Arduino/blob/master/tools/platformio-build.py

struct IOException : public std::exception {
   const char * what () const throw () {
      return "IO";
   }
};

struct IllegalStateException : public IOException {
   const char * what () const throw () {
      return "Illegal State";
   }
};

struct FileNotFoundException : public IOException {
   const char * what () const throw () {
      return "Not found";
   }
};


#endif // MEATLOAF_EXCEPTION
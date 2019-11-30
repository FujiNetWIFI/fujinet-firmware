#ifndef TNFS_H
#define TNFS_H

#include <Arduino.h>

#include <WiFiUdp.h>
#ifdef ESP_8266
#include <FS.h>
#elif defined(ESP_32)
#include <SPIFFS.h>
#endif

#define TNFS_SERVER "mozzwald.com"
#define TNFS_PORT 16384


class tnfsClient : public File

/* SPIFFS File defn

class File : public Stream
{
public:
    File(FileImplPtr p = FileImplPtr(), FS *baseFS = nullptr) : _p(p), _fakeDir(nullptr), _baseFS(baseFS) { }

    // Print methods:
    size_t write(uint8_t) override;
    size_t write(const uint8_t *buf, size_t size) override;

    // Stream methods:
    int available() override;
    int read() override;
    int peek() override;
    void flush() override;
    size_t readBytes(char *buffer, size_t length) override {
        return read((uint8_t*)buffer, length);
    }
    size_t read(uint8_t* buf, size_t size);
    bool seek(uint32_t pos, SeekMode mode);
    bool seek(uint32_t pos) {
        return seek(pos, SeekSet);
    }
    size_t position() const;
    size_t size() const;
    void close();
    operator bool() const;
    const char* name() const;
    const char* fullName() const; // Includes path
    bool truncate(uint32_t size);

    bool isFile() const;
    bool isDirectory() const;

    // Arduino "class SD" methods for compatibility
    template<typename T> size_t write(T &src){
      uint8_t obuf[256];
      size_t doneLen = 0;
      size_t sentLen;
      int i;

      while (src.available() > sizeof(obuf)){
        src.read(obuf, sizeof(obuf));
        sentLen = write(obuf, sizeof(obuf));
        doneLen = doneLen + sentLen;
        if(sentLen != sizeof(obuf)){
          return doneLen;
        }
      }

      size_t leftLen = src.available();
      src.read(obuf, leftLen);
      sentLen = write(obuf, leftLen);
      doneLen = doneLen + sentLen;
      return doneLen;
    }
    using Print::write;

    void rewindDirectory();
    File openNextFile();

    String readString() override;

    time_t getLastWrite();
    void setTimeCallback(time_t (*cb)(void));

protected:
    FileImplPtr _p;

    // Arduino SD class emulation
    std::shared_ptr<Dir> _fakeDir;
    FS                  *_baseFS;
    time_t (*timeCallback)(void) = nullptr;
};
*/

{
private:
  byte tnfs_fd;

  union {
    struct
    {
      byte session_idl;
      byte session_idh;
      byte retryCount;
      byte command;
      byte data[508];
    };
    byte rawData[512];
  } tnfsPacket;

  void tnfs_mount();
  void tnfs_open();
  void tnfs_read();

public:
 // tnfsClient(FileImplPtr p = nullptr, FS *baseFS = nullptr) : _p(nullptr), _fakeDir(nullptr), _baseFS(nullptr) { }
  void begin();

  // Print methods:
  size_t write(uint8_t) override;
  size_t write(const uint8_t *buf, size_t size) override;

  // Stream methods:
  int available() override;
  int read() override;
  size_t read(byte *buf, size_t size);
  int peek() override;
  void flush() override;
  size_t readBytes(char *buffer, size_t length) override
  {
    return read((uint8_t *)buffer, length);
  }

  //File methods:
  void seek(uint32_t offset);
  bool seek(uint32_t offset, SeekMode mode)
  {
    seek(offset);
    return true;
  }
  size_t position() const;
  size_t size() const;
  void close();
  operator bool() const;
  const char *name() const;
  const char *fullName() const; // Includes path
  bool truncate(uint32_t size);

  bool isFile() const;
  bool isDirectory() const;
};

#endif

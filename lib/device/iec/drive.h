#ifndef DRIVE_H
#define DRIVE_H

#include "../disk.h"
#include "../fuji/fujiHost.h"

#include <string>
#include <cstring>
#include <unordered_map>
#include <esp_rom_crc.h>

#include "../../bus/iec/IECFileDevice.h"
#include "../../media/media.h"
#include "../meatloaf/meatloaf.h"
#include "../meatloaf/meat_buffer.h"
#include "../meatloaf/wrappers/iec_buffer.h"
#include "../meatloaf/wrappers/directory_stream.h"
#include "utils.h"

#ifdef USE_VDRIVE
#include "../vdrive/VDriveClass.h"
#endif

//#include "dos/_dos.h"
//#include "dos/cbmdos.2.5.h"

#define PRODUCT_ID "MEATLOAF CBM"

class iecDrive;

class iecChannelHandler
{
 public:
  iecChannelHandler(iecDrive *drive);
  virtual ~iecChannelHandler();

  uint8_t read(uint8_t *data, uint8_t n);
  uint8_t write(uint8_t *data, uint8_t n);

  virtual uint8_t writeBufferData() = 0;
  virtual uint8_t readBufferData()  = 0;
  virtual MStream *getStream() { return nullptr; };

 protected:
  iecDrive *m_drive;
  uint8_t  *m_data;
  size_t    m_len, m_ptr;
};


class iecChannelHandlerFile : public iecChannelHandler
{
 public:
  iecChannelHandlerFile(iecDrive *drive, MStream *stream, int fixLoadAddress = -1);
  virtual ~iecChannelHandlerFile();

  virtual uint8_t readBufferData();
  virtual uint8_t writeBufferData();
  virtual MStream *getStream() override { return m_stream; };

 private:
  MStream  *m_stream;
  int       m_fixLoadAddress;
  uint32_t  m_byteCount;
  uint64_t  m_timeStart, m_transportTimeUS;
};


class iecChannelHandlerDir : public iecChannelHandler
{
 public:
  iecChannelHandlerDir(iecDrive *drive, MFile *dir);
  virtual ~iecChannelHandlerDir();

  virtual uint8_t readBufferData();
  virtual uint8_t writeBufferData();

 private:
  void addExtraInfo(std::string title, std::string text);

  MFile   *m_dir;
  uint8_t  m_headerLine;
  std::vector<std::string> m_headers;
};


class driveMemory
{
 private:
  // TODO: Utilize ESP32 HighMemory API to access unused 4MB of PSRAM
  std::vector<uint8_t> ram;         // 0000-07FF  RAM
  // uint8_t via1[1024] = { 0x00 }; // 1800-1BFF  6522 VIA1
  // uint8_t via2[1024] = { 0x00 }; // 1C00-1FFF  6522 VIA2
  std::unique_ptr<MStream> rom;     // C000-FFFF  ROM 16KB

 public:
   driveMemory(size_t ramSize = 2048) : ram(ramSize, 0x00) {
    setROM("dos1541"); // Default to 1541 ROM
  }
  ~driveMemory() = default;

  uint16_t mw_hash = 0;

  bool setRAM(size_t ramSize) {
    ram.resize(ramSize, 0x00);
    return true;
  }

  bool setROM(std::string filename) {
    // Check for ROM file in SD Card then Flash
    auto rom_file = MFSOwner::File("/sd/.rom/" + filename);
    if (rom_file == nullptr) {
      rom_file = MFSOwner::File("/.rom/" + filename);
    }
    if (rom_file == nullptr) {
      return false;
    }

    rom.reset(rom_file->getSourceStream());
    if (!rom) {
      return false;
    }
    Debug_printv("Drive ROM Loaded file[%s] stream[%s] size[%lu]", rom_file->url.c_str(), rom->url.c_str(), rom->size());
    return true;
  }

  size_t read(uint16_t addr, uint8_t *data, size_t len)
  {
    // RAM
    if ( addr < 0x0FFF )
    {
      if ( addr >= 0x0800 )
        addr -= 0x0800; // RAM Mirror

      if (addr + len > ram.size()) {
        // Handle bounds error
        return 0;
      }

      memcpy(data, &ram[addr], len);
      Debug_printv("RAM read %04X:%s", addr, mstr::toHex(data, len).c_str());
      printf("%s",util_hexdump((const uint8_t *)ram.data(), ram.size()).c_str());
      return len;
    }

    // ROM
    if ( rom )
    {
      if ( addr >= 0x8000 )
      {
        if ( addr >= 0xC000 )
          addr -= 0xC000;
        else if ( addr >= 0x8000 )
          addr -= 0x8000; // ROM Mirror

        rom->seek(addr, SEEK_SET);
        return rom->read(data, len);
      }
    }

    return 0;
  }

  void write(uint16_t addr, const uint8_t *data, size_t len)
  {
    // RAM
    if ( addr < 0x0FFF )
    {
      if ( addr >= 0x0800 )
        addr -= 0x0800; // RAM Mirror

      if (addr + len > ram.size()) {
        // Handle bounds error
        return;
      }
      memcpy(&ram[addr], data, len);
      Debug_printv("RAM write %04X:%s [%d]", addr, mstr::toHex(data, len).c_str(), len);
      printf("%s",util_hexdump((const uint8_t *)ram.data(), ram.size()).c_str());

      mw_hash = esp_rom_crc16_le(mw_hash, data, len);
    }
  }

  void execute(uint16_t addr)
  {
    // RAM
    if ( addr < 0x0FFF )
    {
      if ( addr >= 0x0800 )
        addr -= 0x0800; // RAM Mirror

      // ram + addr
      Debug_printv("RAM execute %04X", addr);
      mw_hash = 0;
    }

    // ROM
    if ( rom )
    {
      if ( addr >= 0x8000 )
      {
        if ( addr >= 0xC000 )
          addr -= 0xC000;
        else if ( addr >= 0x8000 )
          addr -= 0x8000; // ROM Mirror

        //rom->seek(addr, SEEK_SET);

        // Translate ROM functions to virtual drive functions
        Debug_printv("ROM execute %04X", addr);
      }
    }
  }

  void reset() {
    ram.assign(ram.size(), 0x00);
  }
};

class iecDrive : public IECFileDevice
{
 public:
  iecDrive(uint8_t devnum = 0xFF);
  ~iecDrive();

  mediatype_t mount(FILE *f, const char *filename, uint32_t disksize,
                    disk_access_flags_t access_mode,
                    mediatype_t disk_type = MEDIATYPE_UNKNOWN);
  void unmount();

  int     id() { return m_devnr; };
  uint8_t getNumOpenChannels();
  uint8_t getStatusCode() { return m_statusCode; }
  void    setStatusCode(uint8_t code, uint8_t trk = 0);
  bool    hasError();

  fujiHost *m_host;

  // overriding the IECDevice isActive() function because device_active
  // must be a global variable
  //bool device_active = true;
  //virtual bool isActive() { return device_active; }

  // needed for fujiDevice compatibility
  bool is_config_device = false;

 private:
  // open file "name" on channel
  virtual bool open(uint8_t channel, const char *name);

  // close file on channel
  virtual void close(uint8_t channel);

  // write bufferSize bytes to file on channel, returning the number of bytes written
  // Returning less than bufferSize signals "cannot receive more data" for this file
  virtual uint8_t write(uint8_t channel, uint8_t *buffer, uint8_t bufferSize, bool eoi);

  // read up to bufferSize bytes from file in channel, returning the number of bytes read
  // returning 0 will signal end-of-file to the receiver. Returning 0
  // for the FIRST call after open() signals an error condition
  // (e.g. C64 load command will show "file not found")
  virtual uint8_t read(uint8_t channel, uint8_t *buffer, uint8_t bufferSize, bool *eoi);

  // called when the bus master reads from channel 15 and the status
  // buffer is currently empty. this should populate buffer with an appropriate
  // status message bufferSize is the maximum allowed length of the message
  virtual void getStatus(char *buffer, uint8_t bufferSize);

  // called when the bus master sends data (i.e. a command) to channel 15
  // command is a 0-terminated string representing the command to execute
  // commandLen contains the full length of the received command (useful if
  // the command itself may contain zeros)
  virtual void execute(const char *command, uint8_t cmdLen);

  // called on falling edge of RESET line
  virtual void reset();

  void set_cwd(std::string path);

  std::unique_ptr<MFile> m_cwd;   // current working directory
  iecChannelHandler *m_channels[16];
  uint8_t m_statusCode, m_statusTrk, m_numOpenChannels;
#ifdef USE_VDRIVE
  VDrive   *m_vdrive;
#endif
  uint32_t  m_byteCount;
  uint64_t  m_timeStart;

  driveMemory m_memory;
};

#endif // DRIVE_H

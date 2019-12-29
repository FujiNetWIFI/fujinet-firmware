/**
   Test #27 - ATX
*/

#define TEST_NAME "#FujiNet ATX"

#ifdef ESP8266
#include <ESP8266WiFi.h>
#endif
#ifdef ESP32
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <SPIFFS.h>
#endif

#include <FS.h>

#include "atx.h"

enum {ID, COMMAND, AUX1, AUX2, CHECKSUM, ACK, NAK, PROCESS, WAIT} cmdState;

#ifdef ESP8266
#define SIO_UART Serial
#define BUG_UART Serial1
#define PIN_LED         2
#define PIN_INT         5
#define PIN_PROC        4
#define PIN_MTR        16
#define PIN_CMD        12
#endif
#ifdef ESP32
#define SIO_UART Serial2
#define BUG_UART Serial
#define PIN_LED1         2
#define PIN_LED2         4
#define PIN_INT         26
#define PIN_PROC        22
#define PIN_MTR         33
#define PIN_CMD         21
#endif

#define DELAY_T5          1500
#define READ_CMD_TIMEOUT  12
#define CMD_TIMEOUT       50

#define STATUS_SKIP       8

// Uncomment for Debug on 2nd UART (GPIO 2)
//#define DEBUG_S

// Uncomment for Debug on TCP/6502 to DEBUG_HOST
// Run:  `nc -vk -l 6502` on DEBUG_HOST
//#define DEBUG_N
//#define DEBUG_HOST "192.168.1.7"

/**
   A Single command frame, both in structured and unstructured
   form.
*/
union
{
  struct
  {
    unsigned char devic;
    unsigned char comnd;
    unsigned char aux1;
    unsigned char aux2;
    unsigned char cksum;
  };
  byte cmdFrameData[5];
} cmdFrame;

byte sector[256];
unsigned short sectorSize;
unsigned char statusByte = 0xFF;
unsigned long cmdTimer;
byte statusSkip;

File atx;
ATXFileHeader atxHeader;
ATXTrackInfo atxTrackInfo[MAX_TRACK];
unsigned short atxBytesPerSector;
unsigned char atxSectorsPerTrack;
unsigned short atxLastAngle;
unsigned char atxCurrentHeadTrack;
volatile unsigned short atxCurrentHeadPosition;
unsigned short last_angle_returned;

#ifdef ESP32
hw_timer_t* headTimer = NULL;
#endif

#ifdef DEBUG_N
WiFiClient wificlient;
#endif

#ifdef DEBUG_S
#define Debug_print(...) BUG_UART.print( __VA_ARGS__ )
#define Debug_printf(...) BUG_UART.printf( __VA_ARGS__ )
#define Debug_println(...) BUG_UART.println( __VA_ARGS__ )
#define DEBUG
#endif
#ifdef DEBUG_N
#define Debug_print(...) wificlient.print( __VA_ARGS__ )
#define Debug_printf(...) wificlient.printf( __VA_ARGS__ )
#define Debug_println(...) wificlient.println( __VA_ARGS__ )
#define DEBUG
#endif

/**
   calculate 8-bit checksum.
*/
byte sio_checksum(byte* chunk, int length)
{
  int chkSum = 0;
  for (int i = 0; i < length; i++) {
    chkSum = ((chkSum + chunk[i]) >> 8) + ((chkSum + chunk[i]) & 0xff);
  }
  return (byte)chkSum;
}


/**
   Update current head position, every 8 microseconds.
*/
void ICACHE_RAM_ATTR headISR()
{
  if (atxCurrentHeadPosition > 26042)
    atxCurrentHeadPosition = 0;
  else
    atxCurrentHeadPosition++;
#ifdef ESP8266
  timer1_write(clockCyclesPerMicrosecond() * 8);
#endif
  // The ESP32 auto-reloads the timer.
}

/**
   ISR for falling COMMAND
*/
void ICACHE_RAM_ATTR sio_isr_cmd()
{
  if (digitalRead(PIN_CMD) == LOW)
  {
    cmdState = ID;
    cmdTimer = millis();
#ifdef ESP32
    digitalWrite(PIN_LED2, LOW); // on
#endif
  }
}

/**
   Return true if valid device ID
*/
bool sio_valid_device_id()
{
  if (cmdFrame.devic == 0x31)
    return true;
  else
    return false;
}

/**
   Get ID
*/
void sio_get_id()
{
  while (!SIO_UART.available()) {
    delayMicroseconds(100);
  }
  cmdFrame.devic = SIO_UART.read();
  if (sio_valid_device_id())
    cmdState = COMMAND;
  else
  {
    cmdState = WAIT;
    cmdTimer = 0;
  }
#ifdef DEBUG
  Debug_print("CMD DEVC: ");
  Debug_println(cmdFrame.devic, HEX);
#endif
}

void sio_get_command()
{
  while (!SIO_UART.available()) {
    delayMicroseconds(100);
  }
  cmdFrame.comnd = SIO_UART.read();
  cmdState = AUX1;

#ifdef DEBUG
  Debug_print("CMD CMND: ");
  Debug_println(cmdFrame.comnd, HEX);
#endif
}

/**
   Get aux1
*/
void sio_get_aux1()
{
  while (!SIO_UART.available()) {
    delayMicroseconds(100);
  }
  cmdFrame.aux1 = SIO_UART.read();
  cmdState = AUX2;

#ifdef DEBUG
  Debug_print("CMD AUX1: ");
  Debug_println(cmdFrame.aux1, HEX);
#endif
}

/**
   Get aux2
*/
void sio_get_aux2()
{
  while (!SIO_UART.available()) {
    delayMicroseconds(100);
  }
  cmdFrame.aux2 = SIO_UART.read();
  cmdState = CHECKSUM;

#ifdef DEBUG
  Debug_print("CMD AUX2: ");
  Debug_println(cmdFrame.aux2, HEX);
#endif
}

/**
   Get Checksum, and compare
*/
void sio_get_checksum()
{
  byte ck;
  while (!SIO_UART.available()) {
    delayMicroseconds(100);
  }
  cmdFrame.cksum = SIO_UART.read();
  ck = sio_checksum((byte *)&cmdFrame.cmdFrameData, 4);

#ifdef DEBUG
  Debug_print("CMD CKSM: ");
  Debug_print(cmdFrame.cksum, HEX);
#endif

  if (ck == cmdFrame.cksum)
  {
#ifdef DEBUG
    Debug_println(", ACK");
#endif
    sio_ack();
  }
  else
  {
#ifdef DEBUG
    Debug_println(", NAK");
#endif
    sio_nak();
  }
}

/**
   Send an acknowledgement
*/
void sio_ack()
{
  delayMicroseconds(500);
  SIO_UART.write('A');
  SIO_UART.flush();
  //cmdState = PROCESS;
  sio_process();
}

/**
   Send a non-acknowledgement
*/
void sio_nak()
{
  delayMicroseconds(500);
  SIO_UART.write('N');
  SIO_UART.flush();
  cmdState = WAIT;
  cmdTimer = 0;
}

/**
   Read
*/
void sio_read()
{
  unsigned short reqSec = (256 * cmdFrame.aux2) + cmdFrame.aux1;
  unsigned short num_bytes;
  unsigned short sector_size;
  byte ck;

  num_bytes = atx_read(reqSec, sector, &sector_size, &statusByte);

#ifdef DEBUG
  Debug_printf("reqSec: %d, sector_size %d, statusByte: %02x\n", reqSec, sector_size, statusByte);
  for (int i = 0; i < num_bytes; i++)
  {
    Debug_printf("%02x ", sector[i]);
  }
  Debug_printf("\n");
#endif

  ck = sio_checksum((byte *)&sector, sector_size);

  delayMicroseconds(DELAY_T5); // t5 delay
  SIO_UART.write('C'); // Command always completes.
  SIO_UART.flush();
  delayMicroseconds(200);
  //delay(1);

  // Write data frame
  SIO_UART.write(sector, sector_size);

  // Write checksum
  SIO_UART.write(ck);
  SIO_UART.flush();
  delayMicroseconds(200);
}

/**
   Status
*/
void sio_status()
{
  byte status[4] = {0x10, 0xFF, 0xFE, 0x00};
  byte ck;

  status[1] = statusByte;
  ck = sio_checksum((byte *)&status, 4);

  delayMicroseconds(DELAY_T5); // t5 delay
  SIO_UART.write('C'); // Command always completes.
  SIO_UART.flush();
  delayMicroseconds(200);

  // Write data frame
  for (int i = 0; i < 4; i++)
    SIO_UART.write(status[i]);

  // Write checksum
  SIO_UART.write(ck);
  SIO_UART.flush();
  delayMicroseconds(200);
}

void sio_process()
{
  switch (cmdFrame.comnd)
  {
    case 'R':
      sio_read();
      break;
    case 'S':
      sio_status();
      break;
  }

  cmdState = WAIT;
  cmdTimer = 0;
}

void sio_incoming() {
  switch (cmdState)
  {
    case ID:
      sio_get_id();
      break;
    case COMMAND:
      sio_get_command();
      break;
    case AUX1:
      sio_get_aux1();
      break;
    case AUX2:
      sio_get_aux2();
      break;
    case CHECKSUM:
      sio_get_checksum();
      break;
    case ACK:
      sio_ack();
      break;
    case NAK:
      sio_nak();
      break;
    case PROCESS:
      sio_process();
      break;
    case WAIT:
      SIO_UART.read(); // Toss it for now
      cmdTimer = 0;
      break;
  }
}

/**
   ATX Open
*/
void atx_open()
{
  ATXTrackHeader trackHeader;
  unsigned long startOffset;

  // Get file header.
  atx.read((byte *)&atxHeader, sizeof(atxHeader));

  startOffset = atxHeader.startData;

  atxSectorsPerTrack = (atxHeader.density == 1 ? 18 : 26);
  atxBytesPerSector = (atxHeader.density == 1 ? 256 : 128);

  for (unsigned char t = 0; t < MAX_TRACK; t++)
  {
    atx.seek(startOffset, SeekSet);

    if (!atx.read((byte *)&trackHeader, sizeof(ATXTrackHeader)))
      break;

    atxTrackInfo[t].offset = startOffset;
    startOffset += trackHeader.size;

#ifdef DEBUG
    Debug_printf("T: %u - O: %lu\n", t, atxTrackInfo[t].offset);
#endif

  }

#ifdef DEBUG
  Debug_printf("Signature: %c%c%c%c\n", atxHeader.signature[0],
               atxHeader.signature[1],
               atxHeader.signature[2],
               atxHeader.signature[3]);

  Debug_printf("Version: %04x\n", atxHeader.version);
  Debug_printf("minVersion: %04x\n", atxHeader.minVersion);
  Debug_printf("Creator: %04x\n", atxHeader.creator);
  Debug_printf("CreatorVersion: %04x\n", atxHeader.creatorVersion);
  Debug_printf("Flags: %08x\n", atxHeader.flags);
  Debug_printf("ImageType: %04x\n", atxHeader.imageType);
  Debug_printf("Density: %02x\n", atxHeader.density);
  Debug_printf("imageId: %08x\n", atxHeader.imageId);
  Debug_printf("imageVersion: %04x\n", atxHeader.imageVersion);
  Debug_printf("startData: %lu\n", atxHeader.startData);
  Debug_printf("endData: %lu\n", atxHeader.endData);
  Debug_printf("atxSectorsPerTrack: %u\n", atxSectorsPerTrack);
  Debug_printf("atxBytesPerSector: %u\n", atxBytesPerSector);
#endif
}

/**
   Wait for specific angular position (in AUs)
*/
void atx_wait_for_angular_position(unsigned short pos)
{
  // if the position is less than the current timer, we need to wait for a rollover
  // to occur
  if (pos < atxCurrentHeadPosition)
  {
#ifdef DEBUG
    Debug_printf("Waiting for rollover.\n");
#endif
    while (atxCurrentHeadPosition > 0) {
      yield();
    }

    // Now wait for head position
#ifdef DEBUG
    Debug_printf("Waiting for pos: %d\n", pos);
#endif
    while (atxCurrentHeadPosition < pos) {
      yield();
    }
  }
}

/**
   Return the next angular position
*/
unsigned short atx_increment_angular_displacement(unsigned short start, unsigned short delta) {
  // increment an angular position by a delta taking a full rotation into consideration
  unsigned short ret = start + delta;
  if (ret > AU_FULL_ROTATION)
    ret -= AU_FULL_ROTATION;
  return ret;
}

unsigned short atx_read_sector_header(unsigned long currentFileOffset, ATXSectorHeader* sectorHeader)
{
  atx.seek(currentFileOffset, SeekSet);
  atx.read((byte *)sectorHeader, sizeof(ATXSectorHeader));
}

unsigned short atx_read(unsigned short num, byte* sector, unsigned short *sectorSize, unsigned char *status) {
  ATXTrackHeader trackHeader;
  ATXSectorListHeader slheader;
  ATXSectorHeader sectorHeader;
  ATXTrackChunk extSectorData;

  unsigned short i;
  unsigned short tgtSectorIndex = 0;         // the index of the target sector within the sector list
  unsigned long tgtSectorOffset = 0;        // the offset of the target sector data
  bool hasError = false;   // flag for drive status errors

  // local variables used for weak data handling
  unsigned char extendedDataRecords = 0;
  int16_t weakOffset = -1;

  // calculate track and relative sector number from the absolute sector number
  unsigned char tgtTrackNumber = (num - 1) / atxSectorsPerTrack + 1;
  unsigned char tgtSectorNumber = (num - 1) % atxSectorsPerTrack + 1;
  unsigned short sectorCount;
  
  // set initial status (in case the target sector is not found)
  *status = 0x10;
  // set the sector size
  *sectorSize = atxBytesPerSector;

  delay(5.22);

  // delay for track stepping if needed
  if (atxCurrentHeadTrack != tgtTrackNumber) {
    signed char diff;
    diff = tgtTrackNumber - atxCurrentHeadTrack;
    if (diff < 0) diff *= -1;
    // wait for each track (this is done in a loop since _delay_ms needs a compile-time constant)
    for (i = 0; i < diff; i++) {
      delay(12.41);
    }
    // delay for head settling
    delay(40);
  }

  // set new head track position
  atxCurrentHeadTrack = tgtTrackNumber;

  // sample current head position
  unsigned short headPosition = atxCurrentHeadPosition;

  // read the track header
  unsigned long currentFileOffset = atxTrackInfo[tgtTrackNumber - 1].offset;
  // exit, if track not present
  if (!currentFileOffset) {
    goto error;
  }
  //faccess_offset(FILE_ACCESS_READ, currentFileOffset, sizeof(struct atxTrackHeader));
  atx.seek(currentFileOffset, SeekSet);
  atx.read((byte *)&trackHeader, sizeof(ATXTrackHeader));
  sectorCount = trackHeader.sectorCount;

  // if there are no sectors in this track or the track number doesn't match, return error
  if (trackHeader.trackNumber == tgtTrackNumber - 1 && sectorCount) {
    // read the sector list header if there are sectors for this track
    currentFileOffset += trackHeader.headerSize;
    //faccess_offset(FILE_ACCESS_READ, currentFileOffset, sizeof(struct atxSectorListHeader));
    atx.seek(currentFileOffset, SeekSet);
    atx.read((byte *)&slheader, sizeof(ATXSectorListHeader));

    // sector list header is variable length, so skip any extra header bytes that may be present
    currentFileOffset += slheader.next - sectorCount * sizeof(ATXSectorHeader);

    int pTT = 0;
    int retries = 4;

    // if we are still below the maximum number of retries that would be performed by the drive firmware...
    unsigned long retryOffset = currentFileOffset;
    while (retries > 0) {
      retries--;
      currentFileOffset = retryOffset;
      // iterate through all sector headers to find the target sector
      for (i = 0; i < sectorCount; i++) {
        if (atx_read_sector_header(currentFileOffset, &sectorHeader)) {
          // if the sector is not flagged as missing and its number matches the one we're looking for...
          if (!(sectorHeader.status & MASK_FDC_MISSING) && sectorHeader.number == tgtSectorNumber) {
            // check if it's the next sector that the head would encounter angularly...
            int tt = sectorHeader.timev - headPosition;
            if (pTT == 0 || (tt > 0 && pTT < 0) || (tt > 0 && pTT > 0 && tt < pTT) || (tt < 0 && pTT < 0 && tt < pTT)) {
              pTT = tt;
              atxLastAngle = sectorHeader.timev;
              *status = sectorHeader.status;
              // On an Atari 810, we have to do some specific behavior
              // when a long sector is encountered (the lost data bit
              // is set):
              //   1. ATX images don't normally set the DRQ status bit
              //      because the behavior is different on 810 vs.
              //      1050 drives. In the case of the 810, the DRQ bit
              //      should be set.
              //   2. The 810 is "blind" to CRC errors on long sectors
              //      because it interrupts the FDC long before
              //      performing the CRC check.
              if (*status & MASK_FDC_DLOST) {
                *status |= 0x02;
              }
              // if the extended data flag is set, increment extended record count for later reading
              if (*status & MASK_EXTENDED_DATA) {
                extendedDataRecords++;
              }
              tgtSectorIndex = i;
              tgtSectorOffset = sectorHeader.data;
            }
          }
          currentFileOffset += sizeof(ATXSectorHeader);
        }
      }
      // if the sector status is bad, delay for a full disk rotation
      if (*status) {
        atx_wait_for_angular_position(atx_increment_angular_displacement(atxCurrentHeadPosition, AU_FULL_ROTATION));
        // otherwise, no need to retry
      } else {
        retries = 0;
      }
    }

    // store the last angle returned for the debugging window
    last_angle_returned = atxLastAngle;

    // if the status is bad, flag as error
    if (*status) {
      hasError = true;
    }

    // if an extended data record exists for this track, iterate through all track chunks to search
    // for those records (note that we stop looking for chunks when we hit the 8-byte terminator; length == 0)
    if (extendedDataRecords > 0) {
      currentFileOffset = atxTrackInfo[tgtTrackNumber - 1].offset + trackHeader.headerSize;
      do {
        // faccess_offset(FILE_ACCESS_READ, currentFileOffset, sizeof(struct atxTrackChunk));
        atx.seek(currentFileOffset,SeekSet);
        atx.read((byte *)&extSectorData,sizeof(ATXTrackChunk));
        if (extSectorData.size > 0) {
          // if the target sector has a weak data flag, grab the start weak offset within the sector data
          if (extSectorData.sectorIndex == tgtSectorIndex && extSectorData.type == 0x10) {
            weakOffset = extSectorData.data;
          }
          currentFileOffset += extSectorData.size;
        }
      } while (extSectorData.size > 0);
    }

    // read the data (re-using tgtSectorIndex variable here to reduce stack consumption)
    if (tgtSectorOffset) {
      atx.seek(atxTrackInfo[tgtTrackNumber-1].offset+tgtSectorOffset,SeekSet);
      tgtSectorIndex = (unsigned short) atx.read(sector, atxBytesPerSector);
      // tgtSectorIndex = (unsigned short) faccess_offset(FILE_ACCESS_READ, gTrackInfo[tgtTrackNumber - 1].offset + tgtSectorOffset, gBytesPerSector);
    }
    if (hasError) {
      tgtSectorIndex = 0;
    }

    // if a weak offset is defined, randomize the appropriate data
    if (weakOffset > -1) {
      for (i = (unsigned short) weakOffset; i < atxBytesPerSector; i++) {
        sector[i] = (unsigned char) (rand() % 256);
      }
    }

    // calculate rotational delay of sector seek
    unsigned short rotationDelay;
    if (atxLastAngle > headPosition) {
      rotationDelay = (atxLastAngle - headPosition);
    } else {
      rotationDelay = (AU_FULL_ROTATION - headPosition + atxLastAngle);
    }

    // determine the angular position we need to wait for by summing the head position, rotational delay and the number
    // of rotational units for a sector read. Then wait for the head to reach that position.
    // (Concern: can the SD card read take more time than the amount the disk would have rotated?)
    atx_wait_for_angular_position(atx_increment_angular_displacement(atx_increment_angular_displacement(headPosition, rotationDelay), AU_ONE_SECTOR_READ));

    // delay for CRC calculation
    delay(2);
  }

error:
  // the Atari expects an inverted FDC status byte
  *status = ~(*status);

  // return the number of bytes read
  return tgtSectorIndex;
}

void setup()
{
#ifdef DEBUG_S
  BUG_UART.begin(115200);
  Debug_println();
  Debug_println(TEST_NAME);
#endif
  SPIFFS.begin();
  atx = SPIFFS.open("/autorun.atx", "r+");
  atx_open();
  // Set up pins
  pinMode(PIN_INT, OUTPUT); // thanks AtariGeezer
  digitalWrite(PIN_INT, HIGH);
  pinMode(PIN_PROC, OUTPUT); // thanks AtariGeezer
  digitalWrite(PIN_PROC, HIGH);
  pinMode(PIN_MTR, INPUT);
  pinMode(PIN_CMD, INPUT);
#ifdef ESP8266
  pinMode(PIN_LED, INPUT);
  digitalWrite(PIN_LED, HIGH); // off
#elif defined(ESP32)
  pinMode(PIN_LED1, OUTPUT);
  pinMode(PIN_LED2, OUTPUT);
  digitalWrite(PIN_LED1, HIGH); // off
  digitalWrite(PIN_LED2, HIGH); // off
#endif

#ifdef DEBUG_N
  /* Get WiFi started, but don't wait for it otherwise SIO
     powered FujiNet fails to boot
  */
  WiFi.begin(DEBUG_SSID, DEBUG_PASSWORD);
#endif

  // Set up serial
  SIO_UART.begin(19200);
#ifdef ESP8266
  SIO_UART.swap();
#endif

  // Attach COMMAND interrupt.
  attachInterrupt(digitalPinToInterrupt(PIN_CMD), sio_isr_cmd, FALLING);
  cmdState = WAIT; // Start in wait state

  // Attach Head position timer
#ifdef ESP8266
  timer1_attachInterrupt(headISR);
  timer1_enable(TIM_DIV1, TIM_EDGE, TIM_LOOP);
  timer1_write(clockCyclesPerMicrosecond() * 8);
#endif
#ifdef ESP32
  headTimer = timerBegin(0, 80, true);
  timerAttachInterrupt(headTimer, &headISR, true);
  timerAlarmWrite(headTimer, 8, true);
  timerAlarmEnable(headTimer);
#endif
}

void loop()
{
#ifdef DEBUG_N
  /* Connect to debug server if we aren't and WiFi is connected */
  if ( !wificlient.connected() && WiFi.status() == WL_CONNECTED )
  {
    wificlient.connect(DEBUG_HOST, 6502);
    wificlient.println(TEST_NAME);
  }
#endif

  if (SIO_UART.available() > 0)
  {
    sio_incoming();
  }

  //  if ((millis() - cmdTimer > CMD_TIMEOUT) && (cmdState != WAIT))
  //  {
  //#ifdef DEBUG
  //    Debug_print("SIO CMD TIMEOUT: ");
  //    Debug_println(cmdState);
  //#endif
  //    cmdState = WAIT;
  //    cmdTimer = 0;
  //  }

#ifdef ESP32
  if (cmdState == WAIT && digitalRead(PIN_LED2) == LOW)
  {
    digitalWrite(PIN_LED2, HIGH); // Turn off SIO LED
  }
#endif
}

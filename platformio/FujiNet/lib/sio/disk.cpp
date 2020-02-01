#include "disk.h"

// Read
void sioDisk::sio_read()
{
  byte ck;
  int offset = (256 * cmdFrame.aux2) + cmdFrame.aux1;
  offset *= 128;
  offset -= 128;
  offset += 16;        // skip 16 byte ATR Header
  _file->seek(offset); //SeekSet is default
  _file->read(sector, 128);

  ck = sio_checksum((byte *)&sector, 128);
  delayMicroseconds(DELAY_T5); // t5 delay
  SIO_UART.write('C');         // Completed command
  SIO_UART.flush();

  // Write data frame
  SIO_UART.write(sector, 128);

  // Write data frame checksum
  SIO_UART.write(ck);
  SIO_UART.flush();
  delayMicroseconds(200);
#ifdef DEBUG_S
  BUG_UART.print("SIO READ OFFSET: ");
  BUG_UART.print(offset);
  BUG_UART.print(" - ");
  BUG_UART.println((offset + 128));
#endif
}

// write for W & P commands
void sioDisk::sio_write()
{
  byte ck;
  int offset = (256 * cmdFrame.aux2) + cmdFrame.aux1;
  offset *= 128;
  offset -= 128;
  offset += 16; // skip 16 byte ATR Header
  _file->seek(offset);

#ifdef DEBUG_S
  BUG_UART.printf("receiving 128b data frame from computer.\n");
#endif

  SIO_UART.readBytes(sector, 128);
  ck = SIO_UART.read(); // Read checksum
  //delayMicroseconds(350);
  SIO_UART.write('A'); // Write ACK

  if (ck == sio_checksum(sector, 128))
  {
    delayMicroseconds(DELAY_T5);
    SIO_UART.write('C');
    _file->write(sector, 128);
    yield();
  }
}

// Status
void sioDisk::sio_status()
{

//void sio_status()
//{
  byte status[4] = {0x10, 0xDF, 0xFE, 0x00};
  //byte deviceSlot = cmdFrame.devic - 0x31;

  if (sectorSize == 256)
  {
    status[0] |= 0x20;
  }

  // todo:
  // if (percomBlock[deviceSlot].sectors_per_trackL == 26)
  // {
  //   status[0] |= 0x80;
  // }

  sio_to_computer(status, sizeof(status), false); // command always completes.
//}

// old
  // byte status[4] = {0x00, 0xFF, 0xFE, 0x00};
  // byte ck;

  // ck = sio_checksum((byte *)&status, 4);

  // delayMicroseconds(DELAY_T5); // t5 delay
  // SIO_UART.write('C');         // Command always completes.
  // SIO_UART.flush();
  // delayMicroseconds(200);
  // //delay(1);

  // // Write data frame
  // for (int i = 0; i < 4; i++)
  //   SIO_UART.write(status[i]);

  // // Write checksum
  // SIO_UART.write(ck);
  // SIO_UART.flush();
  // delayMicroseconds(200);
}

// fake disk format
void sioDisk::sio_format()
{

//void sio_format()
//{
  //unsigned char deviceSlot = cmdFrame.devic - 0x31;

  // Populate bad sector map (no bad sectors)
  for (int i = 0; i < sectorSize; i++)
    sector[i] = 0;

  sector[0] = 0xFF; // no bad sectors.
  sector[1] = 0xFF;

  // Send to computer
  sio_to_computer((byte *)sector, sectorSize, false);
//}

  // old
  // byte ck;

  // for (int i = 0; i < 128; i++)
  //   sector[i] = 0;

  // sector[0] = 0xFF; // no bad sectors.
  // sector[1] = 0xFF;

  // ck = sio_checksum((byte *)&sector, 128);

  // delayMicroseconds(DELAY_T5); // t5 delay
  // SIO_UART.write('C');         // Completed command
  // SIO_UART.flush();

  // // Write data frame
  // SIO_UART.write(sector, 128);

  // // Write data frame checksum
  // SIO_UART.write(ck);
  // SIO_UART.flush();
  // delayMicroseconds(200);
#ifdef DEBUG_S
  BUG_UART.printf("We faked a format.\n");
#endif
}

// Process command
void sioDisk::sio_process()
{
  switch (cmdFrame.comnd)
  {
  case 'R':
    sio_ack();
    sio_read();
    break;
  case 'W':
  case 'P':
    sio_ack();
    sio_write();
    break;
  case 'S':
    sio_ack();
    sio_status();
    break;
  case '!':
    sio_ack();
    sio_format();
    break;
  default:
    sio_nak();
  }
  // cmdState = WAIT;
  //cmdTimer = 0;
}

// mount a disk file
void sioDisk::mount(File *f)
{
  _file = f;
}

#include "printer.h"


paper_t sioPrinter::getPaperType()
{
  return paperType;
}

// close flush output file
void sioPrinter::flushOutput()
{
  _file.flush();
  _file.seek(0);
}

void sioPrinter::resetOutput()
{
  _file.close();
  _file = _FS->open("/paper", "w+");
  if (_file)
  {
    Debug_println("Printer output file (re)opened");
  }
  else
  {
    Debug_println("Error opening printer file");
  }
}

// initialzie printer by creating an output file
void sioPrinter::initPrinter(FS *filesystem)
{
  _FS = filesystem;
  this->resetOutput();
}

// write for W commands
void sioPrinter::sio_write()
{
  byte n = 40;
  byte ck;

  memset(buffer, 0, n); // clear buffer

  /* 
  Auxiliary Byte 1 values per 400/800 OS Manual
  Normal   0x4E 'N'  40 chars
  Sideways 0x53 'S'  29 chars (820 sideways printing)
  Wide     0x57 'W'  "not supported"

  Atari 822 in graphics mode (SIO command 'P') 
           0x50 'L'  40 bytes
  as inferred from screen print program in operators manual

  Auxiliary Byte 2 for Atari 822 might be 0 or 1 in graphics mode
*/

  if (cmdFrame.aux1 == 'N' || cmdFrame.aux1 == 'L')
    n = 40;
  else if (cmdFrame.aux1 == 'S')
    n = 29;

  ck = sio_to_peripheral(buffer, n);

  if (ck == sio_checksum(buffer, n))
  {
    if (n == 29)
    { // reverse the buffer and replace EOL with space
      // needed for PDF sideways printing on A820
      byte temp[29];
      memcpy(temp, buffer, n);
      for (int i = 0; i < n; i++)
      {
        buffer[i] = temp[n - 1 - i];
        if (buffer[i] == EOL)
          buffer[i] = ' ';
      }
      buffer[n++] = EOL;
    }
    if (_pptr->process(buffer, n))
      sio_complete();
    else
    {
      sio_error();
    }
  }
  else
  {
    sio_error();
  }
}

// Status
void sioPrinter::sio_status()
{
  byte status[4];
  /*
  STATUS frame per the 400/800 OS ROM Manual
  Command Status
  Aux 1 Byte (typo says AUX2 byte)
  Timeout
  Unused

  OS ROM Manual continues on Command Status byte:
  bit 0 - invalid command frame
  bit 1 - invalid data frame
  bit 7 - intelligent controller (normally 0)

  STATUS frame per Atari 820 service manual
  The printer controller will return a data frame to the computer
  reflecting the status. The STATUS DATA frame is shown below:
  DONE/ERROR FLAG
  AUX. BYTE 1 from last WRITE COMMAND
  DATA WRITE TIMEOUT
  CHECKSUM
  The FLAG byte contains information relating to the most recent
  command prior to the status request and some controller constants.
  The DATA WRITE Timeout equals the maximum time to print a
  line of data assuming worst case controller produced Timeout
  delay. This Timeout is associated with printer timeout
  discussed earlier. 
*/

  status[0] = 0;
  status[1] = lastAux1;
  status[2] = 5;
  status[3] = 0;

  sio_to_computer(status, sizeof(status), false);
}

// Process command
void sioPrinter::sio_process()
{
  switch (cmdFrame.comnd)
  {
  case 'P': // 0x50 - needed by A822 for graphics mode printing
  case 'W': // 0x57
    lastAux1 = cmdFrame.aux1;
    sio_ack();
    sio_write();
    break;
  case 'S': // 0x53
    sio_ack();
    sio_status();
    break;
  default:
    sio_nak();
  }
  // cmdState = WAIT;
  //cmdTimer = 0;
}

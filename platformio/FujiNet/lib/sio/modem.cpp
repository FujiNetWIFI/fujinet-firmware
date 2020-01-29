#include "modem.h"

// Write for W commands
void sioModem::sio_write()
{
  // for now, just complete
  delayMicroseconds(DELAY_T5);
  SIO_UART.write('C');
#ifdef ESP32
  SIO_UART.flush();
#endif
}

// Status
void sioModem::sio_status()
{
  byte status[2] = {0x00, 0x0C};
  byte ck;

  ck = sio_checksum((byte *)&status, 2);

  delayMicroseconds(DELAY_T5); // t5 delay
  SIO_UART.write('C');         // Command always completes.
  SIO_UART.flush();
  delayMicroseconds(200);

  // Write data frame
  for (int i = 0; i < 2; i++)
    SIO_UART.write(status[i]);

  // Write checksum
  SIO_UART.write(ck);
  SIO_UART.flush();
  delayMicroseconds(200);
}

/**
 ** 850 Control Command

   DTR/RTS/XMT
  D7 Enable DTR (Data Terminal Ready) change
  D5 Enable RTS (Request To Send) change
  D1 Enable XMT (Transmit) change
      0 No change
      1 Change state
  D6 New DTR state (if D7 set)
  D4 New RTS state (if D5 set)
  D0 New XMT state (if D1 set)
      0 Negate / space
*/
void sioModem::sio_control()
{
  if (cmdFrame.aux1 & 0x02)
  {
    XMT = (cmdFrame.aux1 & 0x01 ? true : false);
  }

  if (cmdFrame.aux1 & 0x20)
  {
    RTS = (cmdFrame.aux1 & 0x10 ? true : false);
  }

  if (cmdFrame.aux1 & 0x80)
  {
    DTR = (cmdFrame.aux1 & 0x40 ? true : false);
  }

  delayMicroseconds(DELAY_T5); // t5 delay
  SIO_UART.write('C'); // Command complete
  SIO_UART.flush();
}

/**
   850 Configure Command
*/
void sioModem::sio_config()
{
  delayMicroseconds(DELAY_T5); // t5 delay
  SIO_UART.write('C'); // Command complete
  SIO_UART.flush();

  byte newBaud = 0x0F & cmdFrame.aux1; // Get baud rate
  //byte wordSize = 0x30 & cmdFrame.aux1; // Get word size
  //byte stopBit = (1 << 7) & cmdFrame.aux1; // Get stop bits, 0x80 = 2, 0 = 1

  switch (newBaud)
  {
    case 0x08:
      modemBaud = 300;
      break;
    case 0x09:
      modemBaud = 600;
      break;
    case 0xA:
      modemBaud = 1200;
      break;
    case 0x0B:
      modemBaud = 1800;
      break;
    case 0x0C:
      modemBaud = 2400;
      break;
    case 0x0D:
      modemBaud = 4800;
      break;
    case 0x0E:
      modemBaud = 9600;
      break;
    case 0x0F:
      modemBaud = 19200;
      break;
  }
}

/*
 Concurrent/Stream mode
*/
void sioModem::sio_stream()
{
  char response[] = {0x28, 0xA0, 0x00, 0xA0, 0x28, 0xA0, 0x00, 0xA0, 0x78}; // 19200
  byte ck;

  switch (modemBaud)
  {
    case 300:
      response[0] = response[4] = 0xA0;
      response[2] = response[6] = 0x0B;
      break;
    case 600:
      response[0] = response[4] = 0xCC;
      response[2] = response[6] = 0x05;
      break;
    case 1200:
      response[0] = response[4] = 0xE3;
      response[2] = response[6] = 0x02;
      break;
    case 1800:
      response[0] = response[4] = 0xEA;
      response[2] = response[6] = 0x01;
      break;
    case 2400:
      response[0] = response[4] = 0x6E;
      response[2] = response[6] = 0x01;
      break;
    case 4800:
      response[0] = response[4] = 0xB3;
      response[2] = response[6] = 0x00;
      break;
    case 9600:
      response[0] = response[4] = 0x56;
      response[2] = response[6] = 0x00;
      break;
    case 19200:
      response[0] = response[4] = 0x28;
      response[2] = response[6] = 0x00;
      break;
  }

  ck = sio_checksum((byte *)response, 9); // Get the CHECKSUM

  delayMicroseconds(DELAY_T5); // t5 delay
  SIO_UART.write('C'); // Completed command
  SIO_UART.flush();

  SIO_UART.write((byte *)response, 9); // Send the response
  SIO_UART.write(ck); // Write data frame checksum
  SIO_UART.flush();

  SIO_UART.updateBaudRate(modemBaud);
  modemActive = true;
}

// Process command
void sioModem::sio_process()
{
  switch (cmdFrame.comnd)
  {
    case '!': // $21, Relocator Download
      //sio_relocator();
      break;
    case '&': // $26, Handler download
      //sio_handler();
      break;
    case '?': // $3F, Type 1 Poll
      //sio_poll_1();
      break;
    case 'A': // $41, Control
      //sio_control();
      break;
    case 'B': // $42, Configure
      //sio_config();
      break;
    case 'S': // $53, Status
      sio_status();
      break;
    case 'W': // $57, Write
      sio_write();
      break;
    case 'X': // $58, Concurrent/Stream
      sio_stream();
      break;
  }
  cmdState = WAIT;
}

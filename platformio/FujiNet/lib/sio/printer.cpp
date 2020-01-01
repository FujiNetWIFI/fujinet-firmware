#include "printer.h"

// write for W & P commands
void sioPrinter::sio_write()
{
  byte ck;
  // int offset = (256 * cmdFrame.aux2) + cmdFrame.aux1;
  // offset *= 128;
  // offset -= 128;
  // offset += 16; // skip 16 byte ATR Header

#ifdef DEBUG_S
  BUG_UART.printf("receiving line print from computer:   ");
#endif

  SIO_UART.readBytes(buffer, 40);
  ck = SIO_UART.read(); // Read checksum
  //delayMicroseconds(350);
  SIO_UART.write('A'); // Write ACK

  if (ck == sio_checksum(buffer, 40))
  {
    delayMicroseconds(DELAY_T5);
    SIO_UART.write('C');
#ifdef DEBUG_S
    int i = 0;
    while (buffer[i] != 155)
    {
      BUG_UART.write(buffer[i]);
      i++;
    }
    BUG_UART.println("");
#endif
    yield();
  }
}

// Status
void sioPrinter::sio_status()
{
  byte status[4] = {0x01, 0x01, 0x01, 0x01};
  byte ck;

  ck = sio_checksum((byte *)&status, 4);

  delayMicroseconds(DELAY_T5); // t5 delay
  SIO_UART.write('C');         // Command always completes.
  SIO_UART.flush();
  delayMicroseconds(200);
  //delay(1);

  // Write data frame
  for (int i = 0; i < 4; i++)
    SIO_UART.write(status[i]);

  // Write checksum
  SIO_UART.write(ck);
  SIO_UART.flush();
  delayMicroseconds(200);
}

// Process command
void sioPrinter::sio_process()
{
  switch (cmdFrame.comnd)
  {
  case 'W':
    sio_write();
    break;
  case 'S':
    sio_status();
    break;
  }
  cmdState = WAIT;
  //cmdTimer = 0;
}

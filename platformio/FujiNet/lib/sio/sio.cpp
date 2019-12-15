#include "sio.h"

// ISR for falling COMMAND
volatile bool cmdFlag = false;
void ICACHE_RAM_ATTR sio_isr_cmd()
{
  cmdFlag = true;
}

// calculate 8-bit checksum.
byte sioDevice::sio_checksum(byte *chunk, int length)
{
  int chkSum = 0;
  for (int i = 0; i < length; i++)
  {
    chkSum = ((chkSum + chunk[i]) >> 8) + ((chkSum + chunk[i]) & 0xff);
  }
  return (byte)chkSum;
}

// Get ID
void sioDevice::sio_get_id()
{
  cmdFrame.devic = SIO_UART.read();
  if (cmdFrame.devic == _devnum)
    cmdState = COMMAND;
  else
  {
    cmdState = WAIT;
    cmdTimer = 0;
  }

#ifdef DEBUG_S
  BUG_UART.print("CMD DEVC: ");
  BUG_UART.println(cmdFrame.devic, HEX);
#endif
}

// Get Command
void sioDevice::sio_get_command()
{
  cmdFrame.comnd = SIO_UART.read();
  cmdState = AUX1;
#ifdef DEBUG_S
  BUG_UART.print("CMD CMND: ");
  BUG_UART.println(cmdFrame.comnd, HEX);
#endif
}

// Get aux1
void sioDevice::sio_get_aux1()
{
  cmdFrame.aux1 = SIO_UART.read();
  cmdState = AUX2;

#ifdef DEBUG_S
  BUG_UART.print("CMD AUX1: ");
  BUG_UART.println(cmdFrame.aux1, HEX);
#endif
}

// Get aux2
void sioDevice::sio_get_aux2()
{
  cmdFrame.aux2 = SIO_UART.read();
  cmdState = CHECKSUM;

#ifdef DEBUG_S
  BUG_UART.print("CMD AUX2: ");
  BUG_UART.println(cmdFrame.aux2, HEX);
#endif
}

// Send an acknowledgement
void sioDevice::sio_ack()
{
  delayMicroseconds(500);
  SIO_UART.write('A');
  SIO_UART.flush();
  //cmdState = PROCESS;
  sio_process();
}

// Send a non-acknowledgement
void sioDevice::sio_nak()
{
  delayMicroseconds(500);
  SIO_UART.write('N');
  SIO_UART.flush();
  cmdState = WAIT;
  cmdTimer = 0;
}

// Get Checksum, and compare
void sioDevice::sio_get_checksum()
{
  byte ck;
  cmdFrame.cksum = SIO_UART.read();
  ck = sio_checksum((byte *)&cmdFrame.cmdFrameData, 4);

#ifdef DEBUG_S
  BUG_UART.print("CMD CKSM: ");
  BUG_UART.print(cmdFrame.cksum, HEX);
#endif

  if (ck == cmdFrame.cksum)
  {
#ifdef DEBUG_S
    BUG_UART.println(", ACK");
#endif
    sio_ack();
  }
  else
  {
#ifdef DEBUG_S
    BUG_UART.println(", NAK");
#endif
    sio_nak();
  }
}

// state machine branching
void sioDevice::sio_incoming()
{
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

// periodically handle the sioDevice in the loop()
void sioDevice::handle()
{
  if (cmdFlag)
  {
    if (digitalRead(PIN_CMD) == LOW) // this check may not be necessary
    {
      cmdState = ID;
      cmdTimer = millis();
      cmdFlag = false;
    }
  }

  if (SIO_UART.available() > 0)
  {
    sio_incoming();
  }

  if (millis() - cmdTimer > CMD_TIMEOUT && cmdState != WAIT)
  {
#ifdef DEBUG_S
    BUG_UART.print("SIO CMD TIMEOUT: ");
    BUG_UART.println(cmdState);
#endif
    cmdState = WAIT;
    cmdTimer = 0;
  }
}

// setup SIO bus
void sioBus::setup()
{
  // Set up serial
  SIO_UART.begin(19200);
#ifdef ESP_8266
  SIO_UART.swap();
#endif

  pinMode(PIN_INT, INPUT_PULLUP);
  pinMode(PIN_PROC, INPUT_PULLUP);
  pinMode(PIN_MTR, INPUT_PULLDOWN);
  pinMode(PIN_CMD, INPUT_PULLUP);

  // Attach COMMAND interrupt.
  attachInterrupt(digitalPinToInterrupt(PIN_CMD), sio_isr_cmd, FALLING);
}

sioBus SIO;
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
//   cmdFrame.devic = SIO_UART.read();
//   if (cmdFrame.devic == _devnum)
//     cmdState = COMMAND;
//   else
//   {
//     cmdState = WAIT;
//     //cmdTimer = 0;
//   }

// #ifdef DEBUG_S
//   BUG_UART.print("CMD DEVC: ");
//   BUG_UART.println(cmdFrame.devic, HEX);
// #endif
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
  sio_process(); // why skip state machine update and just jump from here?
}

// Send a non-acknowledgement
void sioDevice::sio_nak()
{
  delayMicroseconds(500);
  SIO_UART.write('N');
  SIO_UART.flush();
  cmdState = WAIT;
  //cmdTimer = 0;
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
    //sio_get_id();
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
  case PROCESS: // state not sued
    sio_process();
    break;
  case WAIT:
    SIO_UART.read(); // Toss it for now
    //cmdTimer = 0;
    break;
  }
}

// periodically handle the sioDevice in the loop()
// how to capture command ID from the bus and hand off to the correct device? Right now ...
// when we detect the CMD_PIN active low, we set the state machine to ID, start the command timer,
// then if there's serial data, we go to sio_get_id() by way of sio_incoming().
// then we check the ID and if it's right, we jump to COMMAND state, return and move on.
// Also, during WAIT we toss UART data.
//
// so ...
//
// we move the CMD_PIN handling and sio_get_id() to the sioBus class, grab the ID, start the command timer,
// then search through the daisyChain for a matching ID. Once we find an ID, we set it's sioDevice cmdState to COMMAND.
// We change service() so it only reads the SIO_UART when cmdState != WAIT.
// or rather, only call sioDevice->service() when sioDevice->state() != WAIT.
// we never will call sio_incoming when there's a WAIT state.
// need to figure out reseting cmdTimer when state goes to WAIT or there's a NAK
// if no device is != WAIT, we toss the SIO_UART byte & set cmdTimer to 0.
// Maybe we have a BUSY state for the sioBus that's an OR of all the cmdState != WAIT.
//
// todo: cmdTimer to sioBus, assign cmdState after finding device ID
// when checking cmdState loop through devices?
// make *activeDev when found device ID. call activeDev->sio_incoming() != nullPtr. otherwise toss UART.read
// if activeDev->cmdState == WAIT then activeDev = mullPtr
//
// NEED TO GET device state machine out of the bus state machine
//
void sioBus::service()
{
  if (cmdFlag)
  {
    if (digitalRead(PIN_CMD) == LOW) // this check may not be necessary
    {
      device(0)->cmdState = ID;
      busState = ID;
      cmdTimer = millis();
      cmdFlag = false;
    }
  }

  if (SIO_UART.available() > 0)
  {
    if (busState == ID)
    {
      //device(0)->sio_incoming();
      device(0)->cmdFrame.devic = SIO_UART.read();
      if (device(0)->cmdFrame.devic == device(0)->_devnum)
      {
        device(0)->cmdState = COMMAND;
        busState = PROCESS;
      }
      else
      {
        device(0)->cmdState = WAIT;
        //cmdTimer = 0;
      }

#ifdef DEBUG_S
      BUG_UART.print("CMD DEVC: ");
      BUG_UART.println(device(0)->cmdFrame.devic, HEX);
#endif
    }
    else
    {
      device(0)->sio_incoming();
    }
  }

  if (millis() - cmdTimer > CMD_TIMEOUT && device(0)->cmdState != WAIT)
  {
#ifdef DEBUG_S
    BUG_UART.print("SIO CMD TIMEOUT: bus-");
    BUG_UART.print(busState);
    BUG_UART.print(" dev-");
    BUG_UART.println(device(0)->cmdState);
#endif
    device(0)->cmdState = WAIT;
    busState = WAIT;
    //cmdTimer = 0;
  }
  if (device(0)->cmdState == WAIT || busState == WAIT)
  {
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

void sioBus::addDevice(sioDevice *p, int N) //, String name)
{
  p->_devnum = N;
  //p->_devname = name;
  daisyChain.add(p);
}

int sioBus::numDevices()
{
  return daisyChain.size();
}

sioDevice *sioBus::device(int i)
{
  return daisyChain.get(i);
}

sioBus SIO;
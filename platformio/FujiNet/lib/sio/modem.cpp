#include "modem.h"

// Write for W commands
void sioModem::sio_write()
{
  // for now, just complete
  sio_complete();
}

// Status
void sioModem::sio_status()
{
  byte status[2] = {0x00, 0x0C};
  sio_to_computer(status, sizeof(status), false);
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

  // for now, just complete
  sio_complete();
}

/**
   850 Configure Command
*/
void sioModem::sio_config()
{
  sio_complete(); // complete and then set newbaud

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

  sio_to_computer((byte *)response, sizeof(response), false);
#ifndef ESP32
  SIO_UART.flush();
#endif

  SIO_UART.updateBaudRate(modemBaud);
  modemActive = true;
#ifdef DEBUG
  Debug_print("MODEM ACTIVE @"); Debug_println(modemBaud);
#endif
}

/**
   replacement println for AT that is CR/EOL aware
*/
void sioModem::at_cmd_println(const char* s)
{
  SIO_UART.print(s);

  if (cmdAtascii == true)
  {
    SIO_UART.write(0x9B);
  }
  else
  {
    SIO_UART.write(0x0D);
    SIO_UART.write(0x0A);
  }
}

void sioModem::at_cmd_println(long int i)
{
  SIO_UART.print(i);

  if (cmdAtascii == true)
  {
    SIO_UART.write(0x9B);
  }
  else
  {
    SIO_UART.write(0x0D);
    SIO_UART.write(0x0A);
  }
}

void sioModem::at_cmd_println(String s)
{
  SIO_UART.print(s);

  if (cmdAtascii == true)
  {
    SIO_UART.write(0x9B);
  }
  else
  {
    SIO_UART.write(0x0D);
    SIO_UART.write(0x0A);
  }
}

void sioModem::at_cmd_println(IPAddress ipa)
{
  SIO_UART.print(ipa);

  if (cmdAtascii == true)
  {
    SIO_UART.write(0x9B);
  }
  else
  {
    SIO_UART.write(0x0D);
    SIO_UART.write(0x0A);
  }
}

/*
   Perform a command given in AT Modem command mode
*/
void sioModem::modemCommand()
{
  cmd.trim();
  if (cmd == "") return;
  at_cmd_println("");
  String upCmd = cmd;
  upCmd.toUpperCase();

  long newBps = 0;

  // Replace EOL with CR.
  if (upCmd.indexOf(0x9b) != 0)
    upCmd[upCmd.indexOf(0x9b)] = 0x0D;

  /**** Just AT ****/
  if (upCmd == "AT") at_cmd_println("OK");

  /**** Dial to host ****/
  else if ((upCmd.indexOf("ATDT") == 0) || (upCmd.indexOf("ATDP") == 0) || (upCmd.indexOf("ATDI") == 0))
  {
    int portIndex = cmd.indexOf(":");
    String host, port;
    if (portIndex != -1)
    {
      host = cmd.substring(4, portIndex);
      port = cmd.substring(portIndex + 1, cmd.length());
    }
    else
    {
      host = cmd.substring(4, cmd.length());
      port = "23"; // Telnet default
    }
#ifdef DEBUG
    Debug_print("DIALING: ");
    Debug_println(host);
#endif
    if (host == "5551234") // Fake it for BobTerm
    {
      delay(1300); // Wait a moment so bobterm catches it
      SIO_UART.print("CONNECT ");
      at_cmd_println(modemBaud);
#ifdef ESP32
      SIO_UART.flush();
#endif
#ifdef DEBUG
      Debug_println("CONNECT FAKE!");
#endif
    }
    else
    {
      SIO_UART.print("Connecting to ");
      SIO_UART.print(host);
      SIO_UART.print(":");
      at_cmd_println(port);
      char *hostChr = new char[host.length() + 1];
      host.toCharArray(hostChr, host.length() + 1);
      int portInt = port.toInt();
      tcpClient.setNoDelay(true); // Try to disable naggle
      if (tcpClient.connect(hostChr, portInt))
      {
        tcpClient.setNoDelay(true); // Try to disable naggle
        SIO_UART.print("CONNECT ");
        at_cmd_println(modemBaud);
        cmdMode = false;
#ifdef ESP32
        SIO_UART.flush();
#endif
        if (listenPort > 0) tcpServer.stop();
      }
      else
      {
        at_cmd_println("NO CARRIER");
      }
      delete hostChr;
    }
  }

  /**** Connect to WIFI ****/
  else if (upCmd.indexOf("ATWIFI") == 0)
  {
    int keyIndex = cmd.indexOf(",");
    String ssid, key;
    if (keyIndex != -1)
    {
      ssid = cmd.substring(6, keyIndex);
      key = cmd.substring(keyIndex + 1, cmd.length());
    }
    else
    {
      ssid = cmd.substring(6, cmd.length());
      key = "";
    }
    char *ssidChr = new char[ssid.length() + 1];
    ssid.toCharArray(ssidChr, ssid.length() + 1);
    char *keyChr = new char[key.length() + 1];
    key.toCharArray(keyChr, key.length() + 1);
    SIO_UART.print("Connecting to ");
    SIO_UART.print(ssid);
    SIO_UART.print("/");
    at_cmd_println(key);
    WiFi.begin(ssidChr, keyChr);
    for (int i = 0; i < 100; i++)
    {
      delay(100);
      if (WiFi.status() == WL_CONNECTED)
      {
        at_cmd_println("OK");
        break;
      }
    }
    if (WiFi.status() != WL_CONNECTED)
    {
      at_cmd_println("ERROR");
    }
    delete ssidChr;
    delete keyChr;
  }

  /**** Change baud rate from default ****/
  else if (upCmd == "AT300") newBps = 300;
  else if (upCmd == "AT1200") newBps = 1200;
  else if (upCmd == "AT2400") newBps = 2400;
  else if (upCmd == "AT4800") newBps = 4800;
  else if (upCmd == "AT9600") newBps = 9600;
  else if (upCmd == "AT19200") newBps = 19200;
  else if (upCmd == "AT38400") newBps = 38400;
  else if (upCmd == "AT57600") newBps = 57600;
  else if (upCmd == "AT115200") newBps = 115200;

  /**** Change telnet mode ****/
  else if (upCmd == "ATNET0")
  {
    telnet = false;
    at_cmd_println("OK");
  }
  else if (upCmd == "ATNET1")
  {
    telnet = true;
    at_cmd_println("OK");
  }

  /**** Answer to incoming connection ****/
  else if ((upCmd == "ATA") && tcpServer.hasClient())
  {
    tcpClient = tcpServer.available();
    tcpClient.setNoDelay(true); // try to disable naggle
    tcpServer.stop();
    SIO_UART.print("CONNECT ");
    at_cmd_println(modemBaud);
    cmdMode = false;
#ifdef ESP32
    SIO_UART.flush();
#endif
  }

  /**** See my IP address ****/
  else if (upCmd == "ATIP")
  {
    at_cmd_println(WiFi.localIP());
    at_cmd_println("OK");
  }

  /**** Print Help ****/
  else if (upCmd == "AT?")
  {
    at_cmd_println("       FujiNet Virtual Modem 850");
    at_cmd_println("=======================================");
    at_cmd_println("");
    at_cmd_println("ATWIFI<ssid>,<key> | Connect to WIFI");
    //at_cmd_println("AT<baud>           | Change Baud Rate");
    at_cmd_println("ATDT<host>:<port>  | Connect by TCP");
    at_cmd_println("ATIP               | See my IP address");
    at_cmd_println("ATNET0             | Disable telnet");
    at_cmd_println("                   | command handling");
    at_cmd_println("ATPORT<port>       | Set listening port");
    at_cmd_println("ATGET<URL>         | HTTP GET");
    at_cmd_println("");
    if (listenPort > 0)
    {
      SIO_UART.print("Listening to connections on port ");
      at_cmd_println(listenPort);
      at_cmd_println("which result in RING and you can");
      at_cmd_println("answer with ATA.");
      tcpServer.begin(listenPort);
    }
    else
    {
      at_cmd_println("Incoming connections are disabled.");
    }
    at_cmd_println("");
    at_cmd_println("OK");
  }

  /**** HTTP GET request ****/
  else if (upCmd.indexOf("ATGET") == 0)
  {
    // From the URL, aquire required variables
    // (12 = "ATGEThttp://")
    int portIndex = cmd.indexOf(":", 12); // Index where port number might begin
    int pathIndex = cmd.indexOf("/", 12); // Index first host name and possible port ends and path begins
    int port;
    String path, host;
    if (pathIndex < 0)
    {
      pathIndex = cmd.length();
    }
    if (portIndex < 0)
    {
      port = 80;
      portIndex = pathIndex;
    }
    else
    {
      port = cmd.substring(portIndex + 1, pathIndex).toInt();
    }
    host = cmd.substring(12, portIndex);
    path = cmd.substring(pathIndex, cmd.length());
    if (path == "") path = "/";
    char *hostChr = new char[host.length() + 1];
    host.toCharArray(hostChr, host.length() + 1);

    // Debug
    SIO_UART.print("Getting path ");
    SIO_UART.print(path);
    SIO_UART.print(" from port ");
    SIO_UART.print(port);
    SIO_UART.print(" of host ");
    SIO_UART.print(host);
    at_cmd_println("...");

    // Establish connection
    if (!tcpClient.connect(hostChr, port))
    {
      at_cmd_println("NO CARRIER");
    }
    else
    {
      SIO_UART.print("CONNECT ");
      at_cmd_println(modemBaud);
      cmdMode = false;

      // Send a HTTP request before continuing the connection as usual
      String request = "GET ";
      request += path;
      request += " HTTP/1.1\r\nHost: ";
      request += host;
      request += "\r\nConnection: close\r\n\r\n";
      tcpClient.print(request);
    }
    delete hostChr;
  }

  /**** Set Listening Port ****/
  else if (upCmd.indexOf("ATPORT") == 0)
  {
    long port;
    port = cmd.substring(6).toInt();
    if (port > 65535 || port < 0)
    {
      at_cmd_println("ERROR");
    }
    else
    {
      listenPort = port;
      tcpServer.begin(listenPort);
      at_cmd_println("OK");
    }
  }

  /**** Unknown command ****/
  else at_cmd_println("ERROR");

  /**** Tasks to do after command has been parsed ****/
  if (newBps)
  {
    at_cmd_println("OK");
    delay(150); // Sleep enough for 4 bytes at any previous baud rate to finish ("\nOK\n")
    SIO_UART.updateBaudRate(newBps);
    modemBaud = newBps;
  }

  cmd = "";
}

/*
  Handle incoming & outgoing data for modem
*/
void sioModem::sio_handle_modem()
{
  /**** AT command mode ****/
  if (cmdMode == true)
  {
    // In command mode but new unanswered incoming connection on server listen socket
    if ((listenPort > 0) && (tcpServer.hasClient()))
    {
      // Print RING every now and then while the new incoming connection exists
      if ((millis() - lastRingMs) > RING_INTERVAL)
      {
        at_cmd_println("RING");
        lastRingMs = millis();
      }
    }

    // In command mode - don't exchange with TCP but gather characters to a string
    if (SIO_UART.available() || blockWritePending == true)
    {
      char chr = SIO_UART.read();

      if ((blockWritePending == true) && (*blockPtr != 0x00))
        chr = *blockPtr++;
      else if (blockWritePending == true)
      {
        blockWritePending = false;
        yield();
        return;
      }

      // Return, enter, new line, carriage return.. anything goes to end the command
      if ((chr == '\n') || (chr == '\r') || (chr == 0x9B))
      {
#ifdef DEBUG
        Debug_print(cmd);
        Debug_println(" | CR");
#endif
        // flip which EOL to display based on last CR or EOL received.
        if (chr == 0x9B)
        {
          cmdAtascii = true;
        }
        else
          cmdAtascii = false;

        modemCommand();
      }
      // Backspace or delete deletes previous character
      else if ((chr == 8) || (chr == 127))
      {
        cmd.remove(cmd.length() - 1);
        // We don't assume that backspace is destructive
        // Clear with a space
        SIO_UART.write(8);
        SIO_UART.write(' ');
        SIO_UART.write(8);
      }
      else if (chr == 0x7E)
      {
        // ATASCII backspace
        cmd.remove(cmd.length() - 1);
        SIO_UART.write(0x7E); // we can assume ATASCII BS is destructive.
      }
      else
      {
        if (cmd.length() < MAX_CMD_LENGTH) cmd.concat(chr);
        SIO_UART.print(chr);
      }
    }
  }
  /**** Connected mode ****/
  else
  {
    // Transmit from terminal to TCP
    if (SIO_UART.available() || blockWritePending == true)
    {
      //led_on();

      // In telnet in worst case we have to escape every byte
      // so leave half of the buffer always free
      int max_buf_size;
      if (telnet == true)
        max_buf_size = TX_BUF_SIZE / 2;
      else
        max_buf_size = TX_BUF_SIZE;

      // Read from serial, the amount available up to
      // maximum size of the buffer
      size_t len = std::min(SIO_UART.available(), max_buf_size);

      if (blockWritePending == true)
      {
        //memcpy(&txBuf, &sector, 64);
        blockWritePending = false;
      }
      else
      {
        SIO_UART.readBytes(&txBuf[0], len);
      }

      // Disconnect if going to AT mode with "+++" sequence
      for (int i = 0; i < (int)len; i++)
      {
        if (txBuf[i] == '+') plusCount++; else plusCount = 0;
        if (plusCount >= 3)
        {
          plusTime = millis();
        }
        if (txBuf[i] != '+')
        {
          plusCount = 0;
        }
      }

      // Double (escape) every 0xff for telnet, shifting the following bytes
      // towards the end of the buffer from that point
      if (telnet == true)
      {
        for (int i = len - 1; i >= 0; i--)
        {
          if (txBuf[i] == 0xff)
          {
            for (int j = TX_BUF_SIZE - 1; j > i; j--)
            {
              txBuf[j] = txBuf[j - 1];
            }
            len++;
          }
        }
      }

      // Write the buffer to TCP finally
      tcpClient.write(&txBuf[0], len);
      // tcpClient.flush();
      yield();
    }

    // Transmit from TCP to terminal
    if (tcpClient.available())
    {
      //led_on();
      char buf[128];
      int avail = tcpClient.available();
      int i;

      if (avail > 128)
      {
        tcpClient.readBytes(buf, 128);
        for (i = 0; i < 128; i++)
          SIO_UART.write(buf[i]);
      }
      else
      {
        tcpClient.readBytes(buf, avail);
        for (i = 0; i < avail; i++)
          SIO_UART.write(buf[i]);
      }
    }
  }

  // If we have received "+++" as last bytes from serial port and there
  // has been over a second without any more bytes, disconnect
  if (plusCount >= 3)
  {
    if (millis() - plusTime > 1000)
    {
      tcpClient.stop();
      plusCount = 0;
    }
  }

  // Go to command mode if TCP disconnected and not in command mode
  if ((tcpClient.connected()) && (cmdMode == false) && (DTR == 0))
  {
    tcpClient.flush();
    tcpClient.stop();
    cmdMode = true;
    at_cmd_println("NO CARRIER");
    if (listenPort > 0) tcpServer.begin();
  }
  else if ((!tcpClient.connected()) && (cmdMode == false))
  {
    cmdMode = true;
    at_cmd_println("NO CARRIER");
    if (listenPort > 0) tcpServer.begin();
  }
}

/*
  Process command
*/
void sioModem::sio_process()
{
  switch (cmdFrame.comnd)
  {
  // TODO: put in sio_ack() for valid commands
  case '!': // $21, Relocator Download
    //sio_ack();
    //sio_relocator();
    break;
  case '&': // $26, Handler download
    //sio_ack();
    //sio_handler();
    break;
  case '?': // $3F, Type 1 Poll
    //sio_ack();
    //sio_poll_1();
    break;
  case 'A': // $41, Control
    sio_ack();
    sio_control();
    break;
  case 'B': // $42, Configure
    sio_ack();
    sio_config();
    break;
  case 'S': // $53, Status
    sio_ack();
    sio_status();
    break;
  case 'W': // $57, Write
    sio_ack();
    sio_write();
    break;
  case 'X': // $58, Concurrent/Stream
    sio_ack();
    sio_stream();
    break;
  default:
    sio_nak();
  }
}

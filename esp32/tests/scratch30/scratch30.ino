/**
   Test #30 - start over.
*/

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

/**
   Setup
*/
void setup()
{
  SIO_UART.begin(19200);
  BUG_UART.begin(115200);

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

  BUG_UART.printf("Test #30 - scratch\n");
}

/**
   Loop
*/
void loop()
{
  int a;
  if (digitalRead(PIN_CMD) == LOW)
  {
    delayMicroseconds(650); // computer is waiting for us to notice.

    // read cmd frame
    SIO_UART.readBytes(cmdFrame.cmdFrameData, 5);

    BUG_UART.printf("CMD DEVIC: %02x\nCMD COMND: %02x\nCMD AUX1: %02x\nCMD AUX2: %02x\nCMD CKSUM: %02x\n\n",cmdFrame.devic,cmdFrame.comnd,cmdFrame.aux1,cmdFrame.aux2,cmdFrame.cksum);

    while (digitalRead(PIN_CMD)==LOW)
      yield();

    BUG_UART.printf("CMD HI\n\n");
  }
  else
  {
    a = SIO_UART.available();
    if (a)
    {
      BUG_UART.printf("%d excess bytes.\n", a);
      while (SIO_UART.available())
      {
        BUG_UART.printf(".");
        SIO_UART.read(); // dump it.
      }
      BUG_UART.printf("\n\n");
    }
  }
}

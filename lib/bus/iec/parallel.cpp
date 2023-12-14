
#ifdef PARALLEL_BUS

#include "parallel.h"

#include <freertos/queue.h>
#include <freertos/task.h>

/* Dependencies */
#include "gpiox.h" // Required for PCF8575/PCA9674/MCP23017

#include "iec.h"
#include "../../include/pinmap.h"
#include "../../include/debug.h"

parallelBus PARALLEL;

//I2C_t& myI2C = i2c0;  // i2c0 and i2c1 are the default objects

static QueueHandle_t ml_parallel_evt_queue = NULL;

static void IRAM_ATTR ml_parallel_isr_handler(void* arg)
{
    // Generic default interrupt handler
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(ml_parallel_evt_queue, &gpio_num, NULL);
}

static void ml_parallel_intr_task(void* arg)
{
    uint32_t gpio_num;

    while ( true ) 
    {
        if(xQueueReceive(ml_parallel_evt_queue, &gpio_num, portMAX_DELAY)) 
        {
            PARALLEL.service();
        }

        //taskYIELD();
    }
}

void parallelBus::service()
{
    PARALLEL.bus_state = PBUS_IDLE;

    Debug_printv( "User Port Data Interrupt Received!" );

    //Debug_printv("bus_state[%d]", IEC.bus_state);
    if ( IEC.bus_state > BUS_OFFLINE ) // Is C64 is powered on?
    {
        // Update flags and data
        PARALLEL.readByte();
        Debug_printv("receive <<< " BYTE_TO_BINARY_PATTERN " (%0.2d) " BYTE_TO_BINARY_PATTERN " (%0.2d)", BYTE_TO_BINARY(PARALLEL.flags), PARALLEL.flags, BYTE_TO_BINARY(PARALLEL.data), PARALLEL.data);

        // If PC2 is set then parallel is active and a byte is ready to be read!
        if ( PARALLEL.status( PC2 ) )
        {
            PARALLEL.bus_state = PBUS_PROCESS;
            //Debug_printv("receive <<< " BYTE_TO_BINARY_PATTERN " (%0.2d) " BYTE_TO_BINARY_PATTERN " (%0.2d)", BYTE_TO_BINARY(PARALLEL.flags), PARALLEL.flags, BYTE_TO_BINARY(PARALLEL.data), PARALLEL.data);
        }

        // // Set RECEIVE/SEND mode   
        // if ( PARALLEL.status( PA2 ) )
        // {
        //     PARALLEL.mode = MODE_RECEIVE;
        //     GPIOX.portMode( USERPORT_DATA, GPIOX_MODE_INPUT );
        //     PARALLEL.readByte();

        //     Debug_printv("receive <<< " BYTE_TO_BINARY_PATTERN " (%0.2d) " BYTE_TO_BINARY_PATTERN " (%0.2d)", BYTE_TO_BINARY(PARALLEL.flags), PARALLEL.flags, BYTE_TO_BINARY(PARALLEL.data), PARALLEL.data);

        //     // // DolphinDOS Detection
        //     // if ( PARALLEL.status( ATN ) )
        //     // {
        //     //     if ( IEC.data.secondary == IEC_OPEN || IEC.data.secondary == IEC_REOPEN )
        //     //     {
        //     //         IEC.protocol->flags xor_eq PARALLEL_ACTIVE;
        //     //         Debug_printv("dolphindos");
        //     //     }
        //     // }

        //     // // WIC64
        //     // if ( PARALLEL.status( PC2 ) )
        //     // {
        //     //     if ( PARALLEL.data == 0x57 ) // WiC64 commands start with 'W'
        //     //     {
        //     //         IEC.protocol->flags xor_eq WIC64_ACTIVE;
        //     //         Debug_printv("wic64");                  
        //     //     }
        //     // }
        // }
        // else
        // {
        //     PARALLEL.mode = MODE_SEND;
        //     GPIOX.portMode( USERPORT_DATA, GPIOX_MODE_OUTPUT );

        //     Debug_printv("send    >>> " BYTE_TO_BINARY_PATTERN " (%0.2d) " BYTE_TO_BINARY_PATTERN " (%0.2d)", BYTE_TO_BINARY(PARALLEL.flags), PARALLEL.flags, BYTE_TO_BINARY(PARALLEL.data), PARALLEL.data);
        // }
    }
    else
    {
        PARALLEL.reset();
    }
}

void parallelBus::setup ()
{
    // Setup i2c device
    GPIOX.begin();
    reset();
    
    // Create a queue to handle parallel event from ISR
    ml_parallel_evt_queue = xQueueCreate(10, sizeof(uint32_t));

    // Start task
    //xTaskCreate(ml_parallel_intr_task, "ml_parallel_intr_task", 2048, NULL, 10, NULL);
    xTaskCreatePinnedToCore(ml_parallel_intr_task, "ml_parallel_intr_task", 4096, NULL, 10, NULL, 0);

    // Setup interrupt for paralellel port
    gpio_config_t io_conf = {
        .pin_bit_mask = ( 1ULL << PIN_GPIOX_INT ),    // bit mask of the pins that you want to set
        .mode = GPIO_MODE_INPUT,                      // set as input mode
        .pull_up_en = GPIO_PULLUP_DISABLE,            // disable pull-up mode
        .pull_down_en = GPIO_PULLDOWN_DISABLE,        // disable pull-down mode
        .intr_type = GPIO_INTR_NEGEDGE                // interrupt of falling edge
    };

    //configure GPIO with the given settings
    gpio_config(&io_conf);
    gpio_isr_handler_add((gpio_num_t)PIN_GPIOX_INT, ml_parallel_isr_handler, NULL);
}

void parallelBus::reset()
{
    // Reset default pin modes
    // Debug_printv("clear");
    // GPIOX.clear();
    // Debug_printv("pa2");
    // GPIOX.pinMode( PA2, GPIOX_MODE_INPUT );
    // Debug_printv("pc2");
    // GPIOX.pinMode( PC2, GPIOX_MODE_INPUT );
    // Debug_printv("flag2");
    // GPIOX.pinMode( FLAG2, GPIOX_MODE_OUTPUT );

    //Debug_printv("reset! bus_state[%d]", IEC.bus_state);

    //Debug_printv("userport flags");
    GPIOX.portMode( USERPORT_FLAGS, 0x05 ); // Set PA2 & PC2 to INPUT
    GPIOX.digitalWrite( FLAG2, HIGH);

    //Debug_printv("userport data");
    setMode( MODE_RECEIVE );
}

void parallelBus::setMode(parallel_mode_t mode)
{
    if ( mode == MODE_RECEIVE )
        GPIOX.portMode( USERPORT_DATA, GPIOX_MODE_INPUT );
    else
        GPIOX.portMode( USERPORT_DATA, GPIOX_MODE_OUTPUT );
}

void parallelBus::handShake()
{
    // Signal received or sent
    
    // LOW
    GPIOX.digitalWrite( FLAG2, LOW );
    
    // HIGH
    GPIOX.digitalWrite( FLAG2, HIGH );
}

uint8_t parallelBus::readByte()
{

    this->data = GPIOX.read( USERPORT_DATA );
    this->flags = GPIOX.PORT0;

    //Debug_printv("flags[%.2x] data[%.2x]", this->flags, this->data);

    // Acknowledge byte received
    this->handShake();

    return this->data;
}

void parallelBus::writeByte( uint8_t byte )
{
    this->data = byte;

    Debug_printv("flags[%.2x] data[%.2x] byte[%.2x]", this->flags, this->data, byte);
    GPIOX.write( USERPORT_DATA, byte);

    // Tell receiver byte is ready to read
    this->handShake();
}

bool parallelBus::status( user_port_pin_t pin )
{
    if ( pin < 8 ) 
        return ( this->flags & ( 1 >> pin) );
    
    return ( this->data & ( 1 >> ( pin - 8) ) );
}



void wic64_command()
{
    // if (lastinput.startsWith("W")) // Commando startet mit W = Richtig
    // {
    //     if (lastinput.charAt(3) == 1)
    //     {
    //         ex = true;
    //         displaystuff("loading http");
    //         loader(lastinput);

    //         if (errorcode != "")
    //         {
    //             sendmessage(errorcode);
    //         }
    //     }

    //     if (lastinput.charAt(3) == 2)
    //     {
    //         ex = true;
    //         displaystuff("config wifi");
    //         httpstring = lastinput;
    //         sendmessage(setwlan());
    //         delay(3000);
    //         displaystuff("config changed");
    //     }

    //     if (lastinput.charAt(3) == 3)
    //     {
    //         ex = true; // Normal SW update - no debug messages on serial
    //         displaystuff("FW update 1");
    //         handleUpdate();
    //     }

    //     if (lastinput.charAt(3) == 4)
    //     {
    //         ex = true; // Developer SW update - debug output to serial
    //         displaystuff("FW update 2");
    //         handleDeveloper();
    //     }

    //     if (lastinput.charAt(3) == 5)
    //     {
    //         ex = true; // Developer SW update - debug output to serial
    //         displaystuff("FW update 3");
    //         handleDeveloper2();
    //     }

    //     if (lastinput.charAt(3) == 6)
    //     {
    //         ex = true;
    //         displaystuff("get ip");
    //         String ipaddress = WiFi.localIP().toString();
    //         sendmessage(ipaddress);
    //     }

    //     if (lastinput.charAt(3) == 7)
    //     {
    //         ex = true;
    //         displaystuff("get stats");
    //         String stats = __DATE__ " " __TIME__;
    //         sendmessage(stats);
    //     }

    //     if (lastinput.charAt(3) == 8)
    //     {
    //         ex = true;
    //         displaystuff("set server");
    //         lastinput.remove(0, 4);
    //         setserver = lastinput;
    //         preferences.putString("server", lastinput);
    //     }

    //     if (lastinput.charAt(3) == 9)
    //     {
    //         ex = true; // REM Send messages to debug console.
    //         displaystuff("REM");
    //         Serial.println(lastinput);
    //     }

    //     if (lastinput.charAt(3) == 10)
    //     {
    //         ex = true; // Get UDP data and return them to c64
    //         displaystuff("get upd");
    //         sendmessage(getudpmsg());
    //     }

    //     if (lastinput.charAt(3) == 11)
    //     {
    //         ex = true; // Send UDP data to IP
    //         displaystuff("send udp");
    //         sendudpmsg(lastinput);
    //     }

    //     if (lastinput.charAt(3) == 12)
    //     {
    //         ex = true; // wlan scanner
    //         displaystuff("scanning wlan");
    //         sendmessage(getWLAN());
    //     }

    //     if (lastinput.charAt(3) == 13)
    //     {
    //         ex = true; // wlan setup via scanlist
    //         displaystuff("config wifi id");
    //         httpstring = lastinput;
    //         sendmessage(setWLAN_list());
    //         displaystuff("config wifi set");
    //     }

    //     if (lastinput.charAt(3) == 14)
    //     {
    //         ex = true;
    //         displaystuff("change udp port");
    //         httpstring = lastinput;
    //         startudpport();
    //     }

    //     if (lastinput.charAt(3) == 15)
    //     {
    //         ex = true; // Chatserver string decoding
    //         displaystuff("loading httpchat");
    //         loader(lastinput);

    //         if (errorcode != "")
    //         {
    //             sendmessage(errorcode);
    //         }
    //     }

    //     if (lastinput.charAt(3) == 16)
    //     {
    //         ex = true;
    //         displaystuff("get ssid");
    //         sendmessage(WiFi.SSID());
    //     }

    //     if (lastinput.charAt(3) == 17)
    //     {
    //         ex = true;
    //         displaystuff("get rssi");
    //         sendmessage(String(WiFi.RSSI()));
    //     }

    //     if (lastinput.charAt(3) == 18)
    //     {
    //         ex = true;
    //         displaystuff("get server");

    //         if (setserver != "")
    //         {
    //             sendmessage(setserver);
    //         }
    //         else
    //         {
    //             sendmessage("no server set");
    //         }
    //     }

    //     if (lastinput.charAt(3) == 19)
    //     {
    //         ex = true; // XXXX 4 bytes header for padding !
    //         displaystuff("get external ip");
    //         loader("XXXXhttp://sk.sx-64.de/wic64/ip.php");

    //         if (errorcode != "")
    //         {
    //             sendmessage(errorcode);
    //         }
    //     }

    //     if (lastinput.charAt(3) == 20)
    //     {
    //         ex = true;
    //         displaystuff("get mac");
    //         sendmessage(WiFi.macAddress());
    //     }

    //     if (lastinput.charAt(3) == 30)
    //     {
    //         ex = true; // Get TCP data and return them to c64 INCOMPLETE
    //         displaystuff("get tcp");
    //         getudpmsg();

    //         if (errorcode != "")
    //         {
    //             sendmessage(errorcode);
    //         }
    //     }

    //     if (lastinput.charAt(3) == 31)
    //     {
    //         ex = true; // Get TCP data and return them to c64 INCOMPLETE
    //         displaystuff("send tcp");
    //         sendudpmsg(lastinput);
    //         sendmessage("");
    //         log_i("tcp send %s", lastinput);
    //     }

    //     if (lastinput.charAt(3) == 32)
    //     {
    //         ex = true;
    //         displaystuff("set tcp port");
    //         httpstring = lastinput;
    //         settcpport();
    //     }

    //     if (lastinput.charAt(3) == 99)
    //     {
    //         ex = true;
    //         displaystuff("factory reset");
    //         WiFi.begin("-", "-");
    //         WiFi.disconnect(true);
    //         preferences.putString("server", defaultserver);
    //         display.clearDisplay();
    //         delay(3000);
    //         ESP.restart();
    //     }
    // }
}

#endif // PARALLEL_BUS
#ifdef BUILD_IEC

#include "iec.h"

#include <cstring>
#include <memory>

#include "soc/io_mux_reg.h"
#include "driver/gpio.h"
#include "hal/gpio_hal.h"

#include "../../include/debug.h"
#include "../../include/pinmap.h"
#include "../../include/cbm_defines.h"
#include "led.h"
#include "protocol/cpbstandardserial.h"
#include "string_utils.h"
#include "utils.h"

#define MAIN_STACKSIZE 4096
#define MAIN_PRIORITY 20
#define MAIN_CPUAFFINITY 1

using namespace Protocol;

systemBus IEC;

static void IRAM_ATTR cbm_on_atn_isr_handler(void *arg)
{
    systemBus *b = (systemBus *)arg;

    //b->pull(PIN_IEC_SRQ);

    // Go to listener mode and get command
    b->release(PIN_IEC_CLK_OUT);
    b->pull(PIN_IEC_DATA_OUT);

    b->flags = CLEAR;
    b->flags |= ATN_PULLED;
    b->state = BUS_ACTIVE;

    //b->release(PIN_IEC_SRQ);
}

static void IRAM_ATTR cbm_on_clk_isr_handler(void *arg)
{
    systemBus *b = (systemBus *)arg;

    //b->pull(PIN_IEC_SRQ);

    // get bit
    b->byte >>= 1;
    if ( !b->status ( PIN_IEC_DATA_IN ) ) b->byte |= 0x80;
    b->bit++;

    //b->release(PIN_IEC_SRQ);
}

/**
 * Static callback function for the interrupt rate limiting timer. It sets the interruptProceed
 * flag to true. This is set to false when the interrupt is serviced.
 */
static void onTimer(void *info)
{
    systemBus *parent = (systemBus *)info;
    //portENTER_CRITICAL_ISR(&parent->timerMux);
    parent->interruptSRQ = !parent->interruptSRQ;
    //portEXIT_CRITICAL_ISR(&parent->timerMux);
}

static void ml_iec_intr_task(void* arg)
{
    while ( true ) 
    {
        if ( IEC.enabled )
            IEC.service();

        if ( IEC.state < BUS_ACTIVE )
            taskYIELD();
    }
}

void systemBus::init_gpio(gpio_num_t _pin)
{
    PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[_pin], PIN_FUNC_GPIO);
    gpio_set_direction(_pin, GPIO_MODE_INPUT);
    gpio_pullup_en(_pin);
    gpio_set_pull_mode(_pin, GPIO_PULLUP_ONLY);
    gpio_set_level(_pin, LOW);
    return;
}

// true => PULL => LOW
void IRAM_ATTR systemBus::pull ( uint8_t _pin )
{
    int _reg = GPIO_ENABLE_REG;
    if (_pin > 31) 
    { 
        _reg = GPIO_ENABLE1_REG, _pin -= 32;
    } 
    REG_SET_BIT(_reg, 1ULL << _pin); // GPIO_MODE_OUTPUT
}

// false => RELEASE => HIGH
void IRAM_ATTR systemBus::release ( uint8_t _pin )
{
    int _reg = GPIO_ENABLE_REG;
    if (_pin > 31) 
    { 
        _reg = GPIO_ENABLE1_REG, _pin -= 32;
    } 
    REG_CLR_BIT(_reg, 1ULL << _pin); // GPIO_MODE_INPUT
}

bool IRAM_ATTR systemBus::status ( uint8_t _pin )
{
    int _reg = GPIO_IN_REG;
    if (_pin > 31) 
    { 
        _reg = GPIO_IN1_REG, _pin -= 32;
    } 
    return (REG_READ(_reg) & BIT(_pin)) ? RELEASED : PULLED;
}

bool IRAM_ATTR systemBus::status ()
{
    // uint64_t pin_states;
    // pin_states = REG_READ(GPIO_IN1_REG);
    // pin_states << 32 & REG_READ(GPIO_IN_REG);
    // pin_atn = pin_states & BIT(PIN_IEC_ATN);
    // pin_clk = pin_states & BIT(PIN_IEC_CLK_IN);
    // pin_data = pin_states & BIT(PIN_IEC_DATA_IN);
    // pin_srq = pin_states & BIT(PIN_IEC_SRQ);
    // pin_reset = pin_states & BIT(PIN_IEC_RESET);
    return true;
};



// uint8_t IRAM_ATTR systemBus::status()
// {
//     int data = 0;

//     // gpio_config_t io_config =
//     // {
//     //     .pin_bit_mask = BIT(PIN_IEC_CLK_IN) | BIT(PIN_IEC_CLK_OUT) |BIT(PIN_IEC_DATA_IN) | BIT(PIN_IEC_DATA_OUT) | BIT(PIN_IEC_SRQ) | BIT(PIN_IEC_RESET),
//     //     .mode = GPIO_MODE_INPUT,
//     //     .pull_up_en = GPIO_PULLUP_ENABLE,
//     //     .intr_type = GPIO_INTR_DISABLE,
//     // };

//     // ESP_ERROR_CHECK(gpio_config(&io_config));
//     uint64_t io_data = REG_READ(GPIO_IN_REG);

//     //data |= (0 & (io_data & (1 << PIN_IEC_ATN)));
//     data |= (1 & (io_data & (1 << PIN_IEC_CLK_IN)));
//     data |= (2 & (io_data & (1 << PIN_IEC_DATA_IN)));
//     data |= (3 & (io_data & (1 << PIN_IEC_SRQ)));
//     data |= (4 & (io_data & (1 << PIN_IEC_RESET)));

//     return data;
// }

void systemBus::setup()
{
    Debug_printf("IEC systemBus::setup()\r\n");

    flags = CLEAR;
    protocol = selectProtocol();

    // initial pin modes in GPIO
    init_gpio(PIN_IEC_ATN);
    init_gpio(PIN_IEC_CLK_IN);
    init_gpio(PIN_IEC_CLK_OUT);
    init_gpio(PIN_IEC_DATA_IN);
    init_gpio(PIN_IEC_DATA_OUT);
    init_gpio(PIN_IEC_SRQ);
#ifdef IEC_HAS_RESET
    init_gpio(PIN_IEC_RESET);
#endif

    // Start task
//    xTaskCreatePinnedToCore(ml_iec_intr_task, "ml_iec_intr_task", 4096, NULL, 20, NULL, 1);

    // Setup interrupt for ATN
    gpio_config_t io_conf = {
        .pin_bit_mask = ( 1ULL << PIN_IEC_ATN ),    // bit mask of the pins that you want to set
        .mode = GPIO_MODE_INPUT,                    // set as input mode
        .pull_up_en = GPIO_PULLUP_DISABLE,          // disable pull-up mode
        .pull_down_en = GPIO_PULLDOWN_DISABLE,      // disable pull-down mode
        .intr_type = GPIO_INTR_NEGEDGE              // interrupt of falling edge
    };
    gpio_config(&io_conf);
    gpio_isr_handler_add((gpio_num_t)PIN_IEC_ATN, cbm_on_atn_isr_handler, this);

    // Setup interrupt config for CLK
    io_conf = {
        .pin_bit_mask = ( 1ULL << PIN_IEC_CLK_IN ),    // bit mask of the pins that you want to set
        .mode = GPIO_MODE_INPUT,                    // set as input mode
        .pull_up_en = GPIO_PULLUP_DISABLE,          // disable pull-up mode
        .pull_down_en = GPIO_PULLDOWN_DISABLE,      // disable pull-down mode
        .intr_type = GPIO_INTR_POSEDGE              // interrupt of rising edge
    };
    gpio_config(&io_conf);
    gpio_isr_handler_add((gpio_num_t)PIN_IEC_CLK_IN, cbm_on_clk_isr_handler, this);

    // Start SRQ timer service
    timer_start();
}


void IRAM_ATTR systemBus::service()
{
    // pull( PIN_IEC_SRQ );

    // Disable Interrupt
    // gpio_intr_disable((gpio_num_t)PIN_IEC_ATN);

    if (state < BUS_ACTIVE)
    {
        // debugTiming();

        // Handle SRQ for devices
        for (auto devicep : _daisyChain)
        {
            for (unsigned char i=0;i<16;i++)
                devicep->poll_interrupt(i);
        }

        return;
    }

#ifdef IEC_HAS_RESET
    // Check if CBM is sending a reset (setting the RESET line high). This is typically
    // when the CBM is reset itself. In this case, we are supposed to reset all states to initial.
    bool pin_reset = status(PIN_IEC_RESET);
    if (pin_reset == PULLED)
    {
        if (status(PIN_IEC_ATN) == PULLED)
        {
            // If RESET & ATN are both PULLED then CBM is off
            state = BUS_OFFLINE;
            // gpio_intr_enable((gpio_num_t)PIN_IEC_ATN);
            return;
        }

        //Debug_printf("IEC Reset! reset[%d]\r\n", pin_reset);
        data.init(); // Clear bus data
        releaseLines();
        state = BUS_IDLE;
        //Debug_printv("bus init");

        // Reset virtual devices
        reset_all_our_devices();
        // gpio_intr_enable((gpio_num_t)PIN_IEC_ATN);
        return;
    }
#endif

    // Command or Data Mode
    do
    {
        if (state == BUS_ACTIVE)
        {
            //pull ( PIN_IEC_SRQ );

            // *** IMPORTANT! This helps keep us in sync!
            // Sometimes the C64 pulls ATN but doesn't pull CLOCK right away
            protocol->timeoutWait ( PIN_IEC_CLK_IN, PULLED, TIMEOUT_ATNCLK, false );

            // Read bus command bytes
            //Debug_printv("command");
            read_command();

            //release ( PIN_IEC_SRQ );
        }

        if (state == BUS_PROCESS)
        {
            // Reset bit/byte
            bit = 0;
            byte = 0;

            //Debug_printv("data");
            //pull ( PIN_IEC_SRQ );

            // Data Mode - Get Command or Data
            if (data.primary == IEC_LISTEN)
            {
                //Debug_printv("calling deviceListen()\r\n");
                //pull ( PIN_IEC_SRQ );
                deviceListen();
                //release ( PIN_IEC_SRQ );
            }
            else if (data.primary == IEC_TALK)
            {
                //Debug_printv("calling deviceTalk()\r\n");
                //pull ( PIN_IEC_SRQ );
                deviceTalk();
                //release ( PIN_IEC_SRQ );
                Debug_printf(" (%.2X %s  %.2d CHANNEL)\r\n", data.secondary, data.action.c_str(), data.channel);
            }
            else if (data.primary == IEC_UNLISTEN)
            {
                state = BUS_RELEASE;
            }

            // Switch to detected protocol
            if (data.secondary == IEC_OPEN || data.secondary == IEC_REOPEN)
            {
                //pull ( PIN_IEC_SRQ );
                protocol = selectProtocol();
                //release ( PIN_IEC_SRQ );
            }

            // Queue control codes and command in specified device
            //pull ( PIN_IEC_SRQ );
            auto d = deviceById(data.device);
            if (d != nullptr)
            {
                device_state_t device_state = d->queue_command(data);

                //fnLedManager.set(eLed::LED_BUS, true);

                //Debug_printv("bus[%d] device[%d]", state, device_state);
                // for (auto devicep : _daisyChain)
                // {
                    device_state = d->process();
                    if ( device_state < DEVICE_ACTIVE || device_state == DEVICE_TALK )
                    {
                        state = BUS_RELEASE;
                    }
                // }
            }

            //Debug_printv("bus[%d] device[%d] flags[%d]", state, device_state, flags);

            // Switch back to standard serial
            detected_protocol = PROTOCOL_SERIAL;
            protocol = selectProtocol();
            //release ( PIN_IEC_SRQ );

            if ( status ( PIN_IEC_ATN ) )
            {
                state = BUS_ACTIVE;
            }
        }

    } while( state > BUS_IDLE );

     // Clean Up
    if ( state == BUS_RELEASE )
    {
        releaseLines();
        data.init();
    }

    //Debug_printv ( "primary[%.2X] secondary[%.2X] bus[%d] flags[%d]", data.primary, data.secondary, state, flags );
    //Debug_printv ( "device[%d] channel[%d]", data.device, data.channel);

    Debug_printv("bus[%d] flags[%d]", state, flags);
    Debug_printf("Heap: %lu\r\n",esp_get_free_internal_heap_size());

    //release( PIN_IEC_SRQ );
    //fnLedStrip.stopRainbow();
    //fnLedManager.set(eLed::LED_BUS, false);
}

void systemBus::read_command()
{
    //pull( PIN_IEC_SRQ );
    uint8_t c = receiveByte();
    //release( PIN_IEC_SRQ );

    // Check for error
    if ( flags & ERROR )
    {
        Debug_printv("Error reading command. flags[%d] c[%X]", flags, c);
        state = BUS_ERROR;
        
        return;
    }
    else if ( flags & EMPTY_STREAM)
    {
        state = BUS_RELEASE;
    }
    else
    {
        if ( flags & JIFFYDOS_ACTIVE )
        {
            Debug_printf("   IEC: [JD][%.2X]", c);
            detected_protocol = PROTOCOL_JIFFYDOS;
        }
        else
        {
            Debug_printf("   IEC: [%.2X]", c);
        }

        // Decode command byte
        uint8_t command = c & 0x60;
        if (c == IEC_UNLISTEN)
            command = IEC_UNLISTEN;
        if (c == IEC_UNTALK)
            command = IEC_UNTALK;

        //Debug_printv ( "device[%d] channel[%d]", data.device, data.channel);
        //Debug_printv ("command[%.2X]", command);

        switch (command)
        {
        // case IEC_GLOBAL:
        //     data.primary = IEC_GLOBAL;
        //     data.device = c ^ IEC_GLOBAL;
        //     state = BUS_IDLE;
        //     Debug_printf(" (00 GLOBAL %.2d COMMAND)\r\n", data.device);
        //     break;

        case IEC_LISTEN:
            data.primary = IEC_LISTEN;
            data.device = c ^ IEC_LISTEN;
            data.secondary = IEC_REOPEN; // Default secondary command
            data.channel = CHANNEL_COMMAND;  // Default channel
            data.payload = "";
            state = BUS_ACTIVE;
            Debug_printf(" (20 LISTEN %.2d DEVICE)\r\n", data.device);
            break;

        case IEC_UNLISTEN:
            data.primary = IEC_UNLISTEN;
            state = BUS_PROCESS;
            Debug_printf(" (3F UNLISTEN)\r\n");
            break;

        case IEC_TALK:
            data.primary = IEC_TALK;
            data.device = c ^ IEC_TALK;
            data.secondary = IEC_REOPEN; // Default secondary command
            data.channel = CHANNEL_COMMAND;  // Default channel
            state = BUS_ACTIVE;
            Debug_printf(" (40 TALK  %.2d DEVICE)\r\n", data.device);
            break;

        case IEC_UNTALK:
            data.primary = IEC_UNTALK;
            data.secondary = 0x00;
            state = BUS_RELEASE;
            Debug_printf(" (5F UNTALK)\r\n");
            break;

        default:

            //pull ( PIN_IEC_SRQ );
            std::string secondary;
            state = BUS_PROCESS;

            command = c & 0xF0;
            switch ( command )
            {
            case IEC_OPEN:
                data.secondary = IEC_OPEN;
                data.channel = c ^ IEC_OPEN;
                data.action = "OPEN";
                break;

            case IEC_REOPEN:
                data.secondary = IEC_REOPEN;
                data.channel = c ^ IEC_REOPEN;
                data.action = "DATA";
                break;

            case IEC_CLOSE:
                data.secondary = IEC_CLOSE;
                data.channel = c ^ IEC_CLOSE;
                data.action = "CLOSE";
                break;

            default:
                state = BUS_IDLE;
            }

            if ( data.primary != IEC_TALK )
                Debug_printf(" (%.2X %s  %.2d CHANNEL)\r\n", data.secondary, data.action.c_str(), data.channel);
        }
    }

    //if ( state == BUS_ACTIVE )  // Normal behaviour is to ignore everything if it's not for us
    if ( state == BUS_PROCESS )  // Use this to sniff the secondary commands
    {
        if ( !isDeviceEnabled( data.device ) )
        {
            // Is this command for us?
            state = BUS_RELEASE; // NOPE!
        }
    }
    else if ( state == BUS_PROCESS )
    {
        // *** IMPORTANT! This helps keep us in sync!
        // Sometimes ATN isn't released immediately. Wait for ATN to be
        // released before trying to process the command. 
        // Long ATN delay (>1.5ms) seems to occur more frequently with VIC-20.
        //pull ( PIN_IEC_SRQ );
        protocol->timeoutWait ( PIN_IEC_ATN, RELEASED, TIMEOUT_DEFAULT, false );

        // Delay after ATN is RELEASED
        //protocol->wait( TIMING_Ttk, false );
        //release ( PIN_IEC_SRQ );
    }


#ifdef PARALLEL_BUS
    // Switch to Parallel if detected
    if ( PARALLEL.state == PBUS_PROCESS )
    {
        if ( data.primary == IEC_LISTEN || data.primary == IEC_TALK )
            detected_protocol = PROTOCOL_SPEEDDOS;
        else if ( data.primary == IEC_OPEN || data.primary == IEC_REOPEN )
            detected_protocol = PROTOCOL_DOLPHINDOS;

        // Switch to parallel protocol
        protocol = selectProtocol();

        if ( data.primary == IEC_LISTEN )
            PARALLEL.setMode( MODE_RECEIVE );
        else
            PARALLEL.setMode( MODE_SEND );

        // Acknowledge parallel mode
        PARALLEL.handShake();
    }
#endif

    //Debug_printv ( "code[%.2X] primary[%.2X] secondary[%.2X] bus[%d] flags[%d]", c, data.primary, data.secondary, state, flags );
    //Debug_printv ( "device[%d] channel[%d]", data.device, data.channel);

    //release( PIN_IEC_SRQ );
}

void systemBus::read_payload()
{
    // Record the command string until ATN is PULLED
    // NOTE: string is just a container, it may contain arbitrary bytes but a LOT of code treats payload as a string
    std::string listen_command = "";

    // ATN might get pulled right away if there is no command string to send
    //pull ( PIN_IEC_SRQ );
    while (status(PIN_IEC_ATN) != PULLED)
    {
        //pull ( PIN_IEC_SRQ );
        int16_t c = protocol->receiveByte();
        //Debug_printv("c[%2X]", c);
        //release ( PIN_IEC_SRQ );

        if (flags & EMPTY_STREAM || flags & ERROR)
        {
            Debug_printv("flags[%02X]", flags);
            state = BUS_ERROR;
            //release ( PIN_IEC_SRQ );
            return;
        }

        //if (c != 0xFFFFFFFF ) // && c != 0x0D) // Leave 0x0D to be stripped later
        //{
            listen_command += (uint8_t)c;
        //}

        if (flags & EOI_RECVD)
            break;
    }
    data.payload = listen_command;

    state = BUS_IDLE;
    //release ( PIN_IEC_SRQ );
}

/**
 * Start the Interrupt rate limiting timer
 */
void systemBus::timer_start()
{
    esp_timer_create_args_t tcfg;
    tcfg.arg = this;
    tcfg.callback = onTimer;
    tcfg.dispatch_method = esp_timer_dispatch_t::ESP_TIMER_TASK;
    tcfg.name = nullptr;
    esp_timer_create(&tcfg, &rateTimerHandle);
    esp_timer_start_periodic(rateTimerHandle, timerRate * 1000);
}

/**
 * Stop the Interrupt rate limiting timer
 */
void systemBus::timer_stop()
{
    // Delete existing timer
    if (rateTimerHandle != nullptr)
    {
        Debug_println("Deleting existing rateTimer\r\n");
        esp_timer_stop(rateTimerHandle);
        esp_timer_delete(rateTimerHandle);
        rateTimerHandle = nullptr;
    }
}

std::shared_ptr<IECProtocol> systemBus::selectProtocol() 
{
    //Debug_printv("protocol[%d]", detected_protocol);
    
    switch(detected_protocol)
    {
        case PROTOCOL_JIFFYDOS:
        {
            auto p = std::make_shared<JiffyDOS>();
            return std::static_pointer_cast<IECProtocol>(p);
        }
#ifdef PARALLEL_BUS
        case PROTOCOL_DOLPHINDOS:
        {
            auto p = std::make_shared<DolphinDOS>();
            return std::static_pointer_cast<IECProtocol>(p);
        }
#endif
        default:
        {
#ifdef PARALLEL_BUS
            PARALLEL.state = PBUS_IDLE;
#endif
            auto p = std::make_shared<CPBStandardSerial>();
            return std::static_pointer_cast<IECProtocol>(p);
        }
    }
}

systemBus virtualDevice::get_bus()
{
    return IEC;
}

device_state_t virtualDevice::process()
{
    switch ((bus_command_t)commanddata.primary)
    {
    case bus_command_t::IEC_LISTEN:
        state = DEVICE_LISTEN;
        break;
    case bus_command_t::IEC_UNLISTEN:
        state = DEVICE_PROCESS;
        break;
    case bus_command_t::IEC_TALK:
        state = DEVICE_TALK;
        break;
    default:
        break;
    }

    switch ((bus_command_t)commanddata.secondary)
    {
    case bus_command_t::IEC_OPEN:
        payload = commanddata.payload;
        break;
    case bus_command_t::IEC_CLOSE:
        reset_state();
        break;
    case bus_command_t::IEC_REOPEN:
        if (state == DEVICE_TALK)
        {
        }
        else if (state == DEVICE_LISTEN)
        {
            payload = commanddata.payload;
        }
        break;
    default:
        break;
    }

    return state;
}

// This is only used in iecDrive, and it has its own implementation
void virtualDevice::iec_talk_command_buffer_status()
{
    // Removed implementation as it wasn't being used
}

void virtualDevice::dumpData()
{
    Debug_printf("%9s: %02X\r\n", "Primary", commanddata.primary);
    Debug_printf("%9s: %02u\r\n", "Device", commanddata.device);
    Debug_printf("%9s: %02X\r\n", "Secondary", commanddata.secondary);
    Debug_printf("%9s: %02u\r\n", "Channel", commanddata.channel);
    Debug_printf("%9s: %s\r\n", "Payload", commanddata.payload.c_str());
}

void systemBus::assert_interrupt()
{
    if (interruptSRQ)
        pull(PIN_IEC_SRQ);
    else
        release(PIN_IEC_SRQ);
}

uint8_t systemBus::receiveByte()
{
    //pull( PIN_IEC_SRQ );
    uint8_t b = protocol->receiveByte();
#ifdef DATA_STREAM
    Serial.printf("%.2X ", (int16_t)b);
#endif

    if ( flags & ERROR )
    {
        Debug_printv("error");
    }
    //release( PIN_IEC_SRQ );
    return b;
}

std::string systemBus::receiveBytes()
{
    std::string s;

    do
    {
        uint8_t b = receiveByte();
        if ( !(flags & ERROR) )
            s += b;
    }while(!(flags & EOI_RECVD));
    return s;
}

bool systemBus::sendByte(const char c, bool eoi)
{
    gpio_intr_disable( PIN_IEC_CLK_IN );
    if (!protocol->sendByte(c, eoi))
    {
        if (!(flags & ATN_PULLED))
        {
            flags |= ERROR;
            Debug_printv("error");
            gpio_intr_enable( PIN_IEC_CLK_IN );
            return false;
        }
    }

#ifdef DATA_STREAM
    if (eoi)
    {
        Serial.printf("%.2X [eoi]\r\n", c);
    }
    else
    {
        if (!(flags & ATN_PULLED))
            Serial.printf("%.2X ", c);
    }
#endif
    gpio_intr_enable( PIN_IEC_CLK_IN );
    return true;
}

bool systemBus::sendBytes(const char *buf, size_t len, bool eoi)
{
    bool success = false;

    for (size_t i = 0; i < len; i++)
    {
        if (i == (len - 1) && eoi)
            success = sendByte(buf[i], true);
        else
            success = sendByte(buf[i], false);

        if ( IEC.status ( PIN_IEC_ATN ) )
        {
            return true;
        }
    }

    return success;
}

bool systemBus::sendBytes(std::string s, bool eoi)
{
    return sendBytes(s.c_str(), s.size(), eoi);
}

void systemBus::process_cmd()
{
    // fnLedManager.set(eLed::LED_BUS, true);

    // TODO implement

    // fnLedManager.set(eLed::LED_BUS, false);
}

void systemBus::process_queue()
{
    // TODO IMPLEMENT
}

void IRAM_ATTR systemBus::deviceListen()
{
    // If the command is SECONDARY and it is not to expect just a small command on the command channel, then
    // we're into something more heavy. Otherwise read it all out right here until UNLISTEN is received.
    if (data.secondary == IEC_REOPEN && data.channel != CHANNEL_COMMAND)
    {
        // A heapload of data might come now, too big for this context to handle so the caller handles this, we're done here.
        // Debug_printf(" (%.2X SECONDARY) (%.2X CHANNEL)\r\n", data.primary, data.channel);
        Debug_printf("REOPEN on non-command channel.\r\n");
        state = BUS_ACTIVE;
    }

    // OPEN or DATA
    else if (data.secondary == IEC_OPEN || data.secondary == IEC_REOPEN)
    {
        read_payload();
        std::string s = mstr::toHex(data.payload);
        Serial.printf("Device #%02d:%02d {%s} [%s]\r\n", data.device, data.channel, data.payload.c_str(), s.c_str());
    }

    // CLOSE Named Channel
    else if (data.secondary == IEC_CLOSE)
    {
        // Debug_printf(" (E0 CLOSE) (%d CHANNEL)\r\n", data.channel);
        state = BUS_PROCESS;
    }

    // Unknown
    else
    {
        Debug_printf(" OTHER (%.2X COMMAND) (%.2X CHANNEL) ", data.secondary, data.channel);
        state = BUS_ERROR;
    }
}

void IRAM_ATTR systemBus::deviceTalk()
{
    // Now do bus turnaround
    //pull(PIN_IEC_SRQ);
    if (!turnAround())
    {
        Debug_printv("error flags[%d]", flags);
        state = BUS_ERROR;
        return;
    }
    //release(PIN_IEC_SRQ);

    // We have recieved a CMD and we should talk now:
    state = BUS_PROCESS;
}

bool IRAM_ATTR systemBus::turnAround()
{
    /*
    TURNAROUND
    An unusual sequence takes place following ATN if the computer wishes the remote device to
    become a talker. This will usually take place only after a Talk command has been sent.
    Immediately after ATN is RELEASED, the selected device will be behaving like a listener. After all, it's
    been listening during the ATN cycle, and the computer has been a talker. At this instant, we
    have "wrong way" logic; the device is holding down the Data line, and the computer is holding the
    Clock line. We must turn this around. 
    
    Here's the sequence:
    1. The computer pulls the Data line to true (it's already there), as well as releases the Clock line to false. 
    2. When the device sees the Clock line is releaseed, it releases the Data line (which stays true anyway since 
    the computer is now holding it down) and then pulls down the Clock line. 
    
    We're now in our starting position, with the talker (that's the device) holding the Clock true, and 
    the listener (the computer) holding the Data line true. The computer watches for this state; only when it has 
    gone through the cycle correctly will it be ready to receive data. And data will be signalled, of course, with 
    the usual sequence: the talker releases the Clock line to signal that it's ready to send.
    */

    // Wait for CLK to be released
    //pull ( PIN_IEC_SRQ );
    if (protocol->timeoutWait(PIN_IEC_CLK_IN, RELEASED, TIMEOUT_Ttlta) == TIMEOUT_Ttlta)
    {
        Debug_printv("Wait until the computer releases the CLK line\r\n");
        Debug_printv("IEC: TURNAROUND TIMEOUT\r\n");
        flags |= ERROR;
        return false; // return error because timeout
    }
    release ( PIN_IEC_DATA_OUT );
    pull ( PIN_IEC_CLK_OUT );
    //release ( PIN_IEC_SRQ );

    // 80us minimum delay after TURNAROUND
    // *** IMPORTANT!
    protocol->wait( TIMING_Tda, false );

    return true;
} // turnAround

void systemBus::reset_all_our_devices()
{
    // TODO iterate through our bus and send reset to each device.
}

void systemBus::setBitTiming(std::string set, int p1, int p2, int p3, int p4)
{
     uint8_t i = 0; // Send
    if (mstr::equals(set, "r")) i = 1;
    if (p1) protocol->bit_pair_timing[i][0] = p1;
    if (p2) protocol->bit_pair_timing[i][1] = p2;
    if (p3) protocol->bit_pair_timing[i][2] = p3;
    if (p4) protocol->bit_pair_timing[i][3] = p4;

    Debug_printv("i[%d] timing[%d][%d][%d][%d]", i,
                    protocol->bit_pair_timing[i][0], 
                    protocol->bit_pair_timing[i][1], 
                    protocol->bit_pair_timing[i][2], 
                    protocol->bit_pair_timing[i][3]);
}

void IRAM_ATTR systemBus::releaseLines(bool wait)
{
    //pull ( PIN_IEC_SRQ );

    // Release lines
    release(PIN_IEC_CLK_OUT);
    release(PIN_IEC_DATA_OUT);

    // Wait for ATN to release and quit
    if (wait)
    {
        Debug_printv("Waiting for ATN to release");
        protocol->timeoutWait ( PIN_IEC_ATN, RELEASED, TIMEOUT_DEFAULT, false );
    }

    //release ( PIN_IEC_SRQ );
}

void IRAM_ATTR systemBus::senderTimeout()
{
    releaseLines();
    this->state = BUS_ERROR;

    protocol->wait( TIMING_EMPTY );
    pull( PIN_IEC_DATA_OUT );
} // senderTimeout

void systemBus::addDevice(virtualDevice *pDevice, int device_id)
{
    if (!pDevice)
    {
        Debug_printf("systemBus::addDevice() pDevice == nullptr! returning.\r\n");
        return;
    }

    // TODO, add device shortcut pointer logic like others
    Serial.printf("Device #%02d Ready!\r\n", device_id);

    pDevice->_devnum = device_id;
    _daisyChain.push_front(pDevice);
    enabledDevices |= 1UL << device_id;
}

void systemBus::remDevice(virtualDevice *pDevice)
{
    if (!pDevice)
    {
        Debug_printf("system Bus::remDevice() pDevice == nullptr! returning\r\n");
        return;
    }

    _daisyChain.remove(pDevice);
    enabledDevices &= ~ ( 1UL << pDevice->_devnum );
}

bool systemBus::isDeviceEnabled ( const uint8_t device_id )
{
    return ( enabledDevices & ( 1 << device_id ) );
} // isDeviceEnabled



void systemBus::changeDeviceId(virtualDevice *pDevice, int device_id)
{
    if (!pDevice)
    {
        Debug_printf("systemBus::changeDeviceId() pDevice == nullptr! returning.\r\n");
        return;
    }

    for (auto devicep : _daisyChain)
    {
        if (devicep == pDevice)
            devicep->_devnum = device_id;
    }
}

virtualDevice *systemBus::deviceById(int device_id)
{
    for (auto devicep : _daisyChain)
    {
        if (devicep->_devnum == device_id)
            return devicep;
    }
    return nullptr;
}

void systemBus::shutdown()
{
    shuttingDown = true;

    for (auto devicep : _daisyChain)
    {
        Debug_printf("Shutting down device #%02d\r\n", devicep->id());
        devicep->shutdown();
    }
    Debug_printf("All devices shut down.\r\n");
}



void systemBus::debugTiming()
{
	int pin = PIN_IEC_ATN;
	pull(pin);
	protocol->wait(10);
	release(pin);
	protocol->wait(10);

	pin = PIN_IEC_CLK_OUT;
	pull(pin);
	protocol->wait(20);
	release(pin);
	protocol->wait(20);

	pin = PIN_IEC_DATA_OUT;
	pull(pin);
	protocol->wait(30);
	release(pin);
	protocol->wait(30);

	pin = PIN_IEC_SRQ;
	pull(pin);
	protocol->wait(40);
	release(pin);
	protocol->wait(40);

	// pin = PIN_IEC_ATN;
	// pull(pin);
	// protocol->wait(100); // 100
	// release(pin);
	// protocol->wait(1);

	// pin = PIN_IEC_CLK_OUT;
	// pull(pin);
	// protocol->wait(200); // 200
	// release(pin);
	// protocol->wait(1);
}

#endif /* BUILD_IEC */

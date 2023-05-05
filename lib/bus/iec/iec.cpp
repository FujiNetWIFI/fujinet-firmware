#ifdef BUILD_IEC

#include <cstring>
#include "iec.h"
#include "../../include/debug.h"
#include "../../include/pinmap.h"
#include "led.h"
#include "protocol/iecProtocolSerial.h"
#include "network.h"
#include "string_utils.h"
#include "utils.h"

// http://www.ffd2.com/fridge/docs/1541dis.html#E853

// ******************************  IRQ routine for serial bus
// E853   AD 01 18   LDA $1801     read port A, erase IRQ flag
// E856   A9 01      LDA #$01
// E858   85 7C      STA $7C       set flag for 'ATN received'
// E85A   60         RTS

// ******************************  servicing the serial bus
// E85B   78         SEI
// E85C   A9 00      LDA #$00
// E85E   85 7C      STA $7C       erase flag for 'ATN received'
// E860   85 79      STA $79       erase flag for LISTEN
// E862   85 7A      STA $7A       erase flag for TALK
// E864   A2 45      LDX #$45
// E866   9A         TXS           initialize stack pointer
// E867   A9 80      LDA #$80
// E869   85 F8      STA $F8       erase end flag
// E86B   85 7D      STA $7D       erase EOI flag
// E86D   20 B7 E9   JSR $E9B7     CLOCK OUT lo (PULLED)
// E870   20 A5 E9   JSR $E9A5     DATA OUT, bit '0', hi (RELEASED)
// E873   AD 00 18   LDA $1800
// E876   09 10      ORA #$10      switch data lines to input
// E878   8D 00 18   STA $1800
// E87B   AD 00 18   LDA $1800     read IEEE port
// E87E   10 57      BPL $E8D7     EOI?
// E880   29 04      AND #$04      CLOCK IN?
// E882   D0 F7      BNE $E87B     no
// E884   20 C9 E9   JSR $E9C9     get byte from bus
// E887   C9 3F      CMP #$3F      unlisten?
// E889   D0 06      BNE $E891     no
// E88B   A9 00      LDA #$00
// E88D   85 79      STA $79       reset flag for LISTEN
// E88F   F0 71      BEQ $E902
// E891   C9 5F      CMP #$5F      untalk?
// E893   D0 06      BNE $E89B     no
// E895   A9 00      LDA #$00
// E897   85 7A      STA $7A       reset flag for TALK
// E899   F0 67      BEQ $E902
// E89B   C5 78      CMP $78       TALK address?
// E89D   D0 0A      BNE $E8A9     no
// E89F   A9 01      LDA #$01
// E8A1   85 7A      STA $7A       set flag for TALK
// E8A3   A9 00      LDA #$00
// E8A5   85 79      STA $79       reset flag for LISTEN
// E8A7   F0 29      BEQ $E8D2
// E8A9   C5 77      CMP $77       LISTEN address?
// E8AB   D0 0A      BNE $E8B7     no
// E8AD   A9 01      LDA #$01
// E8AF   85 79      STA $79       set flag for LISTEN
// E8B1   A9 00      LDA #$00
// E8B3   85 7A      STA $7A       reset flag for TALK
// E8B5   F0 1B      BEQ $E8D2
// E8B7   AA         TAX
// E8B8   29 60      AND #$60
// E8BA   C9 60      CMP #$60      set bit 5 and 6
// E8BC   D0 3F      BNE $E8FD     no
// E8BE   8A         TXA
// E8BF   85 84      STA $84       byte is secondary address
// E8C1   29 0F      AND #$0F
// E8C3   85 83      STA $83       channel number
// E8C5   A5 84      LDA $84
// E8C7   29 F0      AND #$F0
// E8C9   C9 E0      CMP #$E0      CLOSE?
// E8CB   D0 35      BNE $E902
// E8CD   58         CLI
// E8CE   20 C0 DA   JSR $DAC0     CLOSE routine
// E8D1   78         SEI
// E8D2   2C 00 18   BIT $1800
// E8D5   30 AD      BMI $E884
// E8D7   A9 00      LDA #$00
// E8D9   85 7D      STA $7D       set EOI
// E8DB   AD 00 18   LDA $1800     IEEE port
// E8DE   29 EF      AND #$EF      switch data lines to output
// E8E0   8D 00 18   STA $1800
// E8E3   A5 79      LDA $79       LISTEN active?
// E8E5   F0 06      BEQ $E8ED     no
// E8E7   20 2E EA   JSR $EA2E     receive data
// E8EA   4C E7 EB   JMP $EBE7     to delay loop

// E8ED   A5 7A      LDA $7A       TALK active?
// E8EF   F0 09      BEQ $E8FA     no
// E8F1   20 9C E9   JSR $E99C     DATA OUT, bit '1', lo (PULLED)
// E8F4   20 AE E9   JSR $E9AE     CLOCK OUT hi (RELEASED)
// E8F7   20 09 E9   JSR $E909     send data
// E8FA   4C 4E EA   JMP $EA4E     to delay loop
// E8FD   A9 10      LDA #$10      either TALK or LISTEN, ignore byte
// E8FF   8D 00 18   STA $1800     switch data lines to input
// E902   2C 00 18   BIT $1800
// E905   10 D0      BPL $E8D7
// E907   30 F9      BMI $E902     wait for handshake

static void IRAM_ATTR cbm_on_attention_isr_handler(void *arg)
{
    systemBus *b = (systemBus *)arg;

    // Go to listener mode and get command
    // E86D   20 B7 E9   JSR $E9B7     CLOCK OUT lo
    // E870   20 A5 E9   JSR $E9A5     DATA OUT, bit '0', hi
    b->release(PIN_IEC_CLK_OUT);
    b->pull(PIN_IEC_DATA_OUT);

    b->flags |= ATN_PULLED;
    if (b->bus_state < BUS_ACTIVE)
        b->bus_state = BUS_ACTIVE;
}

void IRAM_ATTR systemBus::service()
{
    // Handle SRQ for network.

    virtualDevice *d = deviceById(12);

    if (d)
        for (int i = 0; i < 16; i++)
        {
            d->poll_interrupt(i);
        }
        
    if (bus_state < BUS_ACTIVE)
        return;

        // pull( PIN_IEC_SRQ );

        // Disable Interrupt
        // gpio_intr_disable((gpio_num_t)PIN_IEC_ATN);

        // TODO IMPLEMENT

#ifdef IEC_HAS_RESET

    // Check if CBM is sending a reset (setting the RESET line high). This is typically
    // when the CBM is reset itself. In this case, we are supposed to reset all states to initial.
    bool pin_reset = status(PIN_IEC_RESET);
    if (pin_reset == PULLED)
    {
        if (status(PIN_IEC_ATN) == PULLED)
        {
            // If RESET & ATN are both PULLED then CBM is off
            bus_state = BUS_OFFLINE;
            // gpio_intr_enable((gpio_num_t)PIN_IEC_ATN);
            return;
        }

        Debug_printf("IEC Reset! reset[%d]\r\n", pin_reset);
        data.init(); // Clear bus data
        bus_state = BUS_IDLE;

        // Reset virtual devices
        reset_all_our_devices();
        // gpio_intr_enable((gpio_num_t)PIN_IEC_ATN);
        return;
    }

#endif

    // Command or Data Mode
    do
    {
        // Exit if bus is offline
        if (bus_state == BUS_OFFLINE)
            break;

        // Turn on the lights
        // fnLedManager.set(eLed::LED_BUS, true);

        if (bus_state == BUS_ACTIVE)
        {
            // E85C   A9 00      LDA #$00
            // E85E   85 7C      STA $7C       erase flag for 'ATN received'
            // E860   85 79      STA $79       erase flag for LISTEN
            // E862   85 7A      STA $7A       erase flag for TALK
            // E864   A2 45      LDX #$45
            flags &= CLEAR_LOW;

            // E86D   20 B7 E9   JSR $E9B7     CLOCK OUT lo
            // E870   20 A5 E9   JSR $E9A5     DATA OUT, bit '0', hi
            release(PIN_IEC_CLK_OUT);
            pull(PIN_IEC_DATA_OUT);

            // Read bus command bytes
            Debug_printv("command");
            // E884   20 C9 E9   JSR $E9C9     get byte from bus
            // pull ( PIN_IEC_SRQ );
            read_command();
            // release ( PIN_IEC_SRQ );
        }

        if (bus_state == BUS_PROCESS)
        {
            flags &= CLEAR_LOW;

            Debug_printv("data");
            if (data.secondary == IEC_OPEN || data.secondary == IEC_REOPEN)
            {
                // TODO: switch protocol subclass as needed.
            }

            // Data Mode - Get Command or Data
            if (data.primary == IEC_LISTEN)
            {
                // Debug_printf("calling deviceListen()\n");
                deviceListen();
            }
            else if (data.primary == IEC_TALK)
            {
                // Debug_printf("calling deviceTalk()\n");
                deviceTalk();
            }

            // Queue control codes and command in specified device
            device_state_t device_state = deviceById(data.device)->queue_command(&data);

            // Process commands in devices
            // Debug_printv( "deviceProcess" );

            // Debug_printv("bus[%d] device[%d]", bus_state, device_state);

            if (deviceById(data.device)->process(&data) < DEVICE_ACTIVE || device_state < DEVICE_ACTIVE)
            {
                // Debug_printf("Device idle\n");
                data.init();
            }

            // Debug_printv("bus[%d] device[%d] flags[%d]", bus_state, device_state, flags);
            bus_state = BUS_IDLE;
            flags = CLEAR;
        }

        // Let bus stabalize
        protocol->wait(TIMING_STABLE, 0, false);

        if (status(PIN_IEC_ATN))
            bus_state = BUS_ACTIVE;

    } while (bus_state > BUS_IDLE);

    // Turn off the lights
    // fnLedManager.set(eLed::LED_BUS, false);

    // Cleanup and Re-enable Interrupt
    releaseLines();
    // gpio_intr_enable((gpio_num_t)PIN_IEC_ATN);

    // Debug_printv ( "primary[%.2X] secondary[%.2X] bus[%d] flags[%d]", data.primary, data.secondary, bus_state, flags );
    // Debug_printv ( "device[%d] channel[%d]", data.device, data.channel);

    Debug_printv("exit");
    // release( PIN_IEC_SRQ );
}

void systemBus::read_command()
{
    int16_t c = 0;

    do
    {
        // ATN was pulled read bus command bytes
        // E884   20 C9 E9   JSR $E9C9     get byte from bus
        // pull( PIN_IEC_SRQ );
        c = receiveByte();
        // release( PIN_IEC_SRQ );

        // Check for error
        if (c == 0xFFFFFFFF || flags & ERROR)
        {
            // Debug_printv("Error reading command. flags[%d]", flags);
            if (c == 0xFFFFFFFF)
                bus_state = BUS_OFFLINE;
            else
                bus_state = BUS_ERROR;
        }
        else if (flags & EMPTY_STREAM)
        {
            bus_state = BUS_IDLE;
        }
        else
        {
            Debug_printf("   IEC: [%.2X]", c);

            // Decode command byte
            uint8_t command = c & 0xF0;
            if (c == IEC_UNLISTEN)
                command = IEC_UNLISTEN;
            if (c == IEC_UNTALK)
                command = IEC_UNTALK;

            // Debug_printv ( "device[%d] channel[%d]", data.device, data.channel);
            // Debug_printv ("command[%.2X]", command);

            switch (command)
            {
            case IEC_GLOBAL:
                data.primary = IEC_GLOBAL;
                data.device = c ^ IEC_GLOBAL;
                bus_state = BUS_IDLE;
                Debug_printf(" (00 GLOBAL %.2d COMMAND)\r\n", data.device);
                break;

            case IEC_LISTEN:
                data.primary = IEC_LISTEN;
                data.device = c ^ IEC_LISTEN;
                data.secondary = IEC_REOPEN;    // Default secondary command
                data.channel = CHANNEL_COMMAND; // Default channel
                bus_state = BUS_ACTIVE;
                Debug_printf(" (20 LISTEN %.2d DEVICE)\r\n", data.device);
                break;

            case IEC_UNLISTEN:
                data.primary = IEC_UNLISTEN;
                data.secondary = 0x00;
                bus_state = BUS_IDLE;
                Debug_printf(" (3F UNLISTEN)\r\n");
                break;

            case IEC_TALK:
                data.primary = IEC_TALK;
                data.device = c ^ IEC_TALK;
                data.secondary = IEC_REOPEN;    // Default secondary command
                data.channel = CHANNEL_COMMAND; // Default channel
                bus_state = BUS_ACTIVE;
                Debug_printf(" (40 TALK   %.2d DEVICE)\r\n", data.device);
                break;

            case IEC_UNTALK:
                data.primary = IEC_UNTALK;
                data.secondary = 0x00;
                bus_state = BUS_IDLE;
                Debug_printf(" (5F UNTALK)\r\n");
                break;

            case IEC_OPEN:
                data.secondary = IEC_OPEN;
                data.channel = c ^ IEC_OPEN;
                bus_state = BUS_PROCESS;
                Debug_printf(" (F0 OPEN   %.2d CHANNEL)\r\n", data.channel);
                break;

            case IEC_REOPEN:
                data.secondary = IEC_REOPEN;
                data.channel = c ^ IEC_REOPEN;
                bus_state = BUS_PROCESS;
                Debug_printf(" (60 DATA   %.2d CHANNEL)\r\n", data.channel);
                break;

            case IEC_CLOSE:
                data.secondary = IEC_CLOSE;
                data.channel = c ^ IEC_CLOSE;
                bus_state = BUS_PROCESS;
                Debug_printf(" (E0 CLOSE  %.2d CHANNEL)\r\n", data.channel);
                break;
            }
        }

    } while (bus_state == BUS_ACTIVE);

    // Is this command for us?
    if (!deviceById(data.device) || !deviceById(data.device)->device_active)
    {
        // Debug_printf("Command not for us, ignoring.\n");
        bus_state = BUS_IDLE;
    }

    // If the bus is idle then release the lines
    if (bus_state < BUS_ACTIVE)
    {
        // Debug_printv("release lines");
        data.init();
        releaseLines();
    }

    // Debug_printv ( "code[%.2X] primary[%.2X] secondary[%.2X] bus[%d] flags[%d]", c, data.primary, data.secondary, bus_state, flags );
    // Debug_printv ( "device[%d] channel[%d]", data.device, data.channel);

    // Delay to stabalize bus
    // protocol->wait( TIMING_STABLE, 0, false );

    // release( PIN_IEC_SRQ );
}

void systemBus::read_payload()
{
    // Record the command string until ATN is PULLED
    std::string listen_command = "";

    // ATN might get pulled right away if there is no command string to send
    protocol->wait(TIMING_STABLE);

    while (IEC.status(PIN_IEC_ATN) != PULLED)
    {
        int16_t c = protocol->receiveByte();

        if (flags & EMPTY_STREAM)
        {
            bus_state = BUS_ERROR;
            return;
        }

        if (flags & ERROR)
        {
            bus_state = BUS_ERROR;
            return;
        }

        // if (c != 0x0D && c != 0xFFFFFFFF) // Remove CR from end of command
        if (c != 0xFFFFFFFF)
        {
            listen_command += (uint8_t)c;
        }

        if (flags & EOI_RECVD)
        {
            data.payload = listen_command;
        }
    }

    bus_state = BUS_IDLE;
}

void systemBus::setup()
{
    Debug_printf("IEC systemBus::setup()\n");

    flags = CLEAR;
    protocol = new IecProtocolSerial();
    release(PIN_IEC_CLK_OUT);
    release(PIN_IEC_DATA_OUT);
    release(PIN_IEC_SRQ);

    // initial pin modes in GPIO
    set_pin_mode(PIN_IEC_ATN, gpio_mode_t::GPIO_MODE_INPUT);
    set_pin_mode(PIN_IEC_CLK_IN, gpio_mode_t::GPIO_MODE_INPUT);
    set_pin_mode(PIN_IEC_DATA_IN, gpio_mode_t::GPIO_MODE_INPUT);
    set_pin_mode(PIN_IEC_SRQ, gpio_mode_t::GPIO_MODE_INPUT);
    set_pin_mode(PIN_IEC_RESET, gpio_mode_t::GPIO_MODE_INPUT);

    // Setup interrupt for ATN
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_IEC_ATN), // bit mask of the pins that you want to set
        .mode = GPIO_MODE_INPUT,               // set as input mode
        .pull_up_en = GPIO_PULLUP_DISABLE,     // disable pull-up mode
        .pull_down_en = GPIO_PULLDOWN_DISABLE, // disable pull-down mode
        .intr_type = GPIO_INTR_NEGEDGE         // interrupt of falling edge
    };
    // configure GPIO with the given settings
    gpio_config(&io_conf);
    gpio_isr_handler_add((gpio_num_t)PIN_IEC_ATN, cbm_on_attention_isr_handler, this);
}

systemBus virtualDevice::get_bus()
{
    return IEC;
}

device_state_t virtualDevice::process(IECData *_commanddata)
{
    this->commanddata = _commanddata;

    switch ((bus_command_t)commanddata->primary)
    {
    case bus_command_t::IEC_LISTEN:
        device_state = DEVICE_LISTEN;
        break;
    case bus_command_t::IEC_UNLISTEN:
        device_state = DEVICE_PROCESS;
        break;
    case bus_command_t::IEC_TALK:
        device_state = DEVICE_TALK;
        break;
    default:
        break;
    }

    switch ((bus_command_t)commanddata->secondary)
    {
    case bus_command_t::IEC_OPEN:
        payload = commanddata->payload;
        // mstr::toASCII(payload);
        pt = util_tokenize(payload, ',');
        break;
    case bus_command_t::IEC_CLOSE:
        payload.clear();
        std::queue<std::string>().swap(response_queue);
        pt.clear();
        pt.shrink_to_fit();
        break;
    case bus_command_t::IEC_REOPEN:
        if (device_state == DEVICE_TALK)
        {
        }
        else if (device_state == DEVICE_LISTEN)
        {
            payload = commanddata->payload;
            // mstr::toASCII(payload);
            pt = util_tokenize(payload, ',');
        }
        break;
    default:
        break;
    }

    return device_state;
}

void virtualDevice::iec_talk_command_buffer_status()
{
    char reply[80];
    std::string s;

    fnSystem.delay_microseconds(100);

    if (!status_override.empty())
    {
        Debug_printv("sending explicit response.");
        IEC.sendBytes(status_override);
        status_override.clear();
        status_override.shrink_to_fit();
    }
    else
    {
        snprintf(reply, 80, "%u,%s,%u,%u", iecStatus.error, iecStatus.msg.c_str(), iecStatus.connected, iecStatus.channel);
        s = std::string(reply);
        mstr::toPETSCII(s);
        Debug_printv("sending status: %s\n", reply);
        IEC.sendBytes(s);
    }
}

void virtualDevice::dumpData()
{
    Debug_printf("%9s: %02X\n", "Primary", commanddata->primary);
    Debug_printf("%9s: %02u\n", "Device", commanddata->device);
    Debug_printf("%9s: %02X\n", "Secondary", commanddata->secondary);
    Debug_printf("%9s: %02u\n", "Channel", commanddata->channel);
    Debug_printf("%9s: %s\n", "Payload", commanddata->payload.c_str());
}

int16_t systemBus::receiveByte()
{
    int16_t b;
    b = protocol->receiveByte();
#ifdef DATA_STREAM
    Debug_printf("%.2X ", b);
#endif
    if (b == -1)
    {
        if (!(IEC.flags & ATN_PULLED))
        {
            IEC.flags |= ERROR;
            releaseLines();
            Debug_printv("error");
        }
    }
    return b;
}

bool systemBus::sendByte(const char c, bool eoi)
{
#ifdef DATA_STREAM
    if (eoi)
        Debug_printf("%.2X[eoi] ", c);
    else
        Debug_printf("%.2X ", c);
#endif
    if (!protocol->sendByte(c, eoi))
    {
        if (!(IEC.flags & ATN_PULLED))
        {
            IEC.flags |= ERROR;
            releaseLines();
            Debug_printv("error");
            return false;
        }
    }

    return true;
}

bool systemBus::sendBytes(const char *buf, size_t len, bool eoi)
{
    bool success = false;
#ifdef DATA_STREAM
    Debug_print("{ ");
#endif
    for (size_t i = 0; i < len; i++)
    {
        if (i == (len - 1) && eoi)
            success = sendByte(buf[i], true);
        else
            success = sendByte(buf[i], false);

        if (!success)
        {
            if (!(IEC.flags & ATN_PULLED))
            {
                IEC.flags |= ERROR;
                releaseLines();
                Debug_printv("error");
            }
            return false;
        }
    }
#ifdef DATA_STREAM
    Debug_println("}");
#endif
    return true;
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
        bus_state = BUS_ACTIVE;
    }

    // OPEN or DATA
    else if (data.secondary == IEC_OPEN || data.secondary == IEC_REOPEN)
    {
        read_payload();
        Debug_printf("{%s}\r\n", data.payload.c_str());
    }

    // CLOSE Named Channel
    else if (data.secondary == IEC_CLOSE)
    {
        // Debug_printf(" (E0 CLOSE) (%d CHANNEL)\r\n", data.channel);
        bus_state = BUS_PROCESS;
    }

    // Unknown
    else
    {
        Debug_printf(" OTHER (%.2X COMMAND) (%.2X CHANNEL) ", data.secondary, data.channel);
        bus_state = BUS_ERROR;
    }
}

void IRAM_ATTR systemBus::deviceTalk(void)
{
    // Now do bus turnaround
    pull(PIN_IEC_SRQ);
    if (!turnAround())
    {
        Debug_printv("error flags[%d]", flags);
        bus_state = BUS_ERROR;
        return;
    }
    release(PIN_IEC_SRQ);

    // We have recieved a CMD and we should talk now:
    bus_state = BUS_PROCESS;
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
    Clock line. We must turn this around. Here's the sequence:
    the computer quickly realizes what's going on, and pulls the Data line to true (it's already there), as
    well as releasing the Clock line to false. The device waits for this: when it sees the Clock line go
    true [sic], it releases the Data line (which stays true anyway since the computer is now holding it down)
    and then pulls down the Clock line. We're now in our starting position, with the talker (that's the
    device) holding the Clock true, and the listener (the computer) holding the Data line true. The
    computer watches for this state; only when it has gone through the cycle correctly will it be ready
    to receive data. And data will be signalled, of course, with the usual sequence: the talker releases
    the Clock line to signal that it's ready to send.
    */
    // Debug_printf("IEC turnAround: ");

    // Release control of data line
    // ATN100
    // E8F1   20 9C E9   JSR $E99C     DATA OUT, bit '1', lo (RELEASED)
    // E8F4   20 AE E9   JSR $E9AE     CLOCK OUT hi (PULLED)
    pull(PIN_IEC_CLK_OUT); // $E91F

    // Wait until the computer releases the ATN line
    if (protocol->timeoutWait(PIN_IEC_ATN, RELEASED, FOREVER) == TIMED_OUT)
    {
        Debug_printf("Wait until the computer releases the ATN line");
        flags |= ERROR;
        return false; // return error because timeout
    }

    // // Delay after ATN is RELEASED
    protocol->wait(TIMING_Ttk, 0, false);

    // // We control clock line now
    // release ( PIN_IEC_CLK_OUT );
    // protocol->wait( TIMING_Tda, 0, false );
    // pull ( PIN_IEC_CLK_OUT );

    release(PIN_IEC_DATA_OUT); // $E8F1

    return true;
} // turnAround

void systemBus::reset_all_our_devices()
{
    // TODO iterate through our bus and send reset to each device.
}

void IRAM_ATTR systemBus::releaseLines(bool wait)
{
    // Wait for ATN to release and quit
    if (wait)
    {
        // Debug_printf("Waiting for ATN to release");
        protocol->timeoutWait(PIN_IEC_ATN, RELEASED, FOREVER);
    }

    // Release lines
    release(PIN_IEC_CLK_OUT);
    release(PIN_IEC_DATA_OUT);
}

void IRAM_ATTR systemBus::senderTimeout()
{
    releaseLines();
    this->bus_state = BUS_ERROR;

    protocol->wait(TIMING_EMPTY);
} // senderTimeout

void systemBus::addDevice(virtualDevice *pDevice, int device_id)
{
    if (!pDevice)
    {
        Debug_printf("systemBus::addDevice() pDevice == nullptr! returning.\n");
        return;
    }

    // TODO, add device shortcut pointer logic like others

    pDevice->_devnum = device_id;
    _daisyChain.push_front(pDevice);
}

void systemBus::remDevice(virtualDevice *pDevice)
{
    if (!pDevice)
    {
        Debug_printf("system Bus::remDevice() pDevice == nullptr! returning\n");
        return;
    }

    _daisyChain.remove(pDevice);
}

void systemBus::changeDeviceId(virtualDevice *pDevice, int device_id)
{
    if (!pDevice)
    {
        Debug_printf("systemBus::changeDeviceId() pDevice == nullptr! returning.\n");
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
        Debug_printf("Shutting down device %02x\n", devicep->id());
        devicep->shutdown();
    }
    Debug_printf("All devices shut down.\n");
}

systemBus IEC;

#endif /* BUILD_IEC */
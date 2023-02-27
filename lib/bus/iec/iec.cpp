#ifdef BUILD_IEC

#include <cstring>
#include "iec.h"
#include "../../include/debug.h"
#include "../../include/pinmap.h"
#include "led.h"
#include "protocol/iecProtocolSerial.h"
#include "string_utils.h"

bus_state_t systemBus::deviceListen()
{
    // If the command is SECONDARY and it is not to expect just a small command on the command channel, then
    // we're into something more heavy. Otherwise read it all out right here until UNLISTEN is received.
    if ( data.secondary == IEC_REOPEN && data.channel != CMD_CHANNEL )
    {
        // A heapload of data might come now, too big for this context to handle so the caller handles this, we're done here.
        // Debug_printf(" (%.2X SECONDARY) (%.2X CHANNEL)\r\n", data.primary, data.channel);
        Debug_printf ( "\r\n" );
        return BUS_ACTIVE;
    }

    // OPEN or DATA
    else if ( data.secondary == IEC_OPEN || data.secondary == IEC_REOPEN )
    {
        // Record the command string until ATN is PULLED
        std::string listen_command = "";

        // ATN might get pulled right away if there is no command string to send
        protocol->wait( TIMING_STABLE );

        while ( status ( PIN_IEC_ATN ) != PULLED )
        {
            int16_t c = protocol->receiveByte();

            if ( flags & EMPTY_STREAM )
                break;

            if ( flags & ERROR )
            {
                Debug_printf ( "Some other command [%.2X]", c );
                return BUS_ERROR;
            }

            if ( c != 0x0D && c != 0xFFFFFFFF )
            {
                listen_command += ( uint8_t ) c;
            }

            if ( flags & EOI_RECVD )
                break;
        }

        if ( listen_command.length() )
        {
            // remove media id from command
            if ( listen_command[0]=='0' && listen_command[1]==':' )
                listen_command = mstr::drop(listen_command, 2);

            data.device_command = listen_command;
            mstr::rtrimA0 ( data.device_command );
            Debug_printf ( " {%s}\r\n", data.device_command.c_str() );
            //return BUS_PROCESS;
        }
        else
        {
            data.device_command = "";
            Debug_printf ( "\r\n" );            
        }

        //Debug_printv("listen complete");
        return BUS_PROCESS;
    }

    // CLOSE Named Channel
    else if ( data.secondary == IEC_CLOSE )
    {
        // Debug_printf(" (E0 CLOSE) (%d CHANNEL)\r\n", data.channel);
        return BUS_PROCESS;
    }

    // Unknown
    else
    {
        Debug_printf ( " OTHER (%.2X COMMAND) (%.2X CHANNEL) ", data.secondary, data.channel );
        return BUS_ERROR;
    }
}

void virtualDevice::bus_to_computer(uint8_t *buf, uint16_t len, bool err)
{
    // TODO IMPLEMENT
}

uint8_t virtualDevice::bus_to_peripheral(uint8_t *buf, unsigned short len)
{
    uint8_t ck = 0;

    // TODO IMPLEMENT

    return ck;
}

systemBus virtualDevice::get_bus()
{
    return IEC;
}

void systemBus::process_cmd()
{
    fnLedManager.set(eLed::LED_BUS, true);

    // TODO implement

    fnLedManager.set(eLed::LED_BUS, false);
}

void systemBus::process_queue()
{
    // TODO IMPLEMENT
}

void systemBus::service()
{
    // TODO IMPLEMENT
    bool process_command = false;
    bool pin_atn = status ( PIN_IEC_ATN );

#ifdef IEC_HAS_RESET

    // Check if CBM is sending a reset (setting the RESET line high). This is typically
    // when the CBM is reset itself. In this case, we are supposed to reset all states to initial.
    bool pin_reset = status ( PIN_IEC_RESET );
    if ( pin_reset == PULLED )
    {
        if ( pin_atn == PULLED )
        {
            // If RESET & ATN are both PULLED then CBM is off
            bus_state = BUS_OFFLINE;
            return;
        }

        Debug_printf ( "IEC Reset! reset[%d] atn[%d]\r\n", pin_reset, pin_atn );
        data.init(); // Clear bus data
        bus_state = BUS_IDLE;

        // Reset virtual devices
        reset_all_our_devices();

        return;
    }

#endif

    if ( bus_state == BUS_OFFLINE && pin_atn == PULLED)
        pin_atn = RELEASED;

    // Command or Data Mode
    if ( bus_state == BUS_ACTIVE || pin_atn == PULLED)
    {
        release ( PIN_IEC_CLK_OUT );
        pull ( PIN_IEC_DATA_OUT );

        // ATN was pulled read control code from the bus
        int16_t c = ( bus_command_t ) protocol->receiveByte();

        // Check for error
        if ( c == 0xFFFFFFFF || flags & ERROR )
        {
            //Debug_printv ( "Error reading command" );            
            if ( c == 0xFFFFFFFF )
                bus_state = BUS_OFFLINE;
            else

                bus_state = BUS_ERROR;
        }
        else
        {
            // Debug_println ( "COMMAND MODE" );        

            Debug_printf ( "   IEC: [%.2X]", c );

            // Decode command byte
            uint8_t command = c bitand 0xF0;
            if ( c == IEC_UNLISTEN ) command = IEC_UNLISTEN;
            if ( c == IEC_UNTALK ) command = IEC_UNTALK;

            switch ( command ) {
                case IEC_GLOBAL:
                    data.primary = IEC_GLOBAL;
                    data.device = c xor IEC_GLOBAL;
                    bus_state = BUS_IDLE;
                    Debug_printf ( " (00 GLOBAL %.2d COMMAND)\r\n", data.device );
                    break;

                case IEC_LISTEN:
                    data.primary = IEC_LISTEN;
                    data.device = c xor IEC_LISTEN;
                    data.secondary = IEC_REOPEN;    // Default secondary command
                    data.channel = CMD_CHANNEL;   // Default channel
                    bus_state = BUS_ACTIVE;
                    Debug_printf ( " (20 LISTEN %.2d DEVICE)\r\n", data.device );
                    break;

                case IEC_UNLISTEN:
                    data.primary = IEC_UNLISTEN;
                    data.secondary = 0x00;
                    bus_state = BUS_IDLE;
                    Debug_printf ( " (3F UNLISTEN)\r\n" );
                    break;

                case IEC_TALK:
                    data.primary = IEC_TALK;
                    data.device = c xor IEC_TALK;
                    data.secondary = IEC_REOPEN;    // Default secondary command
                    data.channel = CMD_CHANNEL;   // Default channel
                    bus_state = BUS_ACTIVE;
                    Debug_printf ( " (40 TALK   %.2d DEVICE)\r\n", data.device );
                    break;

                case IEC_UNTALK:
                    data.primary = IEC_UNTALK;
                    data.secondary = 0x00;
                    bus_state = BUS_IDLE;
                    Debug_printf ( " (5F UNTALK)\r\n" );
                    break;

                case IEC_OPEN:
                    data.secondary = IEC_OPEN;
                    data.channel = c xor IEC_OPEN;
                    bus_state = BUS_PROCESS;
                    process_command = true;
                    Debug_printf ( " (F0 OPEN   %.2d CHANNEL)\r\n", data.channel );
                    break;

                case IEC_REOPEN:
                    data.secondary = IEC_REOPEN;
                    data.channel = c xor IEC_REOPEN;
                    bus_state = BUS_PROCESS;
                    process_command = true;
                    Debug_printf ( " (60 DATA   %.2d CHANNEL)\r\n", data.channel );
                    break;

                case IEC_CLOSE:
                    data.secondary = IEC_CLOSE;
                    data.channel = c xor IEC_CLOSE;
                    bus_state = BUS_PROCESS;
                    process_command = true;
                    Debug_printf ( " (E0 CLOSE  %.2d CHANNEL)\r\n", data.channel );
                    break; 
            }

            // Debug_printf ( "code[%.2X] primary[%.2X] secondary[%.2X] bus[%d]", command, data.primary, data.secondary, bus_state );

            // Is this command for us?
            if ( !deviceById(data.device) || !deviceById(data.device)->device_active )
            {
                bus_state = BUS_IDLE;
                process_command = false;
            }
        }

        // If the bus is idle then release the lines
        if ( bus_state < BUS_ACTIVE )
        {
            //Debug_printv("release lines");
            data.init();
            releaseLines();
        }

    }
    else if ( bus_state > BUS_IDLE )
    {
        // If no secondary was set, process primary with defaults
        if ( data.primary > IEC_GLOBAL )
        {
            process_command = true;
        }
    }

    if ( process_command )
    {
        if ( data.secondary == IEC_OPEN || data.secondary == IEC_REOPEN )
        {
            // TODO: switch protocol subclass as needed.
        }

        // Data Mode - Get Command or Data
        if ( data.primary == IEC_LISTEN )
        {
            bus_state = deviceListen();
        }
        else if ( data.primary == IEC_TALK )
        {
            bus_state = deviceTalk();   
        }

        // Queue control codes and command in specified device
        // At the moment there is only the multi-drive device
        device_state_t device_state = deviceById(data.device)->queue_command(&data);

        // Process commands in devices
        // At the moment there is only the multi-drive device
        //Debug_printv( "deviceProcess" );
        if ( deviceById(data.device)->process(&data) < DEVICE_ACTIVE || device_state < DEVICE_ACTIVE )
        {
            //Debug_printv("device idle");
            data.init();
        }

        bus_state = BUS_IDLE;

        flags = CLEAR;

        // Debug_printf( "primary[%.2X] secondary[%.2X] bus_state[%d]", data.primary, data.secondary, bus_state );
        // Debug_printf( "atn[%d] clk[%d] data[%d] srq[%d]", IEC.protocol->status(PIN_IEC_ATN), IEC.protocol->status(PIN_IEC_CLK_IN), IEC.protocol->status(PIN_IEC_DATA_IN), IEC.protocol->status(PIN_IEC_SRQ));
        // protocol->release ( PIN_IEC_SRQ );
    }


    //Debug_printf("command[%.2X] device[%.2d] secondary[%.2d] channel[%.2d]", data.primary, data.device, data.secondary, data.channel);

}

bus_state_t systemBus::deviceTalk ( void )
{
    // Okay, we will talk soon
    // Debug_printf(" (%.2X SECONDARY) (%.2X CHANNEL)\r\n", data.primary, data.channel);

    // Now do bus turnaround
    if (!turnAround())
        return BUS_ERROR;

    // We have recieved a CMD and we should talk now:
    return BUS_PROCESS;
}

bool systemBus::turnAround ()
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

    // Wait until the computer releases the ATN line
    if ( protocol->timeoutWait ( PIN_IEC_ATN, RELEASED, FOREVER ) == TIMED_OUT )
    {
        Debug_printf("Wait until the computer releases the ATN line");
        flags |= ERROR;
        return -1; // return error because timeout
    }

    // Delay after ATN is RELEASED
    fnSystem.delay_microseconds(TIMING_Ttk);

    // Wait until the computer releases the clock line
    if ( protocol->timeoutWait ( PIN_IEC_CLK_IN, RELEASED, FOREVER ) == TIMED_OUT )
    {
        Debug_printf ( "Wait until the computer releases the clock line" );
        flags |= ERROR;
        return -1; // return error because timeout
    }

    release ( PIN_IEC_DATA_OUT );
    fnSystem.delay_microseconds ( TIMING_Tv );
    pull ( PIN_IEC_CLK_OUT );
    fnSystem.delay_microseconds ( TIMING_Tv );

    //Debug_println("turnaround complete");
    return true;
} // turnAround


// this routine will set the direction on the bus back to normal
// (the way it was when the computer was switched on)
bool systemBus::undoTurnAround ()
{
    pull ( PIN_IEC_DATA_OUT );
    release ( PIN_IEC_CLK_OUT );

    // Debug_printf("IEC undoTurnAround: ");

    // Wait until the computer releases the clock line
    if ( protocol->timeoutWait ( PIN_IEC_CLK_IN, RELEASED, FOREVER ) == TIMED_OUT )
    {
        Debug_printf ( "Wait until the computer releases the clock line" );
        flags |= ERROR;
        return -1; // return error because timeout
    }

    // Debug_println("complete");
    return true;
} // undoTurnAround

void systemBus::reset_all_our_devices()
{
   // TODO iterate through our bus and send reset to each device. 
}

void systemBus::releaseLines(bool wait)
{
    // Wait for ATN to release and quit
    if ( wait )
    {
        //Debug_printf("Waiting for ATN to release");
        protocol->timeoutWait ( PIN_IEC_ATN, RELEASED, FOREVER );
    }

    // Release lines
    release ( PIN_IEC_CLK_OUT );
    release ( PIN_IEC_DATA_OUT );
}

void systemBus::setup()
{
    Debug_printf("IEC systemBus::setup()\n");
    protocol = new IecProtocolSerial();
    release ( PIN_IEC_CLK_OUT );
    release ( PIN_IEC_DATA_OUT );
    release ( PIN_IEC_SRQ );

    // initial pin modes in GPIO
    set_pin_mode ( PIN_IEC_ATN, gpio_mode_t::GPIO_MODE_INPUT );
    set_pin_mode ( PIN_IEC_CLK_IN, gpio_mode_t::GPIO_MODE_INPUT );
    set_pin_mode ( PIN_IEC_DATA_IN, gpio_mode_t::GPIO_MODE_INPUT );
    set_pin_mode ( PIN_IEC_SRQ, gpio_mode_t::GPIO_MODE_INPUT );
    set_pin_mode ( PIN_IEC_RESET, gpio_mode_t::GPIO_MODE_INPUT );

    flags = CLEAR;
}

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
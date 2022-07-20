// Meatloaf - A Commodore 64/128 multi-device emulator
// https://github.com/idolpx/meatloaf
// Copyright(C) 2020 James Johnston
//
// Meatloaf is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Meatloaf is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Meatloaf. If not, see <http://www.gnu.org/licenses/>.

#ifndef IEC_H
#define IEC_H

#include <forward_list>
#include <unordered_map>

#include "protocol/cbmstandardserial.h"
//#include "protocol/jiffydos.h"


#define IEC_CMD_MAX_LENGTH  100

using namespace Protocol;


typedef enum
{
    DEVICE_ERROR = -1,
    DEVICE_IDLE = 0,       // Ready and waiting
    DEVICE_LISTEN = 1,     // A command is recieved and data is coming to us
    DEVICE_TALK = 2,       // A command is recieved and we must talk now
} device_state_t;

class IECData
{
    public:
        uint8_t primary;
        uint8_t device;
        uint8_t secondary;
        uint8_t channel;
        std::string device_command;

		void init ( void ) {
			primary = 0;
			device = 0;
			secondary = 0;
			channel = 0;
			device_command = "";
		}
};


class CommandPathTuple
{
    public:
        std::string command;
        std::string fullPath;
        std::string rawPath;
};

class Channel
{
    public:
        std::string url;
        uint32_t cursor;
        bool writing;
};

//
class iecBus; // declare early so can be friend

class iecDevice
{
    protected:
        friend iecBus;

    public:
        // Return values for service:

        std::unordered_map<uint16_t, Channel> channels;

        iecDevice();
        ~iecDevice() {};

        device_state_t queue_command ( void );
        bool process ( void );

        virtual uint8_t command ( void ) = 0;
        virtual uint8_t execute ( void ) = 0;
        virtual uint8_t status ( void ) = 0;

        uint8_t device_id;
        IECData data;
		device_state_t state;

    protected:
        void reset ( void );

        // handler helpers.
        virtual void handleListenCommand ( void ) = 0;
        virtual void handleListenData ( void ) = 0;
        virtual void handleTalk ( uint8_t chan ) = 0;
        virtual void handleOpen ( void );
        virtual void handleClose ( void );

        // Named Channel functions
        Channel currentChannel;
        Channel channelSelect ( void );
        void channelUpdate ( size_t cursor );
        bool channelClose ( bool close_all = false );

        // This is set after an open command and determines what to send next
        uint8_t m_openState;

};


// Return values for service:
typedef enum
{
    BUS_RESET = -2,   // The IEC bus is in a reset state (RESET line).    
    BUS_ERROR = -1,   // A problem occoured, reset communication
    BUS_IDLE = 0,     // Nothing recieved of our concern
    BUS_ACTIVE = 1,   // ATN is pulled and a command byte is expected
    BUS_PROCESS = 2,  // A command is ready to be processed
} bus_state_t;

// IEC commands:
typedef enum
{
    IEC_GLOBAL = 0x00,     // 0x00 + cmd (global command)
    IEC_LISTEN = 0x20,     // 0x20 + device_id (LISTEN) (0-30)
    IEC_UNLISTEN = 0x3F,   // 0x3F (UNLISTEN)
    IEC_TALK = 0x40,       // 0x40 + device_id (TALK) (0-30)
    IEC_UNTALK = 0x5F,     // 0x5F (UNTALK)
    IEC_SECOND = 0x60,     // 0x60 + channel (OPEN CHANNEL) (0-15)
    IEC_CLOSE = 0xE0,      // 0xE0 + channel (CLOSE NAMED CHANNEL) (0-15)
    IEC_OPEN = 0xF0        // 0xF0 + channel (OPEN NAMED CHANNEL) (0-15)
} bus_command_t;

class iecBus
{
    private:
        std::forward_list<iecDevice *> _daisyChain;

        iecDevice *_activeDev = nullptr;
        // sioModem *_modemDev = nullptr;
        // sioFuji *_fujiDev = nullptr;
        // sioNetwork *_netDev[8] = {nullptr};
        // sioMIDIMaze *_midiDev = nullptr;
        // sioCassette *_cassetteDev = nullptr;
        // sioCPM *_cpmDev = nullptr;
        // sioPrinter *_printerdev = nullptr;

    public:
        bus_state_t bus_state;
        device_state_t device_state;
        IECData data;

        CBMStandardSerial protocol;

        iecBus ( void );

        // Initialise iec driver
        bool init();
        // void setup();

        // Checks if CBM is sending an attention message. If this is the case,
        // the message is recieved and stored in iec_data.
        void service ( void );

        void receiveCommand ( void );

        // void shutdown();

        // Checks if CBM is sending a reset (setting the RESET line high). This is typicall
        // when the CBM is reset itself. In this case, we are supposed to reset all states to initial.
//  bool checkRESET();

        // Sends a byte. The communication must be in the correct state: a load command
        // must just have been recieved. If something is not OK, FALSE is returned.
        bool send ( uint8_t data );
        bool send ( std::string data );

        // Same as IEC_send, but indicating that this is the last byte.
        bool sendEOI ( uint8_t data );

        // A special send command that informs file not found condition
        bool sendFNF();

        // Recieves a byte
        int16_t receive ( uint8_t device = 0 );

        // Enabled Device Bit Mask
        uint32_t enabledDevices;
        bool isDeviceEnabled ( const uint8_t deviceNumber );
        void enableDevice ( const uint8_t deviceNumber );
        void disableDevice ( const uint8_t deviceNumber );

        uint8_t numDevices();
        void addDevice ( iecDevice *pDevice, uint8_t device_id );
        void remDevice ( iecDevice *pDevice );
        iecDevice *deviceById ( uint8_t device_id );
        void changeDeviceId ( iecDevice *pDevice, uint8_t device_id );

        void debugTiming();

    private:
        // IEC Bus Commands
        bus_state_t deviceListen ( void ); // 0x20 + device_id   Listen, device (0–30)
		// void deviceUnListen(void);            // 0x3F               Unlisten, all devices
        bus_state_t deviceTalk ( void );   // 0x40 + device_id   Talk, device (0–30)
		// void deviceUnTalk(void);              // 0x5F               Untalk, all devices
		// device_state_t deviceSecond(void);    // 0x60 + channel     Reopen, channel (0–15)
		// device_state_t deviceClose(void);     // 0xE0 + channel     Close, channel (0–15)
		// device_state_t deviceOpen(void);      // 0xF0 + channel     Open, channel (0–15)
		bool turnAround( void );

        void releaseLines ( void );
};

extern iecBus IEC;

#endif // IEC_H

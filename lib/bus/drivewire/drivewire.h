//
// http://www.boisypitre.com/retrocomputing/drivewire/
// https://www.frontiernet.net/~mmarlette/Cloud-9/Hardware/DriveWire3.html
// https://www.cocopedia.com/wiki/index.php/DRIVEWIRE.ZIP
//
// https://sourceforge.net/projects/drivewireserver/
// https://github.com/qbancoffee/drivewire4
// https://github.com/n6il/toolshed/tree/master/hdbdos
//
// https://github.com/MyTDT-Mysoft/COCO-FastLoader
//
// https://www.cocopedia.com/wiki/index.php/Main_Page
// https://github.com/qbancoffee/coco_motherboards
// https://archive.worldofdragon.org/index.php?title=Main_Page
// https://sites.google.com/site/dabarnstudio/drivewire-4-3-4e
// https://sites.google.com/site/dabarnstudio/coco-midi-drivewire
//

#ifndef COCO_H
#define COCO_H

#ifdef ESP32_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#endif

#include <forward_list>
#include <map>
// fnUartBUS (Serial only) was replaced with fnDwCom (Serial|TCP/Becker)
//#include <fnUART.h>
#include "drivewire/dwcom/fnDwCom.h"

#define DRIVEWIRE_BAUDRATE 57600

/* Operation Codes */
#define		OP_NOP		0
#define     OP_JEFF     0xA5
#define     OP_SERREAD  'C'
#define		OP_GETSTAT	'G'
#define		OP_SETSTAT	'S'
#define		OP_READ		'R'
#define		OP_READEX	'R'+128
#define		OP_WRITE	'W'
#define		OP_REREAD	'r'
#define		OP_REREADEX	'r'+128
#define		OP_REWRITE	'w'
#define		OP_INIT		'I'
#define     OP_DWINIT   'Z'
#define		OP_TERM		'T'
#define		OP_TIME		'#'
#define     OP_RESET3   0xF8
#define		OP_RESET2	0xFE
#define		OP_RESET1	0xFF
#define		OP_PRINT	'P'
#define		OP_PRINTFLUSH	'F'
#define     OP_VPORT_READ    'C'
#define     OP_FUJI 0xE2
#define     OP_NET 0xE3
#define     OP_CPM 0xE4

#define FEATURE_EMCEE    0x01
#define FEATURE_DLOAD    0x02
#define FEATURE_HDBDOS   0x04
#define FEATURE_DOSPLUS  0x08
#define FEATURE_PRINTER  0x10
#define FEATURE_SSH      0x20
#define FEATURE_PLAYSND  0x40
#define FEATURE_RESERVED 0x80

#define DWINIT_FEATURES  FEATURE_DLOAD | \
                         FEATURE_HDBDOS | \
                         FEATURE_PRINTER

// struct dwTransferData
// {
// 	int		dw_protocol_vrsn;
// 	FILE		*devpath;
// 	FILE		*dskpath[4];
// 	int		cocoType;
// 	int		baudRate;
// 	unsigned char	lastDrive;
// 	uint32_t	readRetries;
// 	uint32_t	writeRetries;
// 	uint32_t	sectorsRead;
// 	uint32_t	sectorsWritten;
// 	unsigned char	lastOpcode;
// 	unsigned char	lastLSN[3];
// 	unsigned char	lastSector[256];
// 	unsigned char	lastGetStat;
// 	unsigned char	lastSetStat;
// 	uint16_t	lastChecksum;
// 	unsigned char	lastError;
// 	FILE	*prtfp;
// 	unsigned char	lastChar;
// 	char	prtcmd[80];
// };

// EXTERN char device[256];
// EXTERN char dskfile[4][256];
// EXTERN int maxy, maxx;
// EXTERN int updating;
// EXTERN int thread_dead;
// EXTERN FILE *logfp;
// EXTERN WINDOW *window0, *window1, *window2, *window3;
// EXTERN struct dwTransferData datapack;
// EXTERN int interactive;


// This is here because the network protocol adapters speak this
union cmdFrame_t
{
    struct
    {
        uint8_t device;
        uint8_t comnd;
        uint8_t aux1;
        uint8_t aux2;
        uint8_t cksum;
    };
    struct
    {
        uint32_t commanddata;
        uint8_t checksum;
    } __attribute__((packed));
};

// class def'ns
class drivewireModem;          // declare here so can reference it, but define in modem.h
class drivewireFuji;        // declare here so can reference it, but define in fuji.h
class systemBus;      // declare early so can be friend
class drivewireNetwork;     // declare here so can reference it, but define in network.h
class drivewireUDPStream;   // declare here so can reference it, but define in udpstream.h
class drivewireCassette;    // Cassette forward-declaration.
class drivewireCPM;         // CPM device.
class drivewirePrinter;     // Printer device

class virtualDevice
{
protected:
    friend systemBus;

    int _devnum;

    cmdFrame_t cmdFrame;
    bool listen_to_type3_polls = false;
    
    // Optional shutdown/reboot cleanup routine
    virtual void shutdown(){};

public:
    /**
     * @brief Is this virtualDevice holding the virtual disk drive used to boot CONFIG?
     */
    bool is_config_device = false;

    /**
     * @brief is device active (turned on?)
     */
    bool device_active = true;

    /**
     * @brief Get the systemBus object that this virtualDevice is attached to.
     */
    systemBus get_bus();
};

enum drivewire_message : uint16_t
{
    DRIVEWIREMSG_DISKSWAP,  // Rotate disk
    DRIVEWIREMSG_DEBUG_TAPE // Tape debug msg
};

struct drivewire_message_t
{
    drivewire_message message_id;
    uint16_t message_arg;
};

// typedef drivewire_message_t drivewire_message_t;

class systemBus
{
private:
    virtualDevice *_activeDev = nullptr;
    drivewireModem *_modemDev = nullptr;
    drivewireFuji *_fujiDev = nullptr;
    //drivewireNetwork *_netDev[8] = {nullptr};
    drivewireUDPStream *_udpDev = nullptr;
    drivewireCassette *_cassetteDev = nullptr;
    drivewireCPM *_cpmDev = nullptr;
    drivewirePrinter *_printerdev = nullptr;

    void _drivewire_process_cmd();
    void _drivewire_process_queue();

    /**
     * @brief Current Baud Rate
     */
    int _drivewireBaud = DRIVEWIRE_BAUDRATE;

    /**
     * @brief Logical sector number (1-16777216)
     */
    uint32_t lsn;

    /**
     * @brief Drive number (0-255)
     */
    uint8_t drive_num;

    /**
     * @brief Sector data (256 bytes)
     */
    uint8_t sector_data[256];

    /**
     * @brief NOP command (do nothing)
     */
    void op_jeff();
    void op_nop();
    void op_reset();
    void op_readex();
    void op_fuji();
    void op_net();
    void op_cpm();
    void op_write();
    void op_time();
    void op_init();
    void op_dwinit();
    void op_unhandled(uint8_t c);
    void op_getstat();
    void op_setstat();
    void op_serread();
    void op_print();

    // int readSector(struct dwTransferData *dp);
    // int writeSector(struct dwTransferData *dp);
    // int seekSector(struct dwTransferData *dp, int sector);
    // void DoOP_INIT(struct dwTransferData *dp);
    // void DoOP_TERM(struct dwTransferData *dp);
    // void DoOP_RESET(struct dwTransferData *dp);
    // void DoOP_READ(struct dwTransferData *dp, char *logStr);
    // void DoOP_REREAD(struct dwTransferData *dp, char *logStr);
    // void DoOP_READEX(struct dwTransferData *dp, char *logStr);
    // void DoOP_REREADEX(struct dwTransferData *dp, char *logStr);
    // void DoOP_WRITE(struct dwTransferData *dp, char *logStr);
    // void DoOP_REWRITE(struct dwTransferData *dp, char *logStr);
    // void DoOP_GETSTAT(struct dwTransferData *dp);
    // void DoOP_SETSTAT(struct dwTransferData *dp);
    // void DoOP_TERM(struct dwTransferData *dp);
    // void DoOP_TIME(struct dwTransferData *dp);
    // void DoOP_PRINT(struct dwTransferData *dp);
    // void DoOP_PRINTFLUSH(struct dwTransferData *dp);
    // void DoOP_VPORT_READ(struct dwTransferData *dp);
    // char *getStatCode(int statcode);
    // void WinInit(void);
    // void WinSetup(WINDOW *window);
    // void WinUpdate(WINDOW *window, struct dwTransferData *dp);
    // void WinTerm(void);
    // uint16_t computeChecksum(u_char *data, int numbytes);
    // uint16_t computeCRC(u_char *data, int numbytes);
    // int comOpen(struct dwTransferData *dp, const char *device);
    // void comRaw(struct dwTransferData *dp);
    // int comRead(struct dwTransferData *dp, void *data, int numbytes);
    // int comWrite(struct dwTransferData *dp, void *data, int numbytes);
    // int comClose(struct dwTransferData *dp);
    // unsigned int int4(u_char *a);
    // unsigned int int3(u_char *a);
    // unsigned int int2(u_char *a);
    // unsigned int int1(u_char *a);
    // void _int2(uint16_t a, u_char *b);
    // int loadPreferences(struct dwTransferData *datapack);
    // int savePreferences(struct dwTransferData *datapack);
    // void openDSK(struct dwTransferData *dp, int which);
    // void closeDSK(struct dwTransferData *dp, int which);
    // void *DriveWireProcessor(void *dp);
    // void prtOpen(struct dwTransferData *dp);
    // void prtClose(struct dwTransferData *dp);
    // void logOpen(void);
    // void logClose(void);
    // void logHeader(void);
    // void setCoCo(struct dwTransferData* datapack, int cocoType);

public:
    void setup();
    void service();
    void shutdown();

    int getBaudrate();                                          // Gets current DRIVEWIRE baud rate setting
    void setBaudrate(int baud);                                 // Sets DRIVEWIRE to specific baud rate
    void toggleBaudrate();                                      // Toggle between standard and high speed DRIVEWIRE baud rate

    bool shuttingDown = false;                                  // TRUE if we are in shutdown process
    bool getShuttingDown() { return shuttingDown; };
    bool motorActive = false;

    drivewireCassette *getCassette() { return _cassetteDev; }
    drivewirePrinter *getPrinter() { return _printerdev; }
    void setPrinter(drivewirePrinter *_p) { _printerdev = _p; }
    drivewireCPM *getCPM() { return _cpmDev; }
    std::map<uint8_t,drivewireNetwork *> _netDev;

    // I wish this codebase would make up its mind to use camel or snake casing.
    drivewireModem *get_modem() { return _modemDev; }

#ifdef ESP32_PLATFORM
    QueueHandle_t qDrivewireMessages = nullptr;
#endif
};

extern systemBus DRIVEWIRE;

#endif // guard

#ifdef _MAIN
#define EXTERN
#else
#define EXTERN extern
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#if !defined(__sun)
#include <stdint.h>
#endif
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <curses.h>
#include <pthread.h>

#define	REV_MAJOR	3
#define	REV_MINOR	0

#if defined(__APPLE__) || defined(__sun)
#define	FD		_file
#else
#define __LIL_ENDIAN__
#define	FD		_fileno
#endif

/* Operation Codes */
#define		OP_NOP		0
#define		OP_GETSTAT	'G'
#define		OP_SETSTAT	'S'
#define		OP_READ		'R'
#define		OP_READEX	'R'+128
#define		OP_WRITE	'W'
#define		OP_REREAD	'r'
#define		OP_REREADEX	'r'+128
#define		OP_REWRITE	'w'
#define		OP_INIT		'I'
#define		OP_TERM		'T'
#define		OP_TIME		'#'
#define		OP_RESET2	0xFE
#define		OP_RESET1	0xFF
#define		OP_PRINT	'P'
#define		OP_PRINTFLUSH	'F'
#define     OP_VPORT_READ    'C'


struct dwTransferData
{
	int		dw_protocol_vrsn;
	FILE		*devpath;
	FILE		*dskpath[4];
	int		cocoType;
	int		baudRate;
	unsigned char	lastDrive;
	uint32_t	readRetries;
	uint32_t	writeRetries;
	uint32_t	sectorsRead;
	uint32_t	sectorsWritten;
	unsigned char	lastOpcode;
	unsigned char	lastLSN[3];
	unsigned char	lastSector[256];
	unsigned char	lastGetStat;
	unsigned char	lastSetStat;
	uint16_t	lastChecksum;
	unsigned char	lastError;
	FILE	*prtfp;
	unsigned char	lastChar;
	char	prtcmd[80];
};


int readSector(struct dwTransferData *dp);
int writeSector(struct dwTransferData *dp);
int seekSector(struct dwTransferData *dp, int sector);
void DoOP_INIT(struct dwTransferData *dp);
void DoOP_TERM(struct dwTransferData *dp);
void DoOP_RESET(struct dwTransferData *dp);
void DoOP_READ(struct dwTransferData *dp, char *logStr);
void DoOP_REREAD(struct dwTransferData *dp, char *logStr);
void DoOP_READEX(struct dwTransferData *dp, char *logStr);
void DoOP_REREADEX(struct dwTransferData *dp, char *logStr);
void DoOP_WRITE(struct dwTransferData *dp, char *logStr);
void DoOP_REWRITE(struct dwTransferData *dp, char *logStr);
void DoOP_GETSTAT(struct dwTransferData *dp);
void DoOP_SETSTAT(struct dwTransferData *dp);
void DoOP_TERM(struct dwTransferData *dp);
void DoOP_TIME(struct dwTransferData *dp);
void DoOP_PRINT(struct dwTransferData *dp);
void DoOP_PRINTFLUSH(struct dwTransferData *dp);
void DoOP_VPORT_READ(struct dwTransferData *dp);
char *getStatCode(int statcode);
void WinInit(void);
void WinSetup(WINDOW *window);
void WinUpdate(WINDOW *window, struct dwTransferData *dp);
void WinTerm(void);
uint16_t computeChecksum(u_char *data, int numbytes);
uint16_t computeCRC(u_char *data, int numbytes);
int comOpen(struct dwTransferData *dp, const char *device);
void comRaw(struct dwTransferData *dp);
int comRead(struct dwTransferData *dp, void *data, int numbytes);
int comWrite(struct dwTransferData *dp, void *data, int numbytes);
int comClose(struct dwTransferData *dp);
unsigned int int4(u_char *a);
unsigned int int3(u_char *a);
unsigned int int2(u_char *a);
unsigned int int1(u_char *a);
void _int2(uint16_t a, u_char *b);
int loadPreferences(struct dwTransferData *datapack);
int savePreferences(struct dwTransferData *datapack);
void openDSK(struct dwTransferData *dp, int which);
void closeDSK(struct dwTransferData *dp, int which);
void *DriveWireProcessor(void *dp);
void prtOpen(struct dwTransferData *dp);
void prtClose(struct dwTransferData *dp);
void logOpen(void);
void logClose(void);
void logHeader(void);
void setCoCo(struct dwTransferData* datapack, int cocoType);

EXTERN char device[256];
EXTERN char dskfile[4][256];
EXTERN int maxy, maxx;
EXTERN int updating;
EXTERN int thread_dead;
EXTERN FILE *logfp;
EXTERN WINDOW *window0, *window1, *window2, *window3;
EXTERN struct dwTransferData datapack;
EXTERN int interactive;
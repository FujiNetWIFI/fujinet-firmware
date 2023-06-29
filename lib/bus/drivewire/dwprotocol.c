#include "drivewire.h"

void *DriveWireProcessor(void *data)
{
	struct dwTransferData *dp = (struct dwTransferData *)data;

	WinUpdate(window0, dp);

	while (comRead(dp, &(dp->lastOpcode), 1) > 0)
	{
		fd_set	rfds;
		struct timeval	tv;
		char *timeString = NULL;

		{

			switch (dp->lastOpcode)
			{
				case OP_RESET1:
				case OP_RESET2:
					DoOP_RESET(dp);
					break;

				case OP_INIT:
					DoOP_INIT(dp);
					break;

				case OP_TERM:
					DoOP_TERM(dp);
					break;

				case OP_REREAD:
					DoOP_REREAD(dp, "OP_REREAD");
					break;

				case OP_READ:
					DoOP_READ(dp, "OP_READ");
					break;

				case OP_REREADEX:
					DoOP_REREADEX(dp, "OP_REREADEX");
					break;

				case OP_READEX:
					DoOP_READEX(dp, "OP_READEX");
					break;

				case OP_WRITE:
					DoOP_WRITE(dp, "OP_WRITE");
					break;

				case OP_REWRITE:
					DoOP_REWRITE(dp, "OP_REWRITE");
					break;

				case OP_GETSTAT:
					DoOP_GETSTAT(dp);
					break;

				case OP_SETSTAT:
					DoOP_SETSTAT(dp);
					break;

				case OP_TIME:
					DoOP_TIME(dp);
					break;

				case OP_PRINT:
					DoOP_PRINT(dp);
					break;

				case OP_PRINTFLUSH:
					DoOP_PRINTFLUSH(dp);
					break;

				case OP_VPORT_READ:
					DoOP_VPORT_READ(dp);
					break;
                    
				default:
					break;
			}

			fflush(dp->dskpath[dp->lastDrive]);
		}

		WinUpdate(window0, dp);
	}

	return NULL;
}


void DoOP_INIT(struct dwTransferData *dp)
{

	logHeader();
	fprintf(logfp, "OP_INIT\n");

	return;
}


void DoOP_TERM(struct dwTransferData *dp)
{
	logHeader();
	fprintf(logfp, "OP_TERM\n");

	return;
}


void DoOP_REWRITE(struct dwTransferData *dp, char *logStr)
{
	/* 1. Increment retry counter */
	dp->writeRetries++;

	/* 2. Call on WRITE handler */
	DoOP_WRITE(dp, logStr);

	return;
}


void DoOP_WRITE(struct dwTransferData *dp, char *logStr)
{
	u_char cocoChecksum[2];
	u_char response = 0;

	/* 1. Read in drive # and 3 byte LSN */
	comRead(dp, &(dp->lastDrive), 1);
	comRead(dp, dp->lastLSN, 3);

	/* 2. Read in 256 byte lastSector from CoCo */
	comRead(dp, dp->lastSector, 256);

	/* 3. Compute Checksum on sector received */
	if (dp->dw_protocol_vrsn == 1)
	{
		dp->lastChecksum = computeCRC(dp->lastSector, 256);
	}
	else
	{
		dp->lastChecksum = computeChecksum(dp->lastSector, 256);
	}

	/* 4. Read in 2 byte Checksum from CoCo */
	comRead(dp, cocoChecksum, 2);

	/* 5. Compare */
	if (dp->lastChecksum != int2(cocoChecksum))
	{
		response = 0xF3;

		/* 4.1.1 Bad Checksum, ask CoCo to send again, and return */

		comWrite(dp, &response, 1);

		if (logfp != NULL)
		{
			logHeader();
			fprintf(logfp, "%s[%d] LSN[%d] CoCoSum[%d], ServerSum[%d]\n", logStr, dp->lastDrive, int3(dp->lastLSN), int2(cocoChecksum), dp->lastChecksum);
		}
//		printf("WARNING! myChecksum = %X, cocoChecksum = %X\n", dp->lastChecksum, int2(cocoChecksum));

		
		return;
	}

	/* 5. Good Checksum, send positive ACK */
	comWrite(dp, &response, 1);

	/* 6. Seek to LSN in DSK image */
	if (seekSector(dp, int3(dp->lastLSN)) == 0)
	{
		/* 7. Write lastSector to DSK image */
		writeSector(dp);

		/* 8. Everything is ok, send an ok ack */
		comWrite(dp, &response, 1);

		/* 9. Increment sectorsWritten count */
		dp->sectorsWritten++;
	}

	return;
}


void DoOP_RESET(struct dwTransferData *dp)
{
	int a;

	/* 1. Reset counters and flags */
	dp->lastDrive = 0;
	dp->readRetries = 0;
	dp->writeRetries = 0;
	dp->sectorsRead = 0;
	dp->sectorsWritten = 0;
	dp->lastOpcode = OP_RESET1;

	for (a = 0; a < 3; a++)
	{
		dp->lastLSN[a] = 0;
	}

	for (a = 0; a < 256; a++)
	{
		dp->lastSector[a] = 0;
	}

	dp->lastGetStat = 255;
	dp->lastSetStat = 255;
	dp->lastChecksum = 0;
	dp->lastError = 0;

	logHeader();
	fprintf(logfp, "OP_RESET\n");

	/* Finally, sync disks. */
#if 0
	closeDSK(dp, 0);
	closeDSK(dp, 1);
	closeDSK(dp, 2);
	closeDSK(dp, 3);

	openDSK(dp, 0);
	openDSK(dp, 1);
	openDSK(dp, 2);
	openDSK(dp, 3);
#endif
	
	return;
}


void DoOP_REREAD(struct dwTransferData *dp, char *logStr)
{
	/* 1. Increment retry counter */
	dp->readRetries++;

	/* 2. Call on READ handler */
	DoOP_READ(dp, logStr);

	return;
}


void DoOP_READ(struct dwTransferData *dp, char *logStr)
{
	/* 1. Read in drive # and 3 byte LSN */
	comRead(dp, &(dp->lastDrive), 1);
	comRead(dp, dp->lastLSN, 3);

	/* 2. Seek to position in disk image based on LSN received */
	if (seekSector(dp, int3(dp->lastLSN)) == 0)
	{
		/* 3. Read the lastSector at LSN */
		readSector(dp);

		/* 4. Get error value, if any */
		dp->lastError = errno;

		/* 5. Send error code to CoCo */
		comWrite(dp, &(dp->lastError), 1);

		if (dp->lastError == 0)
		{
			u_char cocosum[2];

			/* 5.1.1. No error - send lastSector to CoCo */
			comWrite(dp, dp->lastSector, 256);
	
			/* 5.1.2. Compute Checksum and send to CoCo */
			if (dp->dw_protocol_vrsn == 1)
			{
				dp->lastChecksum = computeCRC(dp->lastSector, 256);
			}
			else
			{
				dp->lastChecksum = computeChecksum(dp->lastSector, 256);
			}
			cocosum[0] = dp->lastChecksum >> 8;
			cocosum[1] = dp->lastChecksum & 0xFF;

			comWrite(dp, (void *)cocosum, 2);

			/* 5.1.3. Increment sectorsRead count */
			dp->sectorsRead++;

			logHeader();
			fprintf(logfp, "%s[%d] LSN[%d] CoCoSum[%d]\n", logStr, dp->lastDrive, int3(dp->lastLSN), int2(cocosum));
		}
	}

	return;
}


void DoOP_REREADEX(struct dwTransferData *dp, char *logStr)
{
	/* 1. Increment retry counter */
	dp->readRetries++;

	/* 2. Call on READ handler */
	DoOP_READEX(dp, logStr);

	return;
}


void DoOP_READEX(struct dwTransferData *dp, char *logStr)
{
	/* 1. Read in drive # and 3 byte LSN */
	comRead(dp, &(dp->lastDrive), 1);
	comRead(dp, dp->lastLSN, 3);

	/* 2. Seek to position in disk image based on LSN received */
	if (seekSector(dp, int3(dp->lastLSN)) == 0)
	{
		/* 3. Read the lastSector at LSN */
		readSector(dp);

		/* 4. Get error value, if any */
		dp->lastError = errno;

		/* 5. Send the sector data to the CoCo */
		comWrite(dp, dp->lastSector, 256);

		/* 6. Read the checksum from the coco */
		u_char cocosum[2];

		comRead(dp, cocosum, 2);

		if (dp->lastError == 0)
		{
			u_char mysum[2];

			dp->lastChecksum = computeChecksum(dp->lastSector, 256);

			mysum[0] = (dp->lastChecksum >> 8) & 0xFF;
			mysum[1] = (dp->lastChecksum << 0) & 0xFF;

			if (cocosum[0] == mysum[0] && cocosum[1] == mysum[1])
			{
				/* Increment sectorsRead count */
				dp->sectorsRead++;

				logHeader();
				fprintf(logfp, "%s[%d] LSN[%d] CoCoSum[%d]\n", logStr, dp->lastDrive, int3(dp->lastLSN), int2(cocosum));
			}

			comWrite(dp, &(dp->lastError), 1);

		}
	}

	return;
}



void DoOP_GETSTAT(struct dwTransferData *dp)
{
	/* 1. Read in drive # and stat code */
	comRead(dp, &(dp->lastDrive), 1);
	comRead(dp, &(dp->lastGetStat), 1);

	logHeader();
	fprintf(logfp, "OP_GETSTAT[%0d] Code[%s]\n", dp->lastDrive, getStatCode(dp->lastGetStat));

	return;
}


void DoOP_SETSTAT(struct dwTransferData *dp)
{
	/* 1. Read in drive # and stat code */
	comRead(dp, &(dp->lastDrive), 1);
	comRead(dp, &(dp->lastSetStat), 1);

	logHeader();
	fprintf(logfp, "OP_SETSTAT[%0d] Code[%s]\n", dp->lastDrive, getStatCode(dp->lastSetStat));

	return;
}


void DoOP_TIME(struct dwTransferData *dp)
{
	time_t	currentTime;
	struct tm *timepak;
	char p[1];


	currentTime = time(NULL);

	timepak = localtime(&currentTime);

	p[0] = timepak->tm_year;
	comWrite(dp, (void *)p, 1);
	p[0] = timepak->tm_mon + 1;
	comWrite(dp, (void *)p, 1);
	p[0] = timepak->tm_mday;
	comWrite(dp, (void *)p, 1);
	p[0] = timepak->tm_hour;
	comWrite(dp, (void *)p, 1);
	p[0] = timepak->tm_min;
	comWrite(dp, (void *)p, 1);
	p[0] = timepak->tm_sec;
	comWrite(dp, (void *)p, 1);
	p[0] = timepak->tm_wday;
	comWrite(dp, (void *)p, 1);

	logHeader();
	fprintf(logfp, "OP_TIME\n");

	return;
}


void DoOP_PRINT(struct dwTransferData *dp)
{
	comRead(dp, &dp->lastChar, 1);
	fwrite(&dp->lastChar, 1, 1, dp->prtfp);

	logHeader();
	fprintf(logfp, "OP_PRINT\n");

	return;
}


void DoOP_PRINTFLUSH(struct dwTransferData *dp)
{
	char buff[128];

	fclose(dp->prtfp);
	sprintf(buff, "cat drivewire.prt %s\n", dp->prtcmd);
	system(buff);
	dp->prtfp = fopen("drivewire.prt", "w+"); // empty it

	logHeader();
	fprintf(logfp, "OP_PRINTFLUSH\n");

	return;
}


void DoOP_VPORT_READ(struct dwTransferData *dp)
{
	logHeader();
	fprintf(logfp, "OP_VPORT_READ\n");
	comWrite(dp, (void *)"\x00\x00", 2);
    
	return;
}


uint16_t computeChecksum(u_char *data, int numbytes)
{
	uint16_t lastChecksum = 0x0000;

	/* Check to see if numbytes is odd or even */
	while (numbytes--)
	{
		lastChecksum += *(data++);
	}

	return(lastChecksum);
}


uint16_t computeCRC(u_char *data, int numbytes)
{
	uint16_t i, crc = 0;
	uint16_t *ptr = (uint16_t *)data;
	
	while(--numbytes >= 0)
	{
		crc = crc ^ *ptr++ << 8;
		
		for (i = 0; i < 8; i++)
		{
			if (crc & 0x8000)
			{
				crc = crc << 1 ^ 0x1021;
			}
			else
			{
				crc = crc << 1;
			}
		}
	}
	
	return (crc & 0xFFFF);
}


int comWrite(struct dwTransferData *dp, void *data, int numbytes)
{
/* Slight delay */
//usleep(10);

	fwrite(data, numbytes, 1, dp->devpath);
//	write(dp->devpath, data, numbytes);

	return(errno);
}


int readSector(struct dwTransferData *dp)
{
	fread(dp->lastSector, 1, 256, dp->dskpath[dp->lastDrive]);

	return(errno);
}


int writeSector(struct dwTransferData *dp)
{
	fwrite(dp->lastSector, 1, 256, dp->dskpath[dp->lastDrive]);

	return(errno);
}


int seekSector(struct dwTransferData *dp, int sector)
{
	if (dp->dskpath[dp->lastDrive] == NULL)
	{
		return -1;	
	}

	fseek(dp->dskpath[dp->lastDrive], sector * 256, SEEK_SET);

	return(errno);
}


int comRead(struct dwTransferData *dp, void *data, int numbytes)
{
	return fread(data, numbytes, 1, dp->devpath);
//	return read(dp->devpath, data, numbytes);
}


void comRaw(struct dwTransferData *dp)
{
	struct termios io_mod;
	int pathid = dp->devpath->FD;

	tcgetattr(pathid, &io_mod);
#if defined(__sun)
	io_mod.c_iflag &= 
		~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL|IXON);
	io_mod.c_oflag &= ~OPOST;
	io_mod.c_lflag &= ~(ECHO|ECHONL|ICANON|ISIG|IEXTEN);
	io_mod.c_cflag &= ~(CSIZE|PARENB);
	io_mod.c_cflag |= CS8;

	cfsetospeed(&io_mod, dp->baudRate);
#else
	cfmakeraw(&io_mod);
	cfsetspeed(&io_mod, dp->baudRate);
#endif

	if (tcsetattr(pathid, TCSANOW, &io_mod) < 0)
	{
		perror("ioctl error");
	}
}



unsigned int int4(u_char *a)
{
	return (unsigned int)( (((u_char)*a)<<24) + ((u_char)*(a+1)<<16) + ((u_char)*(a+2)<<8) + (u_char)*(a+3) );
}


unsigned int int3(u_char *a)
{
	return(unsigned int)( (((u_char)*a)<<16) + ((u_char)*(a+1)<<8) + (u_char)*(a+2) );
}


unsigned int int2(u_char *a)
{
//#ifndef __BIG_ENDIAN__
//	return(unsigned int)( (((u_char)*(a+1))<<8) + (u_char)*a );
//#else
	return(unsigned int)( (((u_char)*a)<<8) + (u_char)*(a+1) );
//#endif
}


unsigned int int1(u_char *a)
{
	return(unsigned int)( ((u_char)*a) );
}


void _int4(unsigned int a, u_char *b)
{
	b[0] = ((a >> 24) & 0xFF); b[1] = ((a >> 16) & 0xFF); b[2] = ((a >> 8) & 0xFF); b[3] = (a & 0xFF);
}


void _int3(unsigned int a, u_char *b)
{
	b[0] = ((a >> 16) & 0xFF); b[1] = ((a >> 8)  & 0xFF); b[2] = (a & 0xFF);
}


void _int2(uint16_t a, u_char *b)
{
	b[0] = ((a >> 8)  & 0xFF); b[1] = (a & 0xFF);
}


void _int1(unsigned int a, u_char *b)
{
	b[0] = (a & 0xFF);
}

char *getStatCode(int statcode)
{
	static char codeName[64];

	switch (statcode)
	{
		case 0x00:
			strcpy(codeName, "SS.Opt");
			break;

		case 0x02:
			strcpy(codeName, "SS.Size");
			break;

		case 0x03:
			strcpy(codeName, "SS.Reset");
			break;

		case 0x04:
			strcpy(codeName, "SS.WTrk");
			break;

		case 0x05:
			strcpy(codeName, "SS.Pos");
			break;

		case 0x06:
			strcpy(codeName, "SS.EOF");
			break;

		case 0x0A:
			strcpy(codeName, "SS.Frz");
			break;

		case 0x0B:
			strcpy(codeName, "SS.SPT");
			break;

		case 0x0C:
			strcpy(codeName, "SS.SQD");
			break;

		case 0x0D:
			strcpy(codeName, "SS.DCmd");
			break;

		case 0x0E:
			strcpy(codeName, "SS.DevNm");
			break;

		case 0x0F:
			strcpy(codeName, "SS.FD");
			break;

		case 0x10:
			strcpy(codeName, "SS.Ticks");
			break;

		case 0x11:
			strcpy(codeName, "SS.Lock");
			break;

		case 0x12:
			strcpy(codeName, "SS.VarSect");
			break;

		case 0x14:
			strcpy(codeName, "SS.BlkRd");
			break;

		case 0x15:
			strcpy(codeName, "SS.BlkWr");
			break;

		case 0x16:
			strcpy(codeName, "SS.Reten");
			break;

		case 0x17:
			strcpy(codeName, "SS.WFM");
			break;

		case 0x18:
			strcpy(codeName, "SS.RFM");
			break;

		case 0x1B:
			strcpy(codeName, "SS.Relea");
			break;

		case 0x1C:
			strcpy(codeName, "SS.Attr");
			break;

		case 0x1E:
			strcpy(codeName, "SS.RsBit");
			break;

		case 0x20:
			strcpy(codeName, "SS.FDInf");
			break;

		case 0x26:
			strcpy(codeName, "SS.DSize");
			break;

		case 255:
			strcpy(codeName, "None");
			break;

		default:
			strcpy(codeName, "???");
			break;
	}

	return(codeName);
}
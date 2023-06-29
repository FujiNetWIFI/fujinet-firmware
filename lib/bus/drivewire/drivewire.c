#define _MAIN
#include "drivewire.h"
#include <getopt.h>

void sighandler(int signum)
{
	switch (signum)
	{
		case SIGUSR1:
			thread_dead = 1;
			pthread_exit(NULL);
			break;
		case SIGHUP:
			closeDSK(&datapack, 0);
			closeDSK(&datapack, 1);
			closeDSK(&datapack, 2);
			closeDSK(&datapack, 3);
			openDSK(&datapack, 0);
			openDSK(&datapack, 1);
			openDSK(&datapack, 2);
			openDSK(&datapack, 3);
			break;
	}
}


int comOpen(struct dwTransferData *dp, const char *device)
{
	char devname[128];


	sprintf(devname, "/dev/%s", device);

	/* Open serial port. */

//	dp->devpath = open(devname, S_IREAD | S_IWRITE);
	dp->devpath = fopen(devname, "a+");

	if (dp->devpath != NULL)
	{
		return 0;
	}

	return -1;
}


int comClose(struct dwTransferData *dp)
{
	close(dp->devpath->FD);

	return 0;
}

void setCoCo(struct dwTransferData *dp, int cocoType)
{
	dp->cocoType = cocoType;
	switch (cocoType)
	{
		case 3:
			switch (dp->dw_protocol_vrsn)
			{
				case 3:
					dp->baudRate = B115200;
					break;
				default:
					dp->baudRate = B57600;
					break;
			}
			break;

		default:
			switch (dp->dw_protocol_vrsn)
			{
				case 3:
					dp->baudRate = B57600;
					break;
				case 2:
					dp->baudRate = B38400;
					break;
			}
			break;

	}

}

void openDSK(struct dwTransferData *dp, int which)
{
	dp->dskpath[which] = fopen(dskfile[which], "r+");

	if (dp->dskpath[which] == NULL)
	{
		strcpy(dskfile[which], "");
	}
}


void closeDSK(struct dwTransferData *dp, int which)
{
	if (dp->dskpath[which] != NULL)
	{
		fclose(dp->dskpath[which]);
		dp->dskpath[which] = NULL;
	}
}


int loadPreferences(struct dwTransferData *datapack)
{
	FILE	*pf;
	char	buffer[81];
	int	i;
	char	*p;


	pf = fopen(".drivewirerc", "r");

	if (pf == NULL)
	{
		return errno;
	}

	for (i = 0; i < 4; i++)
	{
		fgets((char *)dskfile[i], 128, pf);
		p = strchr(dskfile[i], '\n');
		if (p != NULL) { *p = '\0'; }
	}

	fgets((char *)device, 128, pf);
	p = strchr(device, '\n');
	if (p != NULL) { *p = '\0'; }
	fgets(buffer, 128, pf);
	datapack->cocoType = atoi(buffer);
	fgets(buffer, 128, pf);
	datapack->dw_protocol_vrsn = atoi(buffer);
	setCoCo(datapack, datapack->cocoType);
	fgets(datapack->prtcmd, 128, pf);

	return 0;
}


int savePreferences(struct dwTransferData *datapack)
{
	FILE	*pf;
	char	buffer[81];
	int	i;


	pf = fopen(".drivewirerc", "w");

	if (pf == NULL)
	{
		return errno;
	}

	for (i = 0; i < 4; i++)
	{
		fprintf(pf, "%s\n", dskfile[i]);
	}

	fprintf(pf, "%s\n", device);
	fprintf(pf, "%d\n", datapack->cocoType);
	fprintf(pf, "%d\n", datapack->dw_protocol_vrsn);
	fprintf(pf, "%s\n", datapack->prtcmd);

	return 0;
}


void prtOpen(struct dwTransferData *datapack)
{
	datapack->prtfp = fopen("drivewire.prt", "a+");
}


void prtClose(struct dwTransferData *datapack)
{
	fclose(datapack->prtfp);

	datapack->prtfp = NULL;
}


void logOpen(void)
{
	logfp = fopen("drivewire.log", "a+");
}


void logClose(void)
{
	fclose(logfp);

	logfp = NULL;
}


void logHeader(void)
{
	time_t	currentTime;
	struct tm *timepak;

	currentTime = time(NULL);
	timepak = localtime(&currentTime);

	fprintf(logfp, "%04d-%02d-%02d %02d:%02d:%02d (%02d)", 
		1900 + timepak->tm_year,
		timepak->tm_mon + 1,
		timepak->tm_mday,
		timepak->tm_hour,
		timepak->tm_min,
		timepak->tm_sec,
		timepak->tm_wday);

	return;
}

int main(int argc, char **argv)
{
    int i;
    pthread_t thread_id;
    int quitter = 0;

    interactive = true;

    signal(SIGUSR1, sighandler);
    signal(SIGHUP, sighandler);

    datapack.dw_protocol_vrsn = 3;

    if (loadPreferences(&datapack) != 0)
    {
#if defined(__APPLE__)
        strcpy(device, "ttysc");
#elif defined(__sun)
        strcpy(device, "ttya");
#else
        strcpy(device, "ttyS0");
#endif
        strcpy(dskfile[0], "disk0");
        strcpy(dskfile[1], "disk1");
        strcpy(dskfile[2], "disk2");
        strcpy(dskfile[3], "disk3");
        setCoCo(&datapack, 3); // assume CoCo 3
        // change EOLs and send to printer
        strcpy(datapack.prtcmd, "| tr \"\\r\" \"\\n\" | lpr");
        // change EOLs and move to file
        // strcpy(datapack.prtcmd, "| tr \"\\r\" \"\\n\" > printeroutput.txt");
    }

    /* parse arguments */
    char c;
    while ((c = getopt (argc, argv, "h?ip:0:1:2:3:")) != -1)
    {
      switch (c)
        {
        case 'i':
          interactive = false;
          break;
        case '0':
          strcpy(dskfile[0], optarg);
          break;
        case '1':
          strcpy(dskfile[1], optarg);
          break;
        case '2':
          strcpy(dskfile[2], optarg);
          break;
        case '3':
          strcpy(dskfile[3], optarg);
          break;
        case 'p':
          strcpy(device, optarg);
          break;
        case 'h':
        case '?':
          if (optopt == 'p')
            fprintf (stderr, "Option -%c requires an argument.\n", optopt);
          else if (isprint (optopt))
          {
                fprintf (stderr, "Usage: drivewire\n");
                fprintf (stderr, "    -0 <file>     disk image for drive 0\n");
                fprintf (stderr, "    -1 <file>     disk image for drive 1\n");
                fprintf (stderr, "    -2 <file>     disk image for drive 2\n");
                fprintf (stderr, "    -3 <file>     disk image for drive 3\n");
                fprintf (stderr, "    -i            non-interactive mode\n");
                fprintf (stderr, "    -p <port>     serial port\n");
          }
          else
            fprintf (stderr,
                     "Unknown option character `\\x%x'.\n",
                     optopt);
          return 1;
        default:
          abort ();
        }
    }


    if (comOpen(&datapack, device) < 0)
    {
        fprintf(stderr, "Couldn't open %s (error %d)\n", device, errno);
        
        exit(0);
    }
    
    openDSK(&datapack, 0);
    openDSK(&datapack, 1);
    openDSK(&datapack, 2);
    openDSK(&datapack, 3);
    prtOpen(&datapack);
    logOpen();
    
    DoOP_RESET(&datapack);

    comRaw(&datapack);

    WinInit();

//    loadPreferences(&datapack);

    pthread_create(&thread_id, NULL, DriveWireProcessor, (void *)&datapack);

    while (quitter == 0)
    {
        if (interactive == false)
        {
            continue;
        }
        
        char c;

        noecho();

        c = tolower(wgetch(window0));

        echo();

        switch(c)
        {
            case 'q':
                quitter = 1;
                break;

            case 'r':
                DoOP_RESET(&datapack);
                WinUpdate(window0, &datapack);
                break;

            case 'm':
                if (datapack.dw_protocol_vrsn == 1)
                {
                    datapack.dw_protocol_vrsn = 2;
                }
                else if (datapack.dw_protocol_vrsn == 2)
                {
                    datapack.dw_protocol_vrsn = 3;
                }
                else
                {
                    datapack.dw_protocol_vrsn = 1;
                }
                setCoCo(&datapack, datapack.cocoType);
                comRaw(&datapack);
                WinUpdate(window0, &datapack);
                break;

            case 'c':
                if (datapack.cocoType == 3)
                {
                    setCoCo(&datapack, 2);
                }
                else
                {
                    setCoCo(&datapack, 3);
                }
                comRaw(&datapack);
                WinUpdate(window0, &datapack);
                break;

            case '0':
            case '1':
            case '2':
            case '3':
                {
                    int which = c-'0';
                    char tmpfile[128];

                    updating = 1;
                    wmove(window0, 18+which, 20);
                    wclrtoeol(window0);
                    wgetstr(window0, tmpfile);

                    if (tmpfile[0] != '\0')
                    {
                        strcpy(dskfile[which], tmpfile);
                        closeDSK(&datapack, which);
                        openDSK(&datapack, which);
                    }
                    updating = 0;
                    WinUpdate(window0, &datapack);
                    break;
                }

            case 'p':
                {
                    char newSerial[128];
                    struct dwTransferData temp;

                    updating = 1;

                    wmove(window0, 14, 20);
                    wclrtoeol(window0);
                    wgetstr(window0, newSerial);
                    if (comOpen(&temp, newSerial)== 0)
                    {
                        comClose(&datapack);
                        datapack.devpath = temp.devpath;
                        strcpy(device, newSerial);
                        comRaw(&datapack);
                    }

                    updating = 0;
                    WinUpdate(window0, &datapack);
                    break;
                }

            case 'l':
                {
                    char newCmd[80];
                    updating = 1;

                    wmove(window0, 16, 20);
                    wclrtoeol(window0);
                    wgetstr(window0, newCmd);
                    if (newCmd[0] != '\0')
                        strcpy(datapack.prtcmd, newCmd);

                    updating = 0;
                    WinUpdate(window0, &datapack);
                    break;
                }
        }
    }

    pthread_kill(thread_id, SIGUSR1);

    /* Wait for thread to die. */
    while (thread_dead == 0);

    closeDSK(&datapack, 0);
    closeDSK(&datapack, 1);
    closeDSK(&datapack, 2);
    closeDSK(&datapack, 3);
    prtClose(&datapack);
    logClose();

    savePreferences(&datapack);

    comClose(&datapack);

    WinTerm();
}


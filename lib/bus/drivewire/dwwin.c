// #include "drivewire.h"

// void WinInit(void)
// {
//     if (interactive == false)
//     {
//         return;
//     }
    
// 	int y = 1;

// 	initscr();
// 	clear();
// 	window0 = newwin(24, 80, 0, 0);

// 	if (window0 == NULL)
// 	{
// 		printf("window must be at least 80x24!\n");
// 		exit(0);
// 	}

// 	wattron(window0, A_STANDOUT);
// 	wprintw(window0, "DriveWire Server v%d.%d (C) 2009 Boisy G. Pitre", REV_MAJOR, REV_MINOR);
// 	wattroff(window0, A_STANDOUT);


// 	WinSetup(window0);
// }


// void WinSetup(WINDOW *window)
// {
//     if (interactive == false)
//     {
//         return;
//     }
    
// 	int y = 2;

// 	wmove(window, y++, 1);
// 	wprintw(window, "Last OpCode     :");
// 	wmove(window, y++, 1);
// 	wprintw(window, "Sectors Read    :");
// 	wmove(window, y++, 1);
// 	wprintw(window, "Sectors Written :");
// 	wmove(window, y++, 1);
// 	wprintw(window, "Last LSN        :");
// 	wmove(window, y++, 1);
// 	wprintw(window, "Read Retries    :");
// 	wmove(window, y++, 1);
// 	wprintw(window, "Write Retries   :");
// 	wmove(window, y++, 1);
// 	wprintw(window, "%% Good Reads    :");
// 	wmove(window, y++, 1);
// 	wprintw(window, "%% Good Writes   :");
// 	wmove(window, y++, 1);
// 	wprintw(window, "Last GetStat    :");
// 	wmove(window, y++, 1);
// 	wprintw(window, "Last SetStat    :");
//         y++;
// 	wmove(window, y++, 1);
// 	wprintw(window, "CoCo Type       :");
// 	wmove(window, y++, 1);
// 	wprintw(window, "Serial Port     :");
// 	wmove(window, y++, 1);
// 	wprintw(window, "DriveWire Mode  :");
// 	wmove(window, y++, 1);
// 	wprintw(window, "Print Command   :");
//         y++;
// 	wmove(window, y++, 1);
// 	wprintw(window, "Disk 0          :");
// 	wmove(window, y++, 1);
// 	wprintw(window, "Disk 1          :");
// 	wmove(window, y++, 1);
// 	wprintw(window, "Disk 2          :");
// 	wmove(window, y++, 1);
// 	wprintw(window, "Disk 3          :");

//         y++;
// 	wmove(window, y++, 1);

// 	wattron(window, A_STANDOUT);
// 	wprintw(window, "[0-3] Disk   [C]oCo   [P]ort   [R]eset   [M]ode   [L]Print   [Q]uit");
// 	wattroff(window, A_STANDOUT);

// 	/* 2. Refresh */
// 	wrefresh(window);

// 	return;
// }


// void WinUpdate(WINDOW *window, struct dwTransferData *dp)
// {
//     if (interactive == false)
//     {
//         return;
//     }
    
// 	int x = 20;
// 	int y = 2;
// 	int i;

// 	if (updating == 1)
// 	{
// 		return;
// 	}

// 	wmove(window, y++, x);
// 	wclrtoeol(window);
// 	switch (dp->lastOpcode)
// 	{
// 		case OP_NOP:
// 			wprintw(window, "OP_NOP");
// 			break;

// 		case OP_INIT:
// 			wprintw(window, "OP_INIT");
// 			break;

// 		case OP_READ:
// 			wprintw(window, "OP_READ");
// 			break;

// 		case OP_READEX:
// 			wprintw(window, "OP_READEX");
// 			break;

// 		case OP_WRITE:
// 			wprintw(window, "OP_WRITE");
// 			break;

// 		case OP_REREAD:
// 			wprintw(window, "OP_REREAD");
// 			break;

// 		case OP_REREADEX:
// 			wprintw(window, "OP_REREADEX");
// 			break;

// 		case OP_REWRITE:
// 			wprintw(window, "OP_REWRITE");
// 			break;

// 		case OP_TERM:
// 			wprintw(window, "OP_TERM");
// 			break;

// 		case OP_RESET1:
// 		case OP_RESET2:
// 			wprintw(window, "OP_RESET");
// 			break;

// 		case OP_GETSTAT:
// 			wprintw(window, "OP_GETSTAT");
// 			break;

// 		case OP_SETSTAT:
// 			wprintw(window, "OP_SETSTAT");
// 			break;

// 		case OP_TIME:
// 			wprintw(window, "OP_TIME");
// 			break;

// 		case OP_PRINT:
// 			wprintw(window, "OP_PRINT");
// 			break;

// 		case OP_PRINTFLUSH:
// 			wprintw(window, "OP_PRINTFLUSH");
// 			break;

// 		default:
// 			wprintw(window, "UNKNOWN (%d)", dp->lastOpcode);
// 			break;
// 	}

// 	wmove(window, y++, x); wclrtoeol(window);
// 	wprintw(window, "%d", dp->sectorsRead);
// 	wmove(window, y++, x); wclrtoeol(window);
// 	wprintw(window, "%d", dp->sectorsWritten);
// 	wmove(window, y++, x); wclrtoeol(window);
// 	wprintw(window, "%d", int3(dp->lastLSN));
// 	wmove(window, y++, x); wclrtoeol(window);
// 	wprintw(window, "%d", dp->readRetries);
// 	wmove(window, y++, x); wclrtoeol(window);
// 	wprintw(window, "%d", dp->writeRetries);
// 	wmove(window, y++, x); wclrtoeol(window);
// 	if (dp->sectorsRead + dp->readRetries == 0)
// 	{
// 		wprintw(window, "0%%");
// 	}
// 	else
// 	{
// 		float percent = ((float)dp->sectorsRead / ((float)dp->sectorsRead + (float)dp->readRetries)) * 100;
// 		wprintw(window, "%3.3f%%", percent);
// 	}
// 	wmove(window, y++, x); wclrtoeol(window);
// 	if (dp->sectorsWritten + dp->writeRetries == 0)
// 	{
// 		wprintw(window, "0%%");
// 	}
// 	else
// 	{
// 		float percent = ((float)dp->sectorsWritten / ((float)dp->sectorsWritten + (float)dp->writeRetries)) * 100;
// 		wprintw(window, "%3.3f%%", percent);
// 	}
// 	wmove(window, y++, x); wclrtoeol(window);
// 	wprintw(window, "$%02X (%s)", dp->lastGetStat, getStatCode(dp->lastGetStat));
// 	wmove(window, y++, x); wclrtoeol(window);
// 	wprintw(window, "$%02X (%s)", dp->lastSetStat, getStatCode(dp->lastSetStat));
// 	wmove(window, y++, x); wclrtoeol(window);
// 	wmove(window, y++, x); wclrtoeol(window);
// 	switch (dp->cocoType)
// 	{
// 		case 3:
// 			if (dp->dw_protocol_vrsn == 3)
// 			{
// 				wprintw(window, "%s", "CoCo 3 (115200 baud)");
// 			}
// 			else
// 			{
// 				wprintw(window, "%s", "CoCo 3 (57600 baud)");
// 			}
// 			break;

// 		case 2:
// 			if (dp->dw_protocol_vrsn == 3)
// 			{
// 				wprintw(window, "%s", "CoCo 2 (57600 baud)");
// 			}
// 			else
// 			{
// 				wprintw(window, "%s", "CoCo 1/2 (38400 baud)");
// 			}
// 			break;
// 	}
// 	wmove(window, y++, x); wclrtoeol(window);
// 	wprintw(window, "%s", device);
// 	wmove(window, y++, x); wclrtoeol(window);
// 	switch (dp->dw_protocol_vrsn)
// 	{
// 		case 3:
// 			wprintw(window, "3.0");
// 			break;
// 		case 2:
// 			wprintw(window, "2.0");
// 			break;
// 		case 1:
// 			wprintw(window, "1.0");
// 			break;
// 	}

// 	wclrtoeol(window);

// 	wmove(window, y++, x); wclrtoeol(window);
// 	wprintw(window, dp->prtcmd);

// 	wmove(window, y++, x); wclrtoeol(window);

// 	for (i = 0; i < 4; i++)
// 	{
// 		wmove(window, y++, x); wclrtoeol(window);
// 		wprintw(window, "%s", dskfile[i]);
// 	}

// 	/* 2. Refresh */
// 	wrefresh(window);

// 	return;
// }


// void WinTerm(void)
// {
//     if (interactive == false)
//     {
//         return;
//     }
    
// 	endwin();

// 	return;
// }
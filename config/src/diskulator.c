/**
 * Fujinet Configurator
 *
 * Diskulator
 */

#include <atari.h>
#include <string.h>
#include <peekpoke.h>
#include <conio.h>
#include <stdbool.h>
#include <stdlib.h>
#include "diskulator.h"
#include "screen.h"
#include "sio.h"
#include "bar.h"
#include "die.h"
#include "info.h"

unsigned char prefix[256] = "/";
unsigned char path[256];
unsigned char current_entry[FILE_MAXLEN];
unsigned char c;
unsigned char o;
unsigned char files[FILES_MAXCOUNT][FILE_MAXLEN];
unsigned char diskulator_done = false;
unsigned char selected_host;
unsigned char filter[32];
unsigned char prev_consol;
bool host_done = false;
bool slot_done = true;
bool selector_done = false;
bool drive_done = false;
unsigned char k;

extern unsigned char *video_ptr;
extern unsigned char *dlist_ptr;
extern unsigned short screen_memory;
extern unsigned char *font_ptr;

union {
	unsigned char host[8][32];
	unsigned char rawData[256];
} hostSlots;

union {
	struct
	{
		unsigned char hostSlot;
		unsigned char mode;
		unsigned char file[FILE_MAXLEN];
	} slot[8];
	unsigned char rawData[304];
} deviceSlots;

union {
	struct
	{
		unsigned short numSectors;
		unsigned short sectorSize;
		unsigned char hostSlot;
		unsigned char deviceSlot;
		char filename[FILE_MAXLEN];
	};
	unsigned char rawData[42];
} newDisk;

/**
 * Do coldstart
 */
void diskulator_boot(void)
{
	asm("jmp $E477");
}

/**
 * Read host slots
 */
void diskulator_read_host_slots(void)
{
	// Query for host slots
	OS.dcb.ddevic = 0x70;
	OS.dcb.dunit = 1;
	OS.dcb.dcomnd = 0xF4; // Get host slots
	OS.dcb.dstats = 0x40;
	OS.dcb.dbuf = &hostSlots.rawData;
	OS.dcb.dtimlo = 0x0f;
	OS.dcb.dbyt = 256;
	OS.dcb.daux = 0;
	siov();
}

/**
 * Write host slots
 */
void diskulator_write_host_slots(void)
{
	OS.dcb.ddevic = 0x70;
	OS.dcb.dunit = 1;
	OS.dcb.dcomnd = 0xF3;
	OS.dcb.dstats = 0x80;
	OS.dcb.dbuf = &hostSlots.rawData;
	OS.dcb.dtimlo = 0x0f;
	OS.dcb.dbyt = 256;
	OS.dcb.daux = 0;
	siov();
}

/**
 * Mount a host slot
 */
void diskulator_mount_host(unsigned char c)
{
	if (hostSlots.host[c][0] != 0x00)
	{
		OS.dcb.ddevic = 0x70;
		OS.dcb.dunit = 1;
		OS.dcb.dcomnd = 0xF9;
		OS.dcb.dstats = 0x00;
		OS.dcb.dbuf = NULL;
		OS.dcb.dtimlo = 0x0f;
		OS.dcb.dbyt = 0;
		OS.dcb.daux = c;
		siov();
	}
}

/**
 * Read drive tables
 */
void diskulator_read_device_slots(void)
{
	// Read Drive Tables
	OS.dcb.ddevic = 0x70;
	OS.dcb.dunit = 1;
	OS.dcb.dcomnd = 0xF2;
	OS.dcb.dstats = 0x40;
	OS.dcb.dbuf = &deviceSlots.rawData;
	OS.dcb.dtimlo = 0x0f;
	OS.dcb.dbyt = sizeof(deviceSlots.rawData);
	OS.dcb.daux = 0;
	siov();
}

/**
 * Write drive tables
 */
void diskulator_write_device_slots(void)
{
	OS.dcb.ddevic = 0x70;
	OS.dcb.dunit = 1;
	OS.dcb.dcomnd = 0xF1;
	OS.dcb.dstats = 0x80;
	OS.dcb.dbuf = &deviceSlots.rawData;
	OS.dcb.dtimlo = 0x0f;
	OS.dcb.dbyt = sizeof(deviceSlots.rawData);
	OS.dcb.daux = 0;
	siov();
}

/**
 * Mount device slot
 */
void diskulator_mount_device(unsigned char c, unsigned char o)
{
	OS.dcb.ddevic = 0x70;
	OS.dcb.dunit = 1;
	OS.dcb.dcomnd = 0xF8;
	OS.dcb.dstats = 0x00;
	OS.dcb.dbuf = NULL;
	OS.dcb.dtimlo = 0x0f;
	OS.dcb.dbyt = 0;
	OS.dcb.daux1 = c;
	OS.dcb.daux2 = o;
	siov();
}

/**
 * Mount device slot
 */
void diskulator_umount_device(unsigned char c)
{
	OS.dcb.ddevic = 0x70;
	OS.dcb.dunit = 1;
	OS.dcb.dcomnd = 0xE9;
	OS.dcb.dstats = 0x00;
	OS.dcb.dbuf = NULL;
	OS.dcb.dtimlo = 0x0f;
	OS.dcb.dbyt = 0;
	OS.dcb.daux = c;
	siov();
}

/**
 * Create New Disk
 */
void diskulator_new_disk(unsigned char c, unsigned short ns, unsigned short ss)
{
	newDisk.numSectors = ns;
	newDisk.sectorSize = ss;
	newDisk.hostSlot = deviceSlots.slot[c].hostSlot;
	newDisk.deviceSlot = c;
	strcpy(newDisk.filename, deviceSlots.slot[c].file);
	deviceSlots.slot[c].mode = 0x02;

	OS.dcb.ddevic = 0x70;
	OS.dcb.dunit = 1;
	OS.dcb.dcomnd = 0xE7; // TNFS Create Disk
	OS.dcb.dstats = 0x80;
	OS.dcb.dbuf = &newDisk.rawData;
	OS.dcb.dtimlo = 0xFE;
	OS.dcb.dbyt = sizeof(newDisk.rawData);
	OS.dcb.daux = 0;
	siov();
}

/**
 * Open Directory
 */
void diskulator_open_directory(unsigned char hs, char *p)
{
	// Open Dir
	OS.dcb.ddevic = 0x70;
	OS.dcb.dunit = 1;
	OS.dcb.dcomnd = 0xF7;
	OS.dcb.dstats = 0x80;
	OS.dcb.dbuf = p;
	OS.dcb.dtimlo = 0x0F;
	OS.dcb.dbyt = 256;
	OS.dcb.daux = hs;
	siov();
}

/**
 * Read next dir entry
 */
void diskulator_read_directory(unsigned char hs, char *e, unsigned short len)
{
	memset(e, 0, len);
	e[0] = 0x7f;
	OS.dcb.dcomnd = 0xF6;
	OS.dcb.dstats = 0x40;
	OS.dcb.dbuf = e;
	OS.dcb.dbyt = len;
	OS.dcb.daux1 = len;
	OS.dcb.daux2 = hs;
	siov();
}

/**
 * Close directory
 */
void diskulator_close_directory(unsigned char hs)
{
	// Close dir read
	OS.dcb.dcomnd = 0xF5;
	OS.dcb.dstats = 0x00;
	OS.dcb.dbuf = NULL;
	OS.dcb.dtimlo = 0x0f;
	OS.dcb.dbyt = 0;
	OS.dcb.daux = hs;
	siov();
}

/**
 * Mount all Hosts
 */
void diskulator_mount_all_hosts(void)
{
	unsigned char e;

	bar_clear();
	screen_clear();

	screen_puts(0, 0, "MOUNTING ALL HOSTS");

	for (e = 0; e < 8; e++)
	{
		if (deviceSlots.slot[e].hostSlot != 0xFF)
		{
			diskulator_mount_host(deviceSlots.slot[e].hostSlot);
		}

		if (OS.dcb.dstats != 0x01)
		{
			screen_puts(0, 21, "MOUNT ERROR!");
			die();
		}
	}
}

/**
 * Mount all devices
 */
void diskulator_mount_all_devices(void)
{
	unsigned char e;

	bar_clear();
	screen_clear();

	screen_puts(0, 0, "MOUNTING ALL DEVICES");

	for (e = 0; e < 8; e++)
	{
		if (deviceSlots.slot[e].hostSlot != 0xFF)
			diskulator_mount_device(e, deviceSlots.slot[e].mode);

		if (OS.dcb.dstats != 0x01)
		{
			screen_puts(0, 21, "MOUNT ERROR!");
			die();
		}
	}
}

/**
 * Enter a diskulator Host
 */
bool diskulator_host(void)
{
	char tmp_str[8];
	char disk_type;
	unsigned short ns;
	unsigned short ss;
	bool ret = false;

	host_done = false;
	slot_done = true;
	k = 0;

	screen_clear();
	bar_clear();

	// Temporarily patch display list for this screen.
	POKE(0x60F, 6);
	POKE(0x610, 6);
	POKE(0x61B, 2);
	POKE(0x61C, 2);

	screen_puts(0, 0, "   TNFS HOST LIST   ");

	diskulator_read_host_slots();

	if (OS.dcb.dstats != 0x01)
	{
		screen_puts(21, 0, "COULD NOT GET HOSTS!");
		die();
	}

	// Display host slots
	for (c = 0; c < 8; c++)
	{
		unsigned char n = c + 1;
		unsigned char nc[2];

		utoa(n, nc, 10);
		screen_puts(2, c + 1, nc);

		if (hostSlots.host[c][0] != 0x00)
			screen_puts(4, c + 1, hostSlots.host[c]);
		else
			screen_puts(4, c + 1, "Empty");
	}

	// Display Device Slots
	diskulator_read_device_slots();

	screen_puts(20, 9, "    DRIVE SLOTS    ");

	// Display drive slots
	for (c = 0; c < 8; c++)
	{
		unsigned char d[4];
		d[0] = 'D';
		d[1] = 0x31 + c;
		d[2] = ':';
		d[3] = 0x00;
		screen_puts(0, c + 11, d);
		screen_puts(4, c + 11, deviceSlots.slot[c].file[0] != 0x00 ? deviceSlots.slot[c].file : "Empty");
	}

rehosts:
	// reset cursor
	c = 0;

rehosts_jump:
	screen_puts(0, 20, "\xD9\xB2\xA5\xB4\xB5\xB2\xAE\x19Pick\xD9\xA5\x19"
					   "Edit\xD9\xA4\x19"
					   "Drives\xD9\xAF\xB0\xB4\xA9\xAF\xAE\x19"
					   "Boot");
	screen_puts(0, 21, "    \xD9\x91\x8D\x98\x19"
					   "Drives \xD9\xDC\x91\x8D\x98\x19"
					   "Hosts\xD9\xA3\x19"
					   "Config");

	bar_clear();
	bar_show(c + 2);

	while (host_done == false)
	{
		// Quick boot
		if (GTIA_READ.consol == 0x03)
		{
			diskulator_mount_all_hosts();
			diskulator_mount_all_devices();
			diskulator_boot();
		}

		if (kbhit())
		{
			k = cgetc();
			switch (k)
			{
			case 0x1C: // UP
			case '-':
				if (c > 0)
					c--;
				break;
			case 0x1D: // DOWN
			case '=':
				if (c < 8)
					c++;
				break;
			case 0x21: // SHIFT 1-8
			case 0x22:
			case 0x23:
			case 0x24:
			case 0x25:
			case 0x26:
			case 0x27:
				c = k - 0x21;
				break;
			case 0x31:
			case 0x32:
			case 0x33:
			case 0x34:
			case 0x35:
			case 0x36:
			case 0x37:
			case 0x38:
				c = k - 0x31;
				goto jump_to_devs;
				break;
			case 0x40: // special case for 8
				c = 7;
				break;
			case 'c': // config
				host_done = true;
				slot_done = true;
				ret = false;
				info_run();
				break;
			case 'e': // edit
				if (hostSlots.host[c][0] == 0x00)
				{
					screen_puts(3, c + 1, "                                    ");
				}
				screen_input(3, c + 1, hostSlots.host[c]);
				if (hostSlots.host[c][0] == 0x00)
				{
					screen_puts(4, c + 1, "Empty");
				}
				diskulator_write_host_slots();
				break;
			case 'd':
				c = 0;
			jump_to_devs:
				host_done = true;
				slot_done = false;
				screen_puts(0, 20, "        \xD9\xA5\x19"
								   "Eject\xD9\xA8\x19Hosts\xD9\xAE\x19New          ");

				break;
			case 0x9B: // ENTER
				selected_host = c;
				if (hostSlots.host[selected_host][0] != 0x00)
				{
					// Write hosts
					diskulator_write_host_slots();

					// Mount host
					diskulator_mount_host(c);
					memset(prefix, 0, sizeof(prefix));
					strcat(prefix, "/");

					ret = true;
				}
				else
					ret = false;
				host_done = true;
				slot_done = true;
				break;
			}
			if (k > 0)
			{
				bar_clear();
				bar_show(c + 2);
				k = 0;
			}
		}
		prev_consol = GTIA_READ.consol;
	}

	bar_clear();
	if (slot_done == false)
		bar_show(13);

	bar_clear();
	bar_show(c + 13);

	k = 0;

	while (slot_done == false)
	{
		// Quick boot
		if (GTIA_READ.consol == 0x03)
		{
			diskulator_mount_all_hosts();
			diskulator_mount_all_devices();
			diskulator_boot();
		}

		if (kbhit())
		{
			k = cgetc();
			switch (k)
			{
			case 0x1C: // UP
			case '-':
				if (c > 0)
					c--;
				break;
			case 0x1D: // DOWN
			case '=':
				if (c < 8)
					c++;
				break;
			case 0x21:
			case 0x22:
			case 0x23:
			case 0x24:
			case 0x25:
			case 0x26:
			case 0x27:
				c = k - 0x21;
				slot_done = true;
				host_done = false;
				goto rehosts_jump;
				break;
			case 0x31:
			case 0x32:
			case 0x33:
			case 0x34:
			case 0x35:
			case 0x36:
			case 0x37:
			case 0x38:
				c = k - 0x31;
				break;
			case 0x40: // special case for 8
				c = 7;
				slot_done = true;
				host_done = false;
				goto rehosts_jump;
				break;
			case 'c': // config
				host_done = true;
				slot_done = true;
				ret = false;
				info_run();
				break;
			case 'h': // Hosts
				slot_done = true;
				host_done = false;
				goto rehosts;
			case 'e': // EJECT
			doeject:
				diskulator_umount_device(c);
				screen_puts(4, c + 11, "Empty                               ");
				memset(deviceSlots.slot[c].file, 0, sizeof(deviceSlots.slot[c].file));
				deviceSlots.slot[c].hostSlot = 0xFF;
				diskulator_write_device_slots();
				break;
			case 'n': // NEW
				screen_puts(4, c + 11, "                                    ");
				screen_puts(0, 20, "Enter filename of new ATR image        ");
				screen_puts(0, 21, "                                       ");
				memset(tmp_str, 0, sizeof(tmp_str));
				memset(deviceSlots.slot[c].file, 0, sizeof(deviceSlots.slot[c].file));
				screen_input(3, c + 11, deviceSlots.slot[c].file);
				screen_puts(0, 20, "Which Host Slot (1-8)?                 ");
				screen_puts(0, 21, "                                       ");
				memset(tmp_str, 0, sizeof(tmp_str));
				screen_input(23, 20, tmp_str);
				deviceSlots.slot[c].hostSlot = atoi(tmp_str);
				deviceSlots.slot[c].hostSlot -= 1;
				diskulator_mount_host(deviceSlots.slot[c].hostSlot);
				screen_puts(0, 20, "Size?\xD9\x91\x19"
								   "90K  \xD9\x92\x19"
								   "130K  \xD9\x93\x19"
								   "180K  \xD9\x94\x19"
								   "360K  ");
				screen_puts(0, 21, "     \xD9\x95\x19"
								   "720K \xD9\x96\x19"
								   "1440K \xD9\x97\x19"
								   "Custom          ");
				memset(tmp_str, 0, sizeof(tmp_str));
				screen_input(32, 21, tmp_str);
				disk_type = atoi(tmp_str);
				if (disk_type == 1)
				{
					ns = 720;
					ss = 128;
				}
				else if (disk_type == 2)
				{
					ns = 1040;
					ss = 128;
				}
				else if (disk_type == 3)
				{
					ns = 720;
					ss = 256;
				}
				else if (disk_type == 4)
				{
					ns = 1440;
					ss = 256;
				}
				else if (disk_type == 5)
				{
					ns = 2880;
					ss = 256;
				}
				else if (disk_type == 6)
				{
					ns = 5760;
					ss = 256;
				}
				else if (disk_type == 7)
				{
					screen_puts(0, 20, "# Sectors?                            ");
					screen_puts(0, 21, "                                      ");
					memset(tmp_str, 0, sizeof(tmp_str));
					screen_input(12, 20, tmp_str);
					ns = atoi(tmp_str);
					screen_puts(0, 21, "Sector size (128/256)?                ");
					memset(tmp_str, 0, sizeof(tmp_str));
					screen_input(24, 21, tmp_str);
					ss = atoi(tmp_str);
				}

				memset(tmp_str, 0, sizeof(tmp_str));
				screen_puts(0, 20, "Are you sure (Y/N)?                     ");
				screen_puts(0, 21, "                                        ");
				screen_input(21, 20, tmp_str);

				if (tmp_str[0] == 'y')
				{
					screen_puts(0, 20, "Creating new Disk                       ");
					screen_puts(0, 21, "                                        ");
					diskulator_new_disk(c, ns, ss);

					if (OS.dcb.dstats != 1)
						goto doeject;

					diskulator_write_device_slots();
					goto rehosts;
				}
				else
					goto doeject;
				break;
			}
			if (k > 0)
			{
				bar_clear();
				bar_show(c + 13);
				k = 0;
			}
		}
		prev_consol = GTIA_READ.consol;
	}
	return ret;
}

/**
 * Select an image
 */
bool diskulator_select(void)
{
	unsigned char num_entries;
	unsigned char e;
	bool ret = false;

	POKE(0x60F, 2);
	POKE(0x610, 2);
	POKE(0x61B, 6);
	POKE(0x61C, 6);

subdir:
	selector_done = false;
	num_entries = 0;
	screen_clear();
	bar_clear();

	screen_puts(0, 0, "    DISK IMAGES    ");

	screen_puts(0, 21, "ret PICK esc ABORT");
	screen_puts(20, 21, "                  ");

	screen_puts(0, 1, prefix);

	diskulator_open_directory(selected_host, prefix);

	while ((current_entry[0] != 0x7F) && (num_entries < 16))
	{
		diskulator_read_directory(selected_host, current_entry, FILE_MAXLEN);
		if (current_entry[0] == '.')
			continue;
		else if (current_entry[0] == 0x7F)
			break;
		else
		{
			strncpy(files[num_entries], current_entry, FILE_MAXLEN - 1);

			if (current_entry[strlen(current_entry) - 1] == '/')
				screen_puts(0, num_entries + 2, "\x04");

			screen_puts(2, num_entries + 2, current_entry);
			num_entries++;
		}
	}

	diskulator_close_directory(selected_host);
	e = 0;
	bar_clear();
	bar_show(e + 3);

	selector_done = false;

	while (1)
	{
		if (kbhit())
			k = cgetc();

		switch (k)
		{
		case 0x1C: // Up
		case '-':
			if (e > 0)
				e--;
			break;
		case 0x1D: // Down
		case '=':
			if (e < num_entries)
				e++;
			break;
		case 0x1B: // ESC
			selector_done = true;
			memset(path, 0, sizeof(path));
			bar_set_color(0x97);
			ret = false;
			goto image_done;
			break;
		case 0x9B: // Enter
			bar_set_color(0x97);

			if (files[e][strlen(files[e]) - 1] == '/') // subdir
			{
				selector_done = false;
				strcat(prefix, files[e]);
				k = 0;
				goto subdir;
			}

			memset(path, 0, sizeof(path));
			strcat(path, prefix);
			strcat(path, files[e]);
			ret = true;
			goto image_done;
			break;
		}
		if (k > 0)
		{
			bar_clear();
			bar_show(e + 3);
			k = 0;
		}
	}
image_done:
	return ret;
}

/**
 * Select destination drive
 */
void diskulator_drive(void)
{
	unsigned char c, k;

	drive_done = false;

	POKE(0x60F, 2);
	POKE(0x610, 2);
	POKE(0x61B, 6);
	POKE(0x61C, 6);

	screen_clear();
	bar_clear();

	screen_puts(0, 0, "MOUNT TO DRIVE SLOT");
	screen_puts(0, 21, " 1-8 OR return PICK ");
	screen_puts(20, 21, " esc ABORT  e EJECT ");
	diskulator_read_device_slots();

	// Display drive slots
	for (c = 0; c < 8; c++)
	{
		unsigned char d[4];
		d[0] = 'D';
		d[1] = 0x31 + c;
		d[2] = ':';
		d[3] = 0x00;
		screen_puts(0, c + 2, d);
		screen_puts(4, c + 2, deviceSlots.slot[c].file[0] != 0x00 ? deviceSlots.slot[c].file : "Empty");
	}

	c = 0;

	bar_clear();
	bar_show(c + 3);

	while (drive_done == false)
	{
		k = cgetc();
		switch (k)
		{
		case 0x1C: // UP
		case '-':
			if (c > 0)
				c--;
			break;
		case 0x1D: // DOWN
		case '=':
			if (c < 8)
				c++;
			break;
		case 'e': // E
			screen_puts(4, c + 2, "Empty                               ");
			memset(deviceSlots.slot[c].file, 0, sizeof(deviceSlots.slot[c].file));
			deviceSlots.slot[c].hostSlot = 0xFF;
			diskulator_write_device_slots();
			break;
		case 0x1B: // ESC
			drive_done = true;
			break;
		case 0x31:
		case 0x32:
		case 0x33:
		case 0x34:
		case 0x35:
		case 0x36:
		case 0x37:
		case 0x38:
			c = k - 0x31;
			bar_clear();
			bar_show(c + 3);
			goto rorw;
			break;
		case 0x9B: // RETURN
		rorw:
			screen_puts(0, 21, "       ENTER:       ");
			screen_puts(20, 21, "r R/O w R/W esc ABRT");

			o = 0;

			k = cgetc();

			if (k == 'r')
				o |= 0x01;
			else if (k == 'w')
				o |= 0x02;
			else if (k == 0x1B)
				goto drive_slot_abort;

			deviceSlots.slot[c].hostSlot = selected_host;
			deviceSlots.slot[c].mode = o;
			strcpy(deviceSlots.slot[c].file, path);

			diskulator_write_device_slots();
			diskulator_mount_device(c, o);
		drive_slot_abort:
			drive_done = true;
			break;
		}
		if (k > 0)
		{
			bar_clear();
			bar_show(c + 3);
			k = 0;
		}
	}
}

/**
 * Run the Diskulator
 */
void diskulator_run(void)
{
	bool host_selected = false;
	bool disk_selected = false;

	if (GTIA_READ.consol == 0x04) // Option
	{
		diskulator_read_host_slots();
		diskulator_read_device_slots();
		diskulator_mount_all_hosts();
		diskulator_mount_all_devices();
		diskulator_boot();
	}

	while (diskulator_done == false)
	{
		host_selected = diskulator_host();
		if (host_selected == true)
		{
			disk_selected = diskulator_select();
			if (disk_selected == true)
			{
				diskulator_drive();
			}
		}
	}
	diskulator_boot();
}

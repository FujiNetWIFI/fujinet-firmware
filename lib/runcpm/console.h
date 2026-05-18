#ifndef CONSOLE_H
#define CONSOLE_H

/* see main.c for definition */

#ifndef RUNCPM_DECL
#define RUNCPM_DECL
#endif

RUNCPM_DECL uint8 mask8bit = 0x7f;		// TO be used for masking 8 bit characters (XMODEM related)
										// If set to 0x7f, RunCPM masks the 8th bit of characters sent
										// to the console. This is the standard CP/M behavior.
										// If set to 0xff, RunCPM passes 8 bit characters. This is
										// required for XMODEM to work.
										// Use the CONSOLE7 and CONSOLE8 programs to change this on the fly.

RUNCPM_DECL uint8 _chready(void)		// Checks if there's a character ready for input
{
	return(_kbhit() ? 0xff : 0x00);
}

RUNCPM_DECL uint8 _getchNB(void)		// Gets a character, non-blocking, no echo
{
	return(_kbhit() ? _getch() : 0x00);
}

RUNCPM_DECL void _putcon(uint8 ch)		// Puts a character
{
	_putch(ch & mask8bit);
}

RUNCPM_DECL void _puts(const char* str)	// Puts a \0 terminated string
{
	while (*str)
		_putcon(*(str++));
}

RUNCPM_DECL void _puthex8(uint8 c)		// Puts a HH hex string
{
	_putcon(tohex(c >> 4));
	_putcon(tohex(c & 0x0f));
}

RUNCPM_DECL void _puthex16(uint16 w)	// puts a HHHH hex string
{
	_puthex8(w >> 8);
	_puthex8(w & 0x00ff);
}

#endif
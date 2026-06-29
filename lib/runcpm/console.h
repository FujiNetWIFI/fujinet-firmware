#ifndef CONSOLE_H
#define CONSOLE_H

#ifndef RUNCPM_DECL
#define RUNCPM_DECL
#endif

/* see main.c for definition */

RUNCPM_DECL uint8 mask8bit = 0x7f; // TO be used for masking 8 bit characters (XMODEM related)
                       // If set to 0x7f, RunCPM masks the 8th bit of characters sent
                       // to the console. This is the standard CP/M behavior.
                       // If set to 0xff, RunCPM passes 8 bit characters. This is
                       // required for XMODEM to work.
                       // Use the CONSOLE7 and CONSOLE8 programs to change this on the fly.

RUNCPM_DECL void _putcon(uint8 ch) // Puts a character
{
#ifdef BUILD_ATARI
    /* FujiNet: translate a bare Form Feed (^L) to the VT100 home+clear that
     * _clrscr() uses, so ^L-clear works on VT100/ANSI consoles. Only in 7-bit
     * mode; 8-bit transparent (XMODEM, mask8bit==0xff) passes 0x0C through. */
    if (mask8bit == 0x7f && (ch & 0x7f) == 0x0C)
    {
        _putch(0x1B); _putch('['); _putch('1'); _putch(';');
        _putch('1');  _putch('H'); _putch(0x1B); _putch('[');
        _putch('2');  _putch('J');
        return;
    }
#endif
#ifdef STREAMIO
    if (consoleOutputActive)
        _putch(ch & mask8bit);
    if (streamOutputFile)
        fputc(ch & mask8bit, streamOutputFile);
#else
    _putch(ch & mask8bit);
#endif
}

RUNCPM_DECL void _puts(const char *str) // Puts a \0 terminated string
{
    while (*str)
        _putcon(*(str++));
}

RUNCPM_DECL void _puthex8(uint8 c) // Puts a HH hex string
{
    _putcon(tohex(c >> 4));
    _putcon(tohex(c & 0x0f));
}

RUNCPM_DECL void _puthex16(uint16 w) // puts a HHHH hex string
{
    _puthex8(w >> 8);
    _puthex8(w & 0x00ff);
}

#ifdef STREAMIO
RUNCPM_DECL int _nextStreamInChar;

RUNCPM_DECL void _getNextStreamInChar(void) {
    _nextStreamInChar = streamInputFile ? fgetc(streamInputFile) : EOF;
    if (EOF == _nextStreamInChar) {
        streamInputActive = FALSE;
    }
}

RUNCPM_DECL uint8 _getStreamInChar(void) {
    uint8 result = _nextStreamInChar;
    _getNextStreamInChar();
    // TODO: delegate to abstrction_posix.h
    if (0x0a == result)
        result = 0x0d;
    return result;
}

RUNCPM_DECL uint8 _getStreamInCharEcho() {
    uint8 result = _getStreamInChar();
    _putcon(result);
    return result;
}

RUNCPM_DECL void _streamioInit(void) {
    _getNextStreamInChar();
}

RUNCPM_DECL void _streamioReset(void) {
    if (streamOutputFile)
        fclose(streamOutputFile);
}
#endif

RUNCPM_DECL uint8 _chready(void) // Checks if there's a character ready for input
{
#ifdef STREAMIO
    if (streamInputActive)
        return 0xff;
    // TODO: Consider adding/keeping _abort_if_kbd_eof() here.
    _abort_if_kbd_eof();
#endif
    return (_kbhit() ? 0xff : 0x00);
}

RUNCPM_DECL uint8 _getconNB(void) // Gets a character, non-blocking, no echo
{
#ifdef STREAMIO
    if (streamInputActive)
        return _getStreamInChar();
    // TODO: Consider adding/keeping _abort_if_kbd_eof() here.
    _abort_if_kbd_eof();
#endif
    return (_kbhit() ? _getch() : 0x00);
}

RUNCPM_DECL uint8 _getcon(void) // Gets a character, blocking, no echo
{
#ifdef STREAMIO
    if (streamInputActive)
        return _getStreamInChar();
    // TODO: Consider adding/keeping _abort_if_kbd_eof() here.
    _abort_if_kbd_eof();
#endif
    return _getch();
}

RUNCPM_DECL uint8 _getconE(void) // Gets a character, blocking, with echo
{
#ifdef STREAMIO
    if (streamInputActive)
        return _getStreamInCharEcho();
    // TODO: Consider adding/keeping _abort_if_kbd_eof() here.
    _abort_if_kbd_eof();
#endif
    return _getche();
}

#endif

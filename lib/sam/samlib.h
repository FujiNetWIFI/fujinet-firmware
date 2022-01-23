#ifdef BUILD_ATARI
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "reciter.h"
#include "sam.h"
#include "samdebug.h"

#ifdef USESDL
#include <SDL.h>
#include <SDL_audio.h>
#endif

#ifdef ESP_PLATFORM
#include "../../include/pinmap.h"
#endif

#ifdef __cplusplus
extern char input[256];
#endif

#ifndef ESP_PLATFORM
void WriteWav(char *filename, char *buffer, int bufferlength);
#endif // ESP_PLATFORM

void PrintUsage();

#ifdef USESDL
void MixAudio(void *unused, Uint8 *stream, int len);
void OutputSound();
#else
void OutputSound();
#endif

int sam(int argc, char **argv);
#endif /* BUILD_ATARI */
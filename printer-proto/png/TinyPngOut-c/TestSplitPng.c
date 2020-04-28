/* 
 * Split testing for Tiny PNG Output (C)
 * 
 * Copyright (c) 2018 Project Nayuki
 * https://www.nayuki.io/page/tiny-png-output
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program (see COPYING.txt and COPYING.LESSER.txt).
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "TinyPngOut.h"


static void test(uint32_t width, uint32_t height, long trials);
static void printErrorExit(enum TinyPngOut_Status status);
static uint32_t randUint32();


static FILE *randIn;


int main(void) {
	randIn = fopen("/dev/urandom", "rb");
	if (randIn == NULL) {
		perror("Error: fopen /dev/urandom");
		return EXIT_FAILURE;
	}
	
	for (long i = 0; ; ) {
		uint32_t width  = randUint32() % 100000;
		uint32_t height = randUint32() % 100000;
		if (width > 0 && height > 0 && (uint64_t)width * height * 3 < 1000000) {
			fprintf(stderr, "Test %ld:  width=%" PRIu32 " height=%" PRIu32 " pixels=%" PRIu32 " bytes=%" PRIu32 "\n",
				i, width, height, width * height, width * height * 3);
			test(width, height, 10);
			if (i < LONG_MAX)
				i++;
		}
	}
	
	if (fclose(randIn) != 0) {
		perror("Error: fclose");
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}


static void test(uint32_t width, uint32_t height, long trials) {
	// Generate array of random pixel values
	size_t numPixels = (size_t)width * height;
	size_t numBytes = numPixels * 3;
	uint8_t *pixelBytes = calloc(numBytes, sizeof(uint8_t));
	if (pixelBytes == NULL) {
		perror("Error: calloc");
		exit(EXIT_FAILURE);
	}
	if (fread(pixelBytes, sizeof(pixelBytes[0]), numBytes, randIn) != numBytes) {
		perror("Error: fread");
		exit(EXIT_FAILURE);
	}
	
	// Write entire image in one shot
	char *referenceData;
	size_t referenceLen;
	{
		FILE *fout = open_memstream(&referenceData, &referenceLen);  // POSIX API, not standard C library
		if (fout == NULL) {
			perror("Error: open_memstream");
			exit(EXIT_FAILURE);
		}
		struct TinyPngOut pngout;
		enum TinyPngOut_Status status = TinyPngOut_init(&pngout, width, height, fout);
		if (status != TINYPNGOUT_OK)
			printErrorExit(status);
		status = TinyPngOut_write(&pngout, pixelBytes, numPixels);
		if (status != TINYPNGOUT_OK)
			printErrorExit(status);
		if (fclose(fout) != 0) {
			perror("Error: fclose");
			exit(EXIT_FAILURE);
		}
	}
	
	// Try writing in different splits
	for (long i = 0; i < trials; i++) {
		fprintf(stderr, "    Trial %ld:  ", i);
		char *actualData;
		size_t actualLen;
		FILE *fout = open_memstream(&actualData, &actualLen);
		if (fout == NULL) {
			perror("Error: open_memstream");
			exit(EXIT_FAILURE);
		}
		struct TinyPngOut pngout;
		enum TinyPngOut_Status status = TinyPngOut_init(&pngout, width, height, fout);
		if (status != TINYPNGOUT_OK)
			printErrorExit(status);
		
		size_t offset = 0;
		while (offset < numPixels) {
			size_t count = randUint32() % (numPixels - offset + 1);
			fprintf(stderr, "%zu ", count);
			fflush(stderr);
			status = TinyPngOut_write(&pngout, pixelBytes + offset * 3, count);
			if (status != TINYPNGOUT_OK)
				printErrorExit(status);
			offset += count;
		}
		if (fclose(fout) != 0) {
			perror("Error: fclose");
			exit(EXIT_FAILURE);
		}
		
		int cmp = actualLen == referenceLen ? 0 : 1;
		if (cmp == 0)
			cmp = memcmp(actualData, referenceData, actualLen);
		free(actualData);
		fprintf(stderr, "%s\n", (cmp == 0 ? "Same" : "Different"));
		if (cmp != 0)
			exit(EXIT_FAILURE);
	}
	free(pixelBytes);
	free(referenceData);
}


static void printErrorExit(enum TinyPngOut_Status status) {
	const char *msg;
	switch (status) {
		case TINYPNGOUT_OK              :  msg = "OK";                break;
		case TINYPNGOUT_INVALID_ARGUMENT:  msg = "Invalid argument";  break;
		case TINYPNGOUT_IMAGE_TOO_LARGE :  msg = "Image too large";   break;
		case TINYPNGOUT_IO_ERROR        :  msg = "I/O error";         break;
		default                         :  msg = "Unknown error";     break;
	}
	fprintf(stderr, "Error: %s\n", msg);
	exit(EXIT_FAILURE);
}


static uint32_t randUint32() {
	uint8_t b[4];
	if (fread(b, sizeof(uint8_t), 4, randIn) != 4) {
		perror("Error: fread");
		exit(EXIT_FAILURE);
	}
	return (uint32_t)b[0] <<  0
	     | (uint32_t)b[1] <<  8
	     | (uint32_t)b[2] << 16
	     | (uint32_t)b[3] << 24;
}

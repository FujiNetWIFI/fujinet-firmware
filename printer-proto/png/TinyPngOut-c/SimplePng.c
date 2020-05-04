/* 
 * Simple sample image using Tiny PNG Output (C)
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

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "TinyPngOut.h"


static int printError(enum TinyPngOut_Status status);


int main(void) {
	// Sample image data:
	//   [red    , green , blue]
	//   [magenta, yellow, cyan]
	const int WIDTH  = 3;
	const int HEIGHT = 2;
	const uint8_t PIXELS[] = {
		0xFF,0x00,0x00,  0x00,0xFF,0x00,  0x00,0x00,0xFF,
		0xFF,0x00,0xFF,  0xFF,0xFF,0x00,  0x00,0xFF,0xFF,
	};
	
	// Open output file
	FILE *fout = fopen("demo-rgb.png", "wb");
	if (fout == NULL) {
		perror("Error: fopen");
		return EXIT_FAILURE;
	}
	
	// Initialize Tiny PNG Output
	struct TinyPngOut pngout;
	enum TinyPngOut_Status status = TinyPngOut_init(&pngout, (uint32_t)WIDTH, (uint32_t)HEIGHT, fout);
	if (status != TINYPNGOUT_OK)
		return printError(status);
	
	// Write image data
	status = TinyPngOut_write(&pngout, PIXELS, (size_t)(WIDTH * HEIGHT));
	if (status != TINYPNGOUT_OK)
		return printError(status);
	
	// Close output file
	if (fclose(fout) != 0) {
		perror("Error: fclose");
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}


static int printError(enum TinyPngOut_Status status) {
	const char *msg;
	switch (status) {
		case TINYPNGOUT_OK              :  msg = "OK";                break;
		case TINYPNGOUT_INVALID_ARGUMENT:  msg = "Invalid argument";  break;
		case TINYPNGOUT_IMAGE_TOO_LARGE :  msg = "Image too large";   break;
		case TINYPNGOUT_IO_ERROR        :  msg = "I/O error";         break;
		default                         :  msg = "Unknown error";     break;
	}
	fprintf(stderr, "Error: %s\n", msg);
	return EXIT_FAILURE;
}

/* 
 * Mandlebrot image using Tiny PNG Output (C)
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

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "TinyPngOut.h"


/* Image parameters */

static const int width  = 512;
static const int height = 512;

static const double xMin = -1.9;
static const double xMax =  0.5;
static const double yMin = -1.2;
static const double yMax =  1.2;

static const int iterations = 1000;


static uint32_t mandelbrot(int x, int y);
static int printError(enum TinyPngOut_Status status);


/* Function implementations */

int main(void) {
	// Allocate single line buffer
	uint8_t *line = calloc((size_t)width * 3, sizeof(uint8_t));
	if (line == NULL) {
		perror("Error: calloc");
		return EXIT_FAILURE;
	}
	
	// Open output file
	FILE *fout = fopen("demo-mandelbrot.png", "wb");
	if (fout == NULL) {
		perror("Error: fopen");
		return EXIT_FAILURE;
	}
	
	// Initialize Tiny PNG Output
	struct TinyPngOut pngout;
	enum TinyPngOut_Status status = TinyPngOut_init(&pngout, (uint32_t)width, (uint32_t)height, fout);
	if (status != TINYPNGOUT_OK)
		return printError(status);
	
	// Compute and write Mandelbrot one line at a time
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			uint32_t pix = mandelbrot(x, y);
			line[x * 3 + 0] = (uint8_t)(pix >> 16);
			line[x * 3 + 1] = (uint8_t)(pix >>  8);
			line[x * 3 + 2] = (uint8_t)(pix >>  0);
		}
		status = TinyPngOut_write(&pngout, line, (size_t)width);
		if (status != TINYPNGOUT_OK)
			return printError(status);
	}
	free(line);
	
	// Close output file
	if (fclose(fout) != 0) {
		perror("Error: fclose");
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}


static uint32_t mandelbrot(int x, int y) {
	double cr = xMin + (x + 0.5) / width  * (xMax - xMin);
	double ci = yMax - (y + 0.5) / height * (yMax - yMin);
	double zr = 0;
	double zi = 0;
	int i;
	for (i = 0; i < iterations; i++) {
		if (zr * zr + zi * zi > 4)
			break;
		double temp = zr * zr - zi * zi + cr;
		zi = 2 * zr * zi + ci;
		zr = temp;
	}
	double j = (double)i / iterations;
	return (uint32_t)(pow(j, 0.6) * 255 + 0.5) << 16
	     | (uint32_t)(pow(j, 0.3) * 255 + 0.5) <<  8
	     | (uint32_t)(pow(j, 0.1) * 255 + 0.5) <<  0;
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

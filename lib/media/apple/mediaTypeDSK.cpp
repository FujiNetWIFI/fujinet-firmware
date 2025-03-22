#ifdef BUILD_APPLE

#ifndef DEV_RELAY_SLIP
#include "esp_heap_caps.h"
#endif
#include "mediaTypeDSK.h"
#include "../../include/debug.h"
#include <string.h>


// #include <assert.h>
// #include <stdbool.h>
// #include <stdio.h>
// #include <stdint.h>
// #include <string.h>

#define BYTES_PER_TRACK 4096
#define BYTES_PER_SECTOR 256

// routines to convert DSK to WOZ stolen from DSK2WOZ by Tom Harte
// https://github.com/TomHarte/dsk2woz

// forward reference
static void serialise_track(TRK_bitstream *dest, const uint8_t *src, uint8_t track_number, bool is_prodos);

bool MediaTypeDSK::write_sector(int qtrack, int sector, uint8_t *buffer)
{
  size_t offset, size;
  size_t sectors_per_track = 16; // FIXME - what about 13 sector disks?
  uint8_t *trackbuf;
  int track = tmap[qtrack];
  const int phys2log[] = {0, 7, 14, 6, 13, 5, 12, 4, 11, 3, 10, 2, 9, 1, 8, 15};
  const int prodos[] = {0, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 15};


  if (_mediatype != MEDIATYPE_DO &&
      _mediatype != MEDIATYPE_DSK &&
      _mediatype != MEDIATYPE_PO) {
    Debug_printf("\r\nDon't know how to write sector");
    return true;
  }

  sector = phys2log[sector];
  if (_mediatype == MEDIATYPE_PO)
    sector = prodos[sector];

  Debug_printf("\r\nDSK writing track %i sector %i ", track, sector);

  offset = (track * sectors_per_track + sector) * BYTES_PER_SECTOR;
  if (fnio::fseek(_media_fileh, offset, SEEK_SET) != 0)
    return true;
  size = fnio::fwrite(buffer, 1, BYTES_PER_SECTOR, _media_fileh);
  if (size != BYTES_PER_SECTOR)
    return true;

  offset = track * sectors_per_track * BYTES_PER_SECTOR;
  if (fnio::fseek(_media_fileh, offset, SEEK_SET) != 0)
    return true;

  size = sectors_per_track * BYTES_PER_SECTOR;
  trackbuf = (uint8_t *) malloc(size);
  if (!trackbuf)
    return true;

  offset = fnio::fread(trackbuf, 1, size, _media_fileh);
  if (offset != size)
    return true;

  offset = sector * BYTES_PER_SECTOR;
  memcpy(&trackbuf[offset], buffer, BYTES_PER_SECTOR);

  memset(trk_data[track], 0, BITSTREAM_ALLOC_SIZE(WOZ1_TRACK_LEN));
  serialise_track(trk_data[track], trackbuf, track, _mediatype == MEDIATYPE_PO);

  free(trackbuf);

  return false;
}

mediatype_t MediaTypeDSK::mount(fnFile *f, uint32_t disksize)
{
    switch (disksize) {
        case 35 * BYTES_PER_TRACK:
        case 36 * BYTES_PER_TRACK:
        case 40 * BYTES_PER_TRACK:
            // 35, 36, and 40 tracks are supported (same as Applesauce)
            break;
        default:
	        Debug_printf("\nMediaTypeDSK error: unsupported disk image size %ld", disksize);
            return MEDIATYPE_UNKNOWN;
    }

    _media_fileh = f;
    diskiiemulation = true;
    num_tracks = disksize / BYTES_PER_TRACK;

    // allocated SPRAM
    const size_t dsk_image_size = num_tracks * BYTES_PER_TRACK;
#ifdef ESP_PLATFORM
    uint8_t *dsk = (uint8_t*)heap_caps_malloc(dsk_image_size, MALLOC_CAP_SPIRAM);
#else
	uint8_t *dsk = (uint8_t*)malloc(dsk_image_size);
#endif
    if (fnio::fseek(f, 0, SEEK_SET) != 0)
        return MEDIATYPE_UNKNOWN;
    size_t bytes_read = fnio::fread(dsk, 1, dsk_image_size, f);
    if (bytes_read != dsk_image_size)
        return MEDIATYPE_UNKNOWN;

    dsk2woz_info();
    dsk2woz_tmap();
	dsk2woz_tracks(dsk);

    free(dsk);
    return MEDIATYPE_WOZ;
}

void MediaTypeDSK::dsk2woz_info()
{
	optimal_bit_timing = WOZ1_BIT_TIME; // 4 us
    num_blocks = WOZ1_NUM_BLKS; //WOZ1_TRACK_LEN / 512;
}

void MediaTypeDSK::dsk2woz_tmap()
{
	// This is a DSK conversion, so the TMAP table simply maps every
	// track that exists to:
	// (i) its integral position;
	// (ii) the quarter-track position before its integral position; and
	// (iii) the quarter-track position after its integral position.
	//
	// The remaining quarter-track position maps to nothing, which in
	// WOZ is indicated with a value of 255.

	// Let's start by filling the entire TMAP with empty tracks.
	memset(tmap, 0xff, 160);
	// Then we will add in the mappings.
	for(size_t c = 0; c < num_tracks; ++c)
    {
		size_t track_position = c << 2;
        if (c > 0)
            tmap[track_position - 1] = c;
        tmap[track_position] = tmap[track_position + 1] = c;
	}
#ifdef DEBUG
    Debug_printf("\nTrack, Index");
    for (int i = 0; i < MAX_TRACKS; i++)
        Debug_printf("\n%d/4, %d", i, tmap[i]);
#endif
}

bool MediaTypeDSK::dsk2woz_tracks(uint8_t *dsk)
{    // depend upon little endian-ness

    // woz1 track data organized as:
    // Offset	Size	    Name	        Usage
    // +0	    6646 bytes  Bitstream	    The bitstream data padded out to 6646 bytes
    // +6646	uint16	    Bytes Used	    The actual byte count for the bitstream.
    // +6648	uint16	    Bit Count	    The number of bits in the bitstream.
    // +6650	uint16	    Splice Point	Index of first bit after track splice (write hint). If no splice information is provided, then will be 0xFFFF.
    // +6652	uint8	    Splice Nibble	Nibble value to use for splice (write hint).
    // +6653	uint8	    Splice Bit Count	Bit count of splice nibble (write hint).
    // +6654	uint16		Reserved for future use.

    // Debug_printf("\nStart Block, Block Count, Bit Count");

	Debug_printf("\nMediaTypeDSK is_prodos: %s", _mediatype == MEDIATYPE_PO ? "Y" : "N");

	// TODO: adapt this to that
	// Write out all tracks.
	for (size_t c = 0; c < num_tracks; c++)
	{
#ifdef ESP_PLATFORM
		TRK_bitstream *bitstream = (TRK_bitstream *) heap_caps_malloc(BITSTREAM_ALLOC_SIZE(WOZ1_TRACK_LEN), MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
#else
		TRK_bitstream *bitstream = (TRK_bitstream *)malloc(BITSTREAM_ALLOC_SIZE(WOZ1_TRACK_LEN));
#endif
		if (bitstream != nullptr)
		{
			trk_data[c] = bitstream;
			memset(bitstream, 0, BITSTREAM_ALLOC_SIZE(WOZ1_TRACK_LEN));
			serialise_track(bitstream, &dsk[c * 16 * 256], c, _mediatype == MEDIATYPE_PO);
                        bitstream->len_blocks = (bitstream->len_bytes + 511) / 512; //  WOZ1_NUM_BLKS;
			Debug_printf("\nStored %d bytes containing %d bits of track %d into location %lx", bitstream->len_bytes, bitstream->len_bits, c, trk_data[c]);
			Debug_printf(" -- %02x %02x %02x %02x %02x", bitstream->data[0], bitstream->data[1], bitstream->data[2], bitstream->data[3], bitstream->data[4] );
		}
		else
		{
			Debug_printf("\nNo RAM allocated!");
			return true;
            }
	}
	return false;
}

// ================ code below from TomHarte dsk2woz program ===============
/* MIT License

Copyright (c) 2018 Thomas Harte

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
 */
#define set_int32(location, value)              \
	woz[location] = (value)&0xff;               \
	woz[location + 1] = ((value) >> 8) & 0xff;  \
	woz[location + 2] = ((value) >> 16) & 0xff; \
	woz[location + 3] = (value) >> 24;


/*
	DSK sector serialiser. Constructs the 6-and-2 DOS 3.3-style on-disk
	representation of a DOS logical-order sector dump.
*/

/*!
	Appends a byte to a buffer at a supplied position, returning the
	position immediately after the byte. This is equivalent to calling
	write_bit eight times, supplying bit 7, then bit 6, down to bit 0.

	@param buffer The buffer to write into.
	@param position The position to write at.
	@param value The byte to write.
	@return The position immediately after the byte.
*/
static size_t write_byte(uint8_t *buffer, size_t position, int value) {
	const size_t shift = position & 7;
	const size_t byte_position = position >> 3;

	buffer[byte_position] |= value >> shift;
	if(shift) buffer[byte_position+1] |= value << (8 - shift);

	return position + 8;
}

/*!
	Encodes a byte into Apple 4-and-4 format and appends it to a buffer.

	@param buffer The buffer to write into.
	@param position The position to write at.
	@param value The byte to encode and write.
	@return The position immediately after the encoded byte.
*/
static size_t write_4_and_4(uint8_t *buffer, size_t position, int value) {
	position = write_byte(buffer, position, (value >> 1) | 0xaa);
	position = write_byte(buffer, position, value | 0xaa);
	return position;
}

/*!
	Appends a 6-and-2-style sync word to a buffer.

	@param buffer The buffer to write into.
	@param position The position to write at.
	@return The position immediately after the sync word.
*/
static size_t write_sync(uint8_t *buffer, size_t position) {
	position = write_byte(buffer, position, 0xff);
	return position + 2; // Skip two bits, i.e. leave them as 0s.
}

/*!
	Converts a 256-byte source buffer into the 343 byte values that
	contain the Apple 6-and-2 encoding of that buffer.

	@param dest The at-least-343 byte buffer to which the encoded sector is written.
	@param src The 256-byte source data.
*/
static void encode_6_and_2(uint8_t *dest, const uint8_t *src) {
	const uint8_t six_and_two_mapping[] = {
		0x96, 0x97, 0x9a, 0x9b, 0x9d, 0x9e, 0x9f, 0xa6,
		0xa7, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb2, 0xb3,
		0xb4, 0xb5, 0xb6, 0xb7, 0xb9, 0xba, 0xbb, 0xbc,
		0xbd, 0xbe, 0xbf, 0xcb, 0xcd, 0xce, 0xcf, 0xd3,
		0xd6, 0xd7, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde,
		0xdf, 0xe5, 0xe6, 0xe7, 0xe9, 0xea, 0xeb, 0xec,
		0xed, 0xee, 0xef, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6,
		0xf7, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
	};

	// Fill in byte values: the first 86 bytes contain shuffled
	// and combined copies of the bottom two bits of the sector
	// contents; the 256 bytes afterwards are the remaining
	// six bits.
	const uint8_t bit_reverse[] = {0, 2, 1, 3};
	for(size_t c = 0; c < 84; ++c) {
		dest[c] =
			bit_reverse[src[c]&3] |
			(bit_reverse[src[c + 86]&3] << 2) |
			(bit_reverse[src[c + 172]&3] << 4);
	}
	dest[84] =
		(bit_reverse[src[84]&3] << 0) |
		(bit_reverse[src[170]&3] << 2);
	dest[85] =
		(bit_reverse[src[85]&3] << 0) |
		(bit_reverse[src[171]&3] << 2);

	for(size_t c = 0; c < 256; ++c) {
		dest[86 + c] = src[c] >> 2;
	}

	// Exclusive OR each byte with the one before it.
	dest[342] = dest[341];
	size_t location = 342;
	while(location > 1) {
		--location;
		dest[location] ^= dest[location-1];
	}

	// Map six-bit values up to full bytes.
	for(size_t c = 0; c < 343; ++c) {
		dest[c] = six_and_two_mapping[dest[c]];
	}
}

uint16_t decode_6_and_2(uint8_t *dest, const uint8_t *src)
{
  int idx, step;
  uint8_t bits;
  uint16_t checksum;
  const uint8_t bit_reverse[] = {0, 2, 1, 3};
  const uint8_t six_and_two_unmapping[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x02, 0x03, 0x00, 0x04, 0x05, 0x06,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x08,
    0x00, 0x00, 0x00, 0x09, 0x0a, 0x0b, 0x0c, 0x0d,
    0x00, 0x00, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13,
    0x00, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x1b, 0x00, 0x1c, 0x1d, 0x1e,
    0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x20, 0x21,
    0x00, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x29, 0x2a, 0x2b,
    0x00, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32,
    0x00, 0x00, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
    0x00, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
  };


  for (idx = 0; idx < 343; idx++)
    dest[idx] = six_and_two_unmapping[src[idx] - 144];
  for (idx = 0; idx < 341; idx++)
    dest[idx + 1] ^= dest[idx];

  checksum = (dest[341] << 8) | dest[342];

  for (idx = 85; idx >= 0; idx--) {
    bits = dest[idx];
    for (step = 0; step < 3; step++) {
      dest[idx + step * 86] = (dest[idx + (step + 1) * 86] << 2)
	| bit_reverse[(bits >> step * 2) & 0x3];
    }
  }

  return checksum;
}

/*!
	Converts a DSK-style track to a WOZ-style track.

	@param dest The 6646-byte buffer that will contain the WOZ track. Both track contents and the
		proper suffix will be written.
	@param src The 4096-byte buffer that contains the DSK track — 16 instances of 256 bytes, each
		a fully-decoded sector.
	@param track_number The track number to encode into this track.
	@param is_prodos @c true if the DSK image is in Pro-DOS order; @c false if it is in DOS 3.3 order.
*/
static void serialise_track(TRK_bitstream *dest, const uint8_t *src, uint8_t track_number, bool is_prodos) {
	size_t track_position = 0;	// This is the track position **in bits**.

	// Write gap 1.
	for(size_t c = 0; c < 16; ++c) {
		track_position = write_sync(dest->data, track_position);
	}

	// Step through the sectors in physical order.
	for(size_t sector = 0; sector < 16; ++sector) {
		/*
			Write the sector header.
		*/

		// Prologue.
		track_position = write_byte(dest->data, track_position, 0xd5);
		track_position = write_byte(dest->data, track_position, 0xaa);
		track_position = write_byte(dest->data, track_position, 0x96);

		// Volume, track, setor and checksum, all in 4-and-4 format.
		track_position = write_4_and_4(dest->data, track_position, 254);
		track_position = write_4_and_4(dest->data, track_position, track_number);
		track_position = write_4_and_4(dest->data, track_position, sector);
		track_position = write_4_and_4(dest->data, track_position, 254 ^ track_number ^ sector);

		// Epilogue.
		track_position = write_byte(dest->data, track_position, 0xde);
		track_position = write_byte(dest->data, track_position, 0xaa);
		track_position = write_byte(dest->data, track_position, 0xeb);


		// Write gap 2.
		for(size_t c = 0; c < 7; ++c) {
			track_position = write_sync(dest->data, track_position);
		}

		/*
			Write the sector body.
		*/

		// Prologue.
		track_position = write_byte(dest->data, track_position, 0xd5);
		track_position = write_byte(dest->data, track_position, 0xaa);
		track_position = write_byte(dest->data, track_position, 0xad);

		// Map from this physical sector to a logical sector.
		const int logical_sector = (sector == 15) ? 15 : ((sector * (is_prodos ? 8 : 7)) % 15);

		// Sector contents.
		uint8_t contents[343];
		encode_6_and_2(contents, &src[logical_sector * 256]);
		for(size_t c = 0; c < sizeof(contents); ++c) {
			track_position = write_byte(dest->data, track_position, contents[c]);
		}

		// Epilogue.
		track_position = write_byte(dest->data, track_position, 0xde);
		track_position = write_byte(dest->data, track_position, 0xaa);
		track_position = write_byte(dest->data, track_position, 0xeb);

		// Write gap 3.
		for(size_t c = 0; c < 16; ++c) {
			track_position = write_sync(dest->data, track_position);
		}
	}

	// Add the track suffix.
        dest->len_bytes = (track_position + 7) >> 3;
        dest->len_bits = track_position;
}

#endif // BUILD_APPLE

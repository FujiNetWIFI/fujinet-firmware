https://www.w3.org/TR/REC-png.pdf
3.1 PNG file signature
The first eight bytes of a PNG file always contain the following (decimal) values:
3. FILE STRUCTURE 11
137 80 78 71 13 10 26 10
This signature indicates that the remainder of the file contains a single PNG image, consisting of a series of
chunks beginning with an IHDR chunk and ending with an IEND chunk.

3.2 Chunk layout
Each chunk consists of four parts:
Length			A 4-byte unsigned integer giving the number of bytes in the chunk’s data field. The length counts only
the data field, not itself, the chunk type code, or the CRC. Zero is a valid length. Although encoders
and decoders should treat the length as unsigned, its value must not exceed (2ˆ31)-1 bytes.
Chunk Type		A 4-byte chunk type code. For convenience in description and in examining PNG files, type codes
are restricted to consist of uppercase and lowercase ASCII letters (A-Z and a-z, or 65-90 and 97-122
decimal). However, encoders and decoders must treat the codes as fixed binary values, not character
strings. For example, it would not be correct to represent the type code IDAT by the EBCDIC equivalents of those letters. Additional naming conventions for chunk types are discussed in the next section.
Chunk Data		The data bytes appropriate to the chunk type, if any. This field can be of zero length.
CRC				A 4-byte CRC (Cyclic Redundancy Check) calculated on the preceding bytes in the chunk, including the chunk type code and chunk data fields, but not including the length field. The CRC is always
present, even for chunks containing no data. See CRC algorithm (Section 3.4).
The chunk data length can be any number of bytes up to the maximum; therefore, implementors cannot assume that chunks are aligned on any boundaries larger than bytes.


4.1.1 IHDR Image header
The IHDR chunk must appear FIRST. It contains:
Width: 4 bytes
Height: 4 bytes
Bit depth: 1 byte
Color type: 1 byte
Compression method: 1 byte
Filter method: 1 byte
Interlace method: 1 byte

Width and height give the image dimensions in pixels. They are 4-byte integers. Zero is an invalid value.
The maximum for each is (2ˆ31)-1 in order to accommodate languages that have difficulty with unsigned
4-byte values.
Bit depth is a single-byte integer giving the number of bits per sample or per palette index (not per pixel).
Valid values are 1, 2, 4, 8, and 16, although not all values are allowed for all color types.
Color type is a single-byte integer that describes the interpretation of the image data. Color type codes represent sums of the following values: 1 (palette used), 2 (color used), and 4 (alpha channel used). Valid values
are 0, 2, 3, 4, and 6.
Bit depth restrictions for each color type are imposed to simplify implementations and to prohibit combinations that do not compress well. Decoders must support all legal combinations of bit depth and color type.
The allowed combinations are ...

4.1.2 PLTE Palette
The PLTE chunk contains from 1 to 256 palette entries, each a three-byte series of the form:
Red: 1 byte (0 = black, 255 = red)
Green: 1 byte (0 = black, 255 = green)
Blue: 1 byte (0 = black, 255 = blue)
The number of entries is determined from the chunk length. A chunk length not divisible by 3 is an error.
This chunk must appear for color type 3
For color type 3 (indexed color), the PLTE chunk is required. The first entry in PLTE is referenced by pixel
value 0, the second by pixel value 1, etc. The number of palette entries must not exceed the range that can
be represented in the image bit depth (for example, 2ˆ4 = 16 for a bit depth of 4). It is permissible to have
fewer entries than the bit depth would allow. In that case, any out-of-range pixel value found in the image
data is an error

4.1.3 IDAT Image data
The IDAT chunk contains the actual image data. To create this data:
1. Begin with image scanlines represented as described in Image layout (Section 2.3); the layout and total
size of this raw data are determined by the fields of IHDR.
2. Filter the image data according to the filtering method specified by the IHDR chunk. (Note that with
filter method 0, the only one currently defined, this implies prepending a filter type byte to each scanline.)
3. Compress the filtered data using the compression method specified by the IHDR chunk.
The IDAT chunk contains the output datastream of the compression algorithm.
To read the image data, reverse this process.
There can be multiple IDAT chunks; if so, they must appear consecutively with no other intervening chunks.
The compressed datastream is then the concatenation of the contents of all the IDAT chunks. The encoder
can divide the compressed datastream into IDAT chunks however it wishes. (Multiple IDAT chunks are
allowed so that encoders can work in a fixed amount of memory; typically the chunk size will correspond
to the encoder’s buffer size.) It is important to emphasize that IDAT chunk boundaries have no semantic
significance and can occur at any point in the compressed datastream

https://tools.ietf.org/html/rfc1950#page-4
2.2. Data format

      A zlib stream has the following structure:

           0   1
         +---+---+
         |CMF|FLG|   (more-->)
         +---+---+

      (if FLG.FDICT set)

           0   1   2   3
         +---+---+---+---+
         |     DICTID    |   (more-->)
         +---+---+---+---+

         +=====================+---+---+---+---+
         |...compressed data...|    ADLER32    |
         +=====================+---+---+---+---+

      Any data which may appear after ADLER32 are not part of the zlib
      stream.

      CMF (Compression Method and flags)
         This byte is divided into a 4-bit compression method and a 4-
         bit information field depending on the compression method.

            bits 0 to 3  CM     Compression method
            bits 4 to 7  CINFO  Compression info

      CM (Compression method)
         This identifies the compression method used in the file. CM = 8
         denotes the "deflate" compression method with a window size up
         to 32K.  This is the method used by gzip and PNG (see
         references [1] and [2] in Chapter 3, below, for the reference
         documents).  CM = 15 is reserved.  It might be used in a future
         version of this specification to indicate the presence of an
         extra field before the compressed data.

      CINFO (Compression info)
         For CM = 8, CINFO is the base-2 logarithm of the LZ77 window
         size, minus eight (CINFO=7 indicates a 32K window size). Values
         of CINFO above 7 are not allowed in this version of the
         specification.  CINFO is not defined in this specification for
         CM not equal to 8.

      FLG (FLaGs)
         This flag byte is divided as follows:

            bits 0 to 4  FCHECK  (check bits for CMF and FLG)
            bit  5       FDICT   (preset dictionary)
            bits 6 to 7  FLEVEL  (compression level)

         The FCHECK value must be such that CMF and FLG, when viewed as
         a 16-bit unsigned integer stored in MSB order (CMF*256 + FLG),
         is a multiple of 31.

      FDICT (Preset dictionary)
         If FDICT is set, a DICT dictionary identifier is present
         immediately after the FLG byte. The dictionary is a sequence of
         bytes which are initially fed to the compressor without
         producing any compressed output. DICT is the Adler-32 checksum
         of this sequence of bytes (see the definition of ADLER32
         below).  The decompressor can use this identifier to determine
         which dictionary has been used by the compressor.

      FLEVEL (Compression level)
         These flags are available for use by specific compression
         methods.  The "deflate" method (CM = 8) sets these flags as
         follows:

            0 - compressor used fastest algorithm
            1 - compressor used fast algorithm
            2 - compressor used default algorithm
            3 - compressor used maximum compression, slowest algorithm

         The information in FLEVEL is not needed for decompression; it
         is there to indicate if recompression might be worthwhile.

 https://tools.ietf.org/html/rfc1951
 3.2.3. Details of block format

         Each block of compressed data begins with 3 header bits
         containing the following data:

            first bit       BFINAL
            next 2 bits     BTYPE

         Note that the header bits do not necessarily begin on a byte
         boundary, since a block does not necessarily occupy an integral
         number of bytes.

         BFINAL is set if and only if this is the last block of the data
         set.

         BTYPE specifies how the data are compressed, as follows:

            00 - no compression
            01 - compressed with fixed Huffman codes
            10 - compressed with dynamic Huffman codes
            11 - reserved (error)

 3.2.4. Non-compressed blocks (BTYPE=00)

         Any bits of input up to the next byte boundary are ignored.
         The rest of the block consists of the following information:

              0   1   2   3   4...
            +---+---+---+---+================================+
            |  LEN  | NLEN  |... LEN bytes of literal data...|
            +---+---+---+---+================================+

         LEN is the number of data bytes in the block.  NLEN is the
         one's complement of LEN.

Chapter 6 https://www.w3.org/TR/REC-png.pdf
On Filters: The encoder can choose which of these filter algorithms to apply on a scanline-by-scanline basis. In the image data sent to the compression step, each scanline is preceded by a filter type byte that specifies the filter algorithm used for that scanline.

https://tools.ietf.org/html/rfc1950#page-6
 ADLER32 (Adler-32 checksum)
         This contains a checksum value of the uncompressed data
         (excluding any dictionary data) computed according to Adler-32
         algorithm. This algorithm is a 32-bit extension and improvement
         of the Fletcher algorithm, used in the ITU-T X.224 / ISO 8073
         standard. See references [4] and [5] in Chapter 3, below)

         Adler-32 is composed of two sums accumulated per byte: s1 is
         the sum of all bytes, s2 is the sum of all s1 values. Both sums
         are done modulo 65521. s1 is initialized to 1, s2 to zero.  The
         Adler-32 checksum is stored as s2*65536 + s1 in most-
         significant-byte first (network) order.



https://www.w3.org/TR/REC-png.pdf 
4.1.4 IEND Image trailer
The IEND chunk must appear LAST. It marks the end of the PNG datastream. The chunk’s data field is
empty.
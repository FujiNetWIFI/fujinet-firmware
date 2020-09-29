#include "cassette.h"
#include "fnSystem.h"
#include "led.h"
#include "../../include/debug.h"

/** thinking about state machine
 * boolean states:
 *      file mounted or not
 *      motor activated or not 
 *      (play/record button?)
 * state variables:
 *      baud rate
 *      file position (offset)
 * */

//#define CASSETTE_FILE "/test.cas" // zaxxon
#define CASSETTE_FILE "/hello.cas" // basic program

// copied from fuUART.cpp - figure out better way
#ifdef BOARD_HAS_PSRAM
#define UART2_RX 33
#define UART2_TX 21
#else
#define UART2_RX 16
#define UART2_TX 17
#endif

cassetteUART cas_encoder;

uint8_t cassetteUART::available()
{
    return index_in - index_out;
}

void cassetteUART::set_baud(uint16_t b)
{
    baud = b;
    period = 1000000 / baud;
};

uint8_t cassetteUART::get_next_byte()
{
    return buffer[index_out++];
}

int8_t cassetteUART::service(uint8_t b)
{
    unsigned long t = fnSystem.micros();
    if (state_counter == STARTBIT)
    {
        if (b == 1)
        { // found start bit - sync up clock
            state_counter++;
            received_byte = 0; // clear data
            baud_clock = t;    // approx beginning of start bit
#ifdef DEBUG
            Debug_println("Start bit received!");
#endif
        }
    }
    else if (t > baud_clock + period * state_counter + period / 10)
    {
        if (t < baud_clock + period * state_counter + 9 * period / 10)
        {
            if (state_counter == STOPBIT)
            {
                buffer[index_in++] = received_byte;
                state_counter = STARTBIT;
#ifdef DEBUG
                Debug_printf("received %02X\n", received_byte);
#endif
                if (b != 0)
                {
#ifdef DEBUG
                    Debug_println("Stop bit invalid!");
#endif
                    return -1; // frame sync error
                }
            }
            else
            {
                received_byte *= 2; // shift to left
                received_byte += b;
            }
        }
        else
        {
#ifdef DEBUG
            Debug_println("Bit slip error!");
#endif
            state_counter = STARTBIT;
            return -1; // frame sync error
        }
    }
    return 0;
}

void sioCassette::close_cassette_file()
{
    if (_file != nullptr)
        fclose(_file);
    _mounted = false;
}

void sioCassette::open_cassette_file(FileSystem *filesystem)
{
    _FS = filesystem;
    if (_file != nullptr)
        fclose(_file);
    _file = _FS->file_open(CASSETTE_FILE, "w+"); // use "w+" for CSAVE test
    filesize = _FS->filesize(_file);
#ifdef DEBUG
    if (_file != nullptr)
        Debug_println("CAS file opened succesfully!");
    else
        Debug_println("Could not open CAS file :(");
#endif

    tape_offset = 0;
    if (cassetteMode == cassette_mode_t::playback)
        check_for_FUJI_file();

#ifdef DEBUG
    if (tape_flags.FUJI)
        Debug_println("FUJI File Found");
    else if (cassetteMode == cassette_mode_t::playback)
        Debug_println("Not a FUJI File");
    else
        Debug_println("A File for Recording");
    unsigned long a = fnSystem.micros();
    unsigned long b = fnSystem.micros();
    Debug_printf("%u\n", a);
    Debug_printf("%u\n", b);
    Debug_println("A File for Recording");
#endif

    // #ifdef DEBUG
    //     Debug_println("Sync Wait...");
    // #endif

    //     // TO DO decouple opening a file with the initial delay
    //     // TO DO understand non-FUJI file format and why
    //     if (!tape_flags.FUJI)
    //         fnSystem.delay(10000);

    _mounted = true;
}

void sioCassette::sio_enable_cassette()
{
    cassetteActive = true;

    if (cassetteMode == cassette_mode_t::playback)
        fnUartSIO.set_baudrate(CASSETTE_BAUD);

    if (cassetteMode == cassette_mode_t::record && tape_offset == 0)
    {
        fnUartSIO.end();
        fnSystem.set_pin_mode(UART2_RX, gpio_mode_t::GPIO_MODE_INPUT);
#ifdef DEBUG
        Debug_println("stopped hardware UART");
        int a = fnSystem.digital_read(UART2_RX);
        Debug_printf("set pin to input. Value is %d\n", a);
        Debug_println("Writing FUJI File HEADERS");
#endif
        fprintf(_file, "FUJI");
        fputc(16, _file);
        fputc(0, _file);
        fputc(0, _file);
        fputc(0, _file);
        fprintf(_file, "FujiNet CAS File");

        fprintf(_file, "baud");
        fputc(0, _file);
        fputc(0, _file);
        fputc(0x58, _file);
        fputc(0x02, _file);

        fflush(_file);
        tape_offset = ftell(_file);
        block++;
    }

#ifdef DEBUG
    Debug_println("Cassette Mode enabled");
#endif
}

void sioCassette::sio_disable_cassette()
{
    cassetteActive = false;
    if (cassetteMode == cassette_mode_t::playback)
        fnUartSIO.set_baudrate(SIO_STANDARD_BAUDRATE);
    else
        fnUartSIO.begin(SIO_STANDARD_BAUDRATE);

#ifdef DEBUG
    Debug_println("Cassette Mode disabled");
#endif
}

void sioCassette::sio_handle_cassette()
{
    if (cassetteMode == cassette_mode_t::playback)
    {
        if (tape_flags.FUJI)
            tape_offset = send_FUJI_tape_block(tape_offset);
        else
            tape_offset = send_tape_block(tape_offset);

        // if after trying to send data, still at the start, then turn off tape
        if (tape_offset == 0 || !cassetteActive)
        {
            sio_disable_cassette();
        }
    }
    else if (cassetteMode == cassette_mode_t::record)
    {
        tape_offset = receive_FUJI_tape_block(tape_offset);
    }
}

void sioCassette::Clear_atari_sector_buffer(uint16_t len)
{
    //Maze atari_sector_buffer
    unsigned char *ptr;
    ptr = atari_sector_buffer;
    do
    {
        *ptr++ = 0;
        len--;
    } while (len);
}

unsigned sioCassette::send_tape_block(unsigned int offset)
{
    unsigned char *p = atari_sector_buffer + BLOCK_LEN - 1;
    unsigned char i, r;

    // if (offset < FileInfo.vDisk->size) {	//data record
    if (offset < filesize)
    { //data record
#ifdef DEBUG
        //print_str(35,132,2,Yellow,window_bg, (char*) atari_sector_buffer);
        //sprintf_P((char*)atari_sector_buffer,PSTR("Block %u / %u "),offset/BLOCK_LEN+1,(FileInfo.vDisk->size-1)/BLOCK_LEN+1);
        Debug_printf("Block %u of %u \r\n", offset / BLOCK_LEN + 1, filesize / BLOCK_LEN + 1);
#endif
        //read block
        //r = faccess_offset(FILE_ACCESS_READ, offset, BLOCK_LEN);
        fseek(_file, offset, SEEK_SET);
        r = fread(atari_sector_buffer, 1, BLOCK_LEN, _file);

        //shift buffer 3 bytes right
        for (i = 0; i < BLOCK_LEN; i++)
        {
            *(p + 3) = *p;
            p--;
        }
        if (r < BLOCK_LEN)
        {                                  //no full record?
            atari_sector_buffer[2] = 0xfa; //mark partial record
            atari_sector_buffer[130] = r;  //set size in last byte
        }
        else
            atari_sector_buffer[2] = 0xfc; //mark full record

        offset += r;
    }
    else
    { //this is the last/end record
#ifdef DEBUG
        //print_str_P(35, 132, 2, Yellow, window_bg, PSTR("End  "));
        Debug_println("CASSETTE END");
#endif
        Clear_atari_sector_buffer(BLOCK_LEN + 3);
        atari_sector_buffer[2] = 0xfe; //mark end record
        offset = 0;
    }
    atari_sector_buffer[0] = 0x55; //sync marker
    atari_sector_buffer[1] = 0x55;
    // USART_Send_Buffer(atari_sector_buffer, BLOCK_LEN + 3);
    fnUartSIO.write(atari_sector_buffer, BLOCK_LEN + 3);
    //USART_Transmit_Byte(get_checksum(atari_sector_buffer, BLOCK_LEN + 3));
    fnUartSIO.write(sio_checksum(atari_sector_buffer, BLOCK_LEN + 3));
    fnUartSIO.flush(); // wait for all data to be sent just like a tape
    // _delay_ms(300); //PRG(0-N) + PRWT(0.25s) delay
    fnSystem.delay(300);
    return (offset);
}

void sioCassette::check_for_FUJI_file()
{
    struct tape_FUJI_hdr *hdr = (struct tape_FUJI_hdr *)atari_sector_buffer;
    uint8_t *p = hdr->chunk_type;

    // faccess_offset(FILE_ACCESS_READ, 0, sizeof(struct tape_FUJI_hdr));
    fseek(_file, 0, SEEK_SET);
    fread(atari_sector_buffer, 1, sizeof(struct tape_FUJI_hdr), _file);
    if (p[0] == 'F' && //search for FUJI header
        p[1] == 'U' &&
        p[2] == 'J' &&
        p[3] == 'I')
    {
        tape_flags.FUJI = 1;
    }
    else
    {
        tape_flags.FUJI = 0;
    }

    if (tape_flags.turbo) //set fix to
        baud = 1000;      //1000 baud
    else
        baud = 600;
    // TO DO support kbps turbo mode
    // set_tape_baud();

    block = 0;
    return;
}

unsigned int sioCassette::send_FUJI_tape_block(unsigned int offset)
{
    size_t r;
    uint16_t gap, len;
    uint16_t buflen = 256;
    unsigned char first = 1;
    struct tape_FUJI_hdr *hdr = (struct tape_FUJI_hdr *)atari_sector_buffer;
    uint8_t *p = hdr->chunk_type;

    unsigned int starting_offset = offset;

    while (offset < filesize) // FileInfo.vDisk->size)
    {
        // looking for a data header while handling baud changes along the way
#ifdef DEBUG
        Debug_printf("Offset: %u\r\n", offset);
#endif
        fseek(_file, offset, SEEK_SET);
        fread(atari_sector_buffer, 1, sizeof(struct tape_FUJI_hdr), _file);
        len = hdr->chunk_length;

        if (p[0] == 'd' && //is a data header?
            p[1] == 'a' &&
            p[2] == 't' &&
            p[3] == 'a')
        {
            block++;
            break;
        }
        else if (p[0] == 'b' && //is a baud header?
                 p[1] == 'a' &&
                 p[2] == 'u' &&
                 p[3] == 'd')
        {
            if (tape_flags.turbo) //ignore baud hdr
                continue;
            baud = hdr->irg_length;
            fnUartSIO.set_baudrate(baud);
        }
        offset += sizeof(struct tape_FUJI_hdr) + len;
    }

    // TO DO : check that "data" record was actually found - not done by SDrive until after IRG by checking offset<filesize

    gap = hdr->irg_length; //save GAP
    len = hdr->chunk_length;
#ifdef DEBUG
    Debug_printf("Baud: %u Length: %u Gap: %u ", baud, len, gap);
#endif

    // TO DO : turn on LED
    fnLedManager.set(eLed::LED_SIO, true);
    while (gap--)
    {
        fnSystem.delay_microseconds(999); // shave off a usec for the MOTOR pin check
        if (fnSystem.digital_read(PIN_MTR) == DIGI_LOW)
        {
            fnLedManager.set(eLed::LED_SIO, false);
            return starting_offset;
        }
    }
    fnLedManager.set(eLed::LED_SIO, false);

#ifdef DEBUG
    // wait until after delay for new line so can see it in timestamp
    Debug_printf("\r\n");
#endif

    if (offset < filesize)
    {
        // data record
#ifdef DEBUG
        Debug_printf("Block %u\r\n", block);
#endif
        // read block in 256 byte (or fewer) chunks
        offset += sizeof(struct tape_FUJI_hdr); //skip chunk hdr
        while (len)
        {
            if (len > 256)
            {
                buflen = 256;
                len -= 256;
            }
            else
            {
                buflen = len;
                len = 0;
            }

            fseek(_file, offset, SEEK_SET);
            r = fread(atari_sector_buffer, 1, buflen, _file);
            offset += r;

#ifdef DEBUG
            Debug_printf("Sending %u bytes\r\n", buflen);
            for (int i = 0; i < buflen; i++)
                Debug_printf("%02x ", atari_sector_buffer[i]);
#endif
            fnUartSIO.write(atari_sector_buffer, buflen);
            fnUartSIO.flush(); // wait for all data to be sent just like a tape
#ifdef DEBUG
            Debug_printf("\r\n");
#endif

            if (first && atari_sector_buffer[2] == 0xfe)
            {
                // resets block counter for next section
                block = 0;
            }
            first = 0;
        }
        if (block == 0)
        {
            // TO DO : why does Sdrive do this?
            //_delay_ms(200); //add an end gap to be sure
            fnSystem.delay(200);
        }
    }
    else
    {
        //block = 0;
        offset = 0;
    }
    return (offset);
}

unsigned int sioCassette::receive_FUJI_tape_block(unsigned int offset)
{
    // start counting the IRG
    uint64_t tic = fnSystem.millis();
    // TODO just print out fsk periods and don't bother with the UART for testing
    while (!cas_encoder.available())
        cas_encoder.service(decode_fsk());
    uint16_t irg = fnSystem.millis() - tic;
#ifdef DEBUG
    Debug_printf("irg %u\n", irg);
#endif
    // LEFT OFF HERE =================================================================================
    // need to figure out polling/looping logic with receive_FUJI_tape_block()
    // and cassetteUART::service(uint8_t b)
    // start counting IRG, waiting for first startbit,
    offset += fwrite(&irg, 2, 1, _file); // IRG
    return offset;
}

void sioCassette::detect_falling_edge()
{
    unsigned long t = fnSystem.micros();
    do
    {
        if (fnSystem.micros() - t > 1000)
        {
#ifdef DEBUG
            Debug_println("time out waiting for fsk LOW");
#endif
            break;
        }
    } while (fnSystem.digital_read(UART2_RX) == DIGI_LOW);
    do
    {
        if (fnSystem.micros() - t > 1000)
        {
#ifdef DEBUG
            Debug_println("time out waiting for fsk HIGH");
#endif
            break;
        }
    } while (fnSystem.digital_read(UART2_RX) == DIGI_HIGH);
}

uint8_t sioCassette::decode_fsk()
{
    // wait for falling edge and set fsk_clock
    // find next falling edge and compute period
    // check if period different than last (reset denoise counter)
    // if not different, increment denoise counter if < denoise threshold
    // when denoise counter == denoise threshold, set demod output

    // LEFT OFF HERE =================================================================================
    // TODO: just print out fsk periods

    unsigned long old = fsk_clock;
    unsigned long now = fnSystem.micros();

    if (old + period_space < now)
    { // either first tic or missed tic
#ifdef DEBUG
        Debug_println("missed fsk cycle");
#endif
        detect_falling_edge();
        old = fnSystem.micros();
    }
    detect_falling_edge();
    fsk_clock = fnSystem.micros();
#ifdef DEBUG
    Debug_printf("%u\n", fsk_clock - old);
#endif
    // if time difference is short, then mark
    if (fsk_clock - old < (period_mark + period_space) / 2 && fsk_clock - old > period_mark + period_mark / 2)
        return 0; // mark - bus is voltage high which is logic 0
    else
        return 1; // space - logic 1, start bit
}

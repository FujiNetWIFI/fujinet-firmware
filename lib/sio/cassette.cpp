#include "cassette.h"
#include "fnSystem.h"
#include "led.h"
#include "../../include/debug.h"

#include "cstring"

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
#define CASSETTE_FILE "/test" // basic program

// copied from fuUART.cpp - figure out better way
#define UART2_RX 33
#define ESP_INTR_FLAG_DEFAULT 0
#define BOXLEN 5

unsigned long last = 0;
unsigned long delta = 0;
unsigned long boxcar[BOXLEN];
uint8_t boxidx = 0;

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    if (gpio_num == UART2_RX)
    {
        unsigned long now = fnSystem.micros();
        boxcar[boxidx++] = now - last; // interval between current and last ISR call
        if (boxidx > BOXLEN)
            boxidx = 0; // circular buffer action
        delta = 0; // accumulator for boxcar filter
        for (uint8_t i = 0; i < BOXLEN; i++)
        {
            delta += boxcar[i]; // accumulate internvals for averaging
        }
        delta /= BOXLEN; // normalize accumulator to make mean
        last = now; // remember when this was (maybe move up to right before if statement?)
    }
}

softUART casUART;

uint8_t softUART::available()
{
    return index_in - index_out;
}

void softUART::set_baud(uint16_t b)
{
    baud = b;
    period = 1000000 / baud;
};

uint8_t softUART::read()
{
    return buffer[index_out++];
}

int8_t softUART::service(uint8_t b)
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
//            Debug_println("Start bit received!");
#endif
        }
    }
    else if (t > baud_clock + period * state_counter + period / 4)
    {
        if (t < baud_clock + period * state_counter + 9 * period / 4)
        {
            if (state_counter == STOPBIT)
            {
                buffer[index_in++] = received_byte;
                state_counter = STARTBIT;
#ifdef DEBUG
//                Debug_printf("received %02X\n", received_byte);
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
                uint8_t bb = (b == 1) ? 0 : 1;
                received_byte |= (bb << (state_counter - 1));
                state_counter++;
#ifdef DEBUG
//                Debug_printf("bit %u ", state_counter - 1);
//                Debug_printf("%u\n ", b);
#endif
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
    // TODO: do not need
    if (_file != nullptr)
    {
        fclose(_file);
#ifdef DEBUG
        Debug_println("CAS file closed.");
#endif
    }

    // TODO: where to move this?????? disk image unmount needs to do this function
    _mounted = false;
}

void sioCassette::mount_cassette_file(FILE *f, size_t fz)
{
    _file = f;
    filesize = fz;
    
#ifdef DEBUG
    Debug_printf("Cassette image filesize = %u\n", fz);
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
#endif

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

        //change gpio intrrupt type for one pin
        gpio_set_intr_type((gpio_num_t)UART2_RX, GPIO_INTR_ANYEDGE);
        //install gpio isr service
        gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
        //hook isr handler for specific gpio pin
        gpio_isr_handler_add((gpio_num_t)UART2_RX, gpio_isr_handler, (void *)UART2_RX);

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

void sioCassette::set_buttons(bool play_record)
{
    if (!play_record)
        cassetteMode = cassette_mode_t::playback;
    else
        cassetteMode = cassette_mode_t::record;
}

bool sioCassette::get_buttons()
{
    return (cassetteMode == cassette_mode_t::playback);
}

void sioCassette::set_pulldown(bool resistor)
{
            pulldown = resistor;
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

size_t sioCassette::send_tape_block(size_t offset)
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

size_t sioCassette::send_FUJI_tape_block(size_t offset)
{
    size_t r;
    uint16_t gap, len;
    uint16_t buflen = 256;
    unsigned char first = 1;
    struct tape_FUJI_hdr *hdr = (struct tape_FUJI_hdr *)atari_sector_buffer;
    uint8_t *p = hdr->chunk_type;

    size_t starting_offset = offset;

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
        if (has_pulldown() && !motor_line() && gap > 1000)
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
        /*         if (block == 0)
        {
            // TO DO : why does Sdrive do this?
            //_delay_ms(200); //add an end gap to be sure
            fnSystem.delay(200);
        } */
    }
    else
    {
        //block = 0;
        offset = 0;
    }
    return (offset);
}

size_t sioCassette::receive_FUJI_tape_block(size_t offset)
{
    Clear_atari_sector_buffer(BLOCK_LEN + 4);
    uint8_t idx = 0;

    // start counting the IRG
    uint64_t tic = fnSystem.millis();

    // write out data here to file
    offset += fprintf(_file, "data");
    offset += fputc(BLOCK_LEN + 4, _file); // 132 bytes
    offset += fputc(0, _file);

    while (!casUART.available()) // && motor_line()
        casUART.service(decode_fsk());
    uint16_t irg = fnSystem.millis() - tic - 10000 / casUART.get_baud(); // adjust for first byte
#ifdef DEBUG
    Debug_printf("irg %u\n", irg);
#endif
    offset += fwrite(&irg, 2, 1, _file);
    uint8_t b = casUART.read(); // should be 0x55
    atari_sector_buffer[idx++] = b;
#ifdef DEBUG
    Debug_printf("marker 1: %02x\n", b);
#endif

    while (!casUART.available()) // && motor_line()
        casUART.service(decode_fsk());
    b = casUART.read(); // should be 0x55
    atari_sector_buffer[idx++] = b;
#ifdef DEBUG
    Debug_printf("marker 2: %02x\n", b);
#endif

    while (!casUART.available()) // && motor_line()
        casUART.service(decode_fsk());
    b = casUART.read(); // control byte
    atari_sector_buffer[idx++] = b;
#ifdef DEBUG
    Debug_printf("control byte: %02x\n", b);
#endif

    int i = 0;
    while (i < BLOCK_LEN)
    {
        while (!casUART.available()) // && motor_line()
            casUART.service(decode_fsk());
        b = casUART.read(); // data
        atari_sector_buffer[idx++] = b;
#ifdef DEBUG
//        Debug_printf(" %02x", b);
#endif
        i++;
    }
#ifdef DEBUG
//    Debug_printf("\n");
#endif

    while (!casUART.available()) // && motor_line()
        casUART.service(decode_fsk());
    b = casUART.read(); // checksum
    atari_sector_buffer[idx++] = b;
#ifdef DEBUG
    Debug_printf("checksum: %02x\n", b);
#endif

#ifdef DEBUG
    Debug_print("data: ");
    for (int i = 0; i < BLOCK_LEN + 4; i++)
        Debug_printf("%02x ", atari_sector_buffer[i]);
    Debug_printf("\n");
#endif

    offset += fwrite(atari_sector_buffer, 1, BLOCK_LEN + 4, _file);

#ifdef DEBUG
    Debug_printf("file offset: %d\n", offset);
#endif
    return offset;
}

uint8_t sioCassette::decode_fsk()
{
    // take "delta" set in the IRQ and set the demodulator output

    uint8_t out = last_output;

    if (delta > 0)
    {
        // #ifdef DEBUG
        //         Debug_printf("delta: %u\n", delta);
        // #endif
        if (delta > 90 && delta < 97)
            out = 0;
        if (delta > 119 && delta < 130)
            out = 1;
        last_output = out;
    }
    // #ifdef DEBUG
    //     Debug_printf("%lu, ", fnSystem.micros());
    //     Debug_printf("%u\n", out);
    // #endif
    return out;
}

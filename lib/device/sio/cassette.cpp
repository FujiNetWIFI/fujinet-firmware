#ifdef BUILD_ATARI

#include "cassette.h"

#include <cstring>

#include "../../include/debug.h"

#include "fnSystem.h"
#include "fnUART.h"
#include "fnFsSD.h"

#include "led.h"

// TODO: merge/fix this at global level
#ifdef ESP_PLATFORM
#define FN_BUS_LINK fnUartBUS
#else
#define FN_BUS_LINK fnSioCom
#endif

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
#define CASSETTE_FILE "/csave" // basic program

// copied from fuUART.cpp - figure out better way
#define UART2_RX 33
#define ESP_INTR_FLAG_DEFAULT 0
#define BOXLEN 5

unsigned long last = 0;
unsigned long delta = 0;
unsigned long boxcar[BOXLEN];
uint8_t boxidx = 0;

#ifdef ESP_PLATFORM
static void IRAM_ATTR cas_isr_handler(void *arg)
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
#endif

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
//            Debug_println("Start bit received!");
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
//                Debug_printf("received %02X\n", received_byte);
                if (b != 0)
                {
                    Debug_println("Stop bit invalid!");
                    return -1; // frame sync error
                }
            }
            else
            {
                uint8_t bb = (b == 1) ? 0 : 1;
                received_byte |= (bb << (state_counter - 1));
                state_counter++;
//                Debug_printf("bit %u ", state_counter - 1);
//                Debug_printf("%u\n ", b);
            }
        }
        else
        {
            Debug_println("Bit slip error!");
            state_counter = STARTBIT;
            return -1; // frame sync error
        }
    }
    return 0;
}


//************************************************************************************************************
// ***** nerd at work! ******

void sioCassette::close_cassette_file()
{
    // for closing files used for writing
    if (_file != nullptr)
    {
        fnio::fclose(_file);
        Debug_println("CAS file closed.");
    }
}

void sioCassette::open_cassette_file(FileSystem *_FS)
{
    // to open files for writing
    char fn[32];
    char mm[21];
    strcpy(fn, CASSETTE_FILE);
    if (cassetteMode == cassette_mode_t::record)
    {
        sprintf(mm, "%020llu", (unsigned long long)fnSystem.millis());
        strcat(fn, mm);
    }
    strcat(fn, ".cas");

    close_cassette_file();
    _file = _FS->fnfile_open(fn, "wb+"); // use "w+" for CSAVE test
    if (!_file)
    {
        _mounted = false;
        Debug_print("Could not open CAS file :( ");
        Debug_println(fn);
        return;
    }
    Debug_printf("%s - ", fn);
    Debug_println("CAS file opened succesfully!");
}


//************************************************************************************************************


void sioCassette::umount_cassette_file()
{
        Debug_println("CAS file closed.");
        _mounted = false;
}

void sioCassette::mount_cassette_file(fnFile *f, size_t fz)
{

    tape_offset = 0;
    if (cassetteMode == cassette_mode_t::playback)
    {
        Debug_printf("Cassette image filesize = %u\n", (unsigned)fz);
        _file = f;
        filesize = fz;
        check_for_FUJI_file();
    }
    else
    {
        // CONFIG does not mount a CAS file for writing - only read only.
        // disk mount (mediatype_t sioDisk::mount(FILE *f, const char *filename, uint32_t disksize, mediatype_t disk_type))
        // mounts a CAS file by calling this function.
        // There is no facility to specify an output file for writing to C: or CSAVE
        // so instead of using the file mounted in slot 8 by CONFIG, create an output file with some serial number
        // files are created with the cassette is enabled.
 
    }

    _mounted = true;
}

void sioCassette::sio_enable_cassette()
{
    cassetteActive = true;

    if (cassetteMode == cassette_mode_t::playback)
        FN_BUS_LINK.set_baudrate(CASSETTE_BAUDRATE);

    if (cassetteMode == cassette_mode_t::record && tape_offset == 0)
    {
        open_cassette_file(&fnSDFAT); // hardcode SD card?
        FN_BUS_LINK.end();
#ifdef ESP_PLATFORM
        fnSystem.set_pin_mode(UART2_RX, gpio_mode_t::GPIO_MODE_INPUT, SystemManager::pull_updown_t::PULL_NONE, GPIO_INTR_ANYEDGE);

        // hook isr handler for specific gpio pin
        if (gpio_isr_handler_add((gpio_num_t)UART2_RX, cas_isr_handler, (void *)UART2_RX) != ESP_OK)
            {
                Debug_println("error attaching cassette data reading interrupt");
                return;
            }
        // TODO: do i need to unhook isr handler when cassette is disabled?

        Debug_println("stopped hardware UART");
#ifdef DEBUG
        int a = fnSystem.digital_read(UART2_RX);
#endif
        Debug_printf("set pin to input. Value is %d\n", a);
        Debug_println("Writing FUJI File HEADERS");
    #if 0
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
    #else
        unsigned char headers[] = {
            'F', 'U', 'J', 'I', 16, 0, 0, 0,
            'F', 'u', 'j', 'i', 'N', 'e', 't', ' ', 'C', 'A', 'S', ' ', 'F', 'i', 'l', 'e',
            'b', 'a', 'u', 'd', 0, 0, 0x58, 0x02
        };
        fnio::fwrite(headers, sizeof(headers), 1, _file);
    #endif
        fnio::fflush(_file);
        tape_offset = fnio::ftell(_file);
        block++;
#else
        Debug_println("Writing FUJI File HEADERS - NOT IMPLEMENTED!!!");
#endif
    }

    Debug_println("Cassette Mode enabled");
}

void sioCassette::sio_disable_cassette()
{
    if (cassetteActive)
    {
        cassetteActive = false;
        if (cassetteMode == cassette_mode_t::playback)
            FN_BUS_LINK.set_baudrate(SIO_STANDARD_BAUDRATE);
        else
        {
            close_cassette_file();
            //TODO: gpio_isr_handler_remove((gpio_num_t)UART2_RX);
            FN_BUS_LINK.begin(SIO_STANDARD_BAUDRATE);
        }
        Debug_println("Cassette Mode disabled");
    }
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

void sioCassette::rewind()
{
    // Is this all that's needed? -tschak
    tape_offset = 0;
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
#endif
        Debug_printf("Block %u of %u \r\n", offset / BLOCK_LEN + 1, filesize / BLOCK_LEN + 1);
        //read block
        //r = faccess_offset(FILE_ACCESS_READ, offset, BLOCK_LEN);
        fnio::fseek(_file, offset, SEEK_SET);
        r = fnio::fread(atari_sector_buffer, 1, BLOCK_LEN, _file);

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
#endif
        Debug_println("CASSETTE END");
        Clear_atari_sector_buffer(BLOCK_LEN + 3);
        atari_sector_buffer[2] = 0xfe; //mark end record
        offset = 0;
    }
    atari_sector_buffer[0] = 0x55; //sync marker
    atari_sector_buffer[1] = 0x55;
    // USART_Send_Buffer(atari_sector_buffer, BLOCK_LEN + 3);
    FN_BUS_LINK.write(atari_sector_buffer, BLOCK_LEN + 3);
    //USART_Transmit_Byte(get_checksum(atari_sector_buffer, BLOCK_LEN + 3));
    FN_BUS_LINK.write(sio_checksum(atari_sector_buffer, BLOCK_LEN + 3));
    FN_BUS_LINK.flush(); // wait for all data to be sent just like a tape
    // _delay_ms(300); //PRG(0-N) + PRWT(0.25s) delay
    fnSystem.delay(300);
    return (offset);
}

void sioCassette::check_for_FUJI_file()
{
    struct tape_FUJI_hdr *hdr = (struct tape_FUJI_hdr *)atari_sector_buffer;
    uint8_t *p = hdr->chunk_type;

    // faccess_offset(FILE_ACCESS_READ, 0, sizeof(struct tape_FUJI_hdr));
    fnio::fseek(_file, 0, SEEK_SET);
    fnio::fread(atari_sector_buffer, 1, sizeof(struct tape_FUJI_hdr), _file);
    if (p[0] == 'F' && //search for FUJI header
        p[1] == 'U' &&
        p[2] == 'J' &&
        p[3] == 'I')
    {
        tape_flags.FUJI = 1;
            Debug_println("FUJI File Found");
    }
    else
    {
        tape_flags.FUJI = 0;
          Debug_println("Not a FUJI File");
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
        Debug_printf("Offset: %u\r\n", offset);
        fnio::fseek(_file, offset, SEEK_SET);
        fnio::fread(atari_sector_buffer, 1, sizeof(struct tape_FUJI_hdr), _file);
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
            FN_BUS_LINK.set_baudrate(baud);
        }
        offset += sizeof(struct tape_FUJI_hdr) + len;
    }

    // TO DO : check that "data" record was actually found - not done by SDrive until after IRG by checking offset<filesize

    gap = hdr->irg_length; //save GAP
    len = hdr->chunk_length;
    Debug_printf("Baud: %u Length: %u Gap: %u ", baud, len, gap);

    // TO DO : turn on LED
    fnLedManager.set(eLed::LED_BUS, true);
    while (gap)
    {
#ifdef ESP_PLATFORM
        gap--;
        fnSystem.delay_microseconds(999); // shave off a usec for the MOTOR pin check
#else
        int step;
        // FN_BUS_LINK is fnSioCom
        if (FN_BUS_LINK.get_sio_mode() == SioCom::sio_mode::NETSIO)
            step = gap > 1000 ? 1000 : gap; // step is 1000 ms (NetSIO)
        else
            step = gap > 20 ? 20 : gap; // step is 20 ms (SerialSIO)
        gap -= step;
        FN_BUS_LINK.bus_idle(step); // idle bus (i.e. delay for SerialSIO, BUS_IDLE message for NetSIO)
#endif
        if (has_pulldown() && !motor_line() && gap > 1000)
        {
            fnLedManager.set(eLed::LED_BUS, false);
            return starting_offset;
        }
    }
    fnLedManager.set(eLed::LED_BUS, false);

    // wait until after delay for new line so can see it in timestamp
    Debug_printf("\r\n");

    if (offset < filesize)
    {
        // data record
        Debug_printf("Block %u\r\n", block);
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

            fnio::fseek(_file, offset, SEEK_SET);
            r = fnio::fread(atari_sector_buffer, 1, buflen, _file);
            offset += r;

            Debug_printf("Sending %u bytes\r\n", buflen);
            for (int i = 0; i < buflen; i++)
                Debug_printf("%02x ", atari_sector_buffer[i]);
            FN_BUS_LINK.write(atari_sector_buffer, buflen);
            FN_BUS_LINK.flush(); // wait for all data to be sent just like a tape
            Debug_printf("\r\n");

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
#ifdef ESP_PLATFORM
    Debug_println("Start listening for tape block from Atari");
    Clear_atari_sector_buffer(BLOCK_LEN + 4);
    uint8_t idx = 0;

    // start counting the IRG
    uint64_t tic = fnSystem.millis();

    // write out data here to file
    #if 0
    offset += fprintf(_file, "data");
    offset += fputc(BLOCK_LEN + 4, _file); // 132 bytes
    offset += fputc(0, _file);
    #else
    unsigned char data_head[] = {
        'd', 'a' ,'t', 'a', BLOCK_LEN + 4, 0
    };
    offset += fnio::fwrite(data_head, sizeof(data_head), 1, _file);
    #endif

    while (!casUART.available()) // && motor_line()
        casUART.service(decode_fsk());
    uint16_t irg = fnSystem.millis() - tic - 10000 / casUART.get_baud(); // adjust for first byte
    Debug_printf("irg %u\n", irg);
    offset += fnio::fwrite(&irg, 2, 1, _file);
    uint8_t b = casUART.read(); // should be 0x55
    atari_sector_buffer[idx++] = b;
    Debug_printf("marker 1: %02x\n", b);

    while (!casUART.available()) // && motor_line()
        casUART.service(decode_fsk());
    b = casUART.read(); // should be 0x55
    atari_sector_buffer[idx++] = b;
    Debug_printf("marker 2: %02x\n", b);

    while (!casUART.available()) // && motor_line()
        casUART.service(decode_fsk());
    b = casUART.read(); // control byte
    atari_sector_buffer[idx++] = b;
    Debug_printf("control byte: %02x\n", b);

    int i = 0;
    while (i < BLOCK_LEN)
    {
        while (!casUART.available()) // && motor_line()
            casUART.service(decode_fsk());
        b = casUART.read(); // data
        atari_sector_buffer[idx++] = b;
//        Debug_printf(" %02x", b);
        i++;
    }
//    Debug_printf("\n");

    while (!casUART.available()) // && motor_line()
        casUART.service(decode_fsk());
    b = casUART.read(); // checksum
    atari_sector_buffer[idx++] = b;
    Debug_printf("checksum: %02x\n", b);

    Debug_print("data: ");
    for (int i = 0; i < BLOCK_LEN + 4; i++)
        Debug_printf("%02x ", atari_sector_buffer[i]);
    Debug_printf("\n");

    offset += fnio::fwrite(atari_sector_buffer, 1, BLOCK_LEN + 4, _file);

    Debug_printf("file offset: %d\n", offset);
#else
    Debug_println("Start listening for tape block from Atari - NOT IMPLEMENTED!!!");
#endif
    return offset;
}

uint8_t sioCassette::decode_fsk()
{
    // take "delta" set in the IRQ and set the demodulator output

    uint8_t out = last_output;

    if (delta > 0)
    {
        Debug_printf("%lu ", delta);
        if (delta > 90 && delta < 97)
            out = 0;
        if (delta > 119 && delta < 130)
            out = 1;
        last_output = out;
    }
    // Debug_printf("%lu, ", fnSystem.micros());
    // Debug_printf("%u\n", out);
    return out;
}
#endif /* BUILD_ATARI */

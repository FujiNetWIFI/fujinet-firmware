#ifdef BUILD_ATARI

#include "cassette.h"

#include <cstring>

#include "../../include/debug.h"

#include "fnSystem.h"
#include "fnFsSD.h"
#include "fsFlash.h"
#include "fujiDevice.h"
#include "../../media/atari/diskType.h"

#include "led.h"

#ifdef ESP_PLATFORM
#include <esp_rom_gpio.h>
#include <driver/gpio.h>
#include <driver/rmt_tx.h>
#include <driver/rmt_encoder.h>
#include <soc/uart_periph.h>

// Turbo 2000: RMT clock = 1 MHz (1 µs per tick)
#define T2K_RMT_RESOLUTION_HZ 1000000

// Simple encoder callback for T2K — generates RMT symbols on the fly.
// Runs in ISR context (RMT ping-pong refill), MUST be in IRAM so it
// executes instantly without flash cache misses.
// Phases: pilot → sync → data bits (all gapless in one rmt_transmit).
size_t IRAM_ATTR t2k_encode_cb(const void *data, size_t data_size,
                                       size_t symbols_written, size_t symbols_free,
                                       rmt_symbol_word_t *symbols, bool *done, void *arg)
{
    sioCassette *cas = (sioCassette *)arg;
    const uint8_t *bytes = (const uint8_t *)data;
    const size_t pilot_count = cas->_t2k_pilot_pending;
    const size_t sync_count = cas->_t2k_sync_count;
    const size_t total_needed = pilot_count + sync_count + data_size * 8;
    const uint16_t ph = cas->t2k_pilot_half;
    const uint16_t b0h = cas->t2k_bit0_half;
    const uint16_t b1h = cas->t2k_bit1_half;
    const bool msb = cas->t2k_msb_first;
    size_t num = 0;

    while (num < symbols_free && (symbols_written + num) < total_needed)
    {
        size_t pos = symbols_written + num;
        if (pos < pilot_count)
        {
            // Phase 1: pilot tone
            symbols[num].duration0 = ph;
            symbols[num].level0 = 1;
            symbols[num].duration1 = ph;
            symbols[num].level1 = 0;
        }
        else if (pos < pilot_count + sync_count)
        {
            // Phase 2: sync symbols
            symbols[num] = cas->_t2k_sync_syms[pos - pilot_count];
        }
        else
        {
            // Phase 3: data bits
            size_t data_pos = pos - pilot_count - sync_count;
            size_t byte_idx = data_pos >> 3;
            size_t bit_idx = data_pos & 7;
            uint8_t bval = bytes[byte_idx];
            uint16_t half;
            if (msb)
                half = (bval & (0x80 >> bit_idx)) ? b1h : b0h;
            else
                half = (bval & (1 << bit_idx)) ? b1h : b0h;
            symbols[num].duration0 = half;
            symbols[num].level0 = 1;
            symbols[num].duration1 = half;
            symbols[num].level1 = 0;
        }
        num++;
    }

    *done = (symbols_written + num >= total_needed);
    return num;
}
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
        snprintf(mm, sizeof(mm), "%020llu", (unsigned long long)fnSystem.millis());
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
        unmount_turbo_loader();
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

        // If turbo format, mount loader XEX on a free disk slot
        if (tape_flags.turbo2000 || tape_flags.qros)
            mount_turbo_loader();
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
    {
        SYSTEM_BUS.setBaudrate(CASSETTE_BAUDRATE);
        // Only reset boot flag on fresh mount (tape_offset==0), not on
        // motor OFF/ON cycles during T2K playback between blocks.
        // Skip reset if turbo loader XEX is already mounted on disk —
        // PicoBoot handles booting, we must not send CAS boot again.
        if (tape_offset == 0 && _turbo_loader_slot < 0)
        {
            t2k_boot_sent = false;
            qros_boot_sent = false;
        }
    }

    if (cassetteMode == cassette_mode_t::record && tape_offset == 0)
    {
        open_cassette_file(&fnSDFAT); // hardcode SD card?
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
        {
#ifdef ESP_PLATFORM
            if (_rmt_active)
                turbo2000_deinit_rmt();
            if (_qros_pilot_active)
                qros_pilot_off();
#endif
            SYSTEM_BUS.setBaudrate(SIO_STANDARD_BAUDRATE);
        }
        else
        {
            close_cassette_file();
            //TODO: gpio_isr_handler_remove((gpio_num_t)UART2_RX);
        }
        Debug_println("Cassette Mode disabled");
    }
}

void sioCassette::sio_handle_cassette()
{
    if (cassetteMode == cassette_mode_t::playback)
    {
        if (tape_flags.turbo2000)
            tape_offset = send_turbo2000_tape_block(tape_offset);
        else if (tape_flags.qros)
            tape_offset = send_QROS_tape_block(tape_offset);
        else if (tape_flags.FUJI)
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
    t2k_boot_sent = false;
    qros_boot_sent = false;
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

    // if (offset < FileInfo.vDisk->size) {     //data record
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
    SYSTEM_BUS.write(atari_sector_buffer, BLOCK_LEN + 3);
    //USART_Transmit_Byte(get_checksum(atari_sector_buffer, BLOCK_LEN + 3));
    SYSTEM_BUS.write(sio_checksum(atari_sector_buffer, BLOCK_LEN + 3));
    SYSTEM_BUS.flushOutput(); // wait for all data to be sent just like a tape
    // _delay_ms(300); //PRG(0-N) + PRWT(0.25s) delay
    fnSystem.delay(300);
    return (offset);
}

void sioCassette::check_for_FUJI_file()
{
    struct tape_FUJI_hdr *hdr = (struct tape_FUJI_hdr *)atari_sector_buffer;
    uint8_t *p = hdr->chunk_type;

    tape_flags.FUJI = 0;
    tape_flags.turbo2000 = 0;
    tape_flags.qros = 0;

    fnio::fseek(_file, 0, SEEK_SET);
    fnio::fread(atari_sector_buffer, 1, sizeof(struct tape_FUJI_hdr), _file);
    if (p[0] == 'F' && p[1] == 'U' && p[2] == 'J' && p[3] == 'I')
    {
        tape_flags.FUJI = 1;
        Debug_println("FUJI File Found");

        uint16_t fuji_chunk_length = hdr->chunk_length; // save before scans clobber buffer

        // Scan first few chunks to detect Turbo 2000 PWM format
        size_t scan_offset = sizeof(struct tape_FUJI_hdr) + fuji_chunk_length;
        while (scan_offset < filesize && scan_offset < 256)
        {
            fnio::fseek(_file, scan_offset, SEEK_SET);
            fnio::fread(atari_sector_buffer, 1, sizeof(struct tape_FUJI_hdr), _file);
            uint16_t len = hdr->chunk_length;

            // Detect A8CAS PWM chunks → Turbo 2000
            if (p[0] == 'p' && p[1] == 'w' && p[2] == 'm')
            {
                tape_flags.turbo2000 = 1;
                Debug_println("Turbo 2000 PWM format detected (A8CAS)!");
                break;
            }

            // Standard CAS data chunk — stop scanning
            if (p[0] == 'd' && p[1] == 'a' && p[2] == 't' && p[3] == 'a')
                break;
            if (p[0] == 'b' && p[1] == 'a' && p[2] == 'u' && p[3] == 'd')
                break;

            scan_offset += sizeof(struct tape_FUJI_hdr) + len;
        }

        // If FUJI file but no T2K detected, scan for QROS turbo (baud > 3000)
        // QROS turbo uses 6580-6595 (AUDF=127) or 9535-9622 (AUDF=86).
        // Lower bauds (600, 854, 1000 etc.) are standard/KSO and handled
        // by the normal FUJI path.
        if (!tape_flags.turbo2000)
        {
            bool has_600_boot = false;
            scan_offset = sizeof(struct tape_FUJI_hdr) + fuji_chunk_length;
            while (scan_offset < filesize)
            {
                fnio::fseek(_file, scan_offset, SEEK_SET);
                fnio::fread(atari_sector_buffer, 1, sizeof(struct tape_FUJI_hdr), _file);
                uint16_t clen = hdr->chunk_length;

                if (p[0] == 'b' && p[1] == 'a' && p[2] == 'u' && p[3] == 'd')
                {
                    if (hdr->irg_length <= 600)
                    {
                        has_600_boot = true;
                    }
                    else if (hdr->irg_length > 3000)
                    {
                        tape_flags.qros = 1;
                        qros_turbo_baud = hdr->irg_length;
                        qros_boot_sent = has_600_boot; // skip embedded boot if CAS has its own
                        Debug_printf("QROS turbo format detected (baud=%u, has_boot=%d)\n",
                                     qros_turbo_baud, has_600_boot);
                        break;
                    }
                }

                scan_offset += sizeof(struct tape_FUJI_hdr) + clen;
            }
        }
    }
    else
    {
        Debug_println("Not a FUJI File");
    }

    if (tape_flags.turbo2000)
        baud = 600; // nominal, not used for UART — RMT bypasses UART
    else if (tape_flags.turbo)
        baud = 1000;
    else
        baud = 600;

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
            SYSTEM_BUS.setBaudrate(baud);
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
        // SYSTEM_BUS is fnSioCom
        if (SYSTEM_BUS.isBoIP())
            step = gap > 1000 ? 1000 : gap; // step is 1000 ms (NetSIO)
        else
            step = gap > 20 ? 20 : gap; // step is 20 ms (SerialSIO)
        gap -= step;
        SYSTEM_BUS.bus_idle(step); // idle bus (i.e. delay for SerialSIO, BUS_IDLE message for NetSIO)
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
            SYSTEM_BUS.write(atari_sector_buffer, buflen);
            SYSTEM_BUS.flushOutput(); // wait for all data to be sent just like a tape
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

// =============================================================================
// QROS turbo cassette playback
// =============================================================================

// QROS/EMO Stage 1 boot loader — 128 bytes of data (inside 132-byte record)
// Extracted from mercenary_EMO_6600.cas. Loaded at $036B by Atari ROM.
// Sets up POKEY async serial receive, receives Stage 2 (883B) at turbo baud,
// then jumps to $0700.
static const uint8_t qros_stage1_boot[] = {
    0x00, 0x01, 0x6B, 0x03, 0x00, 0x07, 0xA9, 0x34, 0x8D, 0x03, 0xD3, 0x78, 0xA0, 0x00, 0xA2, 0x0A,
    0xAD, 0x0F, 0xD2, 0x29, 0x10, 0xF0, 0xF5, 0x88, 0xD0, 0xF6, 0xCA, 0xD0, 0xF3, 0x84, 0x32, 0xA9,
    0x07, 0x85, 0x33, 0xA9, 0x72, 0x85, 0x34, 0xA9, 0x0A, 0x85, 0x35, 0xA9, 0x7F, 0x8D, 0x04, 0xD2,
    0x8C, 0x06, 0xD2, 0x84, 0x31, 0x84, 0x38, 0x84, 0x39, 0x84, 0x30, 0x84, 0x10, 0xA9, 0x13, 0x20,
    0x47, 0xEC, 0x58, 0x18, 0xA5, 0x39, 0xF0, 0xFC, 0xA4, 0x30, 0x10, 0x06, 0x20, 0x3E, 0xC6, 0x38,
    0xB0, 0x17, 0xA9, 0xC0, 0x20, 0x89, 0xEC, 0xA8, 0xA5, 0x6A, 0x49, 0xC0, 0xD0, 0x01, 0x88, 0x84,
    0x08, 0xA9, 0xAD, 0x85, 0x0A, 0xA9, 0x09, 0x85, 0x0B, 0xA9, 0x3C, 0x8D, 0x02, 0xD3, 0x8D, 0x03,
    0xD3, 0xA2, 0x10, 0xBD, 0x40, 0x03, 0x9D, 0x50, 0x03, 0xE8, 0x10, 0xF7, 0x60, 0x00, 0x00, 0x00,
};

// QROS/EMO Stage 2 main loader — 883 bytes, loads at $0700
// Full turbo loader with Atari DOS binary format support.
// Displays "LOADER -EMOSOFT-V5.1 (c) 02.03.90", waits for START key.
static const uint8_t qros_stage2_loader[] = {
    0xA9, 0xF5, 0x8D, 0xE7, 0x02, 0xA9, 0x0A, 0x8D, 0xE8, 0x02, 0xA2, 0x54, 0xA0, 0x2B, 0x20, 0x86,  // $0700
    0xE4, 0xA2, 0x4B, 0xA0, 0x0A, 0x4C, 0x42, 0xC6, 0xA9, 0x7F, 0x8D, 0x04, 0xD2, 0xA9, 0x00, 0x8D,  // $0710
    0x06, 0xD2, 0xA9, 0x28, 0x8D, 0x08, 0xD2, 0x8E, 0x0F, 0xD2, 0xA9, 0xFD, 0x85, 0x32, 0xA9, 0x03,  // $0720
    0x85, 0x33, 0xA9, 0x80, 0x85, 0x34, 0xA9, 0x04, 0x85, 0x35, 0x60, 0xA5, 0x2A, 0xC9, 0x04, 0xF0,  // $0730
    0x57, 0xC9, 0x08, 0xD0, 0xF5, 0xA9, 0x02, 0x20, 0xFC, 0xFD, 0x30, 0xEE, 0xA0, 0x80, 0x8C, 0x89,  // $0740
    0x02, 0xA9, 0x34, 0x8D, 0x02, 0xD3, 0x8D, 0x03, 0xD3, 0xA2, 0x23, 0xA9, 0x81, 0x20, 0x1A, 0x07,  // $0750
    0x20, 0x84, 0x09, 0x8A, 0xA2, 0x83, 0xCA, 0x9D, 0x72, 0x0A, 0xD0, 0xFA, 0xA2, 0x08, 0xC8, 0xB1,  // $0760
    0x24, 0x49, 0x58, 0x8D, 0x71, 0x0A, 0xD0, 0x01, 0xC8, 0xB1, 0x24, 0xC9, 0x3A, 0xF0, 0xF9, 0x9D,  // $0770
    0x72, 0x0A, 0x49, 0x9B, 0xF0, 0x08, 0xE8, 0x10, 0xEF, 0xA9, 0x9B, 0x9D, 0x72, 0x0A, 0xA9, 0xFD,  // $0780
    0x20, 0x58, 0x09, 0xA9, 0xFD, 0x4C, 0x35, 0x09, 0xA9, 0x01, 0x20, 0xFC, 0xFD, 0x30, 0x9B, 0xA0,  // $0790
    0x00, 0x8C, 0x89, 0x02, 0x84, 0x41, 0xA9, 0x34, 0x8D, 0x02, 0xD3, 0x8D, 0x03, 0xD3, 0x20, 0x88,  // $07A0
    0x08, 0x20, 0xC7, 0x08, 0x30, 0xF8, 0x20, 0xAC, 0x08, 0x20, 0xDA, 0x08, 0xAC, 0x74, 0x0A, 0xC0,  // $07B0
    0xFD, 0xD0, 0xEE, 0xAC, 0x73, 0x0A, 0x88, 0xD0, 0xE8, 0xAC, 0x72, 0x0A, 0xD0, 0xE3, 0xA0, 0x08,  // $07C0
    0x84, 0x3D, 0x8C, 0x8A, 0x02, 0xB9, 0x72, 0x0A, 0x20, 0xB0, 0xF2, 0xC9, 0x9B, 0xF0, 0x06, 0xE6,  // $07D0
    0x3D, 0xA4, 0x3D, 0x10, 0xF0, 0xA0, 0x01, 0x84, 0x41, 0x60, 0xA4, 0x3D, 0xCC, 0x8A, 0x02, 0xB0,  // $07E0
    0x0B, 0xB9, 0x75, 0x0A, 0xE6, 0x3D, 0xA0, 0x01, 0x60, 0x20, 0xAC, 0x08, 0x20, 0xC7, 0x08, 0x10,  // $07F0
    0x3F, 0xA9, 0x3C, 0x8D, 0x02, 0xD3, 0xA9, 0x00, 0x85, 0x41, 0xA9, 0xA0, 0x8D, 0x07, 0xD2, 0xAC,  // $0800
    0xC8, 0x02, 0xEE, 0xC8, 0x02, 0xAD, 0x1F, 0xD0, 0xC9, 0x07, 0xF0, 0xF6, 0x8C, 0xC8, 0x02, 0xA9,  // $0810
    0x34, 0x8D, 0x02, 0xD3, 0x20, 0x88, 0x08, 0x20, 0xC7, 0x08, 0x30, 0xF8, 0xAD, 0xFE, 0x03, 0xCD,  // $0820
    0x73, 0x0A, 0xAD, 0xFD, 0x03, 0xED, 0x72, 0x0A, 0x90, 0xEA, 0xA9, 0x01, 0x85, 0x41, 0xD0, 0xB9,  // $0830
    0xAC, 0xFF, 0x03, 0xC0, 0xFD, 0xF0, 0xB2, 0xC0, 0xFE, 0xF0, 0x37, 0xAC, 0x73, 0x0A, 0xAE, 0x72,  // $0840
    0x0A, 0xC8, 0xD0, 0x01, 0xE8, 0xCC, 0xFE, 0x03, 0xD0, 0xA7, 0xEC, 0xFD, 0x03, 0xD0, 0xA2, 0x20,  // $0850
    0xAC, 0x08, 0x20, 0xDA, 0x08, 0xAC, 0x74, 0x0A, 0xA9, 0x80, 0xC0, 0xFA, 0xD0, 0x09, 0x20, 0xC7,  // $0860
    0x08, 0x20, 0x13, 0x09, 0xAD, 0xF4, 0x0A, 0x8D, 0x8A, 0x02, 0xAD, 0x75, 0x0A, 0xA0, 0x01, 0x84,  // $0870
    0x3D, 0x60, 0x20, 0x13, 0x09, 0xA0, 0x88, 0x60, 0x78, 0xA9, 0x10, 0xA0, 0x6F, 0x2C, 0x0F, 0xD2,  // $0880
    0xF0, 0xF9, 0x88, 0xD0, 0xF8, 0x20, 0xB1, 0x08, 0x8C, 0x0E, 0xD2, 0xA2, 0x13, 0x20, 0x18, 0x07,  // $0890
    0xA9, 0xA0, 0x8D, 0x0A, 0xD2, 0x85, 0x10, 0x8D, 0x0E, 0xD2, 0x58, 0x60, 0x20, 0x2A, 0x07, 0xA0,  // $08A0
    0x00, 0x84, 0x30, 0x84, 0x31, 0x84, 0x38, 0x84, 0x39, 0x84, 0x3C, 0xA9, 0xA0, 0xA6, 0x41, 0xF0,  // $08B0
    0x02, 0xA9, 0xAF, 0x8D, 0x07, 0xD2, 0x60, 0xA4, 0x11, 0xF0, 0x07, 0xA4, 0x39, 0xF0, 0xF8, 0xA4,  // $08C0
    0x30, 0x60, 0x68, 0x68, 0x20, 0x13, 0x09, 0xA0, 0x80, 0x60, 0xA2, 0x7D, 0xBD, 0x80, 0x03, 0x9D,  // $08D0
    0xF5, 0x09, 0xE8, 0xD0, 0xF7, 0x60, 0xAC, 0x89, 0x02, 0x10, 0x28, 0xA4, 0x3D, 0xF0, 0x13, 0xA9,  // $08E0
    0x00, 0x99, 0x75, 0x0A, 0xC8, 0x10, 0xFA, 0xA5, 0x3D, 0x8D, 0xF4, 0x0A, 0xA9, 0xFA, 0x20, 0x35,  // $08F0
    0x09, 0x88, 0x98, 0x99, 0x75, 0x0A, 0xC8, 0x10, 0xFA, 0xA9, 0xFE, 0x20, 0x35, 0x09, 0xA0, 0x32,  // $0900
    0x20, 0x84, 0x09, 0xA9, 0xC0, 0x85, 0x10, 0x8D, 0x0E, 0xD2, 0xA9, 0xA0, 0x8D, 0x07, 0xD2, 0xA9,  // $0910
    0x3C, 0x8D, 0x02, 0xD3, 0x8D, 0x03, 0xD3, 0xA0, 0x01, 0x60, 0xA4, 0x3D, 0x99, 0x75, 0x0A, 0xE6,  // $0920
    0x3D, 0x10, 0xF4, 0xA9, 0xFC, 0xA6, 0x3A, 0xF0, 0xFC, 0xA4, 0x11, 0xF0, 0x43, 0xA2, 0xA0, 0x8E,  // $0930
    0x07, 0xD2, 0xAE, 0x71, 0x0A, 0xF0, 0x09, 0xA6, 0x2B, 0x30, 0x05, 0xA0, 0x0B, 0x20, 0x84, 0x09,  // $0940
    0xEE, 0x73, 0x0A, 0xD0, 0x03, 0xEE, 0x72, 0x0A, 0x8D, 0x74, 0x0A, 0xA0, 0x7D, 0xB9, 0xF5, 0x09,  // $0950
    0x99, 0x80, 0x03, 0xC8, 0xD0, 0xF7, 0x20, 0x2A, 0x07, 0xA9, 0x90, 0x85, 0x10, 0x8D, 0x0E, 0xD2,  // $0960
    0xA9, 0xA8, 0x8D, 0x07, 0xD2, 0x84, 0x3B, 0x84, 0x3A, 0xB1, 0x32, 0x85, 0x31, 0x8D, 0x0D, 0xD2,  // $0970
    0x84, 0x3D, 0xC8, 0x60, 0xA2, 0x00, 0x8D, 0x0A, 0xD4, 0xCA, 0xD0, 0xFA, 0x88, 0xD0, 0xF7, 0x60,  // $0980
    0x20, 0xEA, 0x07, 0x30, 0x0A, 0x48, 0x20, 0xEA, 0x07, 0x30, 0x03, 0xA8, 0x68, 0x60, 0x68, 0x68,  // $0990
    0x68, 0xC0, 0x88, 0xF0, 0x76, 0xA2, 0x38, 0x20, 0x13, 0x07, 0x20, 0x21, 0x0A, 0xA9, 0x37, 0x8D,  // $09A0
    0x54, 0x03, 0xA9, 0x0A, 0x8D, 0x55, 0x03, 0xA2, 0x04, 0x8E, 0x5A, 0x03, 0xCA, 0x20, 0x23, 0x0A,  // $09B0
    0x30, 0xDF, 0x20, 0x90, 0x09, 0xA2, 0x3E, 0xC9, 0xFF, 0xD0, 0xDC, 0xC8, 0xD0, 0xD9, 0x20, 0x90,  // $09C0
    0x09, 0x8D, 0xE0, 0x02, 0x8C, 0xE1, 0x02, 0x4C, 0xE5, 0x09, 0x20, 0x90, 0x09, 0xC9, 0xFF, 0xD0,  // $09D0
    0x04, 0xC0, 0xFF, 0xF0, 0xF5, 0xA2, 0x9D, 0x8E, 0xE2, 0x02, 0xA2, 0x09, 0x8E, 0xE3, 0x02, 0x85,  // $09E0
    0x45, 0x84, 0x46, 0x20, 0x90, 0x09, 0x85, 0x47, 0x84, 0x48, 0x20, 0xEA, 0x07, 0x30, 0xA2, 0x88,  // $09F0
    0x91, 0x45, 0xE6, 0x45, 0xD0, 0x02, 0xE6, 0x46, 0xA5, 0x47, 0xC5, 0x45, 0xA5, 0x48, 0xE5, 0x46,  // $0A00
    0xB0, 0xE8, 0xA9, 0x09, 0x48, 0xA9, 0xD9, 0x48, 0x6C, 0xE2, 0x02, 0x20, 0x21, 0x0A, 0x6C, 0xE0,  // $0A10
    0x02, 0xA2, 0x0C, 0x8E, 0x52, 0x03, 0xA2, 0x10, 0x4C, 0x56, 0xE4, 0x3A, 0x07, 0xE5, 0x08, 0xE9,  // $0A20
    0x07, 0x29, 0x09, 0x39, 0x07, 0x9C, 0x09, 0x54, 0x42, 0x52, 0x41, 0x4B, 0x45, 0x9B, 0x4E, 0x4F,  // $0A30
    0x54, 0x20, 0x44, 0x4F, 0x53, 0x20, 0x46, 0x49, 0x4C, 0x45, 0x9B, 0x7D, 0x1D, 0xA0, 0xCC, 0xCF,  // $0A40
    0xC1, 0xC4, 0xC5, 0xD2, 0xA0, 0x2D, 0x45, 0x4D, 0x4F, 0x53, 0x4F, 0x46, 0x54, 0x2D, 0x56, 0x35,  // $0A50
    0x2E, 0x31, 0x20, 0x28, 0x63, 0x29, 0x20, 0x30, 0x32, 0x2E, 0x30, 0x33, 0x2E, 0x39, 0x30, 0x1D,  // $0A60
    0x9B, 0x00, 0xFA,                                                                                    // $0A70
};

// Ident text sent before Stage 2 (40 bytes, ATASCII)
// "LOADER -EMOSOFT-V5.1 (c) 02.03.90"
static const uint8_t qros_ident_text[] = {
    0x7D, 0x1D, 0xA0, 0xCC, 0xCF, 0xC1, 0xC4, 0xC5, 0xD2, 0xA0, 0x2D, 0x45, 0x4D, 0x4F, 0x53, 0x4F,
    0x46, 0x54, 0x2D, 0x56, 0x35, 0x2E, 0x31, 0x20, 0x28, 0x63, 0x29, 0x20, 0x30, 0x32, 0x2E, 0x30,
    0x33, 0x2E, 0x39, 0x30, 0x1D, 0x9B, 0x00, 0x35,
};

void sioCassette::send_QROS_boot_loader()
{
    // Send QROS two-stage boot loader:
    // Stage 1: standard 600 baud cassette boot record (loads mini loader at $036B)
    // Stage 2: turbo baud pilot + 883 bytes (main loader at $0700)

    Debug_println("QROS: Sending boot loader");

    // --- Stage 1: standard 600 baud cassette boot record ---
    SYSTEM_BUS.setBaudrate(CASSETTE_BAUDRATE);

    // Wait for motor ON (Atari is ready for cassette boot)
    Debug_println("QROS boot: waiting for motor ON");
    while (has_pulldown() && !motor_line())
    {
        fnSystem.delay(10);
    }

    // IRG before first boot record (~19 seconds leader)
    uint16_t gap = 19320;
    Debug_printf("QROS boot: IRG %u ms\n", gap);
    while (gap > 0)
    {
#ifdef ESP_PLATFORM
        gap--;
        fnSystem.delay_microseconds(999);
#else
        fnSystem.delay(1);
        gap--;
#endif
    }

    // Build 132-byte record: 55 55 FC [128 data] checksum
    uint8_t buf[BLOCK_LEN + 4];
    buf[0] = 0x55;
    buf[1] = 0x55;
    buf[2] = 0xFC; // full record
    memcpy(buf + 3, qros_stage1_boot, BLOCK_LEN);
    buf[BLOCK_LEN + 3] = sio_checksum(buf, BLOCK_LEN + 3);

    SYSTEM_BUS.write(buf, BLOCK_LEN + 4);
    SYSTEM_BUS.flushOutput();
    Debug_printf("QROS boot: Stage 1 sent (cksum=0x%02X)\n", buf[BLOCK_LEN + 3]);

    // --- Stage 2: turbo baud with pilot ---
    // Original CAS flow after Stage 1 boot record:
    //   baud 6580 → data 40B IRG=0 (ident) → data 883B IRG=156 (Stage 2)
    // On tape: ident bytes are noise (Stage 1 is in pilot detection,
    // UART start bits reset its counter). The 156ms gap before Stage 2
    // is the actual pilot tone (sustained HIGH). Stage 1 needs ~17ms
    // (2560 consecutive HIGH reads) to detect pilot, then sets up POKEY
    // async receive for exactly 883 bytes into $0700-$0A72.
#ifdef ESP_PLATFORM
    // Stage 1 needs time to start executing after boot record
    fnSystem.delay(100);

    // Switch to turbo baud
    SYSTEM_BUS.setBaudrate(qros_turbo_baud);

    // Send ident text as noise — Stage 1 is in pilot detection mode,
    // UART data (with LOW start bits) resets its pilot counter.
    SYSTEM_BUS.write(qros_ident_text, sizeof(qros_ident_text));
    SYSTEM_BUS.flushOutput();
    Debug_println("QROS boot: ident text sent (noise for Stage 1)");

    // Pilot tone = sustained HIGH. Stage 1 detects pilot here.
    // Original CAS has 156ms, we use 200ms for safety margin.
    qros_pilot_on();
    fnSystem.delay(200);
    qros_pilot_off();

    // Send Stage 2 main loader (883 bytes into $0700-$0A72)
    SYSTEM_BUS.write(qros_stage2_loader, sizeof(qros_stage2_loader));
    SYSTEM_BUS.flushOutput();
    Debug_printf("QROS boot: Stage 2 sent (%u bytes)\n", (unsigned)sizeof(qros_stage2_loader));

    // Give loader time to initialize (display text, wait for user)
    fnSystem.delay(500);
#endif

    qros_boot_sent = true;
    Debug_println("QROS: Boot loader complete");
}

#ifdef ESP_PLATFORM
void sioCassette::qros_pilot_on()
{
    if (_qros_pilot_active)
        return;

    // Flush pending UART output before detaching
    SYSTEM_BUS.flushOutput();

    // Detach UART2 TX from GPIO — same pattern as T2K init_rmt
    esp_rom_gpio_connect_out_signal(PIN_UART2_TX, SIG_GPIO_OUT_IDX, false, false);

    // Set GPIO HIGH = pilot tone (sustained mark level on SIO DATA IN)
    gpio_set_direction((gpio_num_t)PIN_UART2_TX, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)PIN_UART2_TX, 1);

    _qros_pilot_active = true;
    Debug_println("QROS: pilot ON (GPIO HIGH)");
}

void sioCassette::qros_pilot_off()
{
    if (!_qros_pilot_active)
        return;

    // Reattach UART2 TX to GPIO — same pattern as T2K deinit_rmt
    esp_rom_gpio_connect_out_signal(PIN_UART2_TX,
        uart_periph_signal[2].pins[SOC_UART_TX_PIN_IDX].signal, false, false);

    _qros_pilot_active = false;
    Debug_println("QROS: pilot OFF (UART reattached)");
}
#endif

size_t sioCassette::send_QROS_tape_block(size_t offset)
{
#ifdef ESP_PLATFORM
    size_t r;
    uint16_t gap, len;
    struct tape_FUJI_hdr *hdr = (struct tape_FUJI_hdr *)atari_sector_buffer;
    uint8_t *p = hdr->chunk_type;
    bool is_turbo = (baud > 3000);

    // Send embedded boot loader on first call (if CAS has no own 600 Bd boot)
    if (!qros_boot_sent)
    {
        send_QROS_boot_loader();
        if (!qros_boot_sent)
            return offset; // boot failed, retry next call
    }

    size_t starting_offset = offset;

    while (offset < filesize)
    {
        Debug_printf("QROS offset: %u\r\n", (unsigned)offset);
        fnio::fseek(_file, offset, SEEK_SET);
        fnio::fread(atari_sector_buffer, 1, sizeof(struct tape_FUJI_hdr), _file);
        len = hdr->chunk_length;

        if (p[0] == 'd' && p[1] == 'a' && p[2] == 't' && p[3] == 'a')
        {
            block++;
            break;
        }
        else if (p[0] == 'b' && p[1] == 'a' && p[2] == 'u' && p[3] == 'd')
        {
            baud = hdr->irg_length;
            is_turbo = (baud > 3000);
            Debug_printf("QROS baud change: %u (turbo=%d)\n", baud, is_turbo);
        }
        else if (p[0] == 'f' && p[1] == 's' && p[2] == 'k')
        {
            // FSK chunk — skip (digital playback ignores FSK settings)
            Debug_println("QROS: skipping fsk chunk");
        }

        offset += sizeof(struct tape_FUJI_hdr) + len;
    }

    if (offset >= filesize)
    {
        Debug_println("QROS: end of tape");
        return 0;
    }

    gap = hdr->irg_length;
    len = hdr->chunk_length;
    Debug_printf("QROS block %u: baud=%u len=%u gap=%u turbo=%d\n",
                 block, baud, len, gap, is_turbo);

    fnLedManager.set(eLed::LED_BUS, true);

    if (is_turbo && gap > 0)
    {
        // Wait for motor ON before sending turbo data.
        // After boot, the QROS loader displays program name and waits
        // for user to press START. Motor is OFF during this wait.
        if (has_pulldown() && !motor_line())
        {
            Debug_println("QROS: waiting for motor ON (user START key)");
            while (!motor_line())
            {
                fnSystem.delay(10);
            }
            Debug_println("QROS: motor ON, proceeding");
        }

        // Turbo block: generate pilot tone (GPIO HIGH) during IRG
        qros_pilot_on();

        while (gap)
        {
            gap--;
            fnSystem.delay_microseconds(999);
        }

        qros_pilot_off();

        // Set turbo baud rate for data transmission
        SYSTEM_BUS.setBaudrate(baud);
    }
    else
    {
        // Standard 600 baud block: normal IRG delay (no pilot)
        while (gap)
        {
            gap--;
            fnSystem.delay_microseconds(999);

            if (has_pulldown() && !motor_line() && gap > 1000)
            {
                fnLedManager.set(eLed::LED_BUS, false);
                return starting_offset;
            }
        }

        SYSTEM_BUS.setBaudrate(baud);
    }

    fnLedManager.set(eLed::LED_BUS, false);
    Debug_printf("QROS: sending block %u\n", block);

    // Send data
    if (offset < filesize)
    {
        uint16_t buflen;
        offset += sizeof(struct tape_FUJI_hdr);

        while (len)
        {
            buflen = (len > 256) ? 256 : len;
            len -= buflen;

            fnio::fseek(_file, offset, SEEK_SET);
            r = fnio::fread(atari_sector_buffer, 1, buflen, _file);
            offset += r;

            Debug_printf("QROS: sending %u bytes\r\n", buflen);
            SYSTEM_BUS.write(atari_sector_buffer, buflen);
            SYSTEM_BUS.flushOutput();
        }
    }
    else
    {
        offset = 0;
    }

    return offset;
#else
    return 0;
#endif
}

// =============================================================================
// Turbo 2000 PWM cassette playback
// =============================================================================

void sioCassette::mount_turbo_loader()
{
    const char *xex_path = nullptr;
    const char *label = nullptr;

    if (tape_flags.turbo2000)
    {
        xex_path = "/turbo-2000-super-turbo.xex";
        label = "T2K";
    }
    else if (tape_flags.qros)
    {
        // AUDF=$7F (6600 baud) or AUDF=$56 (9600 baud)
        xex_path = (qros_turbo_baud > 8000) ? "/qtos51_9600.xex" : "/qtos51.xex";
        label = "QROS";
    }
    else
        return;

    // Mount on D1: (slot 0) — CAS went to cassette handler so _disk is nullptr.
    // boot_config is already false (set by fujicore_mount_disk_image_success),
    // so the bootdisk won't interfere. Our XEX gets device_active=true and
    // D1: responds with the loader on next boot.
    int8_t slot = 0;
    if (theFuji->get_disk_dev(slot)->disktype() != MEDIATYPE_UNKNOWN)
    {
        Debug_printf("%s: D1: is not free for loader!\n", label);
        return;
    }

    // Open loader from flash
    fnFile *f = fsFlash.fnfile_open(xex_path);
    if (!f)
    {
        Debug_printf("%s: Failed to open %s from flash\n", label, xex_path);
        return;
    }

    size_t fsize = fsFlash.filesize(f);

    // Mount on free slot as XEX (PicoBoot + AUTORUN)
    // Use mount_disk_media() instead of mount() to avoid recursive call —
    // we are already inside sioDisk::mount() which routed CAS here.
    DISK_DEVICE *disk = theFuji->get_disk_dev(slot);
    disk->mount_disk_media(f, xex_path, fsize, MEDIATYPE_XEX);

    _turbo_loader_slot = slot;

    if (tape_flags.turbo2000)
        t2k_boot_sent = true;
    if (tape_flags.qros)
        qros_boot_sent = true;

    // boot_config stays false — bootdisk (autorun.atr) must NOT respond,
    // so our XEX on D1: slot 0 (device_active=true) takes over.

    Debug_printf("%s: Loader mounted on D%d: (%u bytes)\n",
                 label, slot + 1, (unsigned)fsize);
}

void sioCassette::unmount_turbo_loader()
{
    if (_turbo_loader_slot >= 0)
    {
        DISK_DEVICE *disk = theFuji->get_disk_dev(_turbo_loader_slot);
        disk->unmount();
        Debug_printf("T2K: Loader unmounted from D%d:\n", _turbo_loader_slot + 1);
        _turbo_loader_slot = -1;
    }
}

uint16_t sioCassette::t2k_samples_to_us(uint8_t samples)
{
    // CAS sample count = full period. Divide by 2 to get half-period
    // for symmetric square wave generation via RMT.
    return (uint16_t)((uint32_t)samples * 1000000 / t2k_samplerate / 2);
}

#ifdef ESP_PLATFORM

void sioCassette::turbo2000_init_rmt()
{
    if (_rmt_active)
        return;

    // Flush any pending UART output before detaching
    SYSTEM_BUS.flushOutput();

    // Detach UART2 TX from GPIO — same pattern as IWM uses for SPI
    esp_rom_gpio_connect_out_signal(PIN_UART2_TX, SIG_GPIO_OUT_IDX, false, false);
    // Set idle level HIGH (mark) before RMT takes over
    gpio_set_level((gpio_num_t)PIN_UART2_TX, 1);

    // Configure RMT TX channel on SIO DATA IN pin (GPIO 21 → SIO Pin 3, into Atari)
    rmt_tx_channel_config_t tx_cfg = {};
    tx_cfg.gpio_num = (gpio_num_t)PIN_UART2_TX;
    tx_cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    tx_cfg.resolution_hz = T2K_RMT_RESOLUTION_HZ; // 1 µs per tick
    tx_cfg.mem_block_symbols = 64 * 8; // 512 symbols for gapless ping-pong
    tx_cfg.trans_queue_depth = 4;
    tx_cfg.intr_priority = 0;
    tx_cfg.flags.invert_out = false;
    tx_cfg.flags.with_dma = false;
    tx_cfg.flags.io_loop_back = false;
    tx_cfg.flags.io_od_mode = false;
    tx_cfg.flags.allow_pd = false;

    rmt_channel_handle_t channel = nullptr;
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_cfg, &channel));
    _rmt_channel = channel;

    // Copy encoder for pilot tone (uniform pulses, gaps don't matter)
    rmt_copy_encoder_config_t copy_cfg = {};
    rmt_encoder_handle_t copy_enc = nullptr;
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&copy_cfg, &copy_enc));
    _rmt_copy_encoder = copy_enc;

    // Simple encoder for data — generates symbols on the fly in IRAM ISR.
    // No gaps between symbols because it fills RMT memory directly during
    // ping-pong refill (like FozzTexx's Apple II floppy code).
    rmt_simple_encoder_config_t simple_cfg = {};
    simple_cfg.callback = t2k_encode_cb;
    simple_cfg.arg = (void *)this;
    simple_cfg.min_chunk_size = 0;
    rmt_encoder_handle_t simple_enc = nullptr;
    ESP_ERROR_CHECK(rmt_new_simple_encoder(&simple_cfg, &simple_enc));
    _rmt_simple_encoder = simple_enc;

    ESP_ERROR_CHECK(rmt_enable(channel));

    _rmt_active = true;
    Debug_printf("Turbo 2000: RMT initialized on SIO DATA IN pin (GPIO %d), invert=%d\n",
                 PIN_UART2_TX, tx_cfg.flags.invert_out);
}

void sioCassette::turbo2000_deinit_rmt()
{
    if (!_rmt_active)
        return;

    rmt_channel_handle_t channel = (rmt_channel_handle_t)_rmt_channel;

    // Flush any pending RMT data before tearing down
    rmt_tx_wait_all_done(channel, -1);

    // Free pending buffer now that RMT is done with it
    if (_t2k_pending_buf)
    {
        free(_t2k_pending_buf);
        _t2k_pending_buf = nullptr;
    }

    rmt_disable(channel);
    rmt_del_channel(channel);
    if (_rmt_copy_encoder)
        rmt_del_encoder((rmt_encoder_handle_t)_rmt_copy_encoder);
    if (_rmt_simple_encoder)
        rmt_del_encoder((rmt_encoder_handle_t)_rmt_simple_encoder);

    _rmt_channel = nullptr;
    _rmt_copy_encoder = nullptr;
    _rmt_simple_encoder = nullptr;
    _rmt_active = false;

    // Reattach UART2 TX to GPIO 21
    // UART2 TX signal index = uart_periph_signal[2].pins[SOC_UART_TX_PIN_IDX].signal
    esp_rom_gpio_connect_out_signal(PIN_UART2_TX,
        uart_periph_signal[2].pins[SOC_UART_TX_PIN_IDX].signal, false, false);

    Debug_println("Turbo 2000: UART restored on SIO DATA IN pin");
}

void sioCassette::turbo2000_send_pulses(uint16_t half_period_us, int count)
{
    if (!_rmt_active || count <= 0)
        return;

    rmt_channel_handle_t channel = (rmt_channel_handle_t)_rmt_channel;
    rmt_encoder_handle_t encoder = (rmt_encoder_handle_t)_rmt_copy_encoder;

    rmt_transmit_config_t tx_cfg = {};
    tx_cfg.loop_count = 0;
    tx_cfg.flags.eot_level = 0;
    tx_cfg.flags.queue_nonblocking = false;

    // Send in batches of up to 64 symbols.
    // With queue_depth=2, rmt_transmit blocks when queue is full,
    // providing natural backpressure with gapless output.
    // Copy encoder copies data to RMT memory before returning,
    // so reusing the same buffer is safe.
    rmt_symbol_word_t batch[64];
    while (count > 0)
    {
        int n = (count > 64) ? 64 : count;
        for (int i = 0; i < n; i++)
        {
            batch[i].duration0 = half_period_us;
            batch[i].level0 = 1;
            batch[i].duration1 = half_period_us;
            batch[i].level1 = 0;
        }
        esp_err_t err = rmt_transmit(channel, encoder, batch,
            n * sizeof(rmt_symbol_word_t), &tx_cfg);
        if (err != ESP_OK)
        {
            Debug_printf("T2K: rmt_transmit error: %s\n", esp_err_to_name(err));
            return;
        }
        count -= n;
    }
    // No wait here — caller uses turbo2000_flush_rmt() when synchronization needed.
}

void sioCassette::turbo2000_flush_rmt()
{
    if (!_rmt_active)
        return;
    rmt_channel_handle_t channel = (rmt_channel_handle_t)_rmt_channel;

    // Poll with 100ms timeout, checking motor line each iteration.
    // If user presses RESET on Atari, motor goes OFF — stop RMT.
    // Use 2-second threshold to ignore brief motor OFF pulses (the T2K
    // loader briefly turns motor OFF between header and data blocks).
    uint32_t motor_off_start = 0;
    bool motor_was_off = false;
    while (rmt_tx_wait_all_done(channel, 100) == ESP_ERR_TIMEOUT)
    {
        if (has_pulldown() && !motor_line())
        {
            if (!motor_was_off)
            {
                motor_was_off = true;
                motor_off_start = fnSystem.millis();
            }
            else if (fnSystem.millis() - motor_off_start > 2000)
            {
                Debug_println("T2K flush: motor OFF > 2s, stopping RMT");
                rmt_disable(channel);
                rmt_enable(channel);
                break;
            }
        }
        else
        {
            motor_was_off = false;
        }
    }

    // Free pending buffer now that RMT is done with it
    if (_t2k_pending_buf)
    {
        free(_t2k_pending_buf);
        _t2k_pending_buf = nullptr;
    }
}

void sioCassette::turbo2000_send_pilot(uint16_t count)
{
    turbo2000_send_pulses(t2k_pilot_half, count);
}

void sioCassette::turbo2000_send_byte(uint8_t byte)
{
    // Single-byte wrapper for send_bytes
    turbo2000_send_bytes(&byte, 1);
}

void sioCassette::turbo2000_send_bytes(const uint8_t *data, size_t length)
{
    if (!_rmt_active || length == 0)
        return;

    rmt_channel_handle_t channel = (rmt_channel_handle_t)_rmt_channel;
    rmt_encoder_handle_t encoder = (rmt_encoder_handle_t)_rmt_copy_encoder;

    rmt_transmit_config_t tx_cfg = {};
    tx_cfg.loop_count = 0;
    tx_cfg.flags.eot_level = 0;
    tx_cfg.flags.queue_nonblocking = false;

    // Build symbols for multiple bytes at once.
    // Max batch = 64 symbols = 8 bytes (RMT memory block size).
    // With queue_depth=2, rmt_transmit blocks when queue is full,
    // so the next batch is queued while the current one is still
    // transmitting — gapless output with no explicit wait needed.
    rmt_symbol_word_t items[64];
    size_t sym_idx = 0;

    for (size_t b = 0; b < length; b++)
    {
        uint8_t byte = data[b];
        for (int i = 0; i < 8; i++)
        {
            uint16_t half;
            if (t2k_msb_first)
                half = (byte & (1 << (7 - i))) ? t2k_bit1_half : t2k_bit0_half;
            else
                half = (byte & (1 << i)) ? t2k_bit1_half : t2k_bit0_half;

            items[sym_idx].duration0 = half;
            items[sym_idx].level0 = 1;
            items[sym_idx].duration1 = half;
            items[sym_idx].level1 = 0;
            sym_idx++;
        }

        // Queue when batch is full (64 symbols = 8 bytes)
        if (sym_idx >= 64 || b == length - 1)
        {
            esp_err_t err = rmt_transmit(channel, encoder, items,
                sym_idx * sizeof(rmt_symbol_word_t), &tx_cfg);
            if (err != ESP_OK)
            {
                Debug_printf("T2K: rmt_transmit error: %s\n", esp_err_to_name(err));
                return;
            }
            sym_idx = 0;
        }
    }
    // No wait here — RMT pipeline keeps outputting.
    // Caller uses turbo2000_flush_rmt() when synchronization is needed.
}

#endif // ESP_PLATFORM

size_t sioCassette::send_turbo2000_tape_block(size_t offset)
{
#ifdef ESP_PLATFORM
    struct tape_FUJI_hdr *hdr = (struct tape_FUJI_hdr *)atari_sector_buffer;
    uint8_t *p = hdr->chunk_type;
    size_t starting_offset = offset;

    // Mount loader on disk slot if not yet done (e.g. after rewind)
    if (!t2k_boot_sent)
    {
        mount_turbo_loader();
        if (!t2k_boot_sent)
            return starting_offset; // no free slot, retry next call
    }

    fnLedManager.set(eLed::LED_BUS, true);

    bool chunk_preread = false; // true if next chunk header already in atari_sector_buffer

    while (offset < filesize)
    {
        // Read chunk header (skip if already pre-read by previous handler)
        if (!chunk_preread)
        {
            fnio::fseek(_file, offset, SEEK_SET);
            fnio::fread(atari_sector_buffer, 1, sizeof(struct tape_FUJI_hdr), _file);
        }
        chunk_preread = false;
        uint16_t len = hdr->chunk_length;
        uint16_t aux = hdr->irg_length;

        // ------- pwms: speed/config -------
        if (p[0] == 'p' && p[1] == 'w' && p[2] == 'm' && p[3] == 's')
        {
            if (len >= 2)
            {
                uint8_t tmp[2];
                fnio::fread(tmp, 1, 2, _file);
                t2k_samplerate = tmp[0] | (tmp[1] << 8);
            }
            uint8_t config = aux & 0xFF;
            t2k_msb_first = (config >> 2) & 1;
            Debug_printf("T2K pwms: samplerate=%u, config=0x%02X, msb=%d\n",
                         t2k_samplerate, config, t2k_msb_first);
            offset += sizeof(struct tape_FUJI_hdr) + len;
            continue;
        }

        // ------- pwmc: pilot tone -------
        if (p[0] == 'p' && p[1] == 'w' && p[2] == 'm' && p[3] == 'c')
        {
            uint16_t silence_ms = aux;
            uint8_t pilot_pulse_len = 32;  // default
            uint16_t pilot_count = 256;    // default

            if (len >= 1)
            {
                uint8_t tmp[3];
                fnio::fread(tmp, 1, (len >= 3) ? 3 : len, _file);
                pilot_pulse_len = tmp[0];
                if (len >= 3)
                    pilot_count = tmp[1] | (tmp[2] << 8);
            }

            t2k_pilot_half = t2k_samples_to_us(pilot_pulse_len);
            t2k_pilot_count = pilot_count;

            Debug_printf("T2K pwmc: silence=%ums, pilot=%u@%u\n",
                         silence_ms, pilot_count, t2k_pilot_half);

            // Wait for silence period. During silence, the T2K loader may
            // briefly turn motor OFF between blocks (L06C2: PACTL=60).
            // Don't abort — wait for motor to come back ON.
            if (silence_ms > 0)
            {
                if (_rmt_active)
                    turbo2000_flush_rmt();

                uint32_t motor_off_start = 0;
                bool motor_was_off = false;
                while (silence_ms > 0)
                {
                    fnSystem.delay(1);
                    silence_ms--;
                    if (has_pulldown() && !motor_line())
                    {
                        if (!motor_was_off)
                        {
                            motor_off_start = fnSystem.millis();
                            motor_was_off = true;
                            Debug_println("T2K pwmc: motor OFF during silence (expected between blocks)");
                        }
                        // Abort only if motor stays OFF for > 2 seconds (real STOP)
                        if (fnSystem.millis() - motor_off_start > 2000)
                        {
                            Debug_println("T2K pwmc: motor OFF > 2s, aborting");
                            if (_rmt_active) turbo2000_deinit_rmt();
                            fnLedManager.set(eLed::LED_BUS, false);
                            return starting_offset;
                        }
                    }
                    else
                    {
                        motor_was_off = false;
                    }
                }
            }

            // After silence, wait for motor ON. The T2K loader shows the
            // program name and waits for the user to press a key before
            // requesting the data block (motor OFF during this wait).
            // Use a long timeout — user may take a while to press a key.
            if (has_pulldown() && !motor_line())
            {
                Debug_println("T2K pwmc: waiting for motor ON (user key press)");
                while (!motor_line())
                {
                    fnSystem.delay(10);
                    // No timeout — wait indefinitely for user to press key.
                    // The cassette service loop won't be called during motor OFF
                    // because we haven't returned. Check BREAK via RESET only.
                }
                Debug_println("T2K pwmc: motor back ON, continuing");
            }

            // Init RMT if not yet active
            if (!_rmt_active)
                turbo2000_init_rmt();

            // Don't send pilot here — store params for the simple encoder
            // callback. Pilot will be generated as part of the next
            // rmt_transmit (pilot+sync+data gapless in one call).
            _t2k_pilot_pending = t2k_pilot_count;
            Debug_printf("T2K pilot: %u pulses @ %u us (deferred to encoder)\n",
                         t2k_pilot_count, t2k_pilot_half);

            offset += sizeof(struct tape_FUJI_hdr) + len;

            // Pre-read next chunk header.
            if (offset < filesize)
            {
                fnio::fseek(_file, offset, SEEK_SET);
                fnio::fread(atari_sector_buffer, 1, sizeof(struct tape_FUJI_hdr), _file);
                chunk_preread = true;
            }
            continue;
        }

        // ------- pwml: sync/end markers or silence -------
        if (p[0] == 'p' && p[1] == 'w' && p[2] == 'm' && p[3] == 'l')
        {
            uint16_t silence_ms = aux;

            if (silence_ms > 0)
            {
                if (_rmt_active)
                    turbo2000_deinit_rmt();
                fnSystem.delay(silence_ms);
            }

            if (len > 0)
            {
                if (!_rmt_active)
                    turbo2000_init_rmt();

                uint8_t states[64];
                size_t read_len = (len > sizeof(states)) ? sizeof(states) : len;
                fnio::fread(states, 1, read_len, _file);

                // pwml = alternating level durations (16-bit LE, in samples).
                // Each pair of values forms ONE complete pulse cycle:
                //   element 0 = HIGH duration, element 1 = LOW duration.
                // Values are already half-periods — don't divide by 2.

                // Build sync symbols but DON'T send yet — we need to do all
                // file I/O first, then send sync + data back-to-back so the
                // T2K loader sees no gap (it has only ~675µs timeout).
                rmt_symbol_word_t sync_syms[16];
                size_t sync_count = 0;
                Debug_printf("T2K pwml raw (%u bytes):", (unsigned)read_len);
                for (size_t di = 0; di < read_len; di++)
                    Debug_printf(" %02X", states[di]);
                Debug_println("");

                for (size_t i = 0; i + 3 < read_len; i += 4)
                {
                    uint16_t s0 = states[i] | (states[i + 1] << 8);
                    uint16_t s1 = states[i + 2] | (states[i + 3] << 8);
                    uint16_t us0 = (uint16_t)((uint32_t)s0 * 1000000 / t2k_samplerate);
                    uint16_t us1 = (uint16_t)((uint32_t)s1 * 1000000 / t2k_samplerate);
                    Debug_printf("T2K sync sym %u: s0=%u(%uus) s1=%u(%uus)\n",
                                 (unsigned)sync_count, s0, us0, s1, us1);
                    if ((us0 > 0 || us1 > 0) && sync_count < 16)
                    {
                        sync_syms[sync_count].duration0 = us0;
                        sync_syms[sync_count].level0 = 1;
                        sync_syms[sync_count].duration1 = us1;
                        sync_syms[sync_count].level1 = 0;
                        sync_count++;
                    }
                }

                offset += sizeof(struct tape_FUJI_hdr) + len;

                // Read ALL data from the following pwmd block so we can
                // send sync + entire data in ONE rmt_transmit call.
                // This eliminates gaps between batches that corrupt bits.
                t2k_data_present = 0;
                uint8_t *all_data = nullptr;
                uint16_t pwmd_len = 0;
                if (offset < filesize && silence_ms == 0)
                {
                    fnio::fseek(_file, offset, SEEK_SET);
                    fnio::fread(atari_sector_buffer, 1, sizeof(struct tape_FUJI_hdr), _file);
                    chunk_preread = true;

                    if (p[0] == 'p' && p[1] == 'w' && p[2] == 'm' && p[3] == 'd')
                    {
                        uint16_t pwmd_aux = hdr->irg_length;
                        pwmd_len = hdr->chunk_length;
                        t2k_bit0_half = t2k_samples_to_us(pwmd_aux & 0xFF);
                        t2k_bit1_half = t2k_samples_to_us((pwmd_aux >> 8) & 0xFF);

                        // Read ALL pwmd data into RAM
                        if (pwmd_len > 0)
                        {
                            all_data = (uint8_t *)malloc(pwmd_len);
                            if (all_data)
                            {
                                size_t r = fnio::fread(all_data, 1, pwmd_len, _file);
                                t2k_data_present = r;
                            }
                        }

                        // Re-read chunk header so pwmd handler can parse it
                        fnio::fseek(_file, offset, SEEK_SET);
                        fnio::fread(atari_sector_buffer, 1, sizeof(struct tape_FUJI_hdr), _file);
                    }
                }

                // Debug info BEFORE sending anything (serial output is slow)
                if (all_data && t2k_data_present > 0)
                {
                    Debug_printf("T2K presend %u bytes:", (unsigned)t2k_data_present);
                    for (size_t pi = 0; pi < t2k_data_present && pi < 32; pi++)
                        Debug_printf(" %02X", all_data[pi]);
                    Debug_println("");
                }

                // Use simple encoder: generates symbols on the fly in IRAM ISR.
                // Pass raw byte data — callback reads bytes and creates symbols
                // directly into RMT ping-pong memory. Zero gaps guaranteed.
                // (Same technique as FozzTexx's Apple II floppy code.)
                rmt_channel_handle_t channel = (rmt_channel_handle_t)_rmt_channel;
                rmt_transmit_config_t tx_cfg = {};
                tx_cfg.loop_count = 0;
                tx_cfg.flags.eot_level = 0;
                tx_cfg.flags.queue_nonblocking = false;

                // Free any previous pending buffer
                if (_t2k_pending_buf)
                {
                    free(_t2k_pending_buf);
                    _t2k_pending_buf = nullptr;
                }

                // Store sync symbols in class for callback to access
                memcpy(_t2k_sync_syms, sync_syms, sync_count * sizeof(rmt_symbol_word_t));
                _t2k_sync_count = sync_count;

                if (all_data && t2k_data_present > 0)
                {
                    Debug_printf("T2K: simple encoder %u pilot + %u sync + %u bytes data\n",
                                 (unsigned)_t2k_pilot_pending, (unsigned)sync_count,
                                 (unsigned)t2k_data_present);
                    rmt_transmit(channel,
                        (rmt_encoder_handle_t)_rmt_simple_encoder,
                        all_data, t2k_data_present, &tx_cfg);
                    // Do NOT reset _t2k_pilot_pending here!
                    // rmt_transmit is non-blocking — the callback reads
                    // _t2k_pilot_pending asynchronously from ISR context.
                    // Changing it now would corrupt symbol generation.
                    // It gets overwritten by the next pwmc handler.
                    _t2k_pending_buf = all_data; // freed after flush
                    all_data = nullptr; // don't free below
                }
                else if (sync_count > 0 || _t2k_pilot_pending > 0)
                {
                    // Fallback: no pre-read data. Send pilot + sync via copy encoder.
                    if (_t2k_pilot_pending > 0)
                    {
                        turbo2000_send_pilot(_t2k_pilot_pending);
                        _t2k_pilot_pending = 0;
                    }
                    if (sync_count > 0)
                    {
                        rmt_transmit(channel,
                            (rmt_encoder_handle_t)_rmt_copy_encoder,
                            sync_syms, sync_count * sizeof(rmt_symbol_word_t), &tx_cfg);
                    }
                }

                if (all_data)
                    free(all_data);

                continue;
            }

            offset += sizeof(struct tape_FUJI_hdr) + len;
            continue;
        }

        // ------- pwmd: data block -------
        if (p[0] == 'p' && p[1] == 'w' && p[2] == 'm' && p[3] == 'd')
        {
            // aux = pulse0 | (pulse1 << 8)
            t2k_bit0_half = t2k_samples_to_us(aux & 0xFF);
            t2k_bit1_half = t2k_samples_to_us((aux >> 8) & 0xFF);

            Debug_printf("T2K data: %u bytes, bit0=%u us, bit1=%u us, presend=%u\n",
                         len, t2k_bit0_half, t2k_bit1_half, (unsigned)t2k_data_present);

            if (!_rmt_active)
                turbo2000_init_rmt();

            // Read ALL remaining data into RAM first, then send without
            // file I/O interruptions. SD card latency spikes can cause
            // RMT underrun (output goes LOW → lost bits → cascading corruption).
            // Skip bytes already pre-sent by the pwml handler.
            size_t data_offset = offset + sizeof(struct tape_FUJI_hdr) + t2k_data_present;
            size_t remaining = (len > t2k_data_present) ? len - t2k_data_present : 0;
            t2k_data_present = 0;

            if (remaining > 0)
            {
                uint8_t *data_buf = (uint8_t *)malloc(remaining);
                if (data_buf)
                {
                    fnio::fseek(_file, data_offset, SEEK_SET);
                    size_t r = fnio::fread(data_buf, 1, remaining, _file);
                    Debug_printf("T2K pwmd: read %u bytes into RAM, sending\n", (unsigned)r);
                    turbo2000_send_bytes(data_buf, r);
                    free(data_buf);
                }
                else
                {
                    // Fallback: chunked read if malloc fails (large blocks)
                    Debug_printf("T2K pwmd: malloc(%u) failed, using chunked read\n", (unsigned)remaining);
                    while (remaining > 0)
                    {
                        if (has_pulldown() && !motor_line())
                        {
                            Debug_println("T2K: motor OFF during data, aborting");
                            if (_rmt_active) turbo2000_deinit_rmt();
                            fnLedManager.set(eLed::LED_BUS, false);
                            return starting_offset;
                        }

                        size_t chunk = (remaining > 256) ? 256 : remaining;
                        fnio::fseek(_file, data_offset, SEEK_SET);
                        size_t r = fnio::fread(atari_sector_buffer, 1, chunk, _file);
                        turbo2000_send_bytes(atari_sector_buffer, r);
                        data_offset += r;
                        remaining -= r;
                    }
                }
            }

            block++;
            offset += sizeof(struct tape_FUJI_hdr) + len;

            // Process trailing end marker (pwml) if present —
            // send it seamlessly (no flush) so it follows data without gap.
            if (offset < filesize)
            {
                fnio::fseek(_file, offset, SEEK_SET);
                fnio::fread(atari_sector_buffer, 1, sizeof(struct tape_FUJI_hdr), _file);
                if (p[0] == 'p' && p[1] == 'w' && p[2] == 'm' && p[3] == 'l')
                {
                    uint16_t elen = hdr->chunk_length;
                    if (elen > 0)
                    {
                        uint8_t states[8];
                        size_t slen = (elen > sizeof(states)) ? sizeof(states) : elen;
                        fnio::fread(states, 1, slen, _file);

                        // pwml pairs → one RMT symbol each (same as above)
                        rmt_channel_handle_t ch = (rmt_channel_handle_t)_rmt_channel;
                        rmt_encoder_handle_t enc = (rmt_encoder_handle_t)_rmt_copy_encoder;
                        rmt_transmit_config_t tcfg = {};
                        tcfg.loop_count = 0;
                        tcfg.flags.eot_level = 0;
                        tcfg.flags.queue_nonblocking = false;

                        for (size_t i = 0; i + 3 < slen; i += 4)
                        {
                            uint16_t s0 = states[i] | (states[i + 1] << 8);
                            uint16_t s1 = states[i + 2] | (states[i + 3] << 8);
                            uint16_t us0 = (uint16_t)((uint32_t)s0 * 1000000 / t2k_samplerate);
                            uint16_t us1 = (uint16_t)((uint32_t)s1 * 1000000 / t2k_samplerate);
                            if (us0 > 0 || us1 > 0)
                            {
                                rmt_symbol_word_t sym;
                                sym.duration0 = us0;
                                sym.level0 = 1;
                                sym.duration1 = us1;
                                sym.level1 = 0;
                                rmt_transmit(ch, enc, &sym,
                                    sizeof(rmt_symbol_word_t), &tcfg);
                            }
                        }
                    }
                    offset += sizeof(struct tape_FUJI_hdr) + elen;
                }
            }

            // Don't return between blocks! The T2K loader on Atari turns
            // motor OFF briefly between header and data blocks (L06C2: PACTL=60).
            // If we return here, FujiNet sees motor OFF, deactivates cassette,
            // re-sends boot loader, and the T2K loader restarts — losing context.
            // Instead, flush RMT and continue the while loop to process the next
            // CAS chunk (pilot for data block). The pwmc handler will wait for
            // its silence period, giving the Atari time to process the header.
            if (_rmt_active)
                turbo2000_flush_rmt();

            fnLedManager.set(eLed::LED_BUS, false);
            Debug_printf("T2K block %u done, continuing to next chunk\n", block);
            continue; // Continue the while loop — don't fall through!
        }

        // ------- FUJI or unknown chunk: skip -------
        offset += sizeof(struct tape_FUJI_hdr) + len;
    }

    // End of file
    if (_rmt_active)
        turbo2000_deinit_rmt();

    fnLedManager.set(eLed::LED_BUS, false);
    return 0; // signal end of tape
#else
    return 0;
#endif
}

#endif /* BUILD_ATARI */

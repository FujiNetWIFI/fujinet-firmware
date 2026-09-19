#ifdef BUILD_ATARI

#include "cassette.h"

#include "fsk_plan.h"

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
#include <esp_timer.h> // esp_timer_get_time() — Active FSK Rewind physical-position clock interpolation
#endif

// Turbo 2000: RMT clock = 1 MHz (1 µs per tick)
#define T2K_RMT_RESOLUTION_HZ 1000000

// Simple encoder callback for T2K — generates RMT symbols on the fly.
// Runs in ISR context (RMT ping-pong refill), MUST be in IRAM so it
// executes instantly without flash cache misses.
// Phases: pilot → sync → data bits (all gapless in one rmt_transmit).
#ifdef ESP_PLATFORM
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

#ifdef ESP_PLATFORM
sioCassette::sioCassette()
{
    // Singleton lifetime; guards tape_offset/_file/format-state against the
    // HTTP task (rewind()/rewind_seconds()) racing the fnService loop task
    // (sio_handle_cassette()).
    _cassette_lock = xSemaphoreCreateMutex();
    if (_cassette_lock == nullptr)
        Debug_println("sioCassette: FAILED to create _cassette_lock — rewind()/rewind_seconds() will refuse to run");

    // Separate, briefly-held lock for _fsk_active_channel + the rewind
    // request/result handshake. On creation failure, rewind_seconds() falls
    // back to the original whole-run-blocking behavior.
    _fsk_channel_lock = xSemaphoreCreateMutex();
    if (_fsk_channel_lock == nullptr)
        Debug_println("sioCassette: FAILED to create _fsk_channel_lock — Active FSK Rewind disabled, falling back to blocking rewind_seconds()");
}
#endif

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
#ifdef ESP_PLATFORM
    // Held for the WHOLE dispatch, including any in-flight
    // rmt_tx_wait_all_done() — a concurrent rewind_seconds() call waits for
    // the current waveform instead of racing it.
    if (_cassette_lock != nullptr)
        xSemaphoreTake(_cassette_lock, portMAX_DELAY);
#endif

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

#ifdef ESP_PLATFORM
    if (_cassette_lock != nullptr)
        xSemaphoreGive(_cassette_lock);
#endif
}

void sioCassette::rewind()
{
    // Independent of the time walker so it keeps working even on a
    // malformed CAS; always lands on offset 0.
#ifdef ESP_PLATFORM
    if (_cassette_lock != nullptr)
        xSemaphoreTake(_cassette_lock, portMAX_DELAY);
    stop_and_reset_for_reposition();
#endif
    tape_offset = 0;
    // t2k_boot_sent / qros_boot_sent are session state (loader already
    // mounted/booted), not tape-position state — resetting them here would
    // re-trigger the boot sequence mid-load. The only legitimate reset for
    // them is in sio_enable_cassette() on a genuinely fresh mount.
#ifdef ESP_PLATFORM
    if (_cassette_lock != nullptr)
        xSemaphoreGive(_cassette_lock);
#endif
}

bool sioCassette::rewind_seconds(uint32_t seconds)
{
#ifdef ESP_PLATFORM
    if (!_mounted || _cassette_lock == nullptr)
        return false;

    // ---- Active FSK Rewind fast path -----------------------------------
    // Engages only when an FSK run is genuinely mid-transmission (a channel
    // is published in _fsk_active_channel — see fsk_signal_emit(), which
    // publishes only after rmt_transmit() returns ESP_OK). All other formats,
    // and FSK between chunks/idle, never publish it and fall through
    // unchanged to the blocking path below.
    if (_fsk_channel_lock != nullptr)
    {
        xSemaphoreTake(_fsk_channel_lock, portMAX_DELAY);

        if (_fsk_rewind_state != FskRewindReq::IDLE)
        {
            xSemaphoreGive(_fsk_channel_lock);
            Debug_printf("rewind_seconds: BUSY (a request is already being processed)\r\n");
            return false;
        }

        rmt_channel_handle_t channel = (rmt_channel_handle_t)_fsk_active_channel;
        if (channel != nullptr)
        {
            // Capture BEFORE rmt_disable(): the disable call may take up to
            // nearly one RMT ping-pong cycle to return on classic ESP32, and
            // crediting that tail to the physical-position estimate would
            // overstate how much waveform was really emitted.
            const int64_t stop_ts = esp_timer_get_time();

            // Held across the disable call so fsk_signal_end() (blocked on
            // the same lock) can't delete this channel underneath us.
            // rmt_tx_disable()'s internals never touch our locks or wait on
            // the cassette task, so this can't deadlock.
            rmt_disable(channel);

            // Publish the request in the same critical section: whichever of
            // us and fsk_signal_end() runs first under this lock decides
            // whether the channel completed naturally or is claimed here —
            // see fsk_signal_end() for the other half.
            _fsk_rewind_stop_timestamp_us = stop_ts;
            _fsk_rewind_seconds_req = seconds;
            _fsk_rewind_state = FskRewindReq::REQUESTED;
            _fsk_active_channel = nullptr;

            xSemaphoreGive(_fsk_channel_lock);

            // No polling: this blocks on the same mutex the cassette task
            // holds for its whole dispatch, released only after the
            // interrupted play_fsk_chunk() has committed the new tape_offset
            // and written _last_rewind_result.
            xSemaphoreTake(_cassette_lock, portMAX_DELAY);
            const RewindResult result = _last_rewind_result;
            xSemaphoreGive(_cassette_lock);

            Debug_printf("rewind_seconds: active-FSK interrupt result=%d\r\n", (int)result);
            return result == RewindResult::SUCCESS ||
                   result == RewindResult::NATURAL_COMPLETION_FALLBACK;
        }

        xSemaphoreGive(_fsk_channel_lock);
    }
    // ---- End Active FSK Rewind fast path --------------------------------

    xSemaphoreTake(_cassette_lock, portMAX_DELAY); // first operation — before
                                                    // reading any live state

    size_t current_offset = tape_offset; // snapshot taken UNDER the lock

    CassetteWalkState current{};
    if (!walk_tape_time(current_offset, UINT64_MAX, current))
    {
        xSemaphoreGive(_cassette_lock);
        return false; // no changes; cassette stays ready at its prior position
    }

    const uint64_t back_us = static_cast<uint64_t>(seconds) * 1000000ULL;
    const uint64_t target_us =
        (current.time_us > back_us) ? (current.time_us - back_us) : 0;

    CassetteWalkState dest{};
    if (!walk_tape_time(SIZE_MAX, target_us, dest))
    {
        xSemaphoreGive(_cassette_lock);
        return false; // no changes
    }

    stop_and_reset_for_reposition(); // still under the lock: no waveform can
                                      // be active here (sio_handle_cassette()
                                      // needs this same lock to start one)

    // Commit under the lock; t2k_boot_sent/qros_boot_sent are session state,
    // not tape-position state (see rewind() above), so left untouched.
    tape_offset      = dest.offset;
    baud             = dest.baud;
    t2k_samplerate   = dest.t2k_samplerate;
    t2k_bit0_half    = dest.t2k_bit0_half;
    t2k_bit1_half    = dest.t2k_bit1_half;
    t2k_pilot_half   = dest.t2k_pilot_half;
    t2k_pilot_count  = dest.t2k_pilot_count;
    qros_turbo_baud  = dest.qros_turbo_baud;

    xSemaphoreGive(_cassette_lock);
    return true;
#else
    (void)seconds;
    return false;
#endif
}

// ESP-side adapter over cas_walk_tape_time(): a positional reader over the
// shared _file, restoring the caller's cursor on return. Caller must already
// hold _cassette_lock.
bool sioCassette::walk_tape_time(size_t stop_at_offset, uint64_t stop_at_time_us,
                                 CassetteWalkState &out) const
{
    if (_file == nullptr || filesize == 0)
        return false;

    const long saved_pos = fnio::ftell(_file);

    fnFile *f = _file;
    auto reader = [](void *ctx, size_t offset, uint8_t *dst, size_t n) -> size_t
    {
        fnFile *file = static_cast<fnFile *>(ctx);
        if (fnio::fseek(file, static_cast<long int>(offset), SEEK_SET) != 0)
            return 0;
        return fnio::fread(dst, 1, n, file);
    };

    const bool ok = cas_walk_tape_time(filesize, reader, f, stop_at_offset,
                                       stop_at_time_us, out);

    fnio::fseek(_file, saved_pos, SEEK_SET); // restore the caller's cursor exactly

    return ok;
}

// Choke point called before any tape_offset write from rewind()/
// rewind_seconds(); every call here is already idempotent.
void sioCassette::stop_and_reset_for_reposition()
{
#ifdef ESP_PLATFORM
    fsk_signal_end();   // idempotent: stops RMT if active, reattaches UART TX,
                        // unconditionally clears _fsk_signal_active
    fsk_free_blocks();  // idempotent: frees the FSK preload block table +
                        // run descriptors; safe if nothing was preloaded
    if (_rmt_active)
        turbo2000_deinit_rmt();
    qros_pilot_off();   // self-guards on _qros_pilot_active internally
    turbo2000_free_pending_buf();
#endif
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
        // Scan first 2 KB for QROS turbo baud chunk (irg > 3000).
        // QROS turbo uses 6580-6595 (AUDF=127) or 9535-9622 (AUDF=86).
        // Real-world QROS marker offsets: 8 B, 168 B max — 2 KB gives ~10x
        // headroom. We read the whole 2 KB in a single fread to avoid one
        // TNFS round-trip per chunk header; on slow TNFS the old
        // chunk-by-chunk scan caused mount freezes (30+ round-trips).
        if (!tape_flags.turbo2000)
        {
            static uint8_t qros_scan_buf[2048];
            size_t base = sizeof(struct tape_FUJI_hdr) + fuji_chunk_length;
            size_t avail = (filesize > base) ? (filesize - base) : 0;
            size_t want = avail < sizeof(qros_scan_buf) ? avail : sizeof(qros_scan_buf);

            if (want >= sizeof(struct tape_FUJI_hdr))
            {
                fnio::fseek(_file, base, SEEK_SET);
                size_t got = fnio::fread(qros_scan_buf, 1, want, _file);

                bool has_600_boot = false;
                size_t pos = 0;
                while (pos + sizeof(struct tape_FUJI_hdr) <= got)
                {
                    struct tape_FUJI_hdr *h = (struct tape_FUJI_hdr *)&qros_scan_buf[pos];
                    uint8_t *t = h->chunk_type;
                    uint16_t clen = h->chunk_length;

                    if (t[0] == 'b' && t[1] == 'a' && t[2] == 'u' && t[3] == 'd')
                    {
                        if (h->irg_length <= 600)
                        {
                            has_600_boot = true;
                        }
                        else if (h->irg_length > 3000)
                        {
                            tape_flags.qros = 1;
                            qros_turbo_baud = h->irg_length;
                            qros_boot_sent = has_600_boot;
                            Debug_printf("QROS turbo format detected (baud=%u, has_boot=%d)\n",
                                         qros_turbo_baud, has_600_boot);
                            break;
                        }
                    }

                    pos += sizeof(struct tape_FUJI_hdr) + clen;
                }
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
        size_t hdr_read =
            fnio::fread(atari_sector_buffer, 1, sizeof(struct tape_FUJI_hdr), _file);
        // A complete 8-byte header must be present before any field is used;
        // fewer bytes remaining is end-of-tape.
        if (hdr_read < sizeof(struct tape_FUJI_hdr))
            return 0; // EOT: no complete subsequent chunk

        len = hdr->chunk_length;

        // A complete header does not guarantee the declared body fits in the
        // remaining file. data/baud/unknown chunks terminate at EOT (return 0)
        // if the body would overrun EOF. "fsk " is exempt: play_fsk_chunk()
        // clamps a truncated FSK chunk to its present bytes instead.
        const size_t header_size = sizeof(struct tape_FUJI_hdr);
        const size_t bytes_after_header = filesize - offset - header_size;
        const bool body_complete =
            static_cast<size_t>(hdr->chunk_length) <= bytes_after_header;

        if (p[0] == 'd' && //is a data header?
            p[1] == 'a' &&
            p[2] == 't' &&
            p[3] == 'a')
        {
            if (!body_complete)
                return 0; // truncated data body -> EOT before any transmission
            block++;
            break;
        }
        else if (p[0] == 'b' && //is a baud header?
                 p[1] == 'a' &&
                 p[2] == 'u' &&
                 p[3] == 'd')
        {
            if (!body_complete)
                return 0; // truncated baud body -> EOT before changing baud
            if (tape_flags.turbo) //ignore baud hdr
                continue;
            baud = hdr->irg_length;
            SYSTEM_BUS.setBaudrate(baud);
        }
        else if (p[0] == 'f' && //is a raw FSK header? ('f','s','k',' ')
                 p[1] == 's' &&
                 p[2] == 'k' &&
                 p[3] == ' ')
        {
            // Delegate the whole chunk (IRG + signal) to play_fsk_chunk();
            // its return offset is authoritative. Baud is unchanged.
            size_t next = play_fsk_chunk(offset, hdr->chunk_length,
                                         hdr->irg_length);
            if (next == 0)
                return 0; // EOT / no safe continuation
            if (next == offset)
                return offset; // motor-abort retry from this chunk
            offset = next; // structural advance past the FSK chunk; keep walking
            continue;
        }
        // Unknown/non-FSK chunk: skip 8+length only if the declared body is
        // structurally complete; otherwise EOT (never advance past EOF).
        if (!body_complete)
            return 0;
        offset += sizeof(struct tape_FUJI_hdr) + len;
    }

    // TO DO : check that "data" record was actually found - not done by SDrive until after IRG by checking offset<filesize

    // The gap loop + data transmit below must run ONLY when the search loop
    // broke on a 'data' chunk. If it instead exited at filesize (a pure-FSK
    // image), `hdr` still holds the last non-data header and its IRG was
    // already honored inside play_fsk_chunk — terminate here to avoid a
    // duplicate IRG / stale-header fallthrough.
    if (offset >= filesize)
        return 0; // end-of-tape

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

    turbo2000_free_pending_buf(); // RMT is done with it now

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

void sioCassette::turbo2000_free_pending_buf()
{
    if (_t2k_pending_buf)
    {
        free(_t2k_pending_buf);
        _t2k_pending_buf = nullptr;
    }
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

    turbo2000_free_pending_buf(); // RMT is done with it now
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

                turbo2000_free_pending_buf(); // free any previous pending buffer

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

// =====================================================================
// A8CAS raw FSK ("fsk ") chunk playback — segmented whole-payload preload
// into internal RAM. This section only preloads (bounds, alloc, fill,
// failure handling, cleanup); it does not emit any waveform or touch RMT.
// =====================================================================

// fsk_compute_bounds() (fsk_plan.h) is the single source of truth for
// structural bounds/next-offset; play_fsk_chunk() consumes its result
// directly rather than recomputing O+8+L.

#ifdef ESP_PLATFORM

// Reader adapter matching fsk_read_fn: `ctx` is the open fnFile*, already
// positioned. Never requests more than FSK_PRELOAD_READ_MAX (512) bytes,
// staying under the TNFS 525-byte per-read limit.
static size_t fsk_fnio_reader(void *ctx, uint8_t *dst, size_t n)
{
    fnFile *f = static_cast<fnFile *>(ctx);
    if (f == nullptr || dst == nullptr || n == 0)
        return 0;
    return fnio::fread(dst, 1, n, f);
}

#endif // ESP_PLATFORM

#ifdef ESP_PLATFORM

// Shared by fsk_free_blocks() and preload failure paths. Frees every
// allocated block + the pointer table; safe after a partial allocation and
// safe to call more than once.
static void fsk_release_blocks(uint8_t **&blocks, size_t &block_size,
                               size_t &block_count)
{
    if (blocks != nullptr)
    {
        for (size_t i = 0; i < block_count; ++i)
        {
            if (blocks[i] != nullptr)
            {
                heap_caps_free(blocks[i]);
                blocks[i] = nullptr;
            }
        }
        heap_caps_free(blocks);
        blocks = nullptr;
    }
    block_size = 0;
    block_count = 0;
}


// Idempotent cleanup (ESP-only): frees every payload block + the pointer
// table, then resets all payload/ISR-cursor state.
void sioCassette::fsk_free_blocks()
{
    fsk_release_blocks(_fsk_blocks, _fsk_block_size, _fsk_block_count);

    _fsk_payload_len = 0;

    // O(1) ISR-only encoder cursor — reset to a clean baseline.
    _fsk_value_count = 0;
    _fsk_value_index = 0;
    _fsk_payload_pos = 0;
    _fsk_remaining_ticks = 0;
    _fsk_level_high = false;

    // Run descriptors + run cursor — reset to a clean baseline.
    _fsk_run_chunk_count = 0;
    _fsk_run_chunk_index = 0;
}

// Allocates the segmented block table in internal DRAM, fills it from the
// file, and fails safely (no partial waveform, everything freed) if the
// runtime preload cannot complete. TU-local free function (not a member) so
// the block-size/read-max class constants are passed in by the caller.
// data_avail == 0 is a valid "no values" (IRG-only) result.
[[maybe_unused]] static bool fsk_preload_payload(
    fnFile *file, size_t payload_start, size_t data_avail,
    size_t preload_block_bytes, size_t preload_read_max,
    uint8_t **&blocks, size_t &block_size, size_t &block_count,
    size_t &payload_len, size_t &value_count)
{
    // Start from a clean, idempotent baseline.
    fsk_release_blocks(blocks, block_size, block_count);
    payload_len = 0;
    value_count = 0;

    if (file == nullptr)
    {
        Debug_printf("FSK preload: no open file\r\n");
        return false;
    }

    if (data_avail == 0)
    {
        // No payload to load; IRG-only case. Set block_size consistently from
        // the configured block size (0 is acceptable here — nothing is read).
        block_size = preload_block_bytes;
        return true;
    }

    // Defensive validation: a non-empty payload needs valid, non-zero config.
    if (preload_block_bytes == 0)
    {
        Debug_printf("FSK preload: invalid preload_block_bytes == 0\r\n");
        return false;
    }
    if (preload_read_max == 0)
    {
        Debug_printf("FSK preload: invalid preload_read_max == 0\r\n");
        return false;
    }

    const size_t bsize = preload_block_bytes; // e.g. 512 (from the class const)
    const size_t bcount =
        (data_avail + bsize - 1) / bsize; // ceil, <= 128 for a uint16 length

    // Pointer table in internal DRAM, zeroed so partial-alloc cleanup can
    // rely on nullptr entries.
    uint8_t **tbl = static_cast<uint8_t **>(
        heap_caps_calloc(bcount, sizeof(uint8_t *),
                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (tbl == nullptr)
    {
        Debug_printf("FSK preload: pointer table alloc failed (%u entries)\r\n",
                     (unsigned)bcount);
        return false;
    }

    // Publish the (still being filled) table so the failure paths can clean up
    // every block allocated so far.
    blocks = tbl;
    block_size = bsize;
    block_count = bcount;

    // Payload blocks in EXTERNAL PSRAM, not internal RAM: a full-size chunk
    // (up to 65535 bytes) can't coexist with the Wi-Fi/TLS stack in internal
    // RAM. Safe for the ISR to read: the RMT ISR is not IRAM-registered, so
    // it runs with cache enabled and can read PSRAM through it (same as the
    // existing Turbo 2000 path). No PSRAM -> fail the preload safely rather
    // than fall back to the known-broken internal-RAM strategy.
    for (size_t i = 0; i < bcount; ++i)
    {
        blocks[i] = static_cast<uint8_t *>(
            heap_caps_malloc(bsize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (blocks[i] == nullptr)
        {
            Debug_printf("FSK preload: block %u/%u PSRAM alloc failed\r\n",
                         (unsigned)i, (unsigned)bcount);
            fsk_release_blocks(blocks, block_size, block_count);
            return false;
        }
    }

    if (fnio::fseek(file, static_cast<long int>(payload_start), SEEK_SET) != 0)
    {
        Debug_printf("FSK preload: fseek to %u failed\r\n",
                     (unsigned)payload_start);
        fsk_release_blocks(blocks, block_size, block_count);
        return false;
    }

    const size_t loaded =
        fsk_preload_into_blocks(blocks, bcount, bsize, data_avail,
                                preload_read_max, fsk_fnio_reader, file);

    // A short read here is a runtime preload failure (distinct from a
    // structurally truncated CAS, whose already-clamped data_avail loads
    // completely): free everything, emit no partial waveform.
    if (loaded != data_avail)
    {
        Debug_printf("FSK preload: short read (%u of %u bytes)\r\n",
                     (unsigned)loaded, (unsigned)data_avail);
        fsk_release_blocks(blocks, block_size, block_count);
        return false;
    }

    payload_len = data_avail;
    value_count = fsk_value_count(data_avail);
    return true;
}

// Preloads a contiguous zero-IRG FSK run into ONE PSRAM block table. Each
// chunk starts on a fresh block boundary (so per-chunk odd-tail/parity reset
// works generically) and fills the run descriptors (_fsk_run_*). On any
// failure everything is freed and false is returned — no partial waveform.
// A run whose chunks are all zero-length loads nothing and returns true.
bool sioCassette::fsk_preload_run(const size_t *run_offsets,
                                  const size_t *run_data_avail,
                                  size_t count)
{
    // Clean, idempotent baseline.
    fsk_free_blocks();
    _fsk_run_chunk_count = 0;

    if (_file == nullptr || count == 0 || count > FSK_RUN_MAX_CHUNKS)
    {
        Debug_printf("FSK run preload: bad args (count=%u)\r\n", (unsigned)count);
        return false;
    }

    const size_t bsize = FSK_PRELOAD_BLOCK_BYTES; // 512

    // Compute per-chunk block spans (each chunk block-aligned) and the total.
    size_t base[FSK_RUN_MAX_CHUNKS] = {};
    size_t total_blocks = 0;
    size_t total_bytes = 0;
    for (size_t c = 0; c < count; ++c)
    {
        base[c] = total_blocks;
        const size_t da = run_data_avail[c];
        const size_t nb = (da + bsize - 1) / bsize; // ceil; 0 -> 0 blocks
        total_blocks += nb;
        total_bytes += da;
    }

    // A run with zero total payload: nothing to allocate/read; IRG-only.
    if (total_blocks == 0)
    {
        _fsk_block_size = bsize;
        _fsk_run_chunk_count = count;
        for (size_t c = 0; c < count; ++c)
        {
            _fsk_run_value_counts[c] = fsk_value_count(run_data_avail[c]);
            _fsk_run_block_base[c] = 0;
            _fsk_run_file_offsets[c] = run_offsets[c];
        }
        _fsk_payload_len = 0;
        _fsk_value_count = _fsk_run_value_counts[0];
        return true;
    }

    // Pointer table (INTERNAL, zeroed so partial-alloc cleanup sees nullptrs).
    uint8_t **tbl = static_cast<uint8_t **>(
        heap_caps_calloc(total_blocks, sizeof(uint8_t *),
                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (tbl == nullptr)
    {
        Debug_printf("FSK run preload: table alloc failed (%u blocks)\r\n",
                     (unsigned)total_blocks);
        return false;
    }
    _fsk_blocks = tbl;
    _fsk_block_size = bsize;
    _fsk_block_count = total_blocks;

    // Payload blocks in PSRAM.
    for (size_t i = 0; i < total_blocks; ++i)
    {
        _fsk_blocks[i] = static_cast<uint8_t *>(
            heap_caps_malloc(bsize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (_fsk_blocks[i] == nullptr)
        {
            Debug_printf("FSK run preload: block %u/%u PSRAM alloc failed\r\n",
                         (unsigned)i, (unsigned)total_blocks);
            fsk_free_blocks();
            return false;
        }
    }

    // Fill each chunk into its own block span via the bounded <=512 reader.
    for (size_t c = 0; c < count; ++c)
    {
        const size_t da = run_data_avail[c];
        if (da == 0)
            continue;
        if (fnio::fseek(_file, static_cast<long int>(run_offsets[c]),
                        SEEK_SET) != 0)
        {
            Debug_printf("FSK run preload: fseek chunk %u failed\r\n",
                         (unsigned)c);
            fsk_free_blocks();
            return false;
        }
        // Fill this chunk into its own contiguous block span (base[c]..).
        const size_t nb = (da + bsize - 1) / bsize;
        const size_t loaded =
            fsk_preload_into_blocks(&_fsk_blocks[base[c]], nb, bsize, da,
                                    FSK_PRELOAD_READ_MAX, fsk_fnio_reader,
                                    _file);
        if (loaded != da)
        {
            Debug_printf("FSK run preload: chunk %u short read (%u/%u)\r\n",
                         (unsigned)c, (unsigned)loaded, (unsigned)da);
            fsk_free_blocks();
            return false;
        }
    }

    // Publish descriptors.
    _fsk_run_chunk_count = count;
    for (size_t c = 0; c < count; ++c)
    {
        _fsk_run_value_counts[c] = fsk_value_count(run_data_avail[c]);
        _fsk_run_block_base[c] = base[c];
        _fsk_run_file_offsets[c] = run_offsets[c];
    }
    _fsk_payload_len = total_bytes;
    _fsk_value_count = _fsk_run_value_counts[0]; // cursor starts in chunk 0
    return true;
}

// =====================================================================
// A8CAS raw FSK ("fsk ") chunk playback — RMT stateful simple-encoder
// callback + one-transaction signal lifecycle (ESP-only). Reproduces the
// resident segmented payload on PIN_UART2_TX at 1 MHz via ONE continuous
// rmt_transmit, mirroring the Turbo 2000 RMT pattern. Does not preload,
// honor the IRG, walk chunks, or change baud.
// =====================================================================

// RMT stateful simple-encoder callback. Runs in ISR context: no file I/O,
// logging, allocation, or blocking calls. Reads the resident block table
// (`data`) plus the O(1) cursor on `self`. Because every payload byte is
// already resident, it always produces at least one symbol or sets *done —
// it never returns 0 to wait for data (min_chunk_size = 1).
//
// Level follows ORIGINAL A8CAS value-index parity: even index -> logical 0,
// odd -> logical 1. Zero-duration values still consume their index (parity)
// but emit no portion. Long values split into <=32767-tick portions via
// fsk_next_portion, carried across calls in O(1) cursor state.
size_t IRAM_ATTR sioCassette::fsk_encode_cb(const void *data, size_t data_size,
                                            size_t symbols_written,
                                            size_t symbols_free,
                                            rmt_symbol_word_t *symbols,
                                            bool *done, void *arg)
{
    // Write a definite value first; don't rely on the RMT framework to
    // initialize/preserve it. *done is set true only after the last portion
    // of the last value has been emitted.
    if (done == nullptr)
        return 0;

    *done = false;

    sioCassette *self = static_cast<sioCassette *>(arg);
    if (self == nullptr)
    {
        // Terminal invalid state: no source to decode. Signal completion so the
        // framework does not interpret return 0 as lack of progress.
        *done = true;
        return 0;
    }

    // Guard: the only valid rmt_transmit payload is the contiguous POINTER
    // TABLE itself (data == _fsk_blocks), not a single block. Terminates
    // with *done=true on a mismatch — the one place this callback returns 0
    // for a reason other than "waveform complete".
    if (data != static_cast<const void *>(self->_fsk_blocks) ||
        data_size != self->_fsk_block_count * sizeof(self->_fsk_blocks[0]))
    {
        *done = true;
        return 0;
    }

    // Defensive: decoding requires a valid resident block table when there is
    // waveform work to do. (A zero-value payload with a null table is handled by
    // the loop below setting *done immediately with num == 0.)
    if (self->_fsk_value_count > 0 &&
        (self->_fsk_blocks == nullptr || self->_fsk_block_size == 0))
    {
        *done = true;
        return 0;
    }

    // rmt_transmit passes the immutable contiguous pointer table as `data`.
    // The pointed-to blocks are also immutable internal-RAM for the transaction.
    const uint8_t *const *blocks = (const uint8_t *const *)data;
    const size_t blk = self->_fsk_block_size; // bytes per block
    size_t num = 0;

    // Active FSK Rewind physical-progress bookkeeping (no effect on
    // *done/level/duration/chunk logic below). is_prefill is true only for
    // the one call synchronous inside rmt_transmit(); every later call is a
    // real hardware-threshold refill of exactly one 256-symbol half, so its
    // start-of-call tick count is an exact, never-overestimated boundary.
    // Prefill can cover up to 512 symbols in one call, so its own total
    // can't be used as that boundary — _fsk_prefill_half_ticks captures the
    // exact first-256-symbol point instead (below).
    const bool is_prefill = !self->_fsk_transmission_started;
    if (!is_prefill)
    {
        self->_fsk_confirmed_ticks = self->_fsk_pending_boundary_ticks;
        self->_fsk_confirmed_timestamp_us = esp_timer_get_time();
    }

    while (num < symbols_free)
    {
        // Fill both halves of one rmt_symbol_word_t.
        uint16_t levels[2];
        uint16_t durs[2];
        int half = 0;

        while (half < 2)
        {
            // Load next value if the current one is exhausted.
            if (self->_fsk_remaining_ticks == 0)
            {
                // Skip zero-duration values; when the CURRENT chunk's values are
                // exhausted, advance to the next chunk in the run (reset the
                // per-chunk index/parity to 0, jump the logical byte position to
                // that chunk's block base) so a contiguous zero-IRG FSK run plays
                // as one continuous waveform. Each value consumes an index
                // (parity) but a zero-duration value emits nothing.
                bool got_value = false;
                for (;;)
                {
                    // Current chunk still has values?
                    if (self->_fsk_value_index < self->_fsk_value_count)
                    {
                        // Read the value at the current logical position through
                        // the block accessor. fsk_block_le16 reassembles a value
                        // that STRADDLES a block boundary WITHIN a chunk. Chunks
                        // never straddle (each starts on a block boundary). The
                        // payload is fully resident, so this always succeeds.
                        uint16_t v = fsk_block_le16(blocks, blk, self->_fsk_payload_pos);
                        bool lvl = fsk_level_for_index(self->_fsk_value_index); // per-chunk parity
                        self->_fsk_value_index++;
                        self->_fsk_payload_pos += 2;
                        if (v != 0)
                        {
                            uint32_t vt = fsk_ticks_for_value(v);
                            self->_fsk_remaining_ticks = vt;
                            self->_fsk_level_high = lvl;
                            got_value = true;
                            break;
                        }
                        // v == 0: index consumed, no portion; keep scanning.
                        continue;
                    }
                    // Current chunk exhausted -> advance to the next run chunk.
                    if (self->_fsk_run_chunk_index + 1 < self->_fsk_run_chunk_count)
                    {
                        self->_fsk_run_chunk_index++;
                        const size_t nb =
                            self->_fsk_run_block_base[self->_fsk_run_chunk_index];
                        self->_fsk_value_index = 0;              // per-chunk parity resets
                        self->_fsk_value_count =
                            self->_fsk_run_value_counts[self->_fsk_run_chunk_index];
                        self->_fsk_payload_pos = nb * blk;       // chunk starts on a block boundary
                        continue; // scan the next chunk's values
                    }
                    // No more chunks in the run -> the whole run is complete.
                    break;
                }
                if (!got_value) // no more values remain in the entire run
                {
                    // All values consumed -> the whole waveform is complete.
                    if (half == 0)
                    {
                        // Clean symbol boundary: complete.
                        *done = true;
                        (void)symbols_written;
                        (void)data_size;
                        return num;
                    }
                    // Pad the unused second half with a 0-duration same-level
                    // entry; the value stream is exhausted -> waveform complete.
                    levels[half] = levels[half - 1];
                    durs[half] = 0;
                    half++;
                    break;
                }
            }
            uint32_t portion = fsk_next_portion(self->_fsk_remaining_ticks);
            self->_fsk_remaining_ticks -= portion;
            levels[half] = self->_fsk_level_high ? 1 : 0;
            durs[half] = (uint16_t)portion;
            self->_fsk_cumulative_ticks += portion; // functional bookkeeping only
            half++;
        }

        symbols[num].level0 = levels[0];
        symbols[num].duration0 = durs[0];
        symbols[num].level1 = levels[1];
        symbols[num].duration1 = durs[1];
        num++;

        // Fires at most once per prefill call, exactly at the first
        // 256-symbol (first hardware threshold) boundary.
        if (is_prefill && num == 256)
            self->_fsk_prefill_half_ticks = self->_fsk_cumulative_ticks;

        // A chunk boundary alone is NOT completion — only the final run
        // chunk's exhaustion is.
        if (self->_fsk_remaining_ticks == 0 &&
            self->_fsk_value_index >= self->_fsk_value_count &&
            self->_fsk_run_chunk_index + 1 >= self->_fsk_run_chunk_count)
        {
            *done = true; // set true ONLY here on full run completion
            (void)symbols_written;
            (void)data_size;
            return num;
        }
    }

    // Buffer full, values remain; *done stays false so RMT calls again.
    // Reaching here means this call filled its whole offered symbols_free,
    // so its end-of-call cumulative total is the exact boundary the NEXT
    // threshold event will confirm (for prefill, still only the first half).
    self->_fsk_pending_boundary_ticks =
        is_prefill ? self->_fsk_prefill_half_ticks : self->_fsk_cumulative_ticks;

    (void)symbols_written;
    (void)data_size;
    return num; // more remains; *done == false; RMT will call again
}

// ---------------------------------------------------------------------
// Allocates + enables the RMT TX channel and simple encoder for one FSK
// transaction, taking ownership of PIN_UART2_TX. On any failure undoes
// whatever partial setup was done and returns false, leaving
// _fsk_signal_active == false. Baud/UART divisor untouched.
bool sioCassette::fsk_signal_begin(size_t resume_value_index)
{
    if (_fsk_signal_active)
        return true; // already own the signal path

    if ((gpio_num_t)PIN_UART2_TX == GPIO_NUM_NC)
    {
        Debug_printf("FSK signal: PIN_UART2_TX is GPIO_NUM_NC\r\n");
        return false;
    }

    // Flush pending UART output before detaching TX from the pin.
    SYSTEM_BUS.flushOutput();

    // Detach UART2 TX from GPIO — same pattern as turbo2000_init_rmt / qros_pilot_on.
    esp_rom_gpio_connect_out_signal(PIN_UART2_TX, SIG_GPIO_OUT_IDX, false, false);
    // Idle level HIGH (mark) before RMT takes over the pin.
    gpio_set_direction((gpio_num_t)PIN_UART2_TX, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)PIN_UART2_TX, 1);

    // Configure the RMT TX channel at 1 MHz (1 us/tick), matching Turbo 2000.
    rmt_tx_channel_config_t tx_cfg = {};
    tx_cfg.gpio_num = (gpio_num_t)PIN_UART2_TX;
    tx_cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    tx_cfg.resolution_hz = T2K_RMT_RESOLUTION_HZ; // 1 MHz -> 1 us/tick -> 1 A8CAS unit = 100 ticks
    tx_cfg.mem_block_symbols = 64 * 8;            // ping-pong memory for gapless refill
    tx_cfg.trans_queue_depth = 4;
    tx_cfg.intr_priority = 0; // driver-chosen default
    tx_cfg.flags.invert_out = false;
    tx_cfg.flags.with_dma = false;
    tx_cfg.flags.io_loop_back = false;
    tx_cfg.flags.io_od_mode = false;
    tx_cfg.flags.allow_pd = false;

    rmt_channel_handle_t channel = nullptr;
    esp_err_t err = rmt_new_tx_channel(&tx_cfg, &channel);
    if (err != ESP_OK || channel == nullptr)
    {
        Debug_printf("FSK signal: rmt_new_tx_channel failed (%d)\r\n", (int)err);
        // If the call reported failure but still handed back a channel, delete
        // it before restoring UART so no channel is leaked.
        if (channel != nullptr)
            rmt_del_channel(channel);
        esp_rom_gpio_connect_out_signal(
            PIN_UART2_TX, uart_periph_signal[2].pins[SOC_UART_TX_PIN_IDX].signal,
            false, false);
        return false;
    }

    // Stateful simple encoder: generates symbols on the fly in the IRAM ISR from
    // the resident payload. min_chunk_size = 1 so the callback is asked to make
    // progress with as little as one free slot and never waits for source data.
    rmt_simple_encoder_config_t simple_cfg = {};
    simple_cfg.callback = fsk_encode_cb;
    simple_cfg.arg = (void *)this;
    simple_cfg.min_chunk_size = 1;
    rmt_encoder_handle_t simple_enc = nullptr;
    err = rmt_new_simple_encoder(&simple_cfg, &simple_enc);
    if (err != ESP_OK || simple_enc == nullptr)
    {
        Debug_printf("FSK signal: rmt_new_simple_encoder failed (%d)\r\n", (int)err);
        // If the call reported failure but still handed back an encoder, delete
        // it too, then delete the already-created channel before restoring UART.
        if (simple_enc != nullptr)
            rmt_del_encoder(simple_enc);
        rmt_del_channel(channel);
        esp_rom_gpio_connect_out_signal(
            PIN_UART2_TX, uart_periph_signal[2].pins[SOC_UART_TX_PIN_IDX].signal,
            false, false);
        return false;
    }

    err = rmt_enable(channel);
    if (err != ESP_OK)
    {
        Debug_printf("FSK signal: rmt_enable failed (%d)\r\n", (int)err);
        rmt_del_encoder(simple_enc);
        rmt_del_channel(channel);
        esp_rom_gpio_connect_out_signal(
            PIN_UART2_TX, uart_periph_signal[2].pins[SOC_UART_TX_PIN_IDX].signal,
            false, false);
        return false;
    }

    _fsk_rmt_channel = channel;
    _fsk_rmt_encoder = simple_enc;
    _fsk_signal_active = true;

    // Reset the ISR cursor to the start of run chunk 0 — unless
    // resume_value_index seeds an Active FSK Rewind resume partway through
    // it (clamped to that chunk's value_count). Only where the cursor
    // STARTS changes; fsk_encode_cb's advance logic is untouched.
    _fsk_run_chunk_index = 0;
    _fsk_value_count = (_fsk_run_chunk_count > 0) ? _fsk_run_value_counts[0] : 0;
    _fsk_value_index = (resume_value_index < _fsk_value_count) ? resume_value_index : 0;
    _fsk_payload_pos = (_fsk_run_chunk_count > 0)
                           ? _fsk_run_block_base[0] * _fsk_block_size + _fsk_value_index * 2
                           : 0;
    _fsk_remaining_ticks = 0;
    _fsk_level_high = false;

    return true;
}

// ---------------------------------------------------------------------
// Issues exactly ONE continuous rmt_transmit for the whole preloaded
// waveform, then waits for completion. The transaction payload is the
// pointer table itself (`_fsk_blocks`), never `_fsk_blocks[0]` — the
// segmented payload is not contiguous. Pointer table and blocks stay valid
// until rmt_tx_wait_all_done returns.
void sioCassette::fsk_signal_emit()
{
    if (!_fsk_signal_active || _fsk_rmt_channel == nullptr ||
        _fsk_rmt_encoder == nullptr || _fsk_blocks == nullptr)
        return;

    rmt_channel_handle_t channel = (rmt_channel_handle_t)_fsk_rmt_channel;
    rmt_encoder_handle_t encoder = (rmt_encoder_handle_t)_fsk_rmt_encoder;

    rmt_transmit_config_t tx_cfg = {};
    tx_cfg.loop_count = 0; // one-shot, single continuous transaction
    tx_cfg.flags.eot_level = 1; // end at MARK/HIGH, not the default LOW/SPACE

    // Stable contiguous descriptor: the pointer table itself.
    const void *table = (const void *)_fsk_blocks;
    const size_t table_bytes = _fsk_block_count * sizeof(uint8_t *);

    // Reset physical-progress accounting; safe plain writes until
    // _fsk_transmission_started is published below.
    _fsk_cumulative_ticks = 0;
    _fsk_prefill_half_ticks = 0;
    _fsk_pending_boundary_ticks = 0;
    _fsk_confirmed_ticks = 0;

    esp_err_t err = rmt_transmit(channel, encoder, table, table_bytes, &tx_cfg);
    if (err != ESP_OK)
    {
        Debug_printf("FSK signal: rmt_transmit failed (%d)\r\n", (int)err);
        return; // never started; nothing published; teardown via fsk_signal_end
    }

    // rmt_transmit() only returns after the prefill encode has completed and
    // rmt_ll_tx_start() has issued, so this instant is always genuinely
    // mid-transmission. Capture the tx-start reference and gate ISR writes
    // BEFORE publishing the channel, so a rewind can never observe or credit
    // progress against a channel that hasn't truly started.
    _fsk_confirmed_timestamp_us = esp_timer_get_time();
    _fsk_transmission_started = true;

    if (_fsk_channel_lock != nullptr)
    {
        xSemaphoreTake(_fsk_channel_lock, portMAX_DELAY);
        _fsk_active_channel = (void *)channel;
        xSemaphoreGive(_fsk_channel_lock);
    }

    // Returns on natural completion OR a forced rmt_disable() from HTTP —
    // the driver's forced-stop path completes this same queue wait either way.
    rmt_tx_wait_all_done(channel, -1);

    // Stopped either way; fsk_signal_end() (called next) is the single
    // serialization point that decides which one this was.
    _fsk_transmission_started = false;
}

// ---------------------------------------------------------------------
// Idempotent teardown of the RMT signal path: waits for any in-flight
// transaction, deletes encoder + channel, reattaches UART2 TX. Does NOT
// free the preloaded FSK blocks (fsk_free_blocks()'s job).
void sioCassette::fsk_signal_end()
{
    bool had_channel = (_fsk_rmt_channel != nullptr);
    _fsk_interrupted_pending = false; // clear scratch before (maybe) setting it below

    if (_fsk_rmt_channel != nullptr)
    {
        rmt_channel_handle_t channel = (rmt_channel_handle_t)_fsk_rmt_channel;

        // Single serialization point for Active FSK Rewind vs. this teardown.
        // Lock held only for this compare-and-decide, never across the
        // disable/delete below. If _fsk_active_channel still equals ours,
        // nobody interrupted it — unpublish. Otherwise HTTP already claimed
        // it; if a request is REQUESTED it must target THIS channel (only one
        // request in flight — see the BUSY guard), so consume it atomically.
        if (_fsk_channel_lock != nullptr)
        {
            xSemaphoreTake(_fsk_channel_lock, portMAX_DELAY);
            if (_fsk_active_channel == (void *)channel)
            {
                _fsk_active_channel = nullptr;
            }
            else if (_fsk_rewind_state == FskRewindReq::REQUESTED)
            {
                _fsk_interrupted_pending = true;
                _fsk_interrupted_seconds = _fsk_rewind_seconds_req;
                _fsk_interrupted_stop_us = _fsk_rewind_stop_timestamp_us;
                _fsk_rewind_state = FskRewindReq::PROCESSING;
            }
            xSemaphoreGive(_fsk_channel_lock);
        }

        // If HTTP already forced rmt_disable() on this channel, this call is
        // the proven-safe ESP_ERR_INVALID_STATE no-op (FSM guard returns
        // before touching hardware) — never a double-stop hazard.
        rmt_tx_wait_all_done(channel, -1);
        rmt_disable(channel);
        rmt_del_channel(channel);
        _fsk_rmt_channel = nullptr;
    }

    if (_fsk_rmt_encoder != nullptr)
    {
        rmt_del_encoder((rmt_encoder_handle_t)_fsk_rmt_encoder);
        _fsk_rmt_encoder = nullptr;
    }

    // Reattach only if we ever took the pin this cycle.
    if (had_channel || _fsk_signal_active)
    {
        esp_rom_gpio_connect_out_signal(
            PIN_UART2_TX, uart_periph_signal[2].pins[SOC_UART_TX_PIN_IDX].signal,
            false, false);
    }

    _fsk_signal_active = false;
}

// ---------------------------------------------------------------------
// Cassette-task-side resolution of a genuine Active FSK Rewind interrupt.
// Called from play_fsk_chunk(), holding _cassette_lock, right after
// fsk_signal_end() flags _fsk_interrupted_pending and before
// fsk_free_blocks() runs (resident run data is still valid).
//
// 3-tier resolution, resolved_time <= target_time guaranteed at every tier:
//   1. fine: resident FSK data only, no file I/O — start of the value at or
//      before target (a target inside the leading IRG restarts chunk 0);
//   2. fallback: target before this run -> existing coarse walker;
//   3. floor: walker also fails -> tape_offset = 0.
//
// Always clears _fsk_rewind_state back to IDLE before returning — no
// request state is ever left stuck.
uint16_t sioCassette::fsk_resident_run_value_reader(void *ctx, size_t chunk_index,
                                                    size_t value_index)
{
    sioCassette *self = static_cast<sioCassette *>(ctx);
    const size_t base_pos = self->_fsk_run_block_base[chunk_index] * self->_fsk_block_size;
    return fsk_block_le16((const uint8_t *const *)self->_fsk_blocks, self->_fsk_block_size,
                          base_pos + value_index * 2);
}

size_t sioCassette::fsk_resolve_active_rewind(size_t run_chunk0_header_offset,
                                              uint32_t seconds,
                                              int64_t stop_timestamp_us)
{
    size_t resolved_offset = 0;

    CassetteWalkState run_start{};
    if (!walk_tape_time(run_chunk0_header_offset, UINT64_MAX, run_start))
    {
        // Cannot even establish where this run began: always-safe floor.
        _last_rewind_result = RewindResult::FAILED;
        resolved_offset = 0;
    }
    else
    {
        const uint64_t run_start_time_us = run_start.time_us;
        const uint64_t leading_irg_us =
            static_cast<uint64_t>(_fsk_run_leading_irg_ms) * 1000ULL;

        // Physical progress lower bound via clock interpolation. stop_timestamp_us
        // was captured by HTTP BEFORE rmt_disable(), so the disable tail is
        // never credited. _fsk_confirmed_ticks/_timestamp_us were last written
        // by the ISR strictly before rmt_disable() could return (its busy-poll
        // on real TX_DONE guarantees the ISR isn't concurrent), so this plain
        // read needs no lock.
        const int64_t elapsed_us = stop_timestamp_us - _fsk_confirmed_timestamp_us;
        uint64_t candidate_physical_ticks = _fsk_confirmed_ticks;
        if (elapsed_us > 0)
            candidate_physical_ticks += static_cast<uint64_t>(elapsed_us);
        if (candidate_physical_ticks > _fsk_cumulative_ticks)
            candidate_physical_ticks = _fsk_cumulative_ticks; // never claim more than was ever encoded

        const uint64_t current_cas_time_us =
            run_start_time_us + leading_irg_us + candidate_physical_ticks;
        const uint64_t back_us = static_cast<uint64_t>(seconds) * 1000000ULL;
        const uint64_t target_us =
            (current_cas_time_us > back_us) ? (current_cas_time_us - back_us) : 0;

        // Tier 1 (fine): pure resident-run resolution (cassette_time_plan.h),
        // shared verbatim with the host test suite via the same function.
        const FskActiveRewindResolution res = cas_fsk_resolve_active_rewind(
            _fsk_run_value_counts, _fsk_run_chunk_count,
            &sioCassette::fsk_resident_run_value_reader, this,
            run_start_time_us, leading_irg_us, target_us);

        if (res.resolved)
        {
            if (res.inside_leading_irg)
            {
                // Target lands inside this run's own leading IRG: restart
                // chunk 0 from scratch (replay the full IRG); no resume.
                resolved_offset = run_chunk0_header_offset;
                _fsk_resume_pending = false;
            }
            else
            {
                resolved_offset = _fsk_run_file_offsets[res.chunk_index] - 8; // chunk header
                _fsk_resume_pending = (res.value_index > 0);
                _fsk_resume_value_index = res.value_index;
            }
            _last_rewind_result = RewindResult::SUCCESS;
        }
        else
        {
            // Tier 2: existing coarse walker, unchanged, guarantees resolved <= target.
            CassetteWalkState dest{};
            if (walk_tape_time(SIZE_MAX, target_us, dest))
            {
                resolved_offset  = dest.offset;
                baud             = dest.baud;
                t2k_samplerate   = dest.t2k_samplerate;
                t2k_bit0_half    = dest.t2k_bit0_half;
                t2k_bit1_half    = dest.t2k_bit1_half;
                t2k_pilot_half   = dest.t2k_pilot_half;
                t2k_pilot_count  = dest.t2k_pilot_count;
                qros_turbo_baud  = dest.qros_turbo_baud;
                _fsk_resume_pending = false;
                _last_rewind_result = RewindResult::NATURAL_COMPLETION_FALLBACK;
            }
            else
            {
                // Tier 3: always-safe floor, same as rewind()'s own behavior.
                resolved_offset = 0;
                _fsk_resume_pending = false;
                _last_rewind_result = RewindResult::FAILED;
            }
        }
    }

    if (_fsk_channel_lock != nullptr)
    {
        xSemaphoreTake(_fsk_channel_lock, portMAX_DELAY);
        _fsk_rewind_state = FskRewindReq::IDLE;
        xSemaphoreGive(_fsk_channel_lock);
    }

    return resolved_offset;
}

#endif // ESP_PLATFORM

// =====================================================================
// Cross-platform play_fsk_chunk orchestration: owns ONE "fsk " chunk end to
// end (structural bounds -> segmented preload -> IRG -> RMT begin/emit/end
// -> cleanup -> next-offset). Does not recognize "fsk " in the walker or
// touch baud. Return: 0 = EOT (truncated header or overrun body, after
// reproducing the present prefix); offset+8+chunk_length = well-formed
// advance; starting_offset = motor-abort retry. Based on structural
// boundaries only, never on how much actually preloaded at runtime.
// =====================================================================
size_t sioCassette::play_fsk_chunk(size_t offset, uint16_t chunk_length,
                                   uint16_t irg_ms)
{
    const size_t starting_offset = offset;

    // fsk_compute_bounds() is the single source of truth for structural
    // state, including next_offset — no second O+8+L formula here.
    const FskBounds bounds = fsk_compute_bounds(filesize, offset, chunk_length);
    if (!bounds.header_complete)
    {
        return 0; // < 8 header bytes remain: end-of-tape
    }

    const size_t data_avail = bounds.data_avail;
    const size_t value_count = bounds.value_count;
    const bool structurally_truncated = bounds.structurally_truncated;
    // Consumed on the PC path only; avoid an unused-variable warning on ESP,
    // which derives its own _fsk_value_count via the preload.
    (void)value_count;
    (void)structurally_truncated;

    const size_t next_offset = bounds.next_offset;

    // Assigned on entry to each terminal condition; every path funnels
    // through the `done:` cleanup label below.
    size_t result = next_offset;

#ifdef ESP_PLATFORM
    // One-shot Active FSK Rewind resume seed: captured into locals and
    // cleared from the shared member immediately. Normal playback (the
    // common case) leaves resume_this_run false, unchanged from before.
    const bool resume_this_run = _fsk_resume_pending;
    const size_t resume_value_index = _fsk_resume_value_index;
    _fsk_resume_pending = false;

    // Scan + preload the CONTIGUOUS zero-IRG FSK run: authentic A8CAS images
    // split one continuous tape signal across consecutive `fsk ` chunks
    // (only the first carries a non-zero IRG). Reproducing each with its own
    // RMT lifecycle would insert a gap at every container boundary, so we
    // preload the whole maximal run first and play it as ONE lifecycle.
    // This chunk is always run chunk 0; following chunks join while
    // fsk_run_should_join() holds.
    size_t run_offsets[FSK_RUN_MAX_CHUNKS];
    size_t run_data_avail[FSK_RUN_MAX_CHUNKS];
    size_t run_count = 1;
    run_offsets[0] = offset + 8;      // payload start of chunk 0
    run_data_avail[0] = data_avail;   // clamped bytes of chunk 0
    result = next_offset;             // next after chunk 0 (updated as the run grows)

    {
        size_t scan = next_offset; // structural offset of the next candidate
        while (scan != 0 && run_count < FSK_RUN_MAX_CHUNKS)
        {
            // Read the candidate's 8-byte header (bounded, pre-waveform).
            struct tape_FUJI_hdr chdr;
            if (fnio::fseek(_file, static_cast<long int>(scan), SEEK_SET) != 0)
                break;
            if (fnio::fread(&chdr, 1, sizeof(chdr), _file) != sizeof(chdr))
                break;
            const uint8_t *cp = (const uint8_t *)&chdr;
            const bool is_fsk = (cp[0] == 'f' && cp[1] == 's' &&
                                 cp[2] == 'k' && cp[3] == ' ');
            const uint16_t clen = chdr.chunk_length;
            const uint16_t cirg = chdr.irg_length;
            const FskBounds cb = fsk_compute_bounds(filesize, scan, clen);
            if (!fsk_run_should_join(is_fsk, cirg, cb.header_complete,
                                     cb.structurally_truncated))
                break; // non-FSK / IRG>0 / EOF / truncated -> run ends here
            run_offsets[run_count] = scan + 8;
            run_data_avail[run_count] = cb.data_avail;
            run_count++;
            result = cb.next_offset; // advance the walker return past this chunk
            scan = cb.next_offset;
        }
    }

    // Preload the WHOLE run before the IRG/waveform; once RMT begins there is
    // no file I/O. On failure, emit no partial waveform and fall through to
    // the IRG + cleanup with the structural next-offset.
    bool preload_ok = fsk_preload_run(run_offsets, run_data_avail, run_count);
    if (!preload_ok)
    {
        Debug_printf("FSK: run preload failed at offset %u (%u chunks), "
                     "skipping emission\r\n",
                     (unsigned)offset, (unsigned)run_count);
    }

    // This run's leading IRG contribution to Active FSK Rewind's absolute-time
    // formula. On a genuine resume the IRG below is skipped (never replayed),
    // so the contribution is 0, not the on-disk irg_ms — otherwise a rewind
    // landing inside a resumed sub-run would double-count time never spent.
    _fsk_run_leading_irg_ms = resume_this_run ? 0 : irg_ms;

    // Inter-Record Gap (mirrors the data-record gap loop); skipped on a
    // resume, since we're reentering mid-tape, not starting a fresh chunk.
    if (!resume_this_run)
    {
        uint32_t gap = irg_ms;
        fnLedManager.set(eLed::LED_BUS, true);
        while (gap)
        {
            gap--;
            fnSystem.delay_microseconds(999); // shave a usec for the MOTOR check
            if (has_pulldown() && !motor_line() && gap > 1000)
            {
                // Motor de-asserted mid-gap: abort for retry from chunk start.
                fnLedManager.set(eLed::LED_BUS, false);
                result = starting_offset;
                goto done;
            }
        }
        fnLedManager.set(eLed::LED_BUS, false);
    }

    // ESP raw signal: begin -> emit -> end, only if preload succeeded and at
    // least one value exists anywhere in the run (else just honor the IRG).
    {
        size_t run_total_values = 0;
        if (preload_ok)
        {
            for (size_t c = 0; c < _fsk_run_chunk_count; ++c)
                run_total_values += _fsk_run_value_counts[c];
        }
        if (preload_ok && run_total_values > 0)
        {
            const FskRunSummary run_shape =
                fsk_run_summarize(_fsk_blocks, _fsk_block_size, _fsk_run_block_base,
                                  _fsk_run_value_counts, _fsk_run_chunk_count);

            if (!resume_this_run && !run_shape.has_space)
            {
                // No LOW time requested anywhere in the run: the UART already
                // idles at MARK, so skip the RMT lifecycle (its channel
                // create/teardown can drive LOW) and just consume the time.
                // A resumed run always has SPACE overall, so it stays on RMT.
                SYSTEM_BUS.flushOutput();
                const int64_t hold_start_us = esp_timer_get_time();
                const uint64_t hold_total_us = run_shape.total_ticks; // 1 tick = 1 us
                for (;;)
                {
                    const uint64_t elapsed_us =
                        static_cast<uint64_t>(esp_timer_get_time() - hold_start_us);
                    if (elapsed_us >= hold_total_us)
                        break;
                    const uint64_t remaining_us = hold_total_us - elapsed_us;
                    // Same abort rule as the IRG loop: only with > 1 s left.
                    if (has_pulldown() && !motor_line() && remaining_us > 1000000)
                    {
                        result = starting_offset;
                        goto done;
                    }
                    fnSystem.delay_microseconds(
                        remaining_us > 999 ? 999 : static_cast<uint32_t>(remaining_us));
                }
            }
            else if (fsk_signal_begin(resume_this_run ? resume_value_index : 0))
            {
                fsk_signal_emit();   // ONE continuous rmt_transmit + wait-all-done
                fsk_signal_end();    // teardown; also detects whether a rewind
                                     // request claimed this channel (see fsk_signal_end())

                // Must be consumed here, before falling through to `done:` —
                // that label's own fsk_signal_end() call is a no-op that
                // unconditionally clears this same scratch flag.
                if (_fsk_interrupted_pending)
                {
                    result = fsk_resolve_active_rewind(starting_offset,
                                                       _fsk_interrupted_seconds,
                                                       _fsk_interrupted_stop_us);
                    _fsk_interrupted_pending = false;
                }
            }
            else
            {
                // begin() already undid any partial setup and reattached UART.
                Debug_printf("FSK: signal begin failed at offset %u\r\n",
                             (unsigned)offset);
            }
        } // if (preload_ok && run_total_values > 0)
    } // scope for run_total_values

done:
    // Single idempotent cleanup path; blocks freed AFTER wait-done.
    fsk_signal_end();

    fsk_free_blocks();

    return result;

#else  // ---- PC build: structural bounds + IRG + safe skip, no raw signal ----
    (void)data_avail;
    (void)value_count;

    // Honor the IRG deterministically via bus_idle (NetSIO/SerialSIO stepping),
    // mirroring the data path. No payload preload, no RMT, no ESP-only state.
    uint32_t gap = irg_ms;
    fnLedManager.set(eLed::LED_BUS, true);
    while (gap)
    {
        int step;
        if (SYSTEM_BUS.isBoIP())
            step = gap > 1000 ? 1000 : (int)gap; // 1000 ms step (NetSIO)
        else
            step = gap > 20 ? 20 : (int)gap;     // 20 ms step (SerialSIO)
        gap -= step;
        SYSTEM_BUS.bus_idle(step);
        if (has_pulldown() && !motor_line() && gap > 1000)
        {
            fnLedManager.set(eLed::LED_BUS, false);
            return starting_offset; // motor abort -> retry from chunk start
        }
    }
    fnLedManager.set(eLed::LED_BUS, false);

    // No raw signal on the PC build; advance by structural boundary (or EOT for
    // the overrun/truncated case). No ESP-only members referenced.
    return result;
#endif
}

#endif /* BUILD_ATARI */



#define FASTLED_INTERNAL
#include "FastLED.h"

//static const char *TAG = "FastLED";
#include "esp_idf_version.h"


// -- Forward reference
class ESP32RMTController;

// -- Array of all controllers
//    This array is filled at the time controllers are registered 
//    (Usually when the sketch calls addLeds)
static ESP32RMTController * gControllers[FASTLED_RMT_MAX_CONTROLLERS];

// -- Current set of active controllers, indexed by the RMT
//    channel assigned to them.
static ESP32RMTController * gOnChannel[FASTLED_RMT_MAX_CHANNELS];

static int gNumControllers = 0;
static int gNumStarted = 0;
static int gNumDone = 0;
static int gNext = 0;

static intr_handle_t gRMT_intr_handle = NULL;

// -- Global semaphore for the whole show process
//    Semaphore is not given until all data has been sent
static xSemaphoreHandle gTX_sem = NULL;

// -- Make sure we can't call show() too quickly (fastled library)
CMinWait<55>   gWait;

static bool gInitialized = false;

/*
** general DRAM system for printing during faster IRQs
** be careful not to set the size too large, because code that prints
** has the tendancy to do a stack alloc of the same size...
*/

// -- BB: For debugging purposes
#if FASTLED_ESP32_SHOWTIMING == 1

#define MEMORYBUF_SIZE 256
DRAM_ATTR char g_memorybuf[MEMORYBUF_SIZE] = {0};
DRAM_ATTR char *g_memorybuf_write = g_memorybuf;

void IRAM_ATTR memorybuf_add( char *b ) {

    int buflen = strlen(b);

    // don't overflow
    int bufRemain = sizeof(g_memorybuf) - ( g_memorybuf_write - g_memorybuf );
    if ( bufRemain == 0 ) return;
    if (bufRemain < buflen) buflen = bufRemain;

    memcpy(g_memorybuf_write, b, buflen);
    g_memorybuf_write += buflen;
}

void IRAM_ATTR memorybuf_add( char c ) {

    // don't overflow
    int bufRemain = sizeof(g_memorybuf) - ( g_memorybuf_write - g_memorybuf );
    if ( bufRemain < 1 ) return;

    *g_memorybuf_write = c;
    g_memorybuf_write++;
}

void IRAM_ATTR memorybuf_insert( char *b, int buflen ) {
    // don't overflow
    int maxbuf = sizeof(g_memorybuf) - ( g_memorybuf_write - g_memorybuf );
    if ( maxbuf == 0 ) return;
    if (maxbuf < buflen) buflen = maxbuf;

    memcpy(g_memorybuf_write, b, buflen);
    g_memorybuf_write += buflen;
}

// often one wants a separator and an integer, do a helper
void IRAM_ATTR memorybuf_int( int i, char sep) {

    // am I full already?
    int maxbuf = sizeof(g_memorybuf) - ( g_memorybuf_write - g_memorybuf );
    if ( maxbuf == 0 ) return;

    // for speed, just make sure I have 12 bytes, even though maybe I need fewer
    // 12 is the number because I need a null which I will fill with sep, and 
    // there's always the chance of a minus
    if (maxbuf <= 12) return;

    // prep the buf and find the length ( can't copy)
    itoa(i, g_memorybuf_write, 10);
    int buflen = strlen(g_memorybuf_write);
    g_memorybuf_write[buflen] = sep;
    g_memorybuf_write += (buflen + 1);

}

// get from the front... requires a memmove because overlaps.
// *len input is the size of the buf, return is the length you got

// this will always be the most efficient if you ask for a buffer that's as large as the
// capture buffer
void memorybuf_get(char *b, int *len) {
    // amount in the buffer
    int blen = g_memorybuf_write - g_memorybuf ;
    if ( blen == 0 ) {
        *len = 0;
        return;
    }
    if (blen > *len) {
        memcpy(b, g_memorybuf, *len);
        int olen = blen - *len;
        memmove(g_memorybuf, g_memorybuf_write - olen, olen);
        g_memorybuf_write = g_memorybuf + olen;
    }
    else {
        memcpy(b, g_memorybuf, blen);
        g_memorybuf_write = g_memorybuf;
        *len = blen;
    }
    return;
}

#endif /* FASTLED_ESP32_SHOWTIMING == 1 */



/*
** Internal functions that need to be exposed
**
** In 4.0, there's one code structure -- the "ll" interfaces have not been exposed, but
** one can use the rmt_ functions without setting up the driver.
*
** In ESP-IDF 4.1, the functions route through a different structure, which is only set up
** when the internal driver is called.... but one can bypass with the ll functions.
**
** In 4.2, the structures changed again, to go more directly.
**
** Really, epressif. You have so much error checking in your code, trying to protect embedded
** programmers for this and that, and keep changing code that probably doesn't need to change.
*/

#if ( ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0)) && ( ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(4, 2, 0))

#define USE_FASTLED_RMT_FNS 1

#include <hal/rmt_ll.h>

esp_err_t fastled_set_tx_thr_intr_en(rmt_channel_t channel, bool en, uint16_t evt_thresh)
{
    /* regs is rmt_dev_t, which is the static "RMT" */
    rmt_ll_set_tx_limit(&RMT, channel, evt_thresh);
    rmt_ll_enable_tx_thres_interrupt(&RMT, channel, true);
    return(ESP_OK);
}

esp_err_t fastled_set_tx_intr_en(rmt_channel_t channel, bool en)
{
    /* regs is rmt_dev_t, which is the static "RMT" */
    rmt_ll_enable_tx_end_interrupt(&RMT, channel, en);
    return(ESP_OK);
}

esp_err_t fastled_tx_start(rmt_channel_t channel, bool tx_idx_rst)
{

    if (tx_idx_rst) {
        rmt_ll_reset_tx_pointer(&RMT, channel);
    }
    rmt_ll_clear_tx_end_interrupt(&RMT, channel);
    rmt_ll_enable_tx_end_interrupt(&RMT, channel, true);
    rmt_ll_start_tx(&RMT, channel);
    return ESP_OK;
}

#else

#define USE_FASTLED_RMT_FNS 0

#endif // ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(4, 2, 0)

/*
** in later versions of the driver, they very carefully set the "mem_owner"
** flag before copying over. Let's do the same.
*/

// probably already defined.
#ifndef RMT_MEM_OWNER_SW
#define RMT_MEM_OWNER_SW 0
#define RMT_MEM_OWNER_HW 1
#endif

static inline void fastled_set_mem_owner(rmt_channel_t channel, uint8_t owner)
{
    RMT.conf_ch[(uint16_t)channel].conf1.mem_owner = owner;
}



ESP32RMTController::ESP32RMTController(int DATA_PIN, int T1, int T2, int T3)
    : mPixelData(0), 
      mSize(0), 
      mCur(0), 
      mWhichHalf(0),
      mBuffer(0),
      mBufferSize(0),
      mCurPulse(0)
{
    // -- Precompute rmt items corresponding to a zero bit and a one bit
    //    according to the timing values given in the template instantiation
    // T1H
    mOne.level0 = 1;
    mOne.duration0 = ESP_TO_RMT_CYCLES(T1+T2); // TO_RMT_CYCLES(T1+T2);
    // T1L
    mOne.level1 = 0;
    mOne.duration1 = ESP_TO_RMT_CYCLES(T3); // TO_RMT_CYCLES(T3);

    // T0H
    mZero.level0 = 1;
    mZero.duration0 = ESP_TO_RMT_CYCLES(T1); // TO_RMT_CYCLES(T1);
    // T0L
    mZero.level1 = 0;
    mZero.duration1 = ESP_TO_RMT_CYCLES(T2+T3); // TO_RMT_CYCLES(T2 + T3);

    gControllers[gNumControllers] = this;
    gNumControllers++;

    // -- Expected number of CPU cycles between buffer fills
    mCyclesPerFill = (T1 + T2 + T3) * PULSES_PER_FILL;

    // -- If there is ever an interval greater than 1.75 times
    //    the expected time, then bail out.
    mMaxCyclesPerFill = mCyclesPerFill + ((mCyclesPerFill * 3)/4);

    mPin = gpio_num_t(DATA_PIN);
}

// -- Get or create the buffer for the pixel data
//    We can't allocate it ahead of time because we don't have
//    the PixelController object until show is called.
uint32_t * ESP32RMTController::getPixelBuffer(int size_in_bytes)
{
    if (mPixelData == 0) {
        mSize = ((size_in_bytes-1) / sizeof(uint32_t)) + 1;
        mPixelData = (uint32_t *) calloc( mSize, sizeof(uint32_t));
    }
    return mPixelData;
}

// -- Initialize RMT subsystem
//    This only needs to be done once
void ESP32RMTController::init()
{
    if (gInitialized) return;

    // -- Create a semaphore to block execution until all the controllers are done
    if (gTX_sem == NULL) {
        gTX_sem = xSemaphoreCreateBinary();
        xSemaphoreGive(gTX_sem);
    }

    for (int i = 0; i < FASTLED_RMT_MAX_CHANNELS; i++) {

        gOnChannel[i] = NULL;

        // if you are using MEM_BLOCK_NUM, the RMT channel won't be the same as the "channel number"
        rmt_channel_t rmt_channel = rmt_channel_t(i * MEM_BLOCK_NUM);

        // -- RMT configuration for transmission
        // NOTE: In ESP-IDF 4.1++, there is a #define to init, but that doesn't exist
        // in earlier versions
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0)
        rmt_config_t rmt_tx = RMT_DEFAULT_CONFIG_TX(gpio_num_t(0), rmt_channel);
#else
        rmt_config_t rmt_tx;
        memset((void*) &rmt_tx, 0, sizeof(rmt_tx));
        rmt_tx.channel = rmt_channel;
        rmt_tx.rmt_mode = RMT_MODE_TX;
        rmt_tx.gpio_num = gpio_num_t(0);  // The particular pin will be assigned later
#endif

        rmt_tx.mem_block_num = MEM_BLOCK_NUM; 
        rmt_tx.clk_div = DIVIDER;
        rmt_tx.tx_config.loop_en = false;
        rmt_tx.tx_config.carrier_level = RMT_CARRIER_LEVEL_LOW;
        rmt_tx.tx_config.carrier_en = false;
        rmt_tx.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
        rmt_tx.tx_config.idle_output_en = true;

        // -- Apply the configuration
        ESP_ERROR_CHECK( rmt_config(&rmt_tx) );

        if (FASTLED_RMT_BUILTIN_DRIVER) {
            ESP_ERROR_CHECK( rmt_driver_install(rmt_channel, 0, 0) );
        } 
        else {

            // -- Set up the RMT to send 32 bits of the pulse buffer and then
            //    generate an interrupt. When we get this interrupt we
            //    fill the other part in preparation (like double-buffering)
#if USE_FASTLED_RMT_FNS
            ESP_ERROR_CHECK( fastled_set_tx_thr_intr_en(rmt_channel, true, PULSES_PER_FILL) );
#else
            ESP_ERROR_CHECK( rmt_set_tx_thr_intr_en(rmt_channel, true, PULSES_PER_FILL) );
#endif

        }
    }

    if ( ! FASTLED_RMT_BUILTIN_DRIVER ) {
        // -- Allocate the interrupt if we have not done so yet. This
        //    interrupt handler must work for all different kinds of
        //    strips, so it delegates to the refill function for each
        //    specific instantiation of ClocklessController.
        if (gRMT_intr_handle == NULL) {

            ESP_ERROR_CHECK(
                esp_intr_alloc(ETS_RMT_INTR_SOURCE, ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL3, interruptHandler, 0, &gRMT_intr_handle)
            );
        }
    }

    gInitialized = true;
}

// -- Show this string of pixels
//    This is the main entry point for the pixel controller
void ESP32RMTController::showPixels()
{

    if (gNumStarted == 0) {
        // -- First controller: make sure everything is set up
        ESP32RMTController::init();

#if FASTLED_ESP32_FLASH_LOCK == 1
        // -- Make sure no flash operations happen right now
        spi_flash_op_lock();
#endif
    }

    // -- Keep track of the number of strips we've seen
    gNumStarted++;

    // -- The last call to showPixels is the one responsible for doing
    //    all of the actual work
    if (gNumStarted == gNumControllers) {
        gNext = 0;

        // -- This Take always succeeds immediately
        xSemaphoreTake(gTX_sem, portMAX_DELAY);

        // -- Make sure it's been at least 50us since last show
        // this is very conservative if you have multiple channels,
        // arguably there should be a wait on the startnext of each LED string
        gWait.wait();

        // -- First, fill all the available channels and start them
        int channel = 0;
        while ( (channel < FASTLED_RMT_MAX_CHANNELS) && (gNext < gNumControllers) ) {

            ESP32RMTController::startNext(channel);

            channel++;
        }

        // -- Wait here while the data is sent. The interrupt handler
        //    will keep refilling the RMT buffers until it is all
        //    done; then it gives the semaphore back.
        xSemaphoreTake(gTX_sem, portMAX_DELAY);
        xSemaphoreGive(gTX_sem);

        // -- Make sure we don't call showPixels too quickly
        gWait.mark();

        // -- Reset the counters
        gNumStarted = 0;
        gNumDone = 0;
        gNext = 0;

#if FASTLED_ESP32_FLASH_LOCK == 1
        // -- Release the lock on flash operations
        spi_flash_op_unlock();
#endif

#if FASTLED_ESP32_SHOWTIMING == 1
        // the interrupts may have dumped things to the buffer. Print it.
        // warning: this does a fairly large stack allocation. 
        char mb[MEMORYBUF_SIZE+1];
        int mb_len = MEMORYBUF_SIZE;
        memorybuf_get(mb, &mb_len);
        if (mb_len > 0) {
           mb[mb_len] = 0;
           printf(" rmt irq print: %s\n",mb);
       }
#endif /* FASTLED_ESP32_SHOWTIMING == 1 */

    }

}

// -- Start up the next controller
//    This method is static so that it can dispatch to the
//    appropriate startOnChannel method of the given controller.
void ESP32RMTController::startNext(int channel)
{
    if (gNext < gNumControllers) {
        ESP32RMTController * pController = gControllers[gNext];
        pController->startOnChannel(channel);
        gNext++;
    }
}

// -- Start this controller on the given channel
//    This function just initiates the RMT write; it does not wait
//    for it to finish.
void ESP32RMTController::startOnChannel(int channel)
{

    // -- Store a reference to this controller, so we can get it
    //    inside the interrupt handler
    gOnChannel[channel] = this;

    // the RMT channel depends on the MEM_BLOCK
    mRMT_channel = rmt_channel_t(channel * MEM_BLOCK_NUM);

    // -- Assign the pin to this channel
    rmt_set_pin(mRMT_channel, RMT_MODE_TX, mPin);

    if (FASTLED_RMT_BUILTIN_DRIVER) {
        // -- Use the built-in RMT driver to send all the data in one shot
        rmt_register_tx_end_callback(doneOnRMTChannel, (void *) channel);
        rmt_write_items(mRMT_channel, mBuffer, mBufferSize, false);
    } else {
        // -- Use our custom driver to send the data incrementally

        // -- Initialize the counters that keep track of where we are in
        //    the pixel data and the RMT buffer
        mRMT_mem_start = & (RMTMEM.chan[mRMT_channel].data32[0].val);
        mRMT_mem_ptr = mRMT_mem_start;
        mCur = 0;
        mWhichHalf = 0;

        // -- Fill both halves of the RMT buffer (a totality of 64 bits of pixel data)
        fillNext();
        fillNext();

        // -- Turn on the interrupts
#if USE_FASTLED_RMT_FNS
        fastled_set_tx_intr_en(mRMT_channel, true);
#else
        rmt_set_tx_intr_en(mRMT_channel, true);
#endif

        // -- Kick off the transmission
        tx_start();
    }

}

// -- Start RMT transmission
//    Setting this RMT flag is what actually kicks off the peripheral
void ESP32RMTController::tx_start()
{
#if USE_FASTLED_RMT_FNS
    fastled_tx_start(mRMT_channel, true);
#else
    rmt_tx_start(mRMT_channel, true);
#endif

    mLastFill = __clock_cycles();

}

// In the case of the build-in driver, they specify the RMT channel
// so we use the arg instead
void ESP32RMTController::doneOnRMTChannel(rmt_channel_t channel, void * arg) 
{
    doneOnChannel((int) arg, (void *) 0);
}

// -- A controller is done 
//    This function is called when a controller finishes writing
//    its data. It is called either by the custom interrupt
//    handler (below), or as a callback from the built-in
//    interrupt handler. It is static because we don't know which
//    controller is done until we look it up.
void ESP32RMTController::doneOnChannel(int channel, void * arg)
{

    // -- Turn off output on the pin
    // SZG: Do I really need to do this?
    //  ESP32RMTController * pController = gOnChannel[channel];
    // gpio_matrix_out(pController->mPin, 0x100, 0, 0);

    gOnChannel[channel] = NULL;
    gNumDone++;

    if (gNumDone == gNumControllers) {
        // -- If this is the last controller, signal that we are all done
        if (FASTLED_RMT_BUILTIN_DRIVER) {
            xSemaphoreGive(gTX_sem);
        } else {
            portBASE_TYPE HPTaskAwoken = 0;
            xSemaphoreGiveFromISR(gTX_sem, &HPTaskAwoken);
            if (HPTaskAwoken == pdTRUE) portYIELD_FROM_ISR();
        }
    } else {
        // -- Otherwise, if there are still controllers waiting, then
        //    start the next one on this channel
        if (gNext < gNumControllers) {
            startNext(channel);
        }
    }
}
    
// -- Custom interrupt handler
//    This interrupt handler handles two cases: a controller is
//    done writing its data, or a controller needs to fill the
//    next half of the RMT buffer with data.
void IRAM_ATTR ESP32RMTController::interruptHandler(void *arg)
{

    // -- The basic structure of this code is borrowed from the
    //    interrupt handler in esp-idf/components/driver/rmt.c
    uint32_t intr_st = RMT.int_st.val;
    uint8_t channel;

    for (channel = 0; channel < FASTLED_RMT_MAX_CHANNELS; channel++) {

        ESP32RMTController * pController = gOnChannel[channel];
        if (pController != NULL) {

            int rmt_channel = pController->mRMT_channel;

            int tx_done_bit = rmt_channel * 3;
            int tx_next_bit = rmt_channel + 24;

            if (intr_st & BIT(tx_next_bit)) {
                // -- More to send on this channel
                RMT.int_clr.val |= BIT(tx_next_bit);

                // if timing's NOT ok, have to bail
                if (true == pController->timingOk()) {

                    pController->fillNext();

                }
            } // -- Transmission is complete on this channel
            else if (intr_st & BIT(tx_done_bit)) {

                RMT.int_clr.val |= BIT(tx_done_bit);
                doneOnChannel(channel, 0);

            }
        }
    }
}

DRAM_ATTR char g_bail_str[] = "_BAIL_";

// check to see if there's a bad timing. Returns
// we may be behind the necessary timing, so we should bail out of this 'show'.
//
// returns true if the timing is OK, false if bad

bool IRAM_ATTR ESP32RMTController::timingOk() {

    // last time is always delayed, don't check that one
    if (mCur >= mSize)   return(true);

    uint32_t delta = __clock_cycles() - mLastFill;

    // interesting test - what if we only write 4? will nothing else light?
    if ( delta > mMaxCyclesPerFill) {

#if FASTLED_ESP32_SHOWTIMING == 1
        memorybuf_add('!');
        memorybuf_int( CYCLES_TO_US(delta), '-' );
        memorybuf_int( CYCLES_TO_US(mMaxCyclesPerFill), '-');
        memorybuf_int( mCur, ':' );
        memorybuf_int( mSize, ':' );
        memorybuf_add( g_bail_str );
#endif /* FASTLED_ESP32_SHOWTIMING == 1 */

        // how do we bail out? It seems if we simply call rmt_tx_stop, 
        // we'll still flicker on the end. Setting mCur to mSize has the side effect
        // of triggering the other code that says "we're finished"

        // Old code also set this, hoping it wouldn't send garbage bytes
        mCur = mSize;

        // other code also set some zeros to make sure there wasn't anything bad.
        fastled_set_mem_owner(mRMT_channel, RMT_MEM_OWNER_SW);
        for (uint32_t j = 0; j < PULSES_PER_FILL; j++) {
            * mRMT_mem_ptr++ = 0;
        }
        fastled_set_mem_owner(mRMT_channel, RMT_MEM_OWNER_HW);

        return false;
    }

#if FASTLED_ESP32_SHOWTIMING == 1
    else {
        memorybuf_int( CYCLES_TO_US(delta), '-' );
    }
#endif /* FASTLED_ESP32_SHOWTIMING == 1 */

    return true;
}

// -- Fill RMT buffer
//    Puts 32 bits of pixel data into the next 32 slots in the RMT memory
//    Each data bit is represented by a 32-bit RMT item that specifies how
//    long to hold the signal high, followed by how long to hold it low.
void IRAM_ATTR ESP32RMTController::fillNext()
{

    if (mCur < mSize) {

        // -- Get the zero and one values into local variables
        // each one is a "rmt_item_t", which contains two values, which is very convenient
        register uint32_t one_val = mOne.val;
        register uint32_t zero_val = mZero.val;

        // -- Use locals for speed
        volatile register uint32_t * pItem =  mRMT_mem_ptr;

        // set the owner to SW --- current driver does this but its not clear it matters
        fastled_set_mem_owner(mRMT_channel, RMT_MEM_OWNER_SW);
            
        // Shift bits out, MSB first, setting RMTMEM.chan[n].data32[x] to the 
        // rmt_item32_t value corresponding to the buffered bit value

        for (int i=0; i < PULSES_PER_FILL / 32; i++) {
            if (mCur < mSize) {
                register uint32_t thispixel = mPixelData[mCur];
                for (int j = 0; j < 32; j++) {

                    *pItem++ = (thispixel & 0x80000000L) ? one_val : zero_val;
                    // Replaces: RMTMEM.chan[mRMT_channel].data32[mCurPulse].val = val;
                    thispixel <<= 1;
                }
                mCur++;
            }
            else {
                // if you hit the end, add 0 for signal
                *pItem++ = 0;
            }
        }

        // -- Flip to the other half, resetting the pointer if necessary
        mWhichHalf++;
        if (mWhichHalf == 2) {
            pItem = mRMT_mem_start;
            mWhichHalf = 0;
        }

        // -- Store the new pointer back into the object
        mRMT_mem_ptr = pItem;

        // set the owner back to HW
        fastled_set_mem_owner(mRMT_channel, RMT_MEM_OWNER_HW);

        // update the time I last filled
        mLastFill = __clock_cycles();

    } else {
        // -- No more data; signal to the RMT we are done
        fastled_set_mem_owner(mRMT_channel, RMT_MEM_OWNER_SW);
        for (uint32_t j = 0; j < PULSES_PER_FILL; j++) {
            * mRMT_mem_ptr++ = 0;
        }
        fastled_set_mem_owner(mRMT_channel, RMT_MEM_OWNER_HW);
    }
}

// -- Init pulse buffer
//    Set up the buffer that will hold all of the pulse items for this
//    controller. 
//    This function is only used when the built-in RMT driver is chosen
void ESP32RMTController::initPulseBuffer(int size_in_bytes)
{

    mCurPulse = 0;

    // maybe we already have a buffer of the right size, it's likely
    if (mBuffer && (mBufferSize == size_in_bytes * 8))
        return;

    if (mBuffer) { free(mBuffer); mBuffer = 0; }

    // -- Each byte has 8 bits, each bit needs a 32-bit RMT item
    mBufferSize = size_in_bytes * 8;

    mBuffer = (rmt_item32_t *) calloc( mBufferSize, sizeof(rmt_item32_t) );

}

// -- Convert a byte into RMT pulses
//    This function is only used when the built-in RMT driver is chosen
void ESP32RMTController::convertByte(uint32_t byteval)
{
    // -- Write one byte's worth of RMT pulses to the big buffer
    byteval <<= 24;
    for (uint32_t j = 0; j < 8; j++) {
        mBuffer[mCurPulse] = (byteval & 0x80000000L) ? mOne : mZero;
        byteval <<= 1;
        mCurPulse++;
    }
}


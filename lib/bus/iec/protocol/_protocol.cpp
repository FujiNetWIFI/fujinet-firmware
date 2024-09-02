#ifdef BUILD_IEC

#include "_protocol.h"

#include "bus.h"

#include "../../../include/pinmap.h"
#include "../../../include/debug.h"

using namespace Protocol;


/**
 * Callback function to set timeout 
 */
static void onTimer(void *arg)
{
    IECProtocol *p = (IECProtocol *)arg;
    p->timer_timedout = true;
    //IEC.release( PIN_IEC_SRQ );
}

IECProtocol::IECProtocol() {
    esp_timer_create_args_t args = {
        .callback = onTimer,
        .arg = this,
        .name = nullptr
    };
    esp_timer_create(&args, &timer_handle);
};

IECProtocol::~IECProtocol() {
    esp_timer_stop(timer_handle);
    esp_timer_delete(timer_handle);
};

/**
 * Start the timeout timer
 */
void IECProtocol::timer_start(uint64_t timeout_us)
{
    timer_timedout = false;
    esp_timer_stop(timer_handle);
    esp_timer_start_once(timer_handle, timeout_us);
    //IEC.pull( PIN_IEC_SRQ );
}
void IECProtocol::timer_stop()
{
    esp_timer_stop(timer_handle);
    //IEC.release( PIN_IEC_SRQ );
}


// int16_t IRAM_ATTR IECProtocol::timeoutWait(uint8_t pin, bool target_status, size_t wait_us, bool watch_atn)
// {
//     IEC.pull ( PIN_IEC_SRQ );
//     uint64_t start = esp_timer_get_time();
//     uint64_t current = 0;
//     timer_start( wait_us );

// #ifndef IEC_SPLIT_LINES
//     IEC.release ( pin );
// #endif

//     while ( !timer_timedout )
//     {
//         IEC.pull ( PIN_IEC_SRQ );
//         if ( IEC.status ( pin ) == target_status )
//         {
//             timer_stop();
//             current = esp_timer_get_time();
//             IEC.release ( PIN_IEC_SRQ );
//             return ( current - start );
//         }
//         usleep( 2 );
//         if ( watch_atn )
//         {
//             if ( IEC.status ( PIN_IEC_ATN ) )
//             {
//                 IEC.release ( PIN_IEC_SRQ );
//                 return -1;
//             }
//         }
//         IEC.release ( PIN_IEC_SRQ );
//         usleep( 2 );
//     }
//     IEC.release ( PIN_IEC_SRQ );
//     return wait_us;
// }

int16_t IRAM_ATTR IECProtocol::timeoutWait(uint8_t pin, bool target_status, size_t wait_us, bool watch_atn)
{
    uint64_t start = 0;
    uint64_t current = 0;
    uint64_t elapsed = 0;
    bool atn_status = false;

#ifndef IEC_SPLIT_LINES
    IEC.release ( pin );
#endif

    // Quick check to see if the target status is already set
    if ( IEC.status ( pin ) == target_status )
        return elapsed;

    if ( pin == PIN_IEC_ATN )
    {
        watch_atn = false;
    }
    else
    {
#ifndef IEC_SPLIT_LINES
        IEC.release ( PIN_IEC_ATN );
#endif

        // // Sample ATN and set flag to indicate COMMAND or DATA mode
        // atn_status = IEC.status ( PIN_IEC_ATN );
        // if ( atn_status ) IEC.flags |= ATN_PULLED;
    }

    //IEC.pull ( PIN_IEC_SRQ );
    start = esp_timer_get_time();
    while ( IEC.status ( pin ) != target_status )
    {
        current = esp_timer_get_time();
        elapsed = ( current - start );

        if ( elapsed >= wait_us && wait_us != FOREVER )
        {
            //IEC.release ( PIN_IEC_SRQ );
            if ( wait_us == TIMEOUT_DEFAULT )
                return -1;
            
            return wait_us;
        }

        if ( watch_atn )
        {
            // bool atn_check = IEC.status ( PIN_IEC_ATN );
            // if ( atn_check != atn_status )
            if ( IEC.status ( PIN_IEC_ATN ) )
            {
                IEC.flags |= ATN_PULLED;
                //IEC.release ( PIN_IEC_SRQ );
                //Debug_printv("pin[%d] state[%d] wait[%d] elapsed[%d]", pin, target_status, wait, elapsed);
                return -1;
            }
        }

        if ( IEC.state < BUS_ACTIVE || elapsed > FOREVER )
        {
            // Something is messed up.  Get outta here.
            Debug_printv("wth? bus_state[%d]", IEC.state);
            Debug_printv("pin[%d] target_status[%d] wait[%d] elapsed[%d]", pin, target_status, wait_us, elapsed);
            return -1;
        }
    }
    //IEC.release ( PIN_IEC_SRQ );

    // Debug_printv("pin[%d] state[%d] wait[%d] step[%d] t[%d]", pin, target_status, wait, elapsed);
    return elapsed;
}

bool IRAM_ATTR IECProtocol::wait(size_t wait_us, bool watch_atn)
{
    return wait(wait_us, 0, watch_atn);
}

bool IRAM_ATTR IECProtocol::wait(size_t wait_us, uint64_t start, bool watch_atn)
{
    uint64_t current, elapsed;
    current = 0;
    elapsed = 0;

    if ( wait_us == 0 ) return true;
    wait_us--; // Shave 1us for overhead

    //IEC.pull ( PIN_IEC_SRQ );
    if ( start == 0 ) start = esp_timer_get_time();
    while ( elapsed <= wait_us )
    {
        current = esp_timer_get_time();
        elapsed = current - start;

        if ( watch_atn )
        {
            if ( IEC.status ( PIN_IEC_ATN ) )
            {
                IEC.flags |= ATN_PULLED;
                //IEC.release ( PIN_IEC_SRQ );
                //Debug_printv("wait[%d] elapsed[%d] start[%d] current[%d]", wait, elapsed, start, current);
                return false;
            }
        }
    }
    //IEC.release ( PIN_IEC_SRQ );

    return true;
}

#endif /* BUILD_IEC */
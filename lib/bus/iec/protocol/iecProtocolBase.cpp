#ifdef BUILD_IEC

#include "iecProtocolBase.h"
#include "../../../include/pinmap.h"
#include "bus.h"

int16_t IecProtocolBase::timeoutWait(uint8_t pin, bool target_status, size_t wait, bool watch_atn)
{
    uint64_t start, current, elapsed;
    bool atn_status = false;
    elapsed = 0;

    if ( pin == PIN_IEC_ATN )
    {
        watch_atn = false;
    }
    else if ( watch_atn )
    {
        // Sample ATN and set flag to indicate SELECT or DATA mode
        atn_status = IEC.status ( PIN_IEC_ATN );
        if ( atn_status == PULLED)
            IEC.flags |= ATN_PULLED;
    }

    start = current = 0;

    IEC.pull ( PIN_IEC_SRQ );
    while ( IEC.status ( pin ) != target_status )
    {
        fnSystem.delay_microseconds(1);
        elapsed = current++ - start;

        if ( elapsed > wait && wait != FOREVER )
        {
            IEC.release ( PIN_IEC_SRQ );
            if ( wait == TIMEOUT_DEFAULT )
                return -1;
            
            return wait;
        }

        if ( watch_atn )
        {
            bool atn_check = IEC.status ( PIN_IEC_ATN );
            if ( atn_check == PULLED)
                IEC.flags |= ATN_PULLED;

            if ( atn_check != atn_status )
            {
                IEC.release ( PIN_IEC_SRQ );
                //Debug_printv("pin[%d] state[%d] wait[%d] elapsed[%d]", pin, target_status, wait, elapsed);
                return -1;
            }            
        }
    }
    IEC.release ( PIN_IEC_SRQ );

    // Debug_printv("pin[%d] state[%d] wait[%d] step[%d] t[%d]", pin, target_status, wait, elapsed);
    return elapsed;

}

bool IecProtocolBase::wait(size_t wait, uint64_t start, bool watch_atn)
{
    uint64_t current, elapsed;
    bool atn_status = false;
    elapsed = 0;

    if ( wait == 0 ) return true;
    wait--; // Shave 1us for overhead

    if ( start == 0 )
    {
        esp_timer_init();
        start = current = esp_timer_get_time();
    }
    else
    {
        current = esp_timer_get_time();
    }

    if ( watch_atn )
    {
    // Sample ATN and set flag to indicate SELECT or DATA mode
        atn_status = IEC.status ( PIN_IEC_ATN );
    if ( atn_status == PULLED)
        IEC.flags |= ATN_PULLED;
    }

    //IEC.pull ( PIN_IEC_SRQ );
    while ( elapsed < wait )
    {
        current = esp_timer_get_time();
        elapsed = current - start;

        if ( watch_atn )
        {
        bool atn_check = IEC.status ( PIN_IEC_ATN );
        if ( atn_check == PULLED)
            IEC.flags |= ATN_PULLED;

        if ( atn_check != atn_status )
        {
                //IEC.release( PIN_IEC_SRQ );
                //Debug_printv("watch_atn[%d] wait[%d] elapsed[%d]", watch_atn, wait, elapsed);
            return false;
            }            
        }
    }

    //IEC.release ( PIN_IEC_SRQ );
    return true;
}

#endif /* BUILD_IEC */
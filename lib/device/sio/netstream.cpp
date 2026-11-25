#ifdef BUILD_ATARI

#include "netstream.h"

#include "../../include/debug.h"
#include "../../include/pinmap.h"

#include "cassette.h"
#include "compat_inet.h"
#include "fnSystem.h"
#include "fnConfig.h"
#include "utils.h"

#ifdef ESP_PLATFORM
#include <errno.h>
#include <sys/socket.h>
#endif
#ifdef NETSTREAM_ECHO_CLOCK
#include <atomic>
#include <driver/mcpwm_prelude.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_attr.h>
#include <esp_rom_gpio.h>
#include <esp_rom_sys.h>
#include <soc/gpio_reg.h>
#include <soc/gpio_sig_map.h>
#if CONFIG_IDF_TARGET_ESP32
#include <soc/mcpwm_struct.h>
#endif
#endif

static uint64_t netstream_time_us()
{
#ifdef ESP_PLATFORM
    return (uint64_t)esp_timer_get_time();
#else
    return (uint64_t)fnSystem.millis() * 1000ULL;
#endif
}

struct NetstreamAudf3Baud
{
    uint8_t audf3;
    int baud;
};

static const NetstreamAudf3Baud kNetstreamAudf3Ntsc[] = {
    {159, 300},   {204, 600},   {162, 750},   {226, 1201},  {109, 2405},
    {179, 4811},  {86, 9622},   {39, 19454},  {21, 31960},  {16, 38908},
    {15, 40677},  {14, 42614},  {13, 44744},  {12, 47099},  {11, 49716},
    {10, 52640},  {9, 55930},   {8, 59659},   {7, 63621},   {6, 68453},
    {5, 74281},   {4, 81444},   {3, 90493},   {2, 102273},  {1, 118250},
    {0, 127841},
};

static const NetstreamAudf3Baud kNetstreamAudf3Pal[] = {
    {132, 300},   {190, 600},   {151, 750},   {219, 1201},  {106, 2403},
    {177, 4819},  {85, 9638},   {39, 19276},  {21, 31668},  {16, 38553},
    {15, 40305},  {14, 42224},  {13, 44336},  {12, 46669},  {11, 49262},
    {10, 52159},  {9, 55420},   {8, 59114},   {7, 63042},   {6, 67829},
    {5, 73604},   {4, 80702},   {3, 89669},   {2, 101341},  {1, 117171},
    {0, 126673},
};

static int netstream_baud_from_audf3(uint8_t audf3, bool is_pal)
{
    const NetstreamAudf3Baud *table = is_pal ? kNetstreamAudf3Pal : kNetstreamAudf3Ntsc;
    size_t count = is_pal ? (sizeof(kNetstreamAudf3Pal) / sizeof(kNetstreamAudf3Pal[0]))
                          : (sizeof(kNetstreamAudf3Ntsc) / sizeof(kNetstreamAudf3Ntsc[0]));
    for (size_t i = 0; i < count; i++)
    {
        if (table[i].audf3 == audf3)
            return table[i].baud;
    }
    return 0;
}

#ifdef ESP_PLATFORM
static void netstream_enable_clock_pwm(int baud)
{
    if (baud <= 0)
        return;

    ledc_timer_config_t ledc_timer = {};
    ledc_timer.clk_cfg = LEDC_AUTO_CLK;
    ledc_timer.speed_mode = LEDC_ESP32XX_HIGH_SPEED;
    ledc_timer.duty_resolution = LEDC_TIMER_RESOLUTION;
    ledc_timer.timer_num = LEDC_TIMER_1;
    ledc_timer.freq_hz = baud;

    ledc_channel_config_t ledc_channel_sio_ckin = {};
    ledc_channel_sio_ckin.gpio_num = PIN_CKI;
    ledc_channel_sio_ckin.speed_mode = LEDC_ESP32XX_HIGH_SPEED;
    ledc_channel_sio_ckin.channel = LEDC_CHANNEL_1;
    ledc_channel_sio_ckin.intr_type = LEDC_INTR_DISABLE;
    ledc_channel_sio_ckin.timer_sel = LEDC_TIMER_1;
    ledc_channel_sio_ckin.duty = 1;
    ledc_channel_sio_ckin.hpoint = 0;

    ledc_channel_config(&ledc_channel_sio_ckin);
    ledc_timer_config(&ledc_timer);
    ledc_set_duty(LEDC_ESP32XX_HIGH_SPEED, LEDC_CHANNEL_1, 1);
    ledc_update_duty(LEDC_ESP32XX_HIGH_SPEED, LEDC_CHANNEL_1);
}
#endif

#ifdef NETSTREAM_ECHO_CLOCK
/*
 * The echo clock - the synchronous external clock (the Atari receives with SKCTL serial mode 000).
 *
 * POKEY copies the CKI edges to CLOCK OUT after it synchronized them to PHI2 - and POKEY can lose a CKI
 * edge that comes close to a PHI2 edge (the known edge defect - see ijor/sioSync). The free running LEDC
 * clock and the UART data drift through that window and through the sampling point - the Atari gets
 * framing errors on long streams.
 *
 * Here one MCPWM timer (80 MHz) makes CKI. The falling CLOCK OUT edge (our falling CKI edge echoed by
 * POKEY) resets the timer - comparator A raises CKI - comparator B lowers it a fixed time later. So our
 * edges keep one phase against PHI2 and the bit is a whole number of PHI2 cycles (the UART receives at
 * PHI2 / n). Without an echo the timer wraps after the period (a bit plus 3 PHI2 cycles) and goes on.
 *
 * At the start a phase scan moves the falling edge over one PHI2 period with the data line at mark. The
 * CLOCK OUT period grows by one PHI2 cycle where our edge passes the PHI2 edge POKEY synchronizes on -
 * that is the dangerous window. The edge is then placed half a PHI2 period away from it.
 *
 * The data bit is written by the interrupt of a comparator at the falling CKI edge. POKEY reads it about
 * 4-5 us after the rising edge - the bit changes while CKI is low (the 74LS07 then holds CLOCK IN down
 * hard - a DATA IN edge while CKI is high can couple a dip into CLOCK IN). The high part is about 10 us
 * (whole PHI2 cycles - at most half a bit) - so the interrupt has the long low part to run in.
 */
#define NETSTREAM_ECHO_LAT_TICKS 113         // CLOCK OUT falls this long after our falling CKI edge (ticks of 12.5 ns)
#define NETSTREAM_ECHO_HIGH_US 10            // the high part of CKI
#define NETSTREAM_ECHO_INTR_PRIO 3           // the highest priority for C handlers
#define NETSTREAM_ECHO_SCAN_STEPS 16         // phase steps over one PHI2 period
#define NETSTREAM_ECHO_SCAN_IV 8             // CLOCK OUT intervals measured per step
#define NETSTREAM_ECHO_TXQ_SIZE 64           // bytes toward the Atari - power of 2
#define NETSTREAM_ECHO_TXQ_MASK (NETSTREAM_ECHO_TXQ_SIZE - 1)
#define NETSTREAM_ECHO_TXQ_HIGH 8            // pace_to_atari keeps at most this many bytes queued

struct NetstreamEcho
{
    bool active = false;
    bool enabled = false;                      // the timer is enabled
    bool data_attached = false;                // our GPIO drives the SIO DATA IN pin (else the UART TX)
    volatile bool hold = true;                 // the phase scan runs - the data line stays at mark
    mcpwm_timer_handle_t timer = nullptr;
    mcpwm_oper_handle_t oper = nullptr;
    mcpwm_cmpr_handle_t ca = nullptr, cb = nullptr;
    mcpwm_oper_handle_t oper2 = nullptr;
    mcpwm_cmpr_handle_t cc = nullptr;          // the data bit comparator (at the falling edge)
    mcpwm_gen_handle_t gen = nullptr;
    mcpwm_sync_handle_t sync = nullptr;
    int pin = -1;                              // the SIO DATA IN pin (the UART TX pin)
    uint32_t mask = 0;                         // its bit in the GPIO out registers
    bool hi = false;                           // pin 32 or more
    double tphi = 0;                           // ticks per PHI2 cycle
    uint32_t hticks = 0;                       // the high part in ticks
    uint32_t dpos = 0, tbit = 0;               // the data comparator value - the bit in ticks
    int tid = -1;                              // our timer in MCPWM0 (ESP32 - for the lateness) - -1 unknown
    uint32_t bits = 0;                         // ISR - line bits sent
    uint32_t late_max = 0;                     // ISR - the latest data bit interrupt (ticks)
    // the byte queue (single producer pace_to_atari - single consumer the ISR)
    uint8_t q[NETSTREAM_ECHO_TXQ_SIZE];
    std::atomic<uint32_t> head{0};
    std::atomic<uint32_t> tail{0};
    uint16_t frame = 0;                        // ISR - rest of the current byte - LSB = next bit
    uint8_t frame_bits = 0;                    // ISR - bits left in frame
    // the phase scan - the CLOCK OUT period measured by the capture unit
    mcpwm_cap_timer_handle_t ctimer = nullptr;
    mcpwm_cap_channel_handle_t cch = nullptr;
    uint32_t c_last = 0;                       // ISR
    bool c_have = false;                       // ISR
    volatile uint32_t c_n = 0;                 // ISR - intervals stored
    uint32_t c_iv[NETSTREAM_ECHO_SCAN_IV];     // ISR - CLOCK OUT falling to falling in ticks
};
static NetstreamEcho s_echo;

// The TX pin of the SIO UART (the same choice as ESP32UARTChannel::begin)
static int netstream_bus_tx_pin()
{
    switch (PIN_BUS_UART)
    {
#ifdef PIN_UART0_TX
    case 0:
        return PIN_UART0_TX;
#endif
#ifdef PIN_UART1_TX
    case 1:
        return PIN_UART1_TX;
#endif
#ifdef PIN_UART2_TX
    case 2:
        return PIN_UART2_TX;
#endif
    default:
        return GPIO_NUM_NC;
    }
}

static inline __attribute__((always_inline)) void netstream_echo_level(uint32_t b)
{
    if (s_echo.hi)
        REG_WRITE(b ? GPIO_OUT1_W1TS_REG : GPIO_OUT1_W1TC_REG, s_echo.mask);
    else
        REG_WRITE(b ? GPIO_OUT_W1TS_REG : GPIO_OUT_W1TC_REG, s_echo.mask);
}

// One byte toward the Atari - false when the queue is full enough (pace_to_atari tries again)
static bool netstream_echo_push(uint8_t c)
{
    uint32_t head = s_echo.head.load(std::memory_order_relaxed);
    uint32_t tail = s_echo.tail.load(std::memory_order_acquire);
    if ((head - tail) >= NETSTREAM_ECHO_TXQ_HIGH)
        return false;
    s_echo.q[head & NETSTREAM_ECHO_TXQ_MASK] = c;
    s_echo.head.store(head + 1, std::memory_order_release);
    return true;
}

// The data bit at the falling CKI edge - start bit - 8 data bits LSB first - stop bit - else idle mark
static bool IRAM_ATTR netstream_echo_bit_cb(mcpwm_cmpr_handle_t, const mcpwm_compare_event_data_t *, void *)
{
    if (s_echo.hold)
    {
        netstream_echo_level(1);
        return false;
    }
    uint32_t tail = s_echo.tail.load(std::memory_order_relaxed);
    uint32_t head = s_echo.head.load(std::memory_order_acquire);
    uint16_t frame = s_echo.frame;
    uint8_t bits = s_echo.frame_bits;
    if (bits == 0)
    {
        if (tail != head)
        {
            frame = (uint16_t)(0x200 | ((uint16_t)s_echo.q[tail & NETSTREAM_ECHO_TXQ_MASK] << 1));
            tail++;
            bits = 10;
        }
        else
        {
            frame = 1;
            bits = 1;
        }
    }
    netstream_echo_level(frame & 1);
#if CONFIG_IDF_TARGET_ESP32
    if (s_echo.tid >= 0)
    {
        // how late this interrupt runs - the counter is past the data comparator (or reset by the next echo)
        uint32_t cnt = MCPWM0.timer[s_echo.tid].timer_status.val & 0xFFFF;
        uint32_t late = cnt >= s_echo.dpos ? cnt - s_echo.dpos : cnt + s_echo.tbit - s_echo.dpos;
        if (late > s_echo.late_max)
            s_echo.late_max = late;
    }
#endif
    s_echo.frame = frame >> 1;
    s_echo.frame_bits = bits - 1;
    s_echo.tail.store(tail, std::memory_order_release);
    s_echo.bits++;
    return false;
}

static void netstream_echo_cap_free()
{
    if (s_echo.cch != nullptr)
    {
        mcpwm_capture_channel_disable(s_echo.cch);
        mcpwm_del_capture_channel(s_echo.cch);
        s_echo.cch = nullptr;
    }
    if (s_echo.ctimer != nullptr)
    {
        mcpwm_capture_timer_stop(s_echo.ctimer);
        mcpwm_capture_timer_disable(s_echo.ctimer);
        mcpwm_del_capture_timer(s_echo.ctimer);
        s_echo.ctimer = nullptr;
    }
}

static void netstream_echo_free()
{
    netstream_echo_cap_free();
    if (s_echo.timer != nullptr && s_echo.enabled)
    {
        mcpwm_timer_start_stop(s_echo.timer, MCPWM_TIMER_STOP_EMPTY);
        mcpwm_timer_disable(s_echo.timer);
    }
    s_echo.enabled = false;
    if (s_echo.gen != nullptr)
        mcpwm_del_generator(s_echo.gen);
    if (s_echo.cc != nullptr)
        mcpwm_del_comparator(s_echo.cc);
    if (s_echo.oper2 != nullptr)
        mcpwm_del_operator(s_echo.oper2);
    if (s_echo.ca != nullptr)
        mcpwm_del_comparator(s_echo.ca);
    if (s_echo.cb != nullptr)
        mcpwm_del_comparator(s_echo.cb);
    if (s_echo.oper != nullptr)
        mcpwm_del_operator(s_echo.oper);
    if (s_echo.timer != nullptr && s_echo.sync != nullptr)
    {
        mcpwm_timer_sync_phase_config_t none = {};
        mcpwm_timer_set_phase_on_sync(s_echo.timer, &none);
    }
    if (s_echo.sync != nullptr)
        mcpwm_del_sync_src(s_echo.sync);
    if (s_echo.timer != nullptr)
        mcpwm_del_timer(s_echo.timer);
    s_echo.gen = nullptr;
    s_echo.cc = nullptr;
    s_echo.oper2 = nullptr;
    s_echo.ca = s_echo.cb = nullptr;
    s_echo.oper = nullptr;
    s_echo.sync = nullptr;
    s_echo.timer = nullptr;
    s_echo.active = false;
}

// The phase scan - one falling CLOCK OUT edge (ISR)
static bool IRAM_ATTR netstream_echo_cap_cb(mcpwm_cap_channel_handle_t, const mcpwm_capture_event_data_t *e, void *)
{
    uint32_t t = e->cap_value;
    if (s_echo.c_have && s_echo.c_n < NETSTREAM_ECHO_SCAN_IV)
    {
        s_echo.c_iv[s_echo.c_n] = t - s_echo.c_last;
        s_echo.c_n = s_echo.c_n + 1;
    }
    s_echo.c_last = t;
    s_echo.c_have = true;
    return false;
}

// Both CKI edges and the data comparator - the falling edge at cb - the values load at the next echo
static void netstream_echo_place(uint32_t cb)
{
    mcpwm_comparator_set_compare_value(s_echo.ca, cb - s_echo.hticks);
    mcpwm_comparator_set_compare_value(s_echo.cb, cb);
    mcpwm_comparator_set_compare_value(s_echo.cc, cb);
    s_echo.dpos = cb;
}

// The phase scan at the stream start (the data line at mark). Locked, the CLOCK OUT period is a whole number
// of PHI2 cycles. Moving our falling edge later over one PHI2 period, the period stays m cycles until our
// edge passes the PHI2 edge POKEY synchronizes on - from there it is m+1. The edge goes half a period away
// from that step - and by whole periods back or forth so that the bit stays n cycles. Returns the falling
// edge place - base_cb when the scan finds no step.
static uint32_t netstream_echo_scan(uint32_t base_cb, int n)
{
    mcpwm_capture_timer_config_t tcfg = {};
    tcfg.group_id = 0;
    tcfg.clk_src = MCPWM_CAPTURE_CLK_SRC_DEFAULT;
    if (mcpwm_new_capture_timer(&tcfg, &s_echo.ctimer) != ESP_OK)
    {
        s_echo.ctimer = nullptr;
        Debug_println("NETSTREAM: echo clock - no capture timer for the phase scan");
        return base_cb;
    }
    mcpwm_capture_channel_config_t ccfg = {};
    ccfg.gpio_num = PIN_CKO;
    ccfg.prescale = 1;
    ccfg.intr_priority = NETSTREAM_ECHO_INTR_PRIO;
    ccfg.flags.neg_edge = 1;
    ccfg.flags.pull_up = 1;
    mcpwm_capture_event_callbacks_t cbs = {};
    cbs.on_cap = netstream_echo_cap_cb;
    if (mcpwm_new_capture_channel(s_echo.ctimer, &ccfg, &s_echo.cch) != ESP_OK ||
        mcpwm_capture_channel_register_event_callbacks(s_echo.cch, &cbs, nullptr) != ESP_OK ||
        mcpwm_capture_channel_enable(s_echo.cch) != ESP_OK ||
        mcpwm_capture_timer_enable(s_echo.ctimer) != ESP_OK ||
        mcpwm_capture_timer_start(s_echo.ctimer) != ESP_OK)
    {
        netstream_echo_cap_free();
        Debug_println("NETSTREAM: echo clock - phase scan capture setup failed");
        return base_cb;
    }
    const int steps = NETSTREAM_ECHO_SCAN_STEPS;
    const double tphi = s_echo.tphi;
    uint32_t bit_us = (uint32_t)(n * tphi / 80.0) + 1;
    int cyc[NETSTREAM_ECHO_SCAN_STEPS];        // CLOCK OUT period in PHI2 cycles per step (0 = no clear answer)
    char line[NETSTREAM_ECHO_SCAN_STEPS + 1];
    for (int k = 0; k < steps; k++)
    {
        netstream_echo_place(base_cb + (uint32_t)(k * tphi / steps + 0.5));
        esp_rom_delay_us(6 * bit_us);          // the new place loads at the next echo - let it settle
        s_echo.c_have = false;
        s_echo.c_n = 0;
        for (uint32_t w = 0; w < 4 * NETSTREAM_ECHO_SCAN_IV && s_echo.c_n < NETSTREAM_ECHO_SCAN_IV; w++)
            esp_rom_delay_us(bit_us);
        uint32_t cnt = s_echo.c_n;
        int first = cnt ? (int)(s_echo.c_iv[0] / tphi + 0.5) : 0;
        bool same = cnt >= NETSTREAM_ECHO_SCAN_IV / 2;
        for (uint32_t i = 1; i < cnt && same; i++)
            if ((int)(s_echo.c_iv[i] / tphi + 0.5) != first)
                same = false;
        cyc[k] = same ? first : 0;
        int d = cyc[k] - n;
        line[k] = cyc[k] == 0 ? '?' : d == 0 ? '0' : d == 1 ? '+' : d == -1 ? '-' : '*';
    }
    line[steps] = 0;
    netstream_echo_cap_free();
    // the jump - the first clear step whose period is one more than the clear step before it (unclear steps
    // between them are the window itself - there the period changes from bit to bit)
    int kb = -1, jb = -1;
    for (int k = 0, last = -1; k < steps; k++)
    {
        if (cyc[k] == 0)
            continue;
        if (last >= 0 && cyc[k] == cyc[last] + 1)
        {
            kb = k;
            jb = last;
            break;
        }
        last = k;
    }
    if (kb < 0)
    {
        // no jump inside - the window is at the wrap (between step 15 and step 0 one period later) - in the
        // middle of the unclear steps there or at 15.5 - all clear steps must agree
        int c = 0, nun = 0;
        bool agree = true;
        for (int k = 0; k < steps; k++)
        {
            if (cyc[k] == 0)
            {
                nun++;
                continue;
            }
            if (c == 0)
                c = cyc[k];
            else if (cyc[k] != c)
                agree = false;
        }
        if (!agree || c == 0 || nun > steps / 4)
        {
            Debug_printf("NETSTREAM: echo clock phase scan %s - no step found - phase 0\n", line);
            return base_cb;
        }
        int ue = 0, us = 0;                    // unclear steps at the end and at the start
        while (ue < steps && cyc[steps - 1 - ue] == 0)
            ue++;
        while (us < steps && cyc[us] == 0)
            us++;
        double b = steps - 0.5 + (us - ue) / 2.0;
        double sk2 = b - steps / 2;
        Debug_printf("NETSTREAM: echo clock phase scan %s - PHI2 edge at the wrap - edge at step %.1f\n", line, sk2);
        return (uint32_t)(base_cb + sk2 * tphi / steps - (c - n) * tphi + 0.5);
    }
    double sk = (jb + kb) / 2.0 + steps / 2;   // half a period from the window
    double cbd = base_cb + sk * tphi / steps - (cyc[kb] - n) * tphi;
    Debug_printf("NETSTREAM: echo clock phase scan %s - PHI2 edge at step %.1f - edge at step %.1f\n",
                 line, (jb + kb) / 2.0, sk >= steps ? sk - steps : sk);
    return (uint32_t)(cbd + 0.5);
}

// Returns the line rate (PHI2 / whole cycles) - 0 when the echo clock cannot run (the LEDC clock then)
static int netstream_echo_start(int baud, bool pal)
{
    const double phi2 = pal ? 1773447.0 : 1789773.0;
    const double tphi = 80000000.0 / phi2;     // ticks per PHI2 cycle (45.1 PAL - 44.7 NTSC)
    int n = (int)(phi2 / (double)baud + 0.5);  // PHI2 cycles per bit
    s_echo.pin = netstream_bus_tx_pin();
    if (s_echo.pin < 0 || (int)PIN_CKI < 0 || (int)PIN_CKO < 0 || n < 16 || n * tphi > 60000.0)
    {
        Debug_printf("NETSTREAM: echo clock not possible (%d baud)\n", baud);
        return 0;
    }
    double tbit = n * tphi;
    int hc = (int)(NETSTREAM_ECHO_HIGH_US * 80.0 / tphi + 0.999);   // the high part in PHI2 cycles
    if (hc < 4)
        hc = 4;
    if (hc > n / 2)
        hc = n / 2;
    uint32_t base_cb = (uint32_t)(tbit - NETSTREAM_ECHO_LAT_TICKS + 0.5);
    uint32_t hticks = (uint32_t)(hc * tphi + 0.5);
    uint32_t period = (uint32_t)(tbit + 3.0 * tphi + 0.5);          // the scan moves the edge up to one PHI2
    s_echo.hi = s_echo.pin >= 32;
    s_echo.mask = 1u << (s_echo.pin & 31);
    s_echo.tphi = tphi;
    s_echo.hticks = hticks;
    s_echo.tbit = (uint32_t)(tbit + 0.5);
    s_echo.dpos = base_cb;
    s_echo.tid = -1;
    s_echo.bits = 0;
    s_echo.late_max = 0;
    s_echo.head.store(0);
    s_echo.tail.store(0);
    s_echo.frame = 0;
    s_echo.frame_bits = 0;
    s_echo.data_attached = false;
    s_echo.hold = true;

    mcpwm_timer_config_t tcfg = {};
    tcfg.group_id = 0;
    tcfg.clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT;
    tcfg.resolution_hz = 80000000;
    tcfg.count_mode = MCPWM_TIMER_COUNT_MODE_UP;
    tcfg.period_ticks = period;
    tcfg.intr_priority = NETSTREAM_ECHO_INTR_PRIO;   // the first part fixes the priority of the whole group
    mcpwm_operator_config_t ocfg = {};
    ocfg.group_id = 0;
    ocfg.intr_priority = NETSTREAM_ECHO_INTR_PRIO;
    mcpwm_comparator_config_t ccfg = {};
    ccfg.intr_priority = NETSTREAM_ECHO_INTR_PRIO;
    ccfg.flags.update_cmp_on_tez = 1;
    ccfg.flags.update_cmp_on_sync = 1;         // the echo resets the counter - new places load there too
    mcpwm_generator_config_t gcfg = {};
    gcfg.gen_gpio_num = PIN_CKI;
    mcpwm_gpio_sync_src_config_t scfg = {};
    scfg.group_id = 0;
    scfg.gpio_num = PIN_CKO;
    scfg.flags.active_neg = 1;                 // the falling CLOCK OUT edge
    scfg.flags.pull_up = 1;                    // the 74LS07 output is open collector
    mcpwm_comparator_event_callbacks_t cbs = {};
    cbs.on_reach = netstream_echo_bit_cb;
    mcpwm_timer_sync_phase_config_t ph = {};
    ph.count_value = 0;
    ph.direction = MCPWM_TIMER_DIRECTION_UP;
    uint32_t cb = base_cb;
    const char *step = "timer";
    if (mcpwm_new_timer(&tcfg, &s_echo.timer) != ESP_OK)
        goto fail;
#if CONFIG_IDF_TARGET_ESP32
    for (int i = 0; i < 3; i++)                // which hardware timer the driver gave us (up mode keeps period - 1)
        if (MCPWM0.timer[i].timer_cfg0.timer_period == period - 1)
        {
            s_echo.tid = i;
            break;
        }
#endif
    step = "operator";
    if (mcpwm_new_operator(&ocfg, &s_echo.oper) != ESP_OK || mcpwm_operator_connect_timer(s_echo.oper, s_echo.timer) != ESP_OK ||
        mcpwm_new_operator(&ocfg, &s_echo.oper2) != ESP_OK || mcpwm_operator_connect_timer(s_echo.oper2, s_echo.timer) != ESP_OK)
        goto fail;
    step = "comparator";
    if (mcpwm_new_comparator(s_echo.oper, &ccfg, &s_echo.ca) != ESP_OK ||
        mcpwm_new_comparator(s_echo.oper, &ccfg, &s_echo.cb) != ESP_OK ||
        mcpwm_new_comparator(s_echo.oper2, &ccfg, &s_echo.cc) != ESP_OK ||
        mcpwm_comparator_register_event_callbacks(s_echo.cc, &cbs, nullptr) != ESP_OK)
        goto fail;
    netstream_echo_place(base_cb);
    step = "generator";
    if (mcpwm_new_generator(s_echo.oper, &gcfg, &s_echo.gen) != ESP_OK ||
        mcpwm_generator_set_action_on_compare_event(s_echo.gen,
            MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, s_echo.ca, MCPWM_GEN_ACTION_HIGH)) != ESP_OK ||
        mcpwm_generator_set_action_on_compare_event(s_echo.gen,
            MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, s_echo.cb, MCPWM_GEN_ACTION_LOW)) != ESP_OK)
        goto fail;
    step = "sync";
    if (mcpwm_new_gpio_sync_src(&scfg, &s_echo.sync) != ESP_OK)
        goto fail;
    ph.sync_src = s_echo.sync;
    if (mcpwm_timer_set_phase_on_sync(s_echo.timer, &ph) != ESP_OK)
        goto fail;
    step = "start";
    s_echo.active = true;                      // before the first interrupt
    if (mcpwm_timer_enable(s_echo.timer) != ESP_OK)
        goto fail;
    s_echo.enabled = true;
    if (mcpwm_timer_start_stop(s_echo.timer, MCPWM_TIMER_START_NO_STOP) != ESP_OK)
        goto fail;
    cb = netstream_echo_scan(base_cb, n);
    netstream_echo_place(cb);
    esp_rom_delay_us((uint32_t)(4 * tbit / 80.0));
    s_echo.hold = false;                       // data from now on
    Debug_printf("NETSTREAM: echo clock - %d PHI2 cycles per bit (%s) - %d Hz - high %d cycles\n",
                 n, pal ? "PAL" : "NTSC", (int)(phi2 / n + 0.5), hc);
    return (int)(phi2 / n + 0.5);
fail:
    Debug_printf("NETSTREAM: echo clock %s setup failed - LEDC clock\n", step);
    netstream_echo_free();
    return 0;
}

// Our GPIO (echo clock data) or the UART TX on the SIO DATA IN pin - our GPIO only while MOTOR is on
static void netstream_echo_attach_data(bool attach)
{
    if (!s_echo.active || attach == s_echo.data_attached)
        return;
    int pin = s_echo.pin;
    if (attach)
    {
        netstream_echo_level(1);               // mark until the next bit
        esp_rom_gpio_pad_select_gpio(pin);
        esp_rom_gpio_connect_out_signal(pin, SIG_GPIO_OUT_IDX, false, false);
        gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
    }
    else
        uart_set_pin(PIN_BUS_UART, pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    s_echo.data_attached = attach;
}

bool sioNetStream::echo_start(int baud)
{
    int r = netstream_echo_start(baud, netstream_video_pal);
    if (r == 0)
        return false;
    netstream_baud = r;                        // the UART receives with the real line rate
    echo_active = true;
    return true;
}

void sioNetStream::echo_stop()
{
    if (!echo_active)
        return;
    netstream_echo_attach_data(false);
    Debug_printf("NETSTREAM: echo clock stop - %lu line bits - bit interrupt late max %lu us\n",
                 (unsigned long)s_echo.bits, (unsigned long)(s_echo.late_max / 80));
    netstream_echo_free();
    echo_active = false;
}
#endif /* NETSTREAM_ECHO_CLOCK */

void sioNetStream::sio_netstream_motor(bool asserted)
{
#ifdef NETSTREAM_ECHO_CLOCK
    if (echo_active)
        netstream_echo_attach_data(asserted);
#else
    (void)asserted;
#endif
}

// One byte toward the Atari - false when it has to wait (the echo clock queue is full)
bool sioNetStream::netstream_out_byte(uint8_t c)
{
#ifdef NETSTREAM_ECHO_CLOCK
    if (echo_active)
        return netstream_echo_push(c);
#endif
    SYSTEM_BUS.write(&c, 1);
    return true;
}

bool sioNetStream::ensure_netstream_ready()
{
    if (netstreamMode == NetStreamMode::UDP)
        return true;
    if (netStreamTcp.connected())
        return true;
    if (netstream_host_ip == IPADDR_NONE || netstream_port <= 0)
        return false;
    if (!netStreamTcp.connect(netstream_host_ip, (uint16_t)netstream_port))
    {
#ifdef DEBUG_NETSTREAM
        Debug_println("NETSTREAM: TCP connect failed");
#endif
        return false;
    }
    netStreamTcp.setNoDelay(true);
    if (netstreamRegisterEnabled)
    {
        const char* str = "REGISTER";
        netStreamTcp.write((const uint8_t *)str, strlen(str));
    }
    buf_stream_index = 0;
    return true;
}

// Inter-byte gap (us) matching the negotiated baud (8N1 => 10 bits/byte).
// The hub's credit flow-control already paces writes to the true line rate, so this
// gap just smooths delivery; fall back to the fixed constants if baud is unknown.
static uint32_t netstream_gap_us_for_baud(int baud, int port)
{
    if (baud > 0)
        return (uint32_t)(10000000u / (uint32_t)baud);
    return (port == MIDI_PORT) ? NETSTREAM_MIN_GAP_US_MIDI : NETSTREAM_MIN_GAP_US_SIO;
}

void sioNetStream::update_rx_high_water()
{
    if (rx_count > rx_high_water_mark)
        rx_high_water_mark = rx_count;
}

void sioNetStream::log_rx_stats()
{
#ifdef DEBUG_NETSTREAM
    uint64_t now_us = netstream_time_us();
    if ((now_us - last_rx_stats_us) < NETSTREAM_RX_STATS_INTERVAL_US)
        return;
    last_rx_stats_us = now_us;
    Debug_printf("NETSTREAM RX stats: drops=%lu high_water=%u/%u count=%u\n",
                 (unsigned long)rx_drop_count,
                 rx_high_water_mark,
                 NETSTREAM_RX_RING_SIZE,
                 rx_count);
#endif
}

void sioNetStream::enqueue_rx(const uint8_t *data, int len)
{
    for (int i = 0; i < len; i++)
    {
        if (rx_count < NETSTREAM_RX_RING_SIZE)
        {
            rx_ring[rx_head] = data[i];
            rx_head = (rx_head + 1) % NETSTREAM_RX_RING_SIZE;
            rx_count++;
        }
        else
        {
            // Drop oldest byte to keep the most recent stream data.
            rx_tail = (rx_tail + 1) % NETSTREAM_RX_RING_SIZE;
            rx_ring[rx_head] = data[i];
            rx_head = (rx_head + 1) % NETSTREAM_RX_RING_SIZE;
            rx_drop_count++;
        }
        update_rx_high_water();
    }
}

void sioNetStream::enqueue_frame(const uint8_t *data, int len)
{
    // Framed (lossy UDP) path: buffer a whole datagram as one frame, evicting the
    // OLDEST whole frame(s) when over the depth cap or out of physical ring space.
    // Drops are always whole-frame aligned so the Atari deframer never desyncs.
    if (len <= 0)
        return;
    if (len > NETSTREAM_MAX_FRAME)
    {
        // Framed path is for small real-time datagrams; a frame this large can't be
        // held in inflight[]. Drop it whole (keeps the stream frame-aligned).
        rx_drop_count++;
        return;
    }

    while (frame_count > 0 &&
           (frame_count >= rx_cap_frames ||
            frame_count >= NETSTREAM_RX_FRAME_SLOTS ||
            (int)rx_count + len > NETSTREAM_RX_RING_SIZE))
    {
        uint16_t evicted = frame_len[frame_tail];
        frame_tail = (frame_tail + 1) % NETSTREAM_RX_FRAME_SLOTS;
        frame_count--;
        rx_tail = (rx_tail + evicted) % NETSTREAM_RX_RING_SIZE;
        rx_count -= evicted;
        rx_drop_count++;
    }

    for (int i = 0; i < len; i++)
    {
        rx_ring[rx_head] = data[i];
        rx_head = (rx_head + 1) % NETSTREAM_RX_RING_SIZE;
        rx_count++;
    }
    frame_len[frame_head] = (uint16_t)len;
    frame_head = (frame_head + 1) % NETSTREAM_RX_FRAME_SLOTS;
    frame_count++;
    update_rx_high_water();
}

void sioNetStream::drain_net_to_ring()
{
    if (netstreamMode == NetStreamMode::UDP)
    {
        // Pull every datagram already queued, not just one, so a burst that arrived
        // while we were busy doesn't pile up in the kernel UDP receive buffer.
        int guard = 0;
        int packetSize;
        while ((packetSize = netStreamUdp.parsePacket()) > 0 && guard++ < NETSTREAM_RX_DRAIN_MAX_PACKETS)
        {
            netStreamUdp.read(buf_net, NETSTREAM_BUFFER_SIZE);
            if (rx_framed)
                enqueue_frame(buf_net, packetSize);
            else
                enqueue_rx(buf_net, packetSize);
            last_rx_us = netstream_time_us();
#ifdef DEBUG_NETSTREAM
            Debug_printf("STREAM-IN [%llu ms]: ", (unsigned long long)(netstream_time_us() / 1000ULL));
            util_dump_bytes(buf_net, packetSize);
#endif
        }
    }
    else if (ensure_netstream_ready())
    {
        while (rx_count + NETSTREAM_RX_BACKPRESSURE_RESERVE < NETSTREAM_RX_RING_SIZE)
        {
            size_t free_space = NETSTREAM_RX_RING_SIZE - rx_count;
            if (free_space <= NETSTREAM_RX_BACKPRESSURE_RESERVE)
                break;
            free_space -= NETSTREAM_RX_BACKPRESSURE_RESERVE;
#ifdef ESP_PLATFORM
            size_t to_read = (free_space > NETSTREAM_BUFFER_SIZE) ? NETSTREAM_BUFFER_SIZE : free_space;
            int packetSize = recv(netStreamTcp.fd(), (char *)buf_net, to_read, MSG_DONTWAIT);
            if (packetSize <= 0)
            {
                if (packetSize == 0)
                    netStreamTcp.stop();
                else if (errno != EWOULDBLOCK && errno != EAGAIN)
                    netStreamTcp.stop();
                break;
            }
#else
            size_t available = netStreamTcp.available();
            if (available == 0)
                break;
            size_t to_read = (available > NETSTREAM_BUFFER_SIZE) ? NETSTREAM_BUFFER_SIZE : available;
            if (to_read > free_space)
                to_read = free_space;
            int packetSize = netStreamTcp.read(buf_net, to_read);
            if (packetSize <= 0)
                break;
#endif
            enqueue_rx(buf_net, packetSize);
            last_rx_us = netstream_time_us();
#ifdef DEBUG_NETSTREAM
            Debug_printf("STREAM-IN [%llu ms]: ", (unsigned long long)(netstream_time_us() / 1000ULL));
            util_dump_bytes(buf_net, packetSize);
#endif
        }
    }
    log_rx_stats();
}

void sioNetStream::pace_to_atari(uint32_t min_gap_us)
{
    // Pacing to keep NET->SIO output moving even when other paths wait.
    // Cap bytes per call so a backlog can catch up without holding the SIO service
    // loop long enough to delay CMD-assert detection. The gap (and the hub's credit
    // flow-control) still bound the actual rate, so this can't overrun the Atari.
    uint8_t send_count = 0;

    if (rx_framed)
    {
        // Framed path: send the in-flight frame byte-by-byte; load the next whole frame
        // out of the ring when the current one finishes. The in-flight frame lives in
        // inflight[] (outside the droppable queue), so it is never split by an eviction.
        while (send_count < NETSTREAM_PACE_MAX_PER_CALL)
        {
            if (inflight_pos >= inflight_len)
            {
                if (frame_count == 0)
                    break; // nothing buffered
                int L = frame_len[frame_tail];
                frame_tail = (frame_tail + 1) % NETSTREAM_RX_FRAME_SLOTS;
                frame_count--;
                for (int j = 0; j < L; j++)
                {
                    inflight[j] = rx_ring[rx_tail];
                    rx_tail = (rx_tail + 1) % NETSTREAM_RX_RING_SIZE;
                    rx_count--;
                }
                inflight_len = L;
                inflight_pos = 0;
                if (L == 0)
                    continue;
            }
            uint64_t now_us = netstream_time_us();
            if (!echo_paced() && (now_us - last_tx_us) < min_gap_us)
                break;
            if (!netstream_out_byte(inflight[inflight_pos]))
                break;
            inflight_pos++;
            last_tx_us += min_gap_us;
            send_count++;
        }
        return;
    }

    // Byte path (lossless MIDI/TCP): drain the ring directly.
    while (rx_count > 0 && send_count < NETSTREAM_PACE_MAX_PER_CALL)
    {
        uint64_t now_us = netstream_time_us();
        if (!echo_paced() && (now_us - last_tx_us) < min_gap_us)
            break;
        if (!netstream_out_byte(rx_ring[rx_tail]))
            break;
        rx_tail = (rx_tail + 1) % NETSTREAM_RX_RING_SIZE;
        rx_count--;
        last_tx_us += min_gap_us;
        send_count++;
    }
}

void sioNetStream::sio_enable_netstream()
{
    int baud = 0;

    // Disable cassette so it doesn't interfere with SIO Motor Control toggle
    if (SYSTEM_BUS.getCassette() != nullptr)
    {
        cassette_was_active = SYSTEM_BUS.getCassette()->is_active();
        if (cassette_was_active)
            SYSTEM_BUS.getCassette()->sio_disable_cassette();
    }
    else
    {
        cassette_was_active = false;
    }

    if (netstream_has_audf3)
        baud = netstream_baud_from_audf3(netstream_audf3, netstream_video_pal);
    if (baud <= 0 && netstream_port == MIDI_PORT)
        baud = MIDI_BAUDRATE;
    if (baud <= 0)
        baud = SIO_STANDARD_BAUDRATE;

    netstream_baud = baud;
    // Don't set baud until MOTOR asserted
    //SYSTEM_BUS.set_baudrate(netstream_baud);

#ifdef DEBUG_NETSTREAM
    Debug_printf("NETSTREAM baud: %d (AUDF3=%u %s)\n",
                 netstream_baud,
                 netstream_audf3,
                 netstream_video_pal ? "PAL" : "NTSC");
#endif

#ifdef ESP_PLATFORM
    if (netstream_tx_clock_external || netstream_rx_clock_external)
    {
        bool synced = false;
#ifdef NETSTREAM_ECHO_CLOCK
        // The Atari receives with our clock - the data must come with the same clock
        if (netstream_rx_clock_external && !SYSTEM_BUS.isBoIP())
            synced = echo_start(netstream_baud);
#endif
        if (!synced)
            netstream_enable_clock_pwm(netstream_baud);
    }
#endif

    if (netstreamMode == NetStreamMode::UDP)
    {
        // Open the UDP connection. Use ephemeral port when targeting localhost.
        uint32_t host_ip = ntohl((uint32_t)netstream_host_ip);
        bool is_loopback = (host_ip & 0xFF000000u) == 0x7F000000u;
        uint16_t local_port = is_loopback ? 0 : (uint16_t)netstream_port;
        netStreamUdp.begin(local_port);
        if (netstreamRegisterEnabled)
        {
            const char* str = "REGISTER";
            netStreamUdp.beginPacket(netstream_host_ip, netstream_port); // remote IP and port
            netStreamUdp.write((const uint8_t *)str, strlen(str));
            netStreamUdp.endPacket();
        }
    }
    else
    {
        // Open the TCP connection
        ensure_netstream_ready();
    }

    netstreamActive = true;
    last_rx_us = netstream_time_us();
    last_tx_us = last_rx_us;
    rx_head = 0;
    rx_tail = 0;
    rx_count = 0;
    rx_high_water_mark = 0;
    rx_drop_count = 0;
    last_rx_stats_us = last_rx_us;

    // Lossy, frame-aligned shallow buffering applies only to UDP non-MIDI streams;
    // MIDI and TCP stay lossless (full byte ring, no proactive drops).
    rx_framed = (netstreamMode == NetStreamMode::UDP) && (netstream_port != MIDI_PORT);
    int cfg_depth = Config.get_network_netstream_rx_depth();
    int depth = (cfg_depth > 0) ? cfg_depth : NETSTREAM_RX_MAX_FRAMES;
    if (depth < 2)
        depth = 2; // need >=2 so an old frame can be shed while one is in flight
    if (depth > NETSTREAM_RX_FRAME_SLOTS - 1)
        depth = NETSTREAM_RX_FRAME_SLOTS - 1;
    rx_cap_frames = depth;
    frame_head = 0;
    frame_tail = 0;
    frame_count = 0;
    inflight_len = 0;
    inflight_pos = 0;

    Debug_println("NETSTREAM mode ENABLED");
}

void sioNetStream::sio_disable_netstream()
{
    netStreamTcp.stop();
    netStreamUdp.stop();
    if (cassette_was_active && SYSTEM_BUS.getCassette() != nullptr && SYSTEM_BUS.getCassette()->is_mounted())
        SYSTEM_BUS.getCassette()->sio_enable_cassette();
    cassette_was_active = false;
#ifdef NETSTREAM_ECHO_CLOCK
    if (echo_active)
        echo_stop();
    else
#endif
#ifdef ESP_PLATFORM
        ledc_stop(LEDC_ESP32XX_HIGH_SPEED, LEDC_CHANNEL_1, 0);
#endif
        SYSTEM_BUS.setBaudrate(SIO_STANDARD_BAUDRATE);
#ifdef ESP_PLATFORM
    // Reset CKI pin back to output open drain high
    fnSystem.set_pin_mode(PIN_CKI, gpio_mode_t::GPIO_MODE_OUTPUT_OD);
    fnSystem.digital_write(PIN_CKI, DIGI_HIGH);
#endif
    netstreamActive = false;
    Debug_println("NETSTREAM mode DISABLED");
}

void sioNetStream::sio_handle_netstream()
{
    const uint32_t min_gap_us = netstream_gap_us_for_baud(netstream_baud, netstream_port);
    uint64_t batch_start_us = 0;
    bool batch_active = false;

    auto flush_udp_out_batch = [&]()
    {
        if (buf_stream_index == 0)
            return;

        if (netstreamMode == NetStreamMode::UDP)
        {
            if (netstream_host_ip == IPADDR_NONE || netstream_port <= 0)
            {
                buf_stream_index = 0;
                batch_active = false;
                batch_start_us = 0;
                return;
            }
            netStreamUdp.beginPacket(netstream_host_ip, netstream_port); // remote IP and port
            netStreamUdp.write(buf_stream, buf_stream_index);
            netStreamUdp.endPacket();
        }
        else
        {
            if (!ensure_netstream_ready())
            {
                buf_stream_index = 0;
                batch_active = false;
                batch_start_us = 0;
                return;
            }
            netStreamTcp.write(buf_stream, buf_stream_index);
        }

#ifdef DEBUG_NETSTREAM
        Debug_printf("STREAM-OUT [%llu ms]: ", (unsigned long long)(netstream_time_us() / 1000ULL));
        util_dump_bytes(buf_stream, buf_stream_index);
#endif

        buf_stream_index = 0;
        batch_active = false;
        batch_start_us = 0;
    };

    // Pull all queued inbound datagrams into the ring (drop-oldest on overflow).
    drain_net_to_ring();

    pace_to_atari(min_gap_us);

    // Read the data until there's a pause in the incoming stream
    if (SYSTEM_BUS.available() > 0)
    {
        while (true)
        {
            // Break out of NetStream mode if COMMAND is asserted
#ifdef ESP_PLATFORM
            if (fnSystem.digital_read(PIN_CMD) == DIGI_LOW)
#else
            if (SYSTEM_BUS.commandAsserted())
#endif
            {
                Debug_println("CMD Asserted, stopping NetStream");
                sio_disable_netstream();
                return;
            }
            if (SYSTEM_BUS.available() > 0)
            {
                // Collect bytes read in our buffer
                int in_byte = SYSTEM_BUS.read(); // TODO apc: check for error first
                if (!batch_active)
                {
                    batch_start_us = netstream_time_us();
                    batch_active = true;
                }
                buf_stream[buf_stream_index] = (unsigned char)in_byte;
                if (buf_stream_index < NETSTREAM_BUFFER_SIZE - 1)
                    buf_stream_index++;
                if (buf_stream_index >= NETSTREAM_FLUSH_THRESHOLD)
                {
                    // Flush when nearly full to avoid overwrite/drops.
                    flush_udp_out_batch();
                    continue;
                }
                if (batch_active && (netstream_time_us() - batch_start_us) >= NETSTREAM_MAX_BATCH_AGE_US)
                {
                    // Flush old batches even if the stream never pauses.
                    flush_udp_out_batch();
                    continue;
                }
            }
            else
            {
                // Short, bounded waits prevent starving the pacing loop.
                const uint32_t wait_step_us = 250;
                const uint32_t wait_budget_us = 10000;
                uint32_t waited_us = 0;
                bool flushed = false;
                while (waited_us < wait_budget_us && SYSTEM_BUS.available() <= 0)
                {
                    fnSystem.delay_microseconds(wait_step_us);
                    waited_us += wait_step_us;
                    // Keep refilling from the network so inbound isn't starved while
                    // we wait out an outbound burst (else datagrams pile up upstream).
                    drain_net_to_ring();
                    pace_to_atari(min_gap_us);
                    if (buf_stream_index >= NETSTREAM_FLUSH_THRESHOLD ||
                        (batch_active && (netstream_time_us() - batch_start_us) >= NETSTREAM_MAX_BATCH_AGE_US))
                    {
                        flush_udp_out_batch();
                        flushed = true;
                        break;
                    }
                }
                if (flushed && SYSTEM_BUS.available() > 0)
                    continue;
                if (SYSTEM_BUS.available() <= 0)
                    break;
            }
        }

        // Send what we've collected after a pause.
        flush_udp_out_batch();
    }

    // Pace again after serial->UDP handling to avoid starving during outbound bursts.
    pace_to_atari(min_gap_us);
}

void sioNetStream::sio_status(const FujiSIOPacket &packet)
{
    // Nothing to do here
    return;
}

void sioNetStream::sio_process(const FujiSIOPacket &packet)
{
    // Nothing to do here
    return;
}

#endif /* BUILD_ATARI */

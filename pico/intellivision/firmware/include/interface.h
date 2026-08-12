#ifndef INTERFACE_H_
#define INTERFACE_H_

#if defined(PIRTO)
   #define resetLow()  gpio_set_dir(RESET,true); gpio_put(RESET,false);  // Minty to INTV BUS ; RESET Output set to 0
   #define resetHigh() gpio_set_dir(RESET,true); gpio_put(RESET,true);   // RESET is INPUT; B->A, INTV BUS to Pico
#else
   #define resetLow()  gpio_set_dir(RESET,true); gpio_put(RESET,true);    // Minty to INTV BUS ; RESET Output set to 0
   #define resetHigh() gpio_set_dir(RESET,true); gpio_put(RESET,false);   // RESET is INPUT; B->A, INTV BUS to Pico
#endif

#define GPIO_GET_LOW_32(v)    pico_default_asm_volatile ("mrc p0, #0, %0, c0, c8" : "=r" (v));

// Inty bus values (BC1+BC2+BDIR) GPIO 18-17-16
#define BUS_NACT  0b000         //0
#define BUS_BAR   0b001         //1
#define BUS_IAB   0b010         //2
#define BUS_DWS   0b011         //3
#define BUS_ADAR  0b100         //4
#define BUS_DW    0b101         //5
#define BUS_DTB   0b110         //6
#define BUS_INTAK 0b111         //7

#define COMPILER_BARRIER() asm volatile("" ::: "memory")

#define BDIR_MASK   ((uint32_t)1 << BDIR)
#define BC1_MASK    ((uint32_t)1 << BC1)
#define BC2_MASK    ((uint32_t)1 << BC2)
#define LED_MASK    ((uint32_t)1 << LED)
#ifdef DIRC
   #define DIRC_MASK	   ((uint32_t)1 << DIRC)
#endif
#define DATA_MASK   0x0000FFFFL
#define BUS_STATE_MASK  (BDIR_MASK | BC1_MASK | BC2_MASK)
#define ALWAYS_IN_MASK  (BUS_STATE_MASK)
#ifdef DIRC
   #define ALWAYS_OUT_MASK (LED_MASK | DIRC_MASK)
#else
   #define ALWAYS_OUT_MASK (LED_MASK)
#endif

#ifdef DIRC
   #define SET_DATA_MODE_OUT   do {gpio_clr_mask(DIRC_MASK); gpio_set_dir_out_masked(DATA_MASK);} while (0)
   #define SET_DATA_MODE_IN    do {gpio_set_dir_in_masked(DATA_MASK); gpio_set_mask(DIRC_MASK);} while (0)
#else
   #define SET_DATA_MODE_OUT     sio_hw->gpio_oe_set = DATA_MASK; 
   #define SET_DATA_MODE_IN      sio_hw->gpio_oe_clr = DATA_MASK;
#endif

#define DATA_OUT(v) sio_hw->gpio_togl = (sio_hw->gpio_out ^ v) & 0xFFFF;

void resetCart(void);

#endif

#ifdef PINMAP_A2_D32PRO

/* SD Card */
// pins 12-15 are used to interface with the JTAG debugger
// so leave them alone if we're using JTAG
#ifndef JTAG
#define PIN_CARD_DETECT         GPIO_NUM_12 // fnSystem.h
#define PIN_CARD_DETECT_FIX     GPIO_NUM_15 // fnSystem.h
#endif

#define PIN_SD_HOST_CS          GPIO_NUM_4  // LOLIN D32 Pro
#define PIN_SD_HOST_MISO        GPIO_NUM_19
#define PIN_SD_HOST_MOSI        GPIO_NUM_23
#define PIN_SD_HOST_SCK         GPIO_NUM_18

/* UART */
#define PIN_UART0_RX            GPIO_NUM_3  // fnUART.cpp
#define PIN_UART0_TX            GPIO_NUM_1
#define PIN_UART1_RX            GPIO_NUM_9
#define PIN_UART1_TX            GPIO_NUM_10
#define PIN_UART2_RX            GPIO_NUM_33
#define PIN_UART2_TX            GPIO_NUM_21

/* Buttons */
#define PIN_BUTTON_A            GPIO_NUM_NC  // keys.cpp
#define PIN_BUTTON_B            GPIO_NUM_NC
#define PIN_BUTTON_C            GPIO_NUM_NC

/* LEDs */
#define PIN_LED_WIFI            GPIO_NUM_2 // led.cpp
#define PIN_LED_BUS             GPIO_NUM_5 // 4 FN

// pins 12-15 are used to interface with the JTAG debugger
// so leave them alone if we're using JTAG
#ifndef JTAG
#define PIN_LED_BT              GPIO_NUM_13
#else
#define PIN_LED_BT              GPIO_NUM_5  // LOLIN D32 PRO
#endif

/* Audio Output */
#define PIN_DAC1                GPIO_NUM_25 // samlib.h

/* IWM Bus Pins */
//      SP BUS                  GPIO
//      ---------               -----------
#define SP_PHI0                 GPIO_NUM_36
#define SP_PHI1                 GPIO_NUM_39
#define SP_PHI2                 GPIO_NUM_34
#define SP_PHI3                 GPIO_NUM_32
#define SP_WREQ                 GPIO_NUM_33
#define SP_ENABLE               GPIO_NUM_25
#define SP_RDDATA               GPIO_NUM_26
#define SP_WRDATA               GPIO_NUM_27
#define SP_WRPROT               GPIO_NUM_13

/* SP_PHIn pins must all be in same GPIO register because
   GPIO register is hardcoded in IWM_PHASE_COMBINE. */
#define IWM_PHASE_SHIFT(val, phase) ({				\
      uint32_t _offset = SP_PHI##phase % 32;	\
      uint32_t _mask = 1 << _offset;				\
      uint32_t _bit = (val) & _mask;				\
      int32_t _shift = _offset - phase;				\
      _shift < 0 ? _bit << -_shift : _bit >> _shift;		\
    })
#define IWM_PHASE_COMBINE() ({uint32_t _val = GPIO.in1.val;	\
      (uint8_t) (IWM_PHASE_SHIFT(_val, 0)			\
		 | IWM_PHASE_SHIFT(_val, 1)			\
		 | IWM_PHASE_SHIFT(_val, 2)			\
		 | IWM_PHASE_SHIFT(_val, 3));			\
    })

// TODO: go through each line and make sure the code is OK for each one before moving to next
#define SP_DRIVE2               GPIO_NUM_22
#define SP_EN35                 GPIO_NUM_21
#define SP_HDSEL                GPIO_NUM_NC

/* Aliases of other pins */
#define SP_DRIVE1               SP_ENABLE
#define SP_REQ                  SP_PHI0
#define SP_ACK                  SP_WRPROT

#define SP_EXTRA                SP_DRIVE2 // For extra debugging with logic analyzer
#endif /* PINMAP_A2_D32PRO */

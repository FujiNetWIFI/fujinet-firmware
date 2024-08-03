#include <hardware/uart.h>

#include "esp8266.h"
#include "plusrom.h"

#if USE_WIFI

#define UART_DR            uart0_hw->dr
#define UART_RX_AVAIL      !(uart0_hw->fr & UART_UARTFR_RXFE_BITS)
#define UART_TX_BUSY       (uart0_hw->fr & UART_UARTFR_BUSY_BITS)
#define UART_FIFO_TX_FULL  (uart0_hw->fr & UART_UARTFR_TXFF_BITS)

volatile uint8_t __not_in_flash() receive_buffer_write_pointer, receive_buffer_read_pointer;
volatile uint8_t __not_in_flash() out_buffer_write_pointer, out_buffer_send_pointer;
uint8_t __not_in_flash() receive_buffer[256], out_buffer[256];
volatile uint8_t content_counter = 0;
volatile uint8_t prev_c = 0, prev_prev_c = 0, c = 0, len = 0;
volatile uint16_t i;
volatile uint16_t content_len;
volatile enum Transmission_State __not_in_flash() uart_state;

void __time_critical_func(handle_plusrom_comms)(void) {

   int16_t http_header_length;
   int content_length_pos;

   receive_buffer_read_pointer = 0;
   receive_buffer_write_pointer = 0;
   out_buffer_send_pointer = 0;
   out_buffer_write_pointer = 0;

   http_header_length = strlen(http_request_header);
   content_length_pos = http_header_length - 5;

   //dbg("plusrom - uart_state before while: %d\r\n", uart_state);

   while(true) {

      if(uart_state == Close_Rom)
         break;

      if(uart_state == No_Transmission)
         continue;

      // send request

      //dbg("plusrom - send start\r\n");
      i = 0;
      content_len = out_buffer_write_pointer;
      content_len++;

      if(content_len == 0) {
         http_request_header[content_length_pos] = (char)'0';
      } else {
         i = content_length_pos;

         while(content_len != 0) {
            c = (uint8_t)(content_len % 10);
            http_request_header[i--] = (char)(c + '0');
            content_len = content_len/10;
         }
      }

      for(int b=0; b<http_header_length; b++) {
         while(UART_FIFO_TX_FULL) { }

         UART_DR = http_request_header[b];
      }

      for(int b=0; b<(out_buffer_write_pointer - out_buffer_send_pointer + 1); b++) {
         while(UART_FIFO_TX_FULL) { }

         UART_DR = out_buffer[out_buffer_send_pointer + b];
      }

      while(UART_TX_BUSY) { }

      out_buffer_write_pointer = 0;
      out_buffer_send_pointer = 0;
      //dbg("plusrom - send done\r\n");

      // receive response

      //dbg("plusrom - recv start\r\n");
      // skip HTTP header (jump to first byte of payload)
      while(true) {
         if(!UART_RX_AVAIL)
            continue;

         c = (uint8_t) UART_DR;

         if(c == '\n' && c == prev_prev_c)
            break;
         else {
            prev_prev_c = prev_c;
            prev_c = c;
         }
      }

      // read payload len (first byte)
      while(!UART_RX_AVAIL) { }

      len = (uint8_t) UART_DR;

      // read payload
      if(len != 0) {
         for(int b=0; b<len; b++) {
            while(!UART_RX_AVAIL) { }

            receive_buffer[receive_buffer_write_pointer++] = (uint8_t) UART_DR;
         }
      }

      http_request_header[content_length_pos - 1] = ' ';
      http_request_header[content_length_pos - 2] = ' ';
      content_counter = 0;
      len = c = prev_c = prev_prev_c = 0;
      //dbg("plusrom - recv done\r\n");

   } // end while

   //dbg("plusrom - uart_state after while: %d\r\n", uart_state);
}
#endif

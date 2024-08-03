#ifndef PLUSROM_H_
#define PLUSROM_H_

enum Transmission_State {
   No_Transmission,
   Send_Start,
   Send_Prepare_Header,
   Send_Header,
   Send_Content,
   Send_Finished,
   Receive_Header,
   Receive_Length,
   Receive_Content,
   Receive_Finished,
   Close_Rom
};

extern volatile uint8_t receive_buffer_read_pointer, receive_buffer_write_pointer;
extern volatile uint8_t out_buffer_write_pointer, out_buffer_send_pointer;
extern uint8_t receive_buffer[256], out_buffer[256];
extern volatile enum Transmission_State uart_state;

void handle_plusrom_comms(void);

#endif

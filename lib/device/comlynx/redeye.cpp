#ifdef BUILD_LYNX
#include "netstream.h"
#include "../../bus/comlynx/comlynx.h"

#include "../../include/debug.h"
#include "../../include/pinmap.h"

#include "fnSystem.h"
#include "utils.h"

//#define DEBUG_NETSTREAM

GAME_LIST_T game_list[] = {
	{0x0000, 2, "Bill and Ted's Excellent Adventure"},				// standard redeye games
	{0x0001, 4, "Gauntlet: The Third Encounter"},					// seems to switch baud rate in game mode
	{0x0002, 4, "Zalor Mercenary"},
	{0x0004, 4, "Xenophobe"},
	{0x0005, 8, "Todd's Adventure in Slime World"},
	{0x0006, 2, "Robosquash"},
	{0x0007, 4, "Warbirds"},
	{0x001E, 2, "Turbo Sub"},
	{0x0020, 2, "Basketbrawl"},
	{0x0028, 2, "World Class Soccer"},
	{0x0030, 2, "Hockey"},
	{0x0053, 2, "Shanghai"},
	{0x00C8, 6, "Checkered Flag"},
	{0x00D2, 2, "Rampart"},
	{0x00FF, 2, "Xybots"},
	{0x029A, 2, "Joust"},
	{0x0EFE, 2, "Road Riot 4WD"},
	{0x1313, 2, "Supersqweek"},
	{0x1355, 4, "Rampage"},
	{0x2050, 2, "Baseball Heroes"},
	{0x7000, 6, "Battle Wheels"},
	{0xB0B0, 2, "NFL Football"},
	{0xBABE, 2, "Raiden"},
	{0xDAD0, 4, "Tournament Cyberball"},							// may also switch baud rate?
	// Remapped from 0xFFFF in Fujinet Firmware
	{0xE001, 2, "Double Dragon"},									// remapped from 0xFFFF games
	{0xE002, 2, "European Soccer"},
	{0xE003, 2, "Lynx Casino"},
	{0xE004, 2, "Pit Fighter"},
	{0xE005, 2, "Relief Pitcher"},
	{0xE006, 4, "Super Off-Road"},
	// Non-Redeye Games
	{0xE101, 4, "Awesome Golf"},									// games that don't use redeye (shouldn't ever see these in this server)
	{0xE102, 4, "Battlezone 2000"},									// including for completeness as these IDs are used in the
	{0xE103, 4, "California Games"},								// Fujinet firmware
	{0xE104, 6, "Championship Rally"},
	{0xE105, 2, "Fidelity Ulimate Chess Challenge"},
	{0xE106, 4, "Hyperdrome"},
	{0xE107, 4, "Jimmy Connor's Tennis"},
	{0xE108, 2, "Loopz"},
	{0xE109, 2, "Lynx Othello"},
	{0xE10A, 4, "Malibu Bikini Volleyball"},
	{0xE10B, 2, "Ponx"},
	// Generic ID used in some games (see remap above)
	{0xFFFF, 2, "Generic game ID"}
};


void lynxNetStream::comlynx_handle_redeye_netstream() {

	redeye_check_logon_state();

	// Get data from network
	int packetSize = netStream.parsePacket();
	if ((packetSize > 2) && (packetSize < 10)) {	// good packetsize is at least 3, and in practice less than 10
		netStream.read(buf_net, NETSTREAM_BUFFER_SIZE);

		#ifdef DEBUG_NETSTREAM 
		Debug_print("Netstream Redeye FROM NET: ");
 		util_dump_bytes(buf_net, packetSize);
 		#endif // validate this is a good redeye packet

 		if (redeye_validate_packet(buf_net, packetSize)) {
			if (game.state.logon)
				redeye_process_logon_packet_from_net(buf_net);
 			else
 				redeye_process_game_packet_from_net(buf_net);
 		}
 	}

	// Collect data from serial bus
	// serial collect loop, waiting until the serial has been idle for IDLE_TIME (2-3 char time at 62500 baud)
	buf_stream_index = 0;
 	if (SYSTEM_BUS.available() > 0) {											// is there something availabe in FIFO
 		uint64_t last_rx = esp_timer_get_time();
 		while (true) {
			while (SYSTEM_BUS.available() > 0) { 								// got all data in FIFO
				if (buf_stream_index >= NETSTREAM_BUFFER_SIZE)					// too much data for buffer, just exit (should never hit this)
					break;

				buf_stream[buf_stream_index++] = SYSTEM_BUS.read();				// get byte from FIFO
				last_rx = esp_timer_get_time();									// reset idle timer
 			}

			if (buf_stream_index >= NETSTREAM_BUFFER_SIZE)						// too much data for buffer, just exit (should never hit this)
				break;

			if ((esp_timer_get_time() - last_rx) > COMLYNX_IDLE_TIME)			// data has paused for 2-3 bytes at 62500 baud, end of packet
				break;
 		}
 	}

	if (buf_stream_index == 0)
		return;

 	// parse all packets collected from serial bus (should hopefully only be one)
 	uint16_t index = 0;
 	while (index < buf_stream_index) {
		if (buf_stream[index] == 0) {
			index++;
			continue;
		}
		else
			packetSize = buf_stream[index]+2;				// get the redeye packet size (this is 2 less than what the packet payload is)

 		#ifdef DEBUG_NETSTREAM 
		Debug_print("Netstream Redeye FROM LYNX: ");
 		util_dump_bytes(&buf_stream[index], packetSize);
 		#endif

 		// validate this is a good redeye packet
 		if (redeye_validate_packet(&buf_stream[index], packetSize)) {
			if (game.state.logon)
				redeye_process_logon_packet_from_lynx(&buf_stream[index]);
 			else
 				redeye_process_game_packet_from_lynx(&buf_stream[index]);
 		}

 		index += packetSize;
 	}
}


void lynxNetStream::comlynx_enable_redeye()         // also can be used to reset redeye mode
{
    redeye_mode = true;
    redeye_reset_game();

	#ifdef DEBUG
    Debug_println("NETSTREAM redeye mode ENABLED");
	#endif
}


void lynxNetStream::comlynx_disable_redeye()
{
    redeye_mode = false;
    redeye_reset_game();

	#ifdef DEBUG
    	Debug_println("NETSTREAM redeye mode DISABLED");
	#endif
}


 /* Calculate the checksum of incoming from the lynx redeye packets
    Return true if ok, false if not

    typical message:
    05 00 00 01 FF FF F8

    Checksum is calculated on size, plus message bytes.
 */
 bool lynxNetStream::redeye_checksum(uint8_t *buf)
 {
    uint16_t ck;
    uint8_t i;
    uint8_t size;


    size = buf[0];                         // get message size
    if ((size == 0) || (size > 6)) {       // check packets are in range
        //Debug_printf("checksum size %d %d\n", size, buf[0]);
        return false;
    }

    // checksum caculation is 255 - size - message bytes
    ck = 255;
    for (i=0; i < size+1; i++) {
        ck -= buf[i];
    }

    if ((ck & 0xFF) == buf[size+1])
        return true;
    else
        return false;

 }


/* Recalculate the checksum of the lynx redeye packet.
 * We may have to do this if we have changed anything inside the packet (like game ID)
 *
 *  Checksum is calculated on size byte, plus message bytes.
 */
 void lynxNetStream::redeye_recalculate_checksum(uint8_t *buf)
 {
    uint16_t ck;
    uint8_t i;
    uint8_t size;


    size = buf[0];  // get message size

    // checksum caculation is 255 - size - message bytes
    ck = 255;
    for (i=0; i < size+1; i++) {
        ck -= buf[i];
    }

    // set new checksum on packet
    buf[size+1] = (ck & 0xFF);
    return;
}


/* redeye_remap_game_id
 *
 * Remap certain game IDs (based on GUI setting) so that we
 * have a unique game id for each game.
 *
 * 0xFFFF       0xE001  Relief Pitcher
 * 0xFFFF       0xE002  Pit Fighter
 * 0xFFFF       0xE003  Double Dragon
 * 0xFFFF       0xE004  European Soccer
 * 0xFFFF       0xE005  Lynx Casino
 * 0xFFFF       0xE006  Super Off-Road
 *
 * game = (buf_stream[4]+(buf_stream[5]<<8));
 */
void lynxNetStream::redeye_remap_game_id(uint8_t *buf, uint16_t remap)
{
	// Set new game ID
	buf[4] = game.remap_game_id & 0xFF;
	buf[5] = (game.remap_game_id >> 8) & 0xFF;

    // recalculate checksum
    redeye_recalculate_checksum(buf);
    return;
}


/* redeye_find_game
 *
 * Searches the game list for the game id and returns the index into
 * the list, so that we can lookup name and max players.  Returns 255
 * if the game was not found.
 */
uint8_t lynxNetStream::redeye_find_game(uint16_t gid)
{
	uint8_t i;

  	for(i=0; i<NUM_GAMES; i++) {
    	if (game_list[i].game_id == gid)
      	return(i);	// game found
	}
	return(255);	// game not found
}


/* valid sequence data
 *
 * Check that we have valid data for the players requested (we may not immediately)
 */
bool lynxNetStream::redeye_valid_sequence_data(uint8_t seq, uint8_t player_mask)
{
	uint8_t plr;


	// iterate through player mask
	plr = 0;
	while (player_mask) {
		if ((player_mask & 0x01) && (plr < game.num_players)) {
			if (game.state.seq_plr_data[seq][plr][0] == 0) {
				Debug_printf("NETSTREAM %04X %s --> REQUEST Master resend request seq %d, player mask %08b - valid_sequence_data check failed\n", game.game_id, *game.name, seq, player_mask);
				return false;
			}
  		}

   		player_mask = player_mask >> 1;		// next bit in mask
   		plr++;								// increment player #
	}

  return(true);
}


/* redeye_process_logon_packet_from_net
 *
 *
 * game = (buf_stream[4]+(buf_stream[5]<<8));
 */
void lynxNetStream::redeye_process_logon_packet_from_net(uint8_t *buf)
{
	uint8_t size, msg, plrs, countdown;
	uint16_t gid;


   	// is logon ended, and game starting?
	if (!redeye_check_logon_state())
		return;

	// extract info from packet
	size = buf[0];
	msg = buf[1];
	countdown = buf[2];
	plrs = std::popcount(buf[3]);
	gid = (buf[4]+(buf[5]<<8));

	// Not in Logon state, or game ID mismatch, or packet size mismatch?
	if (!game.state.logon || (size != 5) || (gid != game.game_id))
		return;

	// process logon message
	switch(msg) {
		case 0:			// logon annouce packet
			if (game.num_players != plrs) {
				Debug_printf("NETSTREAM redeye game %04X %s --> Logon new player %d\n", game.game_id, *game.name, countdown);
				game.num_players = plrs;
			}
			break;

		case 2:
			Debug_printf("NETSTREAM redeye game %04X %s --> Game starting in %d\n", game.game_id, *game.name, countdown);

            if (game.state.logon_timer == 0)
				game.state.logon_timer = esp_timer_get_time();
			break;
	}

	// Should we remap the game id? Set it back to 0xFFFF for lynx
	if (game.remap_game_id) {
		redeye_remap_game_id(buf_net, 0xFFFF);
	}

    // Send to Lynx UART
    _comlynx_bus->wait_for_idle();
    SYSTEM_BUS.write(buf, size+2);
    SYSTEM_BUS.read(buf, size+2); 		// Trash what we just sent over serial
}


/* redeye_process_game_packet_from_net
 *
 *
 * game = (buf_stream[4]+(buf_stream[5]<<8));
 */
void lynxNetStream::redeye_process_game_packet_from_net(uint8_t *buf)
{
	uint8_t size, seq, msg, plr;
	uint16_t gid;


	// In logon state
	if (game.state.logon)
		return;

	// Parse header dataq
	size = buf[0]+2;
	msg = buf[1] & 0x07;
	plr = (buf[1] & 0x78) >> 3;
	seq = (buf[1] & 0x80) ? 1 : 0;

	// process game message
	switch(msg) {
		case 0:		// looks like we're back in logon, someone pressed restart?
			if (buf[0] == 5) {
				redeye_reset_game();
				Debug_printf("NETSTREAM redeye %04X %s --> re-entering logon mode\n", game.game_id, *game.name);
				return;
			}
		break;

		case 3: 	// data packet
			game.state.plr_data_recv[seq][plr] = 1;
			memcpy(game.state.seq_plr_data[seq][plr], buf, size);
			Debug_printf("NETSTREAM IN redeye %04X %s --> DATA player %d data for seq %d - header:%08b, data size:%d\n", game.game_id, *game.name, plr, seq, buf[1], size);

			// Deal with sequence switch, all data received and we see a new seq #?
			if (redeye_check_data_recv()) {
				for (uint8_t i=0; i<game.num_players; i++) {
					// clear player data received status for current sequence
					game.state.plr_data_recv[0][i] = 0;
					game.state.plr_data_recv[1][i] = 0;
				}
				Debug_printf("NETSTREAM %04X %s --> SEQ full sequence starting\n", game.game_id, *game.name);
			}
			break;

		case 4:		// SendData Req
			// is this request for us?
			if (plr != game.my_player_num)
				return;

			// do we have the data?
			if (game.state.plr_data_recv[seq][plr]) {
				Debug_printf("NETSTREAM redeye %04X %s --> REQUEST player %d data for seq %d, fujinet has it - header:%08b\n", game.game_id, *game.name, plr, seq, buf[1]);

			    // Send to Lynx UART
			    //_comlynx_bus->wait_for_idle();
			    //SYSTEM_BUS.write(game.state.seq_plr_data[seq][plr], game.state.seq_plr_data[seq][plr][0]+2);
    			//SYSTEM_BUS.read(buf_net, game.state.seq_plr_data[seq][plr][0]+2); 		// Trash what we just sent over serial
				//return;
			}
			else {
				Debug_printf("NETSTREAM redeye %04X %s --> REQUEST player %d data for seq %d, fujinet does not have it - header:%08b\n", game.game_id, *game.name, plr, seq, buf[1]);
			}
			break;

		case 5:		// Master resend req
			// if I'm not master, just ignore
			if (game.my_player_num != 0)
				return;

			if (redeye_valid_sequence_data(seq, buf[2])) {				// fujinet can send if we have valid data (should we just pass through to Lynx?)
				//redeye_master_resend_data_to_net(seq, buf[2]);
				//return;
			}
			break;
	}

    // Send to Lynx UART
    _comlynx_bus->wait_for_idle();
    SYSTEM_BUS.write(buf, size);
    SYSTEM_BUS.read(buf, size); 		// Trash what we just sent over serial
}


/* redeye_process_logon_packet_from_lynx
 *
 *
 * game = (buf_stream[4]+(buf_stream[5]<<8));
 */
void lynxNetStream::redeye_process_logon_packet_from_lynx(uint8_t *buf)
{
	uint8_t size, msg, plrs, countdown;
	uint16_t gid;


    // is logon ended, and game starting?
	if (!redeye_check_logon_state())
		return;

	// extract info from packet
	size = buf[0];
	msg = buf[1];
	countdown = buf[2];
	plrs = std::popcount(buf[3]);
	gid = (buf[4]+(buf[5]<<8));

	if (size != 5)			// malformed packet
		return;

	// process logon message
	switch(msg) {
		case 0:			// logon annouce packet
			// Set game ID and name
			if (gid != game.game_id) {
				game.game_id = gid;
                uint8_t i = redeye_find_game(game.game_id);
				if (i == 255) {
					Debug_printf("NETSTREAM redeye could find game %04X in game list\n", game.game_id);
					return;
				}

				game.max_players = game_list[i].max_players;
				game.name = &game_list[i].name;
                game.num_players = 1;
				Debug_printf("NETSTREAM redeye new game %04X %s\n", game.game_id, *game.name);
			}

			// Set my player number
			game.my_player_num = countdown;
			Debug_printf("NETSTREAM redeye %04X %s ---> My player number: %d\n", game.game_id, *game.name, countdown);

			// Set number of players
			if (game.num_players != plrs) {
				Debug_printf("NETSTREAM redeye %04X %s --> Logon new player %d\n", game.game_id, *game.name, countdown);
				game.num_players = plrs;
			}
			break;

		case 2:
			Debug_printf("NETSTREAM redeye %04X %s --> Game starting in %d\n", game.game_id, *game.name, countdown);

            if (game.state.logon_timer == 0)
				game.state.logon_timer = esp_timer_get_time();
			break;
	}

	// Should we remap the game id? Set it back to 0xFFFF for lynx
	if (game.remap_game_id) {
		redeye_remap_game_id(buf, game.remap_game_id);
	}

    // Send to network
	netStream.beginPacket(netstream_host_ip, netstream_port); // remote IP and port
	netStream.write(buf, size+2);
	netStream.endPacket();
}


/* redeye_process_logon_packet_from_lynx
 *
 *
 * game = (buf_stream[4]+(buf_stream[5]<<8));
 */
void lynxNetStream::redeye_process_game_packet_from_lynx(uint8_t *buf)
{
	uint8_t size, seq, msg, plr;


	// In logon state
	if (game.state.logon)
		return;

	// Parse header dataq
	size = buf[0]+2;
	msg = buf[1] & 0x07;
	plr = (buf[1] & 0x78) >> 3;
	seq = (buf[1] & 0x80) ? 1 : 0;

	// process game message
	switch(msg) {
		case 0:		// looks like we're back in logon, someone pressed restart?
			if (buf[0] == 5) {
				redeye_reset_game();
				Debug_printf("NETSTREAM redeye %04X %s --> re-entering logon mode\n", game.game_id, *game.name);
				return;
			}
		break;

		case 3: 	// data packet
			game.state.plr_data_recv[seq][plr] = 1;
			memcpy(game.state.seq_plr_data[seq][plr], buf, size);
			Debug_printf("NETSTREAM OUT redeye %04X %s --> DATA player %d data for seq %d - header:%02X, data size:%d\n", game.game_id, *game.name, plr, seq, buf[1], size);

			// Deal with sequence switch, all data received and we see a new seq #?
			if (redeye_check_data_recv()) {
				for (uint8_t i=0; i<game.num_players; i++) {
					// clear player data received status for current sequence
					game.state.plr_data_recv[0][i] = 0;
					game.state.plr_data_recv[1][i] = 0;
				}
				Debug_printf("NETSTREAM %04X %s --> SEQ full sequence starting\n", game.game_id, *game.name);
			}
			break;

		case 4:		// SendData Req
			// do we have the data?
			if (game.state.plr_data_recv[seq][plr]) {
				Debug_printf("NETSTREAM OUT redeye %04X %s --> REQUEST player %d data for seq %d, fujinet has it - header:%02X\n", game.game_id, *game.name, plr, seq, buf[1]);

   				// Send to network
				//netStream.beginPacket(netstream_host_ip, netstream_port); // remote IP and port
				//netStream.write(game.state.seq_plr_data[seq][plr], game.state.seq_plr_data[seq][plr][0]+2);
				//netStream.endPacket();
				//return;
			}
			else {
				Debug_printf("NETSTREAM redeye %04X %s --> REQUEST player %d data for seq %d, fujinet does not have it - header:%02X\n", game.game_id, *game.name, plr, seq, buf[1]);
			}
			break;

		case 5:		// Master resend req
			if (redeye_valid_sequence_data(seq, buf[2])) {				// fujinet can send if we have valid data
				//redeye_master_resend_data_to_lynx(seq, buf[2]);
				//return;
			}
			break;
	}

    // Send to network
	netStream.beginPacket(netstream_host_ip, netstream_port); // remote IP and port
	netStream.write(buf, size);
	netStream.endPacket();
}


bool lynxNetStream::redeye_validate_packet(uint8_t *buf, uint8_t bufsize)
{  
	// Sanity checks on packet size
	if ((bufsize < 3) || (bufsize > 10) || (buf[0]+2 != bufsize)) {
		Debug_printf("NETSTREAM bad packet size - bufsize:%d buf[0]:%d\n", bufsize, buf[0]);
		return false;
	}

	//if ((buf[0]+2) != bufsize)
 	//	return false;


	// validate the checksum
	if (redeye_checksum(buf))
		return true;
	else {
		Debug_println("NETSTREAM bad checksum");
		return false;
	}
}


/* redeye_reset_game
 *
 * Reset the game state to initial values.
 * */
void lynxNetStream::redeye_reset_game()
{
    uint8_t i;
    
    // clear game info
    game.game_id = 0;
    game.remap_game_id = 0;
    game.max_players = 0;
    game.num_players = 0;
    game.my_player_num = 0;
    
    // reset game state
    game.state.logon = true;
    for(i=0; i<MAX_PLAYERS; i++) {
	    game.state.plr_data_recv[0][i] = 0;
	    game.state.plr_data_recv[1][i] = 0;
	    game.state.seq_plr_data[0][i][0] = 0;
	    game.state.seq_plr_data[1][i][0] = 0;
    }
}


/* check_data_recv
 *
 * Check that all players have sent data for full sequence
 */
bool lynxNetStream::redeye_check_data_recv()
{
	uint8_t i;

	for(i=0; i<game.num_players; i++) {
	if ((game.state.plr_data_recv[0][i] == 0) || (game.state.plr_data_recv[1][i] == 0))
		return(false);
	}

	return(true);
}


bool lynxNetStream::redeye_check_logon_state()
{
	// Are we in logon timer countdown mode?
	if (game.state.logon_timer > 0) {
		uint64_t now = esp_timer_get_time();
		#ifdef REDEYE_DEBUG
		Debug_printf("NETSTREAM GAME %04X %s --> game start countdown: %d\n", game.game_id, *game.name, (now - game.state.logon_timer));
		#endif

		if ((now - game.state.logon_timer) > LOGON_DELAY) {
			game.state.logon = false;
			game.state.logon_timer = 0;

			if (game.game_id == 0001) {
				SYSTEM_BUS.change_baud(31250);
			}

		    Debug_printf("NETSTREAM GAME %04X %s --> Logon ended, players: %d\n", game.game_id, *game.name, game.num_players);
			return(false);
		}
	}

	return(true);
}

#endif /* BUILD_LYNX */
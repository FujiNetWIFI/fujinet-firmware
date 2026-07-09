#ifndef REDEYE_H
#define REDEYE_H


// RedEye Game handling
#define MAX_PLAYERS	16
#define RE_BUFSIZE  32          // only really need 16 bytes, and in practice games rarely use more than 6
#define LOGON_DELAY       150       // logon countdown timer (for real mode)


typedef struct GAME_STATE_T
{
	bool logon;
    uint64_t logon_timer;
	uint8_t plr_data_recv[2][MAX_PLAYERS];
	uint8_t seq_plr_data[2][MAX_PLAYERS][RE_BUFSIZE];
} GAME_STATE_T;

typedef struct GAME_T
{
  uint16_t game_id;             // game id
  uint16_t remap_game_id;		// remap game id
  const char **name;			// pointer to game name in games list
  uint8_t max_players;			// max players for this game
  uint8_t num_players;          // number of players
  uint8_t my_player_num;		// my player number
  GAME_STATE_T state;			// game state
} GAME_T;

typedef struct GAME_LIST_T
{
  uint16_t game_id;				// game id
  uint8_t max_players;			// maximum players for ths game
  const char *name;			    // pointer to the game name
} GAME_LIST_T;

/* Game List */
#define NUM_GAMES   42

#endif
/*
 * server.h
 *
 */
#ifndef SERVER_H
#define SERVER_H
#include "bns.h"
#include "web.h"




// Forwards
struct Game;
struct BnClient;


// Constants
#define MAX_GAME_NAME  16
#define MAX_GAME_DESC  32
#define MAX_GAME_CHARS 8
#define MAX_CHAR_NAME  16
#define MAX_EVTMSG_QBUF 0x10000
#define MAX_ACC_NAME   16



/*
 * User
 *
 */
#define USER_DATA_SIZE  128 // Do not change.

#pragma pack(1)
union User
{
struct
{
    uiw __magic; // for storage integrity checks.

    uib _reserved[1];
    uib name_len; /* 'name' length, excluding null-terminator. */

    union
    {
        char name[MAX_ACC_NAME];
        byte bzh_key[sizeof(name)];
    };

    uid st_created; /* Time at which this record was created. */
    uib mode;
    byte _reserved1[3];
    list lstCharacters; /* List of all characters owned by this account. */
};

byte __record_pad[USER_DATA_SIZE];
};
#pragma pack()

COMPILE_ASSERT(bad_user_sz, sizeof(User) == USER_DATA_SIZE);


/*
 * Character
 *
 */
#define CHARACTER_DATA_SIZE  128 // Do not change.

#pragma pack(1)
union Character
{
struct
{
    uiw __magic; // for storage integrity checks.

    uib _reserved[1];
    uib name_len; /* 'name' length, excluding null-terminator. */

    union
    {
        char name[MAX_CHAR_NAME];
        byte bzh_key[sizeof(name)];
    };

    list_node ln_acc_owner; /* Linked by owning Account. */
    uid st_created; /* Time at which this record was created. */
    uid accid_owner; /* Owning user account. */
    uib flags; /* Various flags */
    uib mode;
    uib clvl; /* Current character level. */
    uib cls; /* Character class. */
    uid ts_join; /* Time at which */
};

// State encoding
uib Enc(bsp_char_desc* pd);

// Methods
void OnLeaveGame(Game* pGame);
void OnJoinGame(Game* pGame);

byte __record_pad[CHARACTER_DATA_SIZE];
};
#pragma pack()

COMPILE_ASSERT(bad_char_sz, sizeof(Character) == CHARACTER_DATA_SIZE);


/*
 * Game
 *
 */
struct Game
{
    list_node ln_type;
    list_node ln_realm;
    BnClient* pbncQuery;


    // Basic game information.
    uid type;
    uid game_id; /* Unique game identifier server-side */
    uid ts_discovered; /* Time at which we positively discovered the game's existance. */
    uid ts_hosted; /* Time at which the game was hosted, computed from the reported uptime. */
    uib max_players; /* Maximum number of players permitted in game. */
    uib cur_players; /* Current number of players in the game. */
    uib bv_chars; /* Bitvector of 'pCharlist' slots; set=used */
    uib host_clvl; /* Character level of the game host. */
    uib clvl_diff; /* Game is restricted to character-level's of host_clvl +/- clvl_diff. Zero if there is no restriction. */
    uib name_len; /* Length of game name ('name'), excludes null terminator */
    uib desc_len; /* Length of game description ('description'), excludes null terminator */
    char name[MAX_GAME_NAME];
    char description[MAX_GAME_DESC];

    // Character list
    Character* pCharlist[MAX_GAME_CHARS];

    // Stats
    uid last_sched;
    uid ts_listed; /* Time at which game was last observed in the game list. */
    uid ts_query_submitted; /* Time at which the last query was sent. */
    uid ts_query_completed; /* Time at which the last successful query completed. */
    uid ts_dirty; /* Earliest time at which the game was positively known to be dirty. */
    uid wasted_queries; /* Number of queries which returned no useful/different information. */

    // Methods
    void Destroy();

    // Game events
    void OnOpen();
    void OnInfo();
    void OnClose();

    // State encoding
    uib EncBasic(bsp_game_desc* pd);
    void EncExtended(bsp_game_desc_ex* pd);
    uib EncCharlist(bsp_char_desc* pd);
};




/*
 * Exposed
 *
 */
uib ServerStartup();


#endif
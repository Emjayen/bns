/*
 * web.h
 *
 */
#ifndef SESSION_H
#define SESSION_H
#include "bns.h"
#include "websocket.h"
#include "iptbl.h"




// BSP messages
#define BSP_AUTH          0x01
#define BSP_FETCH_LIST    0x02
#define BSP_RESOLVE_CHAR  0x03
#define BSP_FEEDBACK      0x05
#define BSP_GAME_STATE    0x10
#define BSP_GAME_OPEN     0x11
#define BSP_GAME_CLOSE    0x12
#define BSP_GAME_UPDATE   0x13
#define BSP_GAME_PADD     0x14
#define BSP_GAME_PREM     0x15
#define BSP_RUN_STATE     0x20
#define BSP_RUN_CLOSE     0x21

// BSP status codes
#define BSP_STATUS_SUCCESS    0x00
#define BSP_STATUS_FAIL       0x01
#define BSP_STATUS_INVALID    0x02
#define BSP_STATUS_NOT_FOUND  0x03
#define BSP_STATUS_TRY_AGAIN  0x04

// Set as the high bit of char_id if player is host.
#define BSP_PFLAG_HOST (1<<31)

// BSP game flags (bsp_game_desc)
#define BSP_GAME_FLAG_RUN (1<<0)

#pragma pack(1)



/*
 * Client->Server 
 *
 */
struct bspc_auth
{
    uib mid;
    byte reserved[3];
    byte token[16];
};

struct bspc_fetch_list
{
    uib mid;
    uib type; /* Specifies the game type */
};

struct bsps_fetch_list
{
    uib mid;
    uib status;
    uib mode;
};

struct bspc_resolve_char
{
    uib mid;
    uid char_id[];
};

struct bsps_resolve_char
{
    uib mid;
    byte ect[]; /* bsp_char_desc[] */
};

struct bspc_feedback
{
    uib mid;
    wchar_t text[];
};


/*
 * Server->Client
 *
 */

// Common structures
struct bsp_game_desc
{
    uib flags;
    char name_and_desc[]; /* Pascal strings */
};

struct bsp_game_desc_ex
{
    uib max_players;
    uib clvl_diff;
    uiw uptime;
};

struct bsp_char_desc
{
    uib clvl;
    uib cls;
    uib name_len;
    char name[]; /* Pascal string */
};

struct bsp_pdesc
{
    uib pid;
    uid char_id;
    uiw dt_join;
};


struct bsp_run_desc
{
    uiw cur_game_id;
    uiw duration_avg;
    uib run_type;
    uib duration_stddev;
    uib popcount_avg;
    uib rating;
};


// Server->Client
struct bsps_auth
{
    uib mid;
};

struct bsps_game_state
{
    uib mid; // BSPS_GAME_STATE
    uiw game_id;
    bsp_game_desc_ex desc_ex;
    byte etc[];
    /* bsp_game_desc desc */
    /* bsp_char_desc[] */
};

struct bsps_game_open
{
    uib mid;
    uiw game_id;
    byte etc[];
    /* bsp_game_desc desc */
};

struct bsps_game_close
{
    uib mid;
    uiw game_id;
};

struct bsps_game_update
{
    uib mid;
    uiw game_id;
    bsp_game_desc_ex desc_ex;
};

struct bsps_game_padd
{
    uib mid;
    uiw game_id;
    byte etc[]; /* bsp_pdesc */
};

struct bsps_game_prem
{
    uib mid;
    uiw game_id;
    uib pid;
};

struct bsps_run_state
{
    uib mid;
    uiw run_id;
    byte ect[]; /* bsp_run_state */
};

struct bsps_run_close
{
    uib mid;
    uiw run_id;
};

#pragma pack()


// Session flags
#define SX_FLAG_WS_HANDSHAKED (1<<0)
#define SX_FLAG_LISTENING     (1<<1)

// Leading slack space required for BIP frames
#define BSP_FRAME_HDR_SZ  4

 // Session
struct Session
{
    list_node ln_sessions;
    list_node ln_listen;


    uiw s;
    uib Flags;
    uib ListenGameType;
    host* pHost;
    uid Addr;
    byte* pEvtMsgQueueTmp; // Pointer where to start delta game event messages in queue; marked at time of listen register.
    uid tsRxLast;

    // Methods
    uib Create(uiw s, uid Addr, host* pHost);
    void Destroy();
    void CleanupCb();
    uib Send(byte* pHdrSlackedBuffer, uid Length);

    // Message handlers
    uib OnReceive(byte* pMsg, uid Length);
    uib OnFetchList(bspc_fetch_list* pMsg, uid Length);
    uib OnResolveChar(bspc_resolve_char* pMsg, uid Length);
    uib OnFeedback(bspc_feedback* pMsg, uid Length);
};


 /*
  * WebStartup
  *
  */
uib WebStartup();


/*
 * ChargeHost
 *
 */
uib ChargeHost(host* pHost, uid Charge);

#endif
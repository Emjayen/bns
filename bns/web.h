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

#define BSP_FETCH_LIST  0x02
#define BSP_GAME_STATE  0x10
#define BSP_GAME_OPEN   0x11
#define BSP_GAME_CLOSE  0x12
#define BSP_GAME_UPDATE 0x13
#define BSP_GAME_PADD   0x14
#define BSP_GAME_PREM   0x15
#define BSP_GAME_DBGDAT 0x80

// BSP status codes
#define BSP_STATUS_SUCCESS    0x00
#define BSP_STATUS_FAIL       0x01
#define BSP_STATUS_INVALID    0x02
#define BSP_STATUS_NOT_FOUND  0x03
#define BSP_STATUS_TRY_AGAIN  0x04



#pragma pack(1)



/*
 * Client->Server 
 *
 */
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


/*
 * Server->Client
 *
 */

// Common structures
struct bsp_game_desc
{
    char name_and_desc[]; /* Pascal strings */
};

struct bsp_game_desc_ex
{
    uib max_players;
    uib cur_players;
    uib clvl_diff;
    uib uptime;
};

struct bsp_char_desc
{
    uid char_id;
    uib clvl;
    uib cls;
    uib dt_join;
    uib name_len;
    char name[]; /* Pascal string */
};


// Server->Client
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
    byte ect[];
    /* bsp_char_desc desc */
};

struct bsps_game_prem
{
    uib mid;
    uiw game_id;
    uid char_id;
};

struct bsps_game_dgbdat
{
    uib mid;
    uiw game_id;
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
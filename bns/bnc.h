/*
 * bnc.h
 *
 */
#ifndef BNC_H
#define BNC_H
#include <pce\pce.h>
#include <bzl\bzl.h>
#include "bns.h"


// Constants
#define BN_MAX_USERNAME  20

// Client states
#define BNC_STATE_NONE           0 /* Disconnected */
#define BNC_STATE_CONNECTING     1 /* Initiating connection */
#define BNC_STATE_CONNECTED      2 /* Connected; sent BNP_STARTUP */
#define BNC_STATE_STARTUP        3 /* Received BNP_STARTUP; sent BNP_VERCHK */
#define BNC_STATE_VERCHK         4 /* Received BNP_VERCHK; sent BNP_LOGIN */
#define BNC_STATE_LOGGED         5 /* Received BNP_LOGIN; sent BNP_REALM_LOGIN */
#define BNC_STATE_LOGGED_REALM   6 /* Received BNP_REALM_LOGIN; connecting to MCP */
#define BNC_STATE_MCP_CONNECTED  7 /* Connected to realm server; sent MCP_STARTUP */
#define BNC_STATE_MCP_STARTUP    8 /* Received MCP_STARTUP; sent MCP_CHARLIST */
#define BNC_STATE_MCP_LISTED     9 /* Received MCP_CHARLIST; sent MCP_LOGON */
#define BNC_STATE_MCP_LOGGED     10 /* Received MCP_LOGON. */

extern const char* TXT_BNC_STATE[];

// Forwards
struct proxy;

// Battle.net Client
struct BnClient
{
    uib State;
    uib Flags;
    uib Mode;
    proxy* pProxy;
    const bn_acc_desc* pAccount;
    const bn_key_desc* pKey;

#ifdef DEBUG
    uid dbg_ts_last_succesful_query;
    uid dbg_ts_last_successful_list;
#endif

    struct
    {
        uiw s;
        uiw port;
        uid addr;
        uid stkn;
        uid ctkn;
        uid partial_len;
        byte* partial_msg;
        char username[BN_MAX_USERNAME];
    } BNP;

    struct
    {
        uiw s;
        uiw port;
        uid addr;
        uid partial_len;
        byte* partial_msg;
        byte rdat[64];
        
        struct
        {
            uid ts_sent;
            void* user_data;
            uiw request_id;
            uib cancelled;
        } pending_info_request;

        struct
        {
            uid ts_sent;
            uiw request_id;
        } pending_list_request;
    } MCP;

    // Timers
    HTIMER htGameInfo;
    HTIMER htGameList;
    HTIMER htReconnect;

    // Methods
    void Initialize(const bn_acc_desc* pAccount, const bn_key_desc* pKey);
    void CancelQueryRequest();
    uib SubmitQueryRequest(char* pGamename, void* pContext);
    uib SubmitListRequest(uid Type);

    // Internal
    uib BnSend(uib MessageId, void* pMsg, uid Len);
    uib McSend(uib MessageId, void* pMsg, uid Len);
    void Connect();
    void Disconnect();
    void SetState(uib NewState);
    void CompleteQueryRequest(mcps_query_game* pMsg);
    void CompleteListRequest(mcps_game_list* pMsg);
};



// Callbacks.
extern void BnReadyQuery(BnClient* pClient);
extern void BnReadyList(BnClient* pClient);
extern void BnQueryComplete(BnClient* pClient, mcps_query_game* pMsg, void* pContext);
extern void BnListComplete(BnClient* pClient, mcps_game_list* pMsg);
extern void BnNotifyStatus(uib Mode, uib bAvailable);


#endif
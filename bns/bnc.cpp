/*
 * bnc.cpp
 *
 */
#include "bnc.h"
#include "bns.h"
#include "socks.h"
#include "prx.h"
#include <bzl\bzl.h>
#include <netlib\netlib.h>



// Constants
#define MAX_SVR_CACHE_SZ  16 /* Maximum number of Battle.net server hostname mappings. */
#define RECV_QUEUE_DEPTH  4
#define RECV_BUFF_SZ      0x1000-4
#define BNP_COUNTRY       "AUS\0Australia"
#define MAX_PARTIAL_MSG   512
#define REALM_PASSWORD    "password"
#define MCP_CHARLIST_COUNT 8
#define GAME_INFO_RATE     1000
#define GAME_LIST_RATE     1000

// Battle.net servers <> realm mapping and DNS lookup cache.
struct _bnsvr
{
    const char* hostname;
    const char* realm;
    uid ts_cache_age; /* Timestamp of our cached DNS lookup */
    uid server_count;
    uid next_server; /* Uniform distribution */
    uid server_list[MAX_SVR_CACHE_SZ];

} Servers[] =
{
    { "useast.battle.net", "USEast" },
    { "uswest.battle.net", "USWest" },
    { "europe.battle.net", "Europe" },
    { "asia.battle.net",   "Asia" },
};


// Types
typedef uib (*PFMSGHANDLER)(BnClient*, void*);


// Helpers
#define BNP_HANDLER(name, param) uib BNP_##name(BnClient* pThis, param* pMsg)
#define MCP_HANDLER(name, param) uib MCP_##name(BnClient* pThis, param* pMsg)

// Message handling tables
extern const PFMSGHANDLER BNP_HANDLER[0x100];
extern const PFMSGHANDLER MCP_HANDLER[0x20];

// Context logging
#define xLWARN(_pbnc, _pFormat, ...) LWARN("[%s] " _pFormat, _pbnc->pAccount->name, __VA_ARGS__)
#define xLERR(_pbnc, _pFormat, ...) LERR("[%s] " _pFormat, _pbnc->pAccount->name, __VA_ARGS__)
#define xLOG(_pbnc, _pFormat, ...) LOG("[%s] " _pFormat, _pbnc->pAccount->name, __VA_ARGS__)
#define xLDBG(_pbnc, _pFormat, ...) LDBG("[%s] " _pFormat, _pbnc->pAccount->name, __VA_ARGS__)





/*
 * TcbGameInfo
 *
 */
static void TcbGameInfo(BnClient* pThis)
{
    uid ts = GetTickCount();
    uid PendingRequestDuration = pThis->MCP.pending_info_request.ts_sent ? (ts - pThis->MCP.pending_info_request.ts_sent) : 0;

 
    if(PendingRequestDuration)
    {
        xLWARN(pThis, "Game query pending after %u ms", PendingRequestDuration);

        if(PendingRequestDuration < CFG_MCP_QUERY_REQUEST_TIMEOUT)
        {
            // Too early.
            return;
        }

        // Request timed out.
        pThis->CompleteQueryRequest(NULL);
    }

    BnReadyQuery(pThis);
}


/*
 * TcbGameList
 *
 */
static void TcbGameList(BnClient* pThis)
{
    uid ts = GetTickCount();
    uid PendingRequestDuration = pThis->MCP.pending_list_request.ts_sent ? (ts - pThis->MCP.pending_list_request.ts_sent) : 0;


    if(PendingRequestDuration)
    {
        xLWARN(pThis, "Game list pending after %u ms", PendingRequestDuration);
        
        if(PendingRequestDuration < CFG_MCP_LIST_REQUEST_TIMEOUT)
        {
            // Too fast.
            return;
        }

        // Request timed out.
        pThis->CompleteListRequest(NULL);
    }

    BnReadyList(pThis);
}


/*
 * BnClient::CompleteQueryRequest
 *
 */
void BnClient::CompleteQueryRequest(mcps_query_game* pMsg)
{
    uid tsRequestDuration = GetTickCount() - MCP.pending_info_request.ts_sent;


   // xLDBG(this, "CompleteQueryRequest: %u (duration: %ums)", MCP.pending_info_request.request_id, tsRequestDuration);

    if(MCP.pending_info_request.cancelled)
    {
        xLDBG(this, "Completed cancelled query request.");
    }

    else
        BnQueryComplete(this, pMsg, MCP.pending_info_request.user_data);

    MCP.pending_info_request.ts_sent = 0;

    if(tsRequestDuration > CFG_MCP_QUERY_DELAY)
        BnReadyQuery(this);
}


/*
 * BnClient::CompleteListRequest
 *
 */
void BnClient::CompleteListRequest(mcps_game_list* pMsg)
{
    uid tsRequestDuration = GetTickCount() - MCP.pending_list_request.ts_sent;


    if(pMsg && *pMsg->etc)
        BnListComplete(this, pMsg);

    if(!pMsg || !*pMsg->etc)
    {
        MCP.pending_list_request.ts_sent = 0;

        if(tsRequestDuration > CFG_MCP_LIST_DELAY)
            BnReadyList(this);
    }
}


/*
 * BnClient::SubmitQueryRequest
 *
 */
uib BnClient::SubmitQueryRequest(char* pGamename, void* pContext)
{
    if(MCP.pending_info_request.ts_sent)
    {
        xLWARN(this, "Unable to submit query request; already pending.");
        return FALSE;
    }

    union
    {
        mcpc_query_game msg;
        byte _pad[sizeof(mcpc_query_game) + 32];
    };

    msg.request_id = ++MCP.pending_info_request.request_id;
    msg.hdr.len = sizeof(msg) + strcpyn(msg.game_name, 16, pGamename) + 1;

    if(!McSend(MCP_QUERY_GAME, &msg, msg.hdr.len))
    {
        xLERR(this, "Failed to send game query");
        return FALSE;
    }

    //xLDBG(this, "Sent MCP_QUERY_GAME: %s (request_id:%u ctx:0x%X)", msg.game_name, msg.request_id, pContext);

    MCP.pending_info_request.ts_sent = GetTickCount();
    MCP.pending_info_request.user_data = pContext;
    MCP.pending_info_request.cancelled = FALSE;

    timer_set(htGameInfo, NULL, CFG_MCP_QUERY_DELAY);

    return TRUE;
}


/*
 * BnClient::SubmitListRequest
 *
 */
uib BnClient::SubmitListRequest(uid Type)
{
    union
    {
        mcpc_game_list msg;
        byte _pad[sizeof(mcpc_game_list)+16];
    };

    msg.request_id = ++MCP.pending_list_request.request_id;
    msg.type = Type;
    msg.filter[0] = '\0';

    if(!McSend(MCP_GAME_LIST, &msg, sizeof(msg) + 1))
    {
        xLERR(this, "Failed to send game list request");
        return FALSE;
    }

    MCP.pending_list_request.ts_sent = GetTickCount();

    timer_set(htGameList, NULL, CFG_MCP_LIST_DELAY);
    
    return TRUE;
}


/*
 * BnClient::CancelQueryRequest
 *
 */
void BnClient::CancelQueryRequest()
{
    if(!MCP.pending_info_request.ts_sent)
    {
        xLWARN(this, "No query to cancel");
        return;
    }

    MCP.pending_info_request.cancelled = TRUE;
}

/*
 * TcbReconnect
 *
 */
static void TcbReconnect(BnClient* pThis)
{
    pThis->Connect();
}


void BnClient::Initialize(const bn_acc_desc* pAccount, const bn_key_desc* pKey)
{
    memzero(this, sizeof(*this));
    this->pAccount = pAccount;
    this->pKey = pKey;
    this->BNP.s = NET_BAD_SOCKET;
    this->MCP.s = NET_BAD_SOCKET;
    this->htGameInfo = timer_create((PFTIMERCALLBACK) &TcbGameInfo, this);
    this->htGameList = timer_create((PFTIMERCALLBACK) &TcbGameList, this);
    this->htReconnect = timer_create((PFTIMERCALLBACK) &TcbReconnect, this);
}



/*
 * OnConnect
 *   Note that we handle both BNP and MCP connect events here; we can differentiate based
 *   on the current state of the client
 *
 */
static void NETLIBCALLBACK OnConnect(BnClient* pThis, void* _1, uid _2, uid Error, net_addr*)
{
    if(Error)
    {
        if(Error == 0xC00000B5 /*STATUS_IO_TIMEOUT*/)
        {
            NotifyProxyFailure(pThis->pProxy);
        }


        SetLastError(Error);
        xLERR(pThis, "Failed to connect to proxy server.");
        goto FAIL;
    }

    uiw s;

    union
    {
        s4p_connect msg;
        byte _pad[sizeof(s4p_connect) + 32];
    };

    msg.version = 4;
    msg.command = 1;
    memcpy(msg.ident, CFG_PRX_SOCKS4_IDENT, sizeof(CFG_PRX_SOCKS4_IDENT));


    // BNP?
    if(pThis->State == BNC_STATE_CONNECTING)
    {
        s = pThis->BNP.s;
        msg.dst_port = pThis->BNP.port;
        msg.dst_addr = pThis->BNP.addr;
        
    }

    // MCP
    else
    {
        s = pThis->MCP.s;
        msg.dst_port = pThis->MCP.port;
        msg.dst_addr = pThis->MCP.addr;
    }

    // This is common for both BNP and MCP.
    for(uib i = 0; i < RECV_QUEUE_DEPTH; i++)
    {
        if(!net_recv(s, NULL, RECV_BUFF_SZ))
        {
            xLERR(pThis, "Failed to initiate receive");
            goto FAIL;
        }
    }

    xLOG(pThis, "Connection to proxy server established; requesting SOCKS4 proxy connect to: %a:%u", msg.dst_addr, msg.dst_port);

    if(!net_send(s, &msg, sizeof(msg) + sizeof(CFG_PRX_SOCKS4_IDENT)))
    {
        xLERR(pThis, "Failed to send SOCKS4 connect");
        goto FAIL;
    }

    // Success
    return;

FAIL:
    pThis->Disconnect();
}


/*
 * BNP_OnReceive
 *
 */
static void NETLIBCALLBACK BNP_OnReceive(BnClient* pThis, byte* pBuffer, uid Bytes, uid Error, net_addr*)
{
    if(Error)
    {
        SetLastError(Error);
        xLERR(pThis, "BNP connection terminated remotely.");
        goto FAIL;
    }

    if(!Bytes)
    {
        xLOG(pThis, "BNP gracefully closed connection remotely.");
        goto FAIL;
    }

    // Waiting for SOCKS response?
    if(pThis->State == BNC_STATE_CONNECTING)
    {
        if(((s4p_status*) pBuffer)->status != 90)
        {
            xLERR(pThis, "Proxy failed to connect to Battle.net, status: %u", ((s4p_status*) pBuffer)->status);
            goto FAIL;
        }

        union
        {
            bnpc_startup msg;
            byte pad[sizeof(bnpc_startup) + sizeof(BNP_COUNTRY)];
        } static BnpStartupMsg =
        {
            BNP_MAGIC,
            BNP_STARTUP,
            sizeof(bnpc_startup) + sizeof(BNP_COUNTRY),

            /* Protocol */ 0,
            /* Platform */ BNP_PLATFORM_WIN,
            /* Product  */ NULL, // Set at runtime
            /* Version  */ 14,
            /* Language */ NULL,
            /* LAN Addr */ NULL,
            /* Tz bias  */ NULL,
            /* Locale   */ NULL,
            /* Lang     */ NULL,
            /* Country  */ BNP_COUNTRY,
        };

        BnpStartupMsg.msg.product = pThis->pKey->d2xp[0] ? BNP_PRODUCT_D2XP : BNP_PRODUCT_D2DV;

        net_send(pThis->BNP.s, (void*) "\x01", 1); // Protocol selector.
        net_send(pThis->BNP.s, &BnpStartupMsg.msg, BnpStartupMsg.msg.hdr.len); // BNP_STARTUP

        xLOG(pThis, "Successfully connected to Battle.net server via proxy.");
        pThis->SetState(BNC_STATE_CONNECTED);

        // Nothing more to do; could conceivably have received protocol data but that should never happen.
        goto DONE;
    }
  

    union
    {
        byte* p;
        bnp_hdr* pHdr;
    };

    p = pBuffer;


    /*
     * If there's a partial message reconstruct a contiguous buffer. This should be very rare.
     *
     */
    if(pThis->BNP.partial_len)
    {
        if(!(p = (byte*) MALLOC(pThis->BNP.partial_len + Bytes)))
        {
            xLERR(pThis, "Failed to allocate reconstituted message buffer (%u bytes)", pThis->BNP.partial_len + Bytes);
            goto FAIL;
        }

        memcpy(p, pThis->BNP.partial_msg, pThis->BNP.partial_len);
        memcpy(p+pThis->BNP.partial_len, pBuffer, Bytes);

        Bytes += pThis->BNP.partial_len;
        pBuffer = p;
    }
    
    while(Bytes)
    {
        if(pHdr->magic != BNP_MAGIC)
        {
            xLERR(pThis, "Corrupt BNP message received.");
            goto FAIL;
        }

        if(Bytes < pHdr->len)
        {
            xLWARN(pThis, "Partial BNP message received (0x%X %u bytes, have %u)", pHdr->mid, pHdr->len, Bytes);
            break;
        }

        if(!BNP_HANDLER[pHdr->mid])
        {
            xLDBG(pThis, "Unhandled BNP message: 0x%X", pHdr->mid);
        }

        if(BNP_HANDLER[pHdr->mid] && !BNP_HANDLER[pHdr->mid](pThis, p))
            goto FAIL;

        Bytes -= pHdr->len;
        p += pHdr->len;
    }

    // Remaining partial message?
    if(Bytes)
    {
        if(Bytes > MAX_PARTIAL_MSG)
        {
            xLERR(pThis, "Partial message exceeds limit (0x%X %u bytes)", pHdr->mid, Bytes);
            goto FAIL;
        }

        memcpy(pThis->BNP.partial_msg, p, Bytes);

        if(pThis->BNP.partial_len)
            FREE(pBuffer);
    }

    // This is always correct regardless of state.
    pThis->BNP.partial_len = Bytes;

DONE:
    // Submit further receive I/O
    if(!net_recv(pThis->BNP.s, NULL, RECV_BUFF_SZ))
    {
        xLERR(pThis, "Failed to initiate receive");
        goto FAIL;
    }

    // Success
    return;

FAIL:
    pThis->Disconnect();
}


/*
 * MCP_OnReceive
 *
 */
static void NETLIBCALLBACK MCP_OnReceive(BnClient* pThis, byte* pBuffer, uid Bytes, uid Error, net_addr*)
{
    if(Error)
    {
        SetLastError(Error);
        xLERR(pThis, "BNP connection terminated remotely.");
        goto FAIL;
    }

    if(!Bytes)
    {
        xLOG(pThis, "BNP gracefully closed connection remotely.");
        goto FAIL;
    }

    // Waiting for SOCKS response?
    if(pThis->State == BNC_STATE_LOGGED_REALM)
    {
        if(((s4p_status*) pBuffer)->status != 90)
        {
            xLERR(pThis, "Proxy failed to connect to realm server, status: %u", ((s4p_status*) pBuffer)->status);
            goto FAIL;
        }

        union
        {
            mcpc_startup msg;
            byte _pad[sizeof(mcpc_startup) + 32];
        };

        uid UsernameLen = strlen(pThis->BNP.username) + 1;

        memcpy(msg.rdat, pThis->MCP.rdat, sizeof(pThis->MCP.rdat));
        memcpy(msg.username, pThis->BNP.username, UsernameLen);

        pThis->SetState(BNC_STATE_MCP_CONNECTED);
        net_send(pThis->MCP.s, (void*) "\x01", 1);
        pThis->McSend(MCP_STARTUP, &msg, sizeof(mcpc_startup) + UsernameLen);

        // Nothing more to do.
        goto DONE;
    }

    union
    {
        byte* p;
        mcp_hdr* pHdr;
    };

    p = pBuffer;


    /*
     * If there's a partial message reconstruct a contiguous buffer. This should be very rare.
     *
     */
    if(pThis->MCP.partial_len)
    {
        if(!(p = (byte*) MALLOC(pThis->MCP.partial_len + Bytes)))
        {
            xLERR(pThis, "Failed to allocate reconstituted message buffer (%u bytes)", pThis->MCP.partial_len + Bytes);
            goto FAIL;
        }

        memcpy(p, pThis->MCP.partial_msg, pThis->MCP.partial_len);
        memcpy(p+pThis->MCP.partial_len, pBuffer, Bytes);

        Bytes += pThis->MCP.partial_len;
        pBuffer = p;
    }

    while(Bytes)
    {
        if(Bytes < pHdr->len)
        {
            xLWARN(pThis, "Partial MCP message received (0x%X %u bytes, have %u)", pHdr->mid, pHdr->len, Bytes);
            break;
        }

        if(!MCP_HANDLER[pHdr->mid])
        {
            xLDBG(pThis, "Unhandled MCP message: 0x%X", pHdr->mid);
        }

        if(MCP_HANDLER[pHdr->mid] && !MCP_HANDLER[pHdr->mid](pThis, p))
            goto FAIL;

        Bytes -= pHdr->len;
        p += pHdr->len;
    }

    // Remaining partial message?
    if(Bytes)
    {
        if(Bytes > MAX_PARTIAL_MSG)
        {
            xLERR(pThis, "Partial message exceeds limit (0x%X %u bytes)", pHdr->mid, Bytes);
            goto FAIL;
        }

        memcpy(pThis->MCP.partial_msg, p, Bytes);

        if(pThis->MCP.partial_len)
            FREE(pBuffer);
    }

    // This is always correct regardless of state.
    pThis->MCP.partial_len = Bytes;


DONE:
    if(!net_recv(pThis->MCP.s, NULL, RECV_BUFF_SZ))
    {
        xLERR(pThis, "Failed to initiate receive");
        goto FAIL;
    }

    // Success
    return;

FAIL:
    pThis->Disconnect();
}


/*
 * BnClient::Connect
 *
 */
void BnClient::Connect()
{
    _bnsvr& rServer = Servers[pAccount->realm];


    SetState(BNC_STATE_CONNECTING);

    timer_set(htReconnect, NULL, NULL);


    if(!(BNP.partial_msg = (byte*) MALLOC(MAX_PARTIAL_MSG)) || !(MCP.partial_msg = (byte*) MALLOC(MAX_PARTIAL_MSG)))
    {
        xLERR(this, "Failed to allocate partial message buffer(s)");
        goto FAIL;
    }
    
    if(!(pProxy = AcquireProxy()))
    {
        xLWARN(this, "Unable to acquire proxy.");
        goto FAIL;
    }

    if((BNP.s = net_create(AF_INET, SOCK_STREAM, IPPROTO_TCP, this)) == NET_BAD_SOCKET || !net_bind(BNP.s, NULL, NULL) || !net_ctrl(BNP.s, IPPROTO_TCP, TCP_NODELAY, (void*) "\x01\x00\x00\x00", 4))
    {
        xLERR(this, "Failed to create BNP socket");
        goto FAIL;
    }

    net_event_filter(BNP.s, NETEV_RECV | NETEV_CONN);
    net_event_callback(BNP.s, &BNP_OnReceive, NULL, &OnConnect, NULL);

    // DNS cache expired?
    if(!rServer.ts_cache_age || GetTickCount() - rServer.ts_cache_age > CFG_BNET_SVR_CACHE_TTL)
    {
        hostent* ph;

        if(!(ph = gethostbyname(rServer.hostname)))
        {
            xLWARN(this, "Failed to resolve server %s", rServer.hostname);
        }

        else
        {
            xLOG(this, "Updated DNS cache for %s:", rServer.hostname);

            rServer.server_count = 0;

            for(uid i = 0; ph->h_addr_list[i] && i < ARRAYSIZE(rServer.server_list); i++)
            {
                rServer.server_list[i] = *((uid*) ph->h_addr_list[i]);
                rServer.server_count++;

                xLOG(this, "#%u: %a", i, rServer.server_list[i]);
            }

            rServer.ts_cache_age = GetTickCount();
        }
    }

    // Choose server.
    if(rServer.next_server >= rServer.server_count)
        rServer.next_server = 0;

    BNP.addr = rServer.server_list[rServer.next_server++];
    BNP.port = bswap16(6112);

    xLOG(this, "Initiating connection; proxy: %a:%u, battle.net: %a:%u", pProxy->addr, pProxy->port, BNP.addr, BNP.port);

    if(!net_connect(BNP.s, pProxy->addr, bswap16(pProxy->port)))
    {
        xLERR(this, "Failed to initiate connection");
        goto FAIL;
    }

    // Success; connect pending.
    return;


FAIL:
    Disconnect();
}


/*
 * BnClient::Disconnect
 *
 */
void BnClient::Disconnect()
{
    SetState(BNC_STATE_NONE);

    xLOG(this, "Disconnected");


    if(State >= BNC_STATE_MCP_LOGGED)
        BnNotifyStatus(Mode, FALSE);

    if(MCP.pending_info_request.ts_sent)
        CompleteQueryRequest(NULL);

    if(MCP.pending_list_request.ts_sent)
        CompleteListRequest(NULL);

    if(BNP.s != NET_BAD_SOCKET)
    {
        net_close(BNP.s);
    }

    if(MCP.s != NET_BAD_SOCKET)
    {
        net_close(MCP.s);
    }

    if(pProxy)
    {
        ReleaseProxy(pProxy);
        pProxy = NULL;
    }

    if(BNP.partial_msg)
    {
        FREE(BNP.partial_msg);
        BNP.partial_msg = NULL;
        BNP.partial_len = 0;
    }

    if(MCP.partial_msg)
    {
        FREE(MCP.partial_msg);
        MCP.partial_msg = NULL;
        MCP.partial_len = 0;
    }

    memzero(&MCP, sizeof(MCP));
    memzero(&BNP, sizeof(BNP));

    MCP.s = NET_BAD_SOCKET;
    BNP.s = NET_BAD_SOCKET;

    if(htGameInfo)
        timer_set(htGameInfo, NULL, NULL);

    if(htGameList)
        timer_set(htGameList, NULL, NULL);

    timer_set(htReconnect, 0, 30000);
}


/*
 * BnClient::SetState
 *
 */
void BnClient::SetState(uib NewState)
{
    State = NewState;
   // xLOG(pThis, "STATE CHANGE: %s", TXT_BNC_STATE[this->State]);
}


/*
 * BnClient::BnSend
 *
 */
uib BnClient::BnSend(uib MessageId, void* pMsg, uid Len)
{
    ((bnp_hdr*) pMsg)->magic = BNP_MAGIC;
    ((bnp_hdr*) pMsg)->mid = MessageId;
    ((bnp_hdr*) pMsg)->len = (uiw) Len;

    if(!net_send(BNP.s, pMsg, ((bnp_hdr*) pMsg)->len))
    {
        xLERR(this, "net_send() BNP failed");
        return FALSE;
    }

    return TRUE;
}


/*
 * BnClient::McSend
 *
 */
uib BnClient::McSend(uib MessageId, void* pMsg, uid Len)
{
    ((mcp_hdr*) pMsg)->len = (uiw) Len;
    ((mcp_hdr*) pMsg)->mid = MessageId;

    if(!net_send(MCP.s, pMsg, ((mcp_hdr*) pMsg)->len))
    {
        xLERR(this, "net_send() MCP failed");
        return FALSE;
    }

    return TRUE;
}


/*
 * BNP protocol handling
 *
 */
BNP_HANDLER(BNP_KEEP_ALIVE, void)
{
    bnp_hdr msg;

    return pThis->BnSend(BNP_KEEP_ALIVE, &msg, sizeof(msg));
}


BNP_HANDLER(BNP_STARTUP, bnps_startup)
{
    char* pVerchkMPQ = pMsg->verchk_file;
    char* pSalt = pMsg->verchk_file + strlen(pVerchkMPQ) + 1;

    pThis->BNP.stkn = pMsg->stkn;
    pThis->BNP.ctkn = GetTickCount();


    xLOG(pThis, "Startup response; auth_method:%u vercheck:%s salt:%s stkn:0x%X ctkn:0x%X", pMsg->auth_method, pVerchkMPQ, pSalt, pMsg->stkn, pThis->BNP.ctkn);

    union
    {
        byte pad[sizeof(bnpc_verchk)+256];
        bnpc_verchk VerMsg;
    };

    union
    {
        byte tmp[32];
        uid salt;
    };

    byte hash[20];
    char verstr[28];

    b64dec(tmp, sizeof(tmp), pSalt, strlen(pSalt));
    memcpy(tmp+sizeof(uid), CFG_BNET_GAME_VERSION, sizeof(CFG_BNET_GAME_VERSION)-1);
    tmp[sizeof(uid) + sizeof(CFG_BNET_GAME_VERSION) - 1] = 0x01;
    sha1(tmp, sizeof(uid) + sizeof(CFG_BNET_GAME_VERSION), hash);
    b64enc(verstr, sizeof(verstr), hash, sizeof(hash));

    VerMsg.ctkn = pThis->BNP.ctkn;
    VerMsg.bin_ver = NULL;
    VerMsg.is_spawn = FALSE;
    VerMsg.key_count = 2;

    if(!DecodeKey(pThis->pKey->d2dv, pThis->BNP.ctkn, pThis->BNP.stkn, &VerMsg.key[0]))
    {
        xLOG(pThis, "Failed to decode D2DV key");
        return FALSE;
    }

    byte* p = (byte*) &VerMsg.key[1];

    if(VerMsg.key_count == 2)
    {
        if(!DecodeKey(pThis->pKey->d2xp, pThis->BNP.ctkn, pThis->BNP.stkn, &VerMsg.key[1]))
        {
            xLOG(pThis, "Failed to decode D2XP key");
            return FALSE;
        }

        p += sizeof(bnp_key);
    }

    VerMsg.bin_hash = *((uid*) verstr);
    memcpy(p, verstr+4, 24);
    p += 24;
    *p++ = '\0';
    memcpy(p, CFG_BNET_PRODUCT_OWNER, sizeof(CFG_BNET_PRODUCT_OWNER));
    p += sizeof(CFG_BNET_PRODUCT_OWNER);

    pThis->SetState(BNC_STATE_STARTUP);
    return pThis->BnSend(BNP_VERCHK, &VerMsg, p - ((byte*) &VerMsg));
}


BNP_HANDLER(BNP_VERCHK, bnps_verchk)
{
    xLOG(pThis, "Version check complete; status:%u info:%s", pMsg->status, pMsg->info);
    
    if(pMsg->status != BNP_VS_SUCCESS)
        return FALSE;

    union
    {
        bnpc_login LoginMsg;
        byte _pad[sizeof(bnpc_login) + 32];
    };

    uid UsernameLen = strlen(pThis->pAccount->name)+1;
    const char* pPassword = pThis->pAccount->password ? pThis->pAccount->password : CFG_DEFAULT_PASSWORD;

    LoginMsg.ctkn = pThis->BNP.ctkn;
    LoginMsg.stkn = pThis->BNP.stkn;
    
    HashPassword(pPassword, pThis->BNP.ctkn, pThis->BNP.stkn, LoginMsg.pass_hash);
    memcpy(LoginMsg.username, pThis->pAccount->name, UsernameLen);

    pThis->SetState(BNC_STATE_VERCHK);
    return pThis->BnSend(BNP_LOGIN, &LoginMsg, sizeof(bnpc_login) + UsernameLen);
}


BNP_HANDLER(BNP_LOGIN, bnps_login)
{
    xLOG(pThis, "Login response; status:%u info:%s", pMsg->status, pMsg->info);

    if(pMsg->status != BNP_LS_SUCCESS)
    {
        if(pMsg->status == BNP_LS_BAD_PASSWORD)
        {
            FATAL("Incorrect password configuration: '%s' '%s'", pThis->pAccount->name, pThis->pAccount->password);
        }

        return FALSE;
    }

    union
    {
        bnpc_login_realm LoginRealmMsg;
        byte _pad[sizeof(bnpc_login_realm) + 32];
    };

    uid RealmNameLen = strlen(Servers[pThis->pAccount->realm].realm) + 1;

    LoginRealmMsg.ctkn = pThis->BNP.ctkn;
    HashPassword((char*) REALM_PASSWORD, pThis->BNP.ctkn, pThis->BNP.stkn, LoginRealmMsg.pass_hash);
    memcpy(LoginRealmMsg.realm, Servers[pThis->pAccount->realm].realm, RealmNameLen);

    pThis->SetState(BNC_STATE_LOGGED);
    return pThis->BnSend(BNP_REALM_LOGIN, &LoginRealmMsg, sizeof(LoginRealmMsg) + RealmNameLen);
    return 1;
}


BNP_HANDLER(BNP_REALM_LOGIN, bnps_login_realm)
{
    xLOG(pThis, "Realm login response; status:0x%X cookie:0x%X length:%u realm_addr:%a realm_port:%u username:%s", pMsg->status, pMsg->cookie, pMsg->hdr.len, pMsg->realm_addr, bswap16(pMsg->realm_port), pMsg->username);

    if(pMsg->hdr.len <= sizeof(bnp_hdr)+8)
        return FALSE;

    pThis->MCP.addr = pMsg->realm_addr;
    pThis->MCP.port = pMsg->realm_port;
    strcpyn(pThis->BNP.username, sizeof(pThis->BNP.username), pMsg->username);

    memcpy(pThis->MCP.rdat, pMsg->rdat_1, sizeof(pMsg->rdat_1));
    memcpy(pThis->MCP.rdat + sizeof(pMsg->rdat_1), pMsg->rdat_2, sizeof(pMsg->rdat_2));

    if((pThis->MCP.s = net_create(AF_INET, SOCK_STREAM, IPPROTO_TCP, pThis)) == NET_BAD_SOCKET || !net_bind(pThis->MCP.s, NULL, NULL) || !net_ctrl(pThis->MCP.s, IPPROTO_TCP, TCP_NODELAY, (void*) "\x01\x00\x00\x00", 4))
    {
        xLERR(pThis, "Failed to create MCP socket");
        return FALSE;
    }

    net_event_filter(pThis->MCP.s, NETEV_RECV|NETEV_CONN);
    net_event_callback(pThis->MCP.s, &MCP_OnReceive, NULL, &OnConnect, NULL);

    pThis->SetState(BNC_STATE_LOGGED_REALM);

    if(!net_connect(pThis->MCP.s, pThis->pProxy->addr, bswap16(pThis->pProxy->port)))
    {
        xLERR(pThis, "Failed to initiate connection to proxy server.");
        return FALSE;
    }

    return TRUE;
}


/*
 * MCP message handlers
 *
 */
MCP_HANDLER(MCP_STARTUP, mcps_startup)
{
    xLOG(pThis, "MCP startup response; status:0x%X", pMsg->status);

    if(pMsg->status)
        return FALSE;


    mcpc_charlist msg;

    msg.request_count = MCP_CHARLIST_COUNT;

    pThis->SetState(BNC_STATE_MCP_STARTUP);
    return pThis->McSend(MCP_CHARLIST, &msg, sizeof(msg));
}


MCP_HANDLER(MCP_CHARLIST, mcps_charlist)
{
    xLOG(pThis, "Received character list: requested:%u total:%u returned:%u", pMsg->request_count, pMsg->total_chars, pMsg->count);

    union
    {
        byte* p;
        mcp_chardesc* pDesc;
    };

    pDesc = &pMsg->list;
    char* pSelectedCharacter = NULL;
    const char* pCharacterName = pThis->pAccount->character ? pThis->pAccount->character : pThis->pAccount->name;

    for(uid i = 0; i < pMsg->count; i++)
    {
        char* pName = pDesc->etc;
        char* pStatstring = pDesc->etc + strlen(pDesc->etc) + 1;

        if(cmpstri(pName, pCharacterName))
        {
            pSelectedCharacter = pName;
            pThis->Mode = StatstringToMode((const byte*) pStatstring) | (pThis->pAccount->realm << 5);
        }

        p += sizeof(mcp_chardesc) + strlen(pName)+1 + strlen(pStatstring)+1;
    }

    if(!pSelectedCharacter)
    {
        xLERR(pThis, "Unable to login realm: character '%s' does not exist on account %s", pCharacterName, pThis->pAccount->name);
        return FALSE;
    }

    union
    {
        mcpc_login msg;
        byte _pad[sizeof(mcpc_login) + 32];
    };

    uid CharnameLen = strlen(pSelectedCharacter) + 1;
    
    memcpy(msg.name, pSelectedCharacter, CharnameLen);

    xLOG(pThis, "Logging into realm as '%s'", msg.name);

    pThis->SetState(BNC_STATE_MCP_LISTED);
    return pThis->McSend(MCP_LOGIN, &msg, sizeof(msg) + CharnameLen);
}


MCP_HANDLER(MCP_LOGIN, mcps_login)
{
    xLOG(pThis, "Received realm login result: %u", pMsg->status);

    if(pMsg->status)
        return FALSE;

    xLOG(pThis, "Successfully logged into realm. username=%s (*%s) mode=0x%X (%s)", pThis->BNP.username, pThis->pAccount->name, pThis->Mode, fmt_gt(pThis->Mode));

    timer_set(pThis->htGameInfo, 0, 1000);
    timer_set(pThis->htGameList, 0, 1000);

    pThis->SetState(BNC_STATE_MCP_LOGGED);

    BnNotifyStatus(pThis->Mode, TRUE);

    return TRUE;
}


MCP_HANDLER(MCP_GAME_LIST, mcps_game_list)
{
    //xLDBG(pThis, "Received game list response: request_id:%u (expect:%u) game_id:%u player_count:%u status:%u name:%s",
    //     pMsg->request_id, pThis->MCP.pending_list_request.request_id,
    //     pMsg->game_id,
    //     pMsg->player_count,
    //     pMsg->status,
    //     pMsg->etc);

    if(!pThis->MCP.pending_list_request.ts_sent)
    {
        xLWARN(pThis, "Received game list while not pending");
    }

    if(pMsg->request_id != pThis->MCP.pending_list_request.request_id)
    {
        xLWARN(pThis, "Received list request_id mismatch; got %u expected %u", pMsg->request_id, pThis->MCP.pending_list_request.request_id);
    }

    else
    {
#ifdef DEBUG
        pThis->dbg_ts_last_successful_list = GetTickCount();
#endif

        pThis->CompleteListRequest(pMsg);
    }

    return TRUE;
}


MCP_HANDLER(MCP_QUERY_GAME, mcps_query_game)
{
    //xLDBG(pThis, "Recv MCP_QUERY_GAME: (request_id:%u ctx:0x%X)", pMsg->request_id, pThis->MCP.pending_info_request.user_data);

    if(!pThis->MCP.pending_info_request.ts_sent)
    {
        xLWARN(pThis, "Received game info query while not pending");
        return TRUE;
    }

    if(pMsg->request_id != pThis->MCP.pending_info_request.request_id)
    {
        xLWARN(pThis, "Received info request_id mismatch; got %u expected %u", pMsg->request_id, pThis->MCP.pending_info_request.request_id);
        return TRUE;
    }

    //else
    //{
#ifdef DEBUG
        pThis->dbg_ts_last_succesful_query = GetTickCount();
#endif

        pThis->CompleteQueryRequest(pMsg);
    //}

    return TRUE;
}



/*
 * BNP handler table
 *
 */
#define HANDLER(name) (PFMSGHANDLER) BNP_##name

const PFMSGHANDLER BNP_HANDLER[0x100] = 
{
    HANDLER(BNP_KEEP_ALIVE),
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL, //HANDLER(BNP_GAME_LIST),
    NULL, //HANDLER(BNP_ENTER_CHAT),
    NULL,
    NULL,
    NULL,
    NULL,
    NULL, //HANDLER(BNP_CHAT_EVENT),
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL, //HANDLER(BNP_PING),
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    HANDLER(BNP_LOGIN),
    NULL,
    NULL,
    NULL,
    HANDLER(BNP_REALM_LOGIN),
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    HANDLER(BNP_STARTUP),
    HANDLER(BNP_VERCHK),
    NULL,
};



/*
 * MCP handler table
 *
 */
#undef HANDLER
#define HANDLER(name) (PFMSGHANDLER) MCP_##name

const PFMSGHANDLER MCP_HANDLER[0x20] =
{
    NULL,
    HANDLER(MCP_STARTUP),
    NULL,
    NULL,
    NULL,
    HANDLER(MCP_GAME_LIST),
    HANDLER(MCP_QUERY_GAME),
    HANDLER(MCP_LOGIN),
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    HANDLER(MCP_CHARLIST),
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
};



// State strings
const char* TXT_BNC_STATE[] =
{
    "NONE",
    "CONNECTING",
    "CONNECTED",
    "STARTUP",
    "VERCHK",
    "LOGGED",
    "LOGGED_REALM",
    "MCP_CONNECTED",
    "MCP_STARTUP",
    "MCP_LISTED",
    "MCP_LOGGED",
};
/*
 * web.cpp
 *
 */
#include "web.h"
#include "iptbl.h"
#include <netlib\netlib.h>




// Constants
#define RECV_BUFF_SZ  (1024-sizeof(void*))
#define RECV_QUEUE_DEPTH  8
#define ACPT_QUEUE_DEPTH  16

// Globals
static uiw sListen;
static Session* plSessions;
static freelist flSessions;
static list lstSessions; // Session::ln_sessions. All sessions, sorted by last receive.
static HTIMER htPing;




/*
 * Session::Create
 *
 */
uib Session::Create(uiw s, uid Addr, host* pHost)
{
    this->s = s;
    this->Addr = Addr;
    this->pHost = pHost;
    this->tsRxLast = GetTickCount();
    this->pHost->conns++;

    net_set_key(this->s, this);

    LDBG("[Sx0%X:%a] Session created (%u)", this, Addr, s);

    list_append(&lstSessions, (list_node*) &ln_sessions);

    for(uib i = 0; i < RECV_QUEUE_DEPTH; i++)
    {
        if(!net_recv(this->s, NULL, RECV_BUFF_SZ))
        {
            LERR("Failed to initiate receive");
            goto FAIL;
        }
    }

    // Success
    return TRUE;

FAIL:
    Destroy();
    return FALSE;
}


/*
 * Session::Destroy
 *
 */
void Session::Destroy()
{
    CleanupCb();

    pHost->conns -= 1;

    LDBG("[Sx0%X:%a] Session destroyed (%u)", this, Addr, s);

    if(s != NET_BAD_SOCKET)
        net_close(s);

    list_remove(&lstSessions, &ln_sessions);
    freelist_free(&flSessions, this);
}


/*
 * OnAccept
 *
 */
static void NETLIBCALLBACK OnAccept(void*, void*, uiw accept_socket, uid Error, net_addr* pSrc)
{
    Session* pSession = NULL;
  

    if(Error)
    {
        SetLastError(Error);
        LERR("Failed to accept websocket connection");
        net_close(accept_socket);
    }

    else
    {
        host* pHost = iptbl_lookup(pSrc->addr);

        if(!ChargeHost(pHost, CFG_BSP_TKNBKT_TCP_COST))
        {
            LWARN("[ABUSE] Rejected %a -- exceeded TCP connection establishment rate.", pSrc->addr);
            net_close(accept_socket);
        }

        else if(pHost->conns >= CFG_BSP_MAX_CONNECTIONS)
        {
            LWARN("[ABUSE] Rejected %a -- reached concurrent TCP connection limit.", pSrc->addr);
            net_close(accept_socket);
        }

        else
        {
            LOG("Websocket connection received from %a", pSrc->addr);

            if(!(pSession = (Session*) freelist_alloc(&flSessions)))
            {
                LERR("Failed to allocate new session");
                net_close(accept_socket);
            }

            else
            {
                memzero(pSession, sizeof(Session));

                if(!pSession->Create(accept_socket, pSrc->addr, pHost))
                {
                    LERR("Failed to initialize new session");
                    // Will destroy socket if fails.
                }

                else
                    LDBG("Successfully created new session");
            }
        }
    }


    if(!net_accept(sListen, net_create(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL)))
    {
        LERR("Failed to initiate accept I/O");
    }
}





/*
 * OnReceive
 *   
 */
static void NETLIBCALLBACK OnWsReceive(Session* pThis, byte* pBuffer, uid Bytes, uid Error, net_addr*)
{
    byte* p = pBuffer;


    if(Error || !Bytes)
    {
        SetLastError(Error);
        LERR("Connection closed remotely.");
        goto FAIL;
    }

    pThis->tsRxLast = GetTickCount();

    LDBG("Websock: Received %u bytes: ", Bytes);

    if(!ChargeHost(pThis->pHost, CFG_BSP_TKNBKT_RX_COST * Bytes))
    {
        LWARN("[ABUSE] %a exceeded rx rate limit", pThis->Addr);
        goto FAIL;
    }

    while(Bytes)
    {
        if(Bytes < WS_MIN_HDR_SZ + 4)
        {
            LWARN("Insufficient data for websocket header");
            break;
        }

        if(!(p[0] & WS_CTLF_FIN))
        {
            LWARN("Received fragmented websocket frame.");
            break;
        }

        if(!(p[1] & WS_FLAG_MASKED))
        {
            LWARN("Received websocket unmasked frame");
            break;
        }

        uib HdrSz = WS_MIN_HDR_SZ+4;
        uib Opcode = p[0] & WS_OP_MASK;
        uid PayloadSz = p[1] & WS_LENGTH_MASK;
        
        if(PayloadSz >= 127)
        {
            LWARN("Websocket extended-length frame received.");
            break;
        }

        if(PayloadSz >= 126)
        {
            if(Bytes < WS_MIN_HDR_SZ + 6)
            {
                LWARN("Insufficient data for websocket header (extended length)");
                break;
            }

            PayloadSz = bswap16(*((uiw*) &p[2]));
            HdrSz += 2;
        }

        const uid FrameSz = HdrSz + PayloadSz;

        if(Bytes < FrameSz)
        {
            LWARN("Insufficient data for websocket frame (%u < %u)", Bytes, FrameSz);
            break;
        }

        p += HdrSz;

        for(uid i = 0; i < PayloadSz; i++)
            p[i] ^= *(p-4+(i&0x3));


        if(Opcode == WS_OP_BIN)
        {
            if(!pThis->OnReceive(p, PayloadSz))
                goto FAIL;
        }

        Bytes -= FrameSz;
        p += FrameSz;
    }


    if(!net_recv(pThis->s, NULL, RECV_BUFF_SZ))
    {
        LERR("Failed to initiate receive I/O");
        goto FAIL;
    }

    // Success
    return;

FAIL:
    pThis->Destroy();
}


/*
 * Session::OnReceive
 *
 */
uib Session::OnReceive(byte* pMsg, uid Length)
{
    switch(*pMsg)
    {
        case BSP_FETCH_LIST: return OnFetchList((bspc_fetch_list*) pMsg, Length); break;
        case BSP_RESOLVE_CHAR: return OnResolveChar((bspc_resolve_char*) pMsg, Length); break;
        case BSP_FEEDBACK: return OnFeedback((bspc_feedback*) pMsg, Length); break;

        default:
            LWARN("[ABUSE] Unknown BSP message: 0x%X (%u)", *pMsg, Length);
            return FALSE;
    }
}


/*
 * OnWsHandshake
 *
 */
#define WS_HANDSHAKE_HEADER  "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "
#define WS_HANDSHAKE_SHA1B64 "XXXXXXXXXXXXXXXXXXXXXXXXXXXX"

static void NETLIBCALLBACK OnWsHandshake(Session* pThis, char* pBuffer, uid Bytes, uid Error, net_addr*)
{
    static char HTTP_WS_HANDSHAKE_RESPONSE[] = WS_HANDSHAKE_HEADER WS_HANDSHAKE_SHA1B64 "\r\n\r\n";


    if(!HttpComputeWebsocketServerKey((byte*) pBuffer, Bytes, RECV_BUFF_SZ, HTTP_WS_HANDSHAKE_RESPONSE + sizeof(WS_HANDSHAKE_HEADER) - 1))
    {
        LERR("Websocket initial HTTP request handshake processing failed.");
        goto FAIL;
    }

    if(!net_send(pThis->s, HTTP_WS_HANDSHAKE_RESPONSE, sizeof(HTTP_WS_HANDSHAKE_RESPONSE)-1))
    {
        LERR("Failed to send Websocket handshake response.");
        goto FAIL;
    }

    LOG("%a successful websocket handshake", pThis->Addr);

    pThis->Flags |= SX_FLAG_WS_HANDSHAKED;
    net_event_callback(pThis->s, &OnWsReceive, NULL, NULL, NULL);

    if(!net_recv(pThis->s, NULL, RECV_BUFF_SZ))
    {
        LERR("Failed to initiate receive I/O");
        goto FAIL;
    }

    // Success
    return;

FAIL:
    pThis->Destroy();
}


/*
 * TcbPing
 *
 */
static void TcbPing(void*)
{
    uid ts = GetTickCount();

    LIST_ITERATE(lstSessions, Session, ln_sessions)
    {
        if((ts-ple->tsRxLast) > (CFG_BSP_KEEPALIVE_INT*2))
        {
            LWARN("Timing out BSP websocket");
            ple->Destroy();
        }

        else if(ple->Flags & SX_FLAG_WS_HANDSHAKED)
        {
            byte ping[2];

            ping[0] = WS_OP_PING | WS_CTLF_FIN;
            ping[1] = 0;

            net_send(ple->s, ping, sizeof(ping));
        }
    }
}



/*
 * WebStartup
 *
 */
uib WebStartup()
{
    if(!(plSessions = (Session*) VirtualAlloc(NULL, CFG_MAX_SESSIONS * sizeof(Session), MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE)))
        return FALSE;

    freelist_init(&flSessions, plSessions, sizeof(Session), CFG_MAX_SESSIONS);

    // Create websocket server socket and begin listening.
    if((sListen = net_create(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL)) == NET_BAD_SOCKET || 
       !net_ctrl(sListen, IPPROTO_TCP, TCP_NODELAY, (void*) "\x01\x00\x00\x00", 4) || 
       !net_ctrl(sListen, SOL_SOCKET, SO_SNDBUF, (void*) "\x00\x00\x00\x00", 4) ||
       !net_bind(sListen, NULL, CFG_WEBSOCKET_PORT) || 
       !net_listen(sListen, 128))
    {
        LERR("Failed to create websocket server socket");
        return FALSE;
    }

    net_event_filter(sListen, NETEV_ACPT | NETEV_RECV);
    net_event_callback(sListen, &OnWsHandshake, NULL, NULL, &OnAccept);

    for(uib i = 0; i < ACPT_QUEUE_DEPTH; i++)
    {
        if(!net_accept(sListen, net_create(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL)))
        {
            LERR("Failed to initiate accept");
            return FALSE;
        }
    }

    // Create ping timer
    if(!(htPing = timer_create(TcbPing, NULL)))
        return FALSE;

    timer_set(htPing, 0, CFG_BSP_KEEPALIVE_INT);
    
    return TRUE;
}


/*
 * WriteWsHdr
 *
 */
static uib WriteWsHdr(byte* pHdr, uid PayloadLen)
{
    if(PayloadLen <= 125)
    {
        pHdr[2] = WS_OP_BIN|WS_CTLF_FIN;
        pHdr[3] = (uib) PayloadLen;
        return 2;
    }

    else
    {
        pHdr[0] = WS_OP_BIN|WS_CTLF_FIN;
        pHdr[1] = 126;
        *((uiw*) &pHdr[2]) = bswap16(((uiw) PayloadLen));
        return 4;
    }
}


/*
 * Session::Send
 *
 */
uib Session::Send(byte* pHdrSlackedBuffer, uid Length)
{
    uib HdrLen = WriteWsHdr(pHdrSlackedBuffer-BSP_FRAME_HDR_SZ, Length);

    Length += HdrLen;
    pHdrSlackedBuffer -= HdrLen;

    return net_send(s, pHdrSlackedBuffer, Length);
}



/*
 * ChargeHost
 *
 */
uib ChargeHost(host* pHost, uid Charge)
{
    uid ts = GetTickCount();

    
    // Refill tokens.
    pHost->bkt_tokens += (((ts - pHost->ts_last_add) >> 10) * CFG_BSP_TKNBKT_RATE);
    pHost->bkt_tokens = __min(pHost->bkt_tokens, CFG_BSP_TKNBKT_MAX_SIZE);
    pHost->ts_last_add = ts;

    if(Charge >= pHost->bkt_tokens)
    {
        pHost->bkt_tokens = 0;
        return FALSE;
    }

    else
    {
        pHost->bkt_tokens -= (uib) Charge;
        return TRUE;
    }
}
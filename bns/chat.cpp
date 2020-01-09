/*
 * chat.cpp
 *
 */
#include "chat.h"
#include "bns.h"
#include "websocket.h"
#include "capi.h"





// Constants
#define MAX_TLS_HANDSHAKE_TOKEN  0x1000
#define RECV_BUFF_SZ  0x4000-sizeof(void*)
#define RECV_QUEUE_DEPTH 4
#define MSG_SCRATCH_SZ  0x200

//#define USE_ECHO_SERVER
#define USE_TLS


#ifndef USE_ECHO_SERVER
// Server config
#define CAPI_HOST   "connect-bot.classic.blizzard.com" /* 117.52.35.110 */
#define CAPI_WS_URI  "/v1/rpc/chat"
#else
// Debugging
#define CAPI_HOST  "echo.websocket.org"
#define CAPI_WS_URI   "/echo"
#endif

#ifdef USE_TLS
#define CAPI_PORT  443
#else
#define CAPI_PORT 80
#endif

// Websocket handshake request
static const char WS_HANDSHAKE_REQUEST[] =
    "GET " CAPI_WS_URI " HTTP/1.1"
    "\r\nHost: " CAPI_HOST
    "\r\nUpgrade: websocket"
    "\r\nConnection: Upgrade"
    "\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ=="
    "\r\nSec-WebSocket-Protocol: json"
    "\r\nSec-WebSocket-Version: 13"
    "\r\n\r\n";

// Globals
static uid HostAddr;
static char Scratch[MSG_SCRATCH_SZ];

// Calculate websocket header size for given payload length
#define WsGetHdrSize(_payload_sz) (6 + (_payload_sz > 125 ? 2 : 0))



static void TestTimerCb(Chat* pThis)
{
    LOG("Sending chat");

    char tmp[128];
    static uid i;

    strfmt(tmp, sizeof(tmp), "Test message %u", ++i);

    pThis->SendChatMessage(tmp);
}

/*
 * OnConnect
 *
 */
static void NETLIBCALLBACK OnConnect(Chat* pThis, void* _1, uid _2, uid Error, net_addr*)
{
    if(Error)
    {
        SetLastError(Error);
        LERR("Failed to connect to CAPI server");
        goto FAIL;
    }

#ifdef USE_TLS
    LOG("Connected to server. Begin TLS handshake");

    byte Buffer[MAX_TLS_HANDSHAKE_TOKEN];
    uid Result;

    if(!tls_handshake(&pThis->tls.ctx, NULL, NULL, Buffer, &(Result = sizeof(Buffer))))
    {
        LERR("Failed to begin TLS handshake.");
        goto FAIL;
    }

    if(Result)
    {
        if(!net_send(pThis->s, Buffer, Result))
        {
            LERR("Failed to send initial TLS handshake token");
        }

        else
            LDBG("Sent %u initial TLS handshake", Result);
    }

#else
    pThis->TlsOnConnect();
#endif

    for(uib i = 0; i < RECV_QUEUE_DEPTH; i++)
    {
        if(!net_recv(pThis->s, NULL, RECV_BUFF_SZ))
        {
            LERR("Failed to initiate receive");
            goto FAIL;
        }
    }

    // Success
    return;

FAIL:
    pThis->Disconnect();
}

/*
 * OnReceive
 *
 */
static void NETLIBCALLBACK OnReceive(Chat* pThis, byte* pBuffer, uid Bytes, uid Error, net_addr*)
{
    if(Error)
    {
        SetLastError(Error);
        LERR("Connection terminated");
        goto FAIL;
    }

    if(!Bytes)
    {
        LOG("Connection closed gracefully by remote host");
        goto FAIL;
    }

#ifdef USE_TLS
    if(!pThis->TlsProcess(pBuffer, Bytes))
    {
        LERR("Receive processing failed");
        goto FAIL;
    }
#else
    if(!pThis->WsProcess(pBuffer, Bytes))
    {
        LERR("Failure during WsOnReceive");
        goto FAIL;
    }
#endif

    if(!net_recv(pThis->s, NULL, RECV_BUFF_SZ))
    {
        LERR("Failed to initiate receive");
        goto FAIL;
    }

    // Success
    return;

FAIL:
    pThis->Disconnect();
}


/*
 * Chat::Initialize
 *
 */
uib Chat::Initialize(chat_cfg* pCfg)
{
    s = NET_BAD_SOCKET;
    this->pCfg = pCfg;

    if(!rb_init(&tls.rb, 0x10000, NULL))
        return FALSE;

    if(!rb_init(&ws.rb, 0x10000, NULL))
        return FALSE;

    return TRUE;
}


/*
 * Chat::Connect
 *
 */
void Chat::Connect()
{
    if((s = net_create(AF_INET, SOCK_STREAM, IPPROTO_TCP, this)) == NET_BAD_SOCKET || !net_bind(s, NULL, NULL) || !net_ctrl(s, IPPROTO_TCP, TCP_NODELAY, (void*) "\x01\x00\x00\x00", 4))
    {
        LERR("Failed to create chat socket");
        goto FAIL;
    }

    net_event_filter(s, NETEV_CONN | NETEV_RECV);
    net_event_callback(s, &::OnReceive, NULL, &::OnConnect, NULL);

    if(!HostAddr)
    {
        hostent* ph;

        if(!(ph = gethostbyname(CAPI_HOST)))
        {
            LERR("Failed to resolve CAPI host " CAPI_HOST);
            goto FAIL;
        }

        HostAddr = *((uid*) ph->h_addr_list[0]);

        LOG("Resolved CAPI host " CAPI_HOST " to: %a", HostAddr);
    }

    LOG("Initiating connection to CAPI server.. ");

    if(!net_connect(s, HostAddr, CAPI_PORT))
    {
        LERR("Failed to initiate connection");
        goto FAIL;
    }

    // Pending connect
    return;

FAIL:
    Disconnect();
}


/*
 * Chat::TlsProcess
 *
 */
uib Chat::TlsProcess(byte* pData, uid Bytes)
{
    if(!rb_write(&tls.rb, pData, Bytes))
        return FALSE; // Buffer overflow.

    while((Bytes = rb_length(&tls.rb)))
    {
        pData = rb_read_ptr(&tls.rb);
        uid Result = 0;

        if(tls.ctx.flags & TLS_CTX_FLAG_HANDSHAKED)
        {
            if(!tls_decrypt(&tls.ctx, &pData, &Bytes, &Result))
            {
                LERR("tls_decrypt() failure");
                return FALSE;
            }
        }

        else
        {
            byte OutBuffer[MAX_TLS_HANDSHAKE_TOKEN*4];
            uid OutSz = sizeof(OutBuffer);

            memzero(OutBuffer, sizeof(OutBuffer));

            if(!tls_handshake(&tls.ctx, pData, &Bytes, OutBuffer, &OutSz))
            {
                LERR("tls_handshake() failure");
                return FALSE;
            }

            else if(OutSz && !net_send(s, OutBuffer, OutSz))
                return FALSE;

            if(tls.ctx.flags & TLS_CTX_FLAG_HANDSHAKED)
            {
                if(!TlsOnConnect())
                    return FALSE;
            }
        }

        if(!Bytes) /* Insufficient/partial data */
            return TRUE;

        rb_read_ex(&tls.rb, Bytes);

        if(Result && !WsProcess(pData, Result))
            return FALSE;
    }

    return TRUE;
}


/*
 * Chat::TlsOnConnect
 *   TLS handshake completed successfully.
 *
 */
uib Chat::TlsOnConnect()
{
    LOG("TLS handshake done; sending websocket handshake request.");

    if(!Send(WS_HANDSHAKE_REQUEST, sizeof(WS_HANDSHAKE_REQUEST)-1))
    {
        LERR("Failed to send websocket handshake request");
        return FALSE;
    }

    return TRUE;
}


/*
 * Chat::Disconnect
 *
 */
void Chat::Disconnect()
{
    tls_cleanup(&tls.ctx);
    
    if(s != NET_BAD_SOCKET)
        net_close(s);
}


/*
 * Chat::WsProcess
 *
 */
uib Chat::WsProcess(byte* pData, uid Bytes)
{
    LDBG("Received application data: %u Bytes", Bytes);

    rb_write(&ws.rb, pData, Bytes);

#ifdef DBG_CAPI_DUMP_RX
    LDBG("Data available (%u):", rb_length(&ws.rb));

    LogHex(rb_read_ptr(&ws.rb), rb_length(&ws.rb));
#endif


    // Still in handshake state.
    if(!ws.handshake_status_response)
    {
        LDBG("Parsing %u bytes of websocket HTTP handshake response", rb_length(&ws.rb));

        if(!(Bytes = http_parse_response((const char*) rb_read_ptr(&ws.rb), rb_length(&ws.rb), &ws.handshake_status_response)))
        {
            LDBG("Insufficient data for HTTP handshake response");
            return TRUE; // Incomplete response.
        }

        LDBG("Websocket HTTP handshake status: %u", ws.handshake_status_response);

        if(ws.handshake_status_response != 101 || !OnConnect())
            return FALSE;
    }
    
    // Websocket protocol processing.
    else
    {
        Bytes = rb_length(&ws.rb);
        byte* p = rb_read_ptr(&ws.rb);

        LDBG("Processing %u bytes of websocket frame data", Bytes);

        while(Bytes)
        {
            uib HdrLen = WS_MIN_HDR_SZ;
            uid PayloadLen = 0;
            uid FrameLen = 0;

            if(Bytes < WS_MIN_HDR_SZ)
            {
                LDBG("Insufficient data for websocket header (%u)", Bytes);
                break;
            }

            const uib opcode = p[0] & WS_OP_MASK;
            const uib flags = p[0] & WS_CTRLF_MASK;
            const uib data_len = p[1];

            LDBG("Received websocket protocol frame; flags=0x%X opcode=0x%X data_len=%u", flags, opcode, data_len);

            if(data_len < 126)
            {
                PayloadLen = data_len;
            }

            else if(data_len == 126)
            {
                HdrLen += 2;
                PayloadLen = bswap16(*((uiw*) &p[2]));
            }
            
            else
            {
                LWARN("Unable to handle 64-bit websocket protocol payloads");
                return FALSE;
            }

            FrameLen = HdrLen + PayloadLen;

            if(Bytes < HdrLen)
            {
                LDBG("Insufficient data for complete websocket header (%u < %u)", Bytes, HdrLen);
                break;
            }

            if(Bytes < FrameLen)
            {
                LDBG("Insufficient data for websocket frame (%u < %u)", Bytes, FrameLen);
                break;
            }
            
            LDBG("Dispatching websocket frame; HdrLen=%u PayloadLen=%u; FrameLen:%u", HdrLen, PayloadLen, FrameLen);

#ifdef DBG_CAPI_DUMP_RX
            //LogHex(p, FrameLen);
#endif
            switch(opcode)
            {
                case WS_OP_TEXT:
                {
                    if(!this->OnReceive((char*) (p+HdrLen), PayloadLen))
                    {
                        LDBG("CAPI receiving processing failed");
                        return FALSE;
                    }
                } break;

                case WS_OP_CLOSE:
                {
                    return FALSE;
                } break;

                case WS_OP_PING:
                {
                    p[0] = WS_OP_PONG | WS_CTLF_FIN;

                    Send(p, FrameLen);

                    LDBG("Sent pong reply");
                } break;

                default:
                {
                    LWARN("Unhandled websocket op: 0x%X", opcode);
                }
            }

            Bytes -= FrameLen;
            p += FrameLen;
        }

        Bytes = rb_length(&ws.rb) - Bytes;
    }

    rb_read_ex(&ws.rb, Bytes);

    return TRUE;
}


/*
 * Chat::OnConnect
 *   Websocket handshake completed successfully.
 *
 */
uib Chat::OnConnect()
{
    return CAPI_Auth(pCfg->auth_key);
}



/*
 * Chat::OnReceive
 *   Websocket (CAPI) payload received; JSON.
 *
 */
uib Chat::OnReceive(char* pData, uid Length)
{
    capi_msg msg;

    if(Length)
        pData[Length-1] = '\0';

    CAPIParse(pData, &msg);

    LDBG("Command: %s", CAPI_TXT_VALUE[msg.f[CAPI_FIELD_COMMAND]._int]);

    // CAPI message handling.
    switch(msg.f[CAPI_FIELD_COMMAND]._int)
    {
        case CAPIS_AUTH:
        {
            LOG("Authenticate response; status code:%u", msg.f[CAPI_FIELD_CODE]._int);

            CAPI_Connect();
        }
        break;

        case CAPIS_CONNECT:
        {
            LOG("Connect response; status code:%u", msg.f[CAPI_FIELD_CODE]._int);
        }
        break;

        case CAPIS_ENTER_CHANNEL:
        {
            LOG("Entered channel; name=%s", msg.f[CAPI_FIELD_CHANNEL]._pstr ? msg.f[CAPI_FIELD_CHANNEL]._pstr : "<NULL>");

            State = CHAT_STATE_READY;
        }
        break;
    }

    return TRUE;
}



/*
 * Chat::TlsSend
 *
 */
uib Chat::TlsSend(void* pNetIoBuffer, uid Bytes)
{
#ifdef USE_TLS
    //LDBG("TlsSend(): %u plaintext bytes", Bytes);

    if(!tls_encrypt(&tls.ctx, (byte*) pNetIoBuffer, &Bytes))
    {
        LERR("tls_encrypt() failed");
        goto FAIL;
    }

    //LDBG("TlsSend(): %u encrypted bytes", Bytes);
#endif

    if(!net_send(s, pNetIoBuffer, Bytes, NETIO_NETHEAP))
    {
        LERR("net_send() failed");
        goto FAIL;
    }

    return TRUE;

FAIL:
    net_mm_free(pNetIoBuffer);
    return FALSE;
}


/*
 * Chat::WsSend
 *
 */
uib Chat::WsSend(const void* pData, uid Bytes)
{
    byte* pNetIoBuffer;
    uib WsHdrSize = WsGetHdrSize(Bytes);

    if(!(pNetIoBuffer = (byte*) net_mm_alloc(tls.ctx.header_sz + tls.ctx.trailer_sz + WsHdrSize + Bytes)))
        return FALSE;

    byte* pd = pNetIoBuffer + tls.ctx.header_sz;

    // Websocket header.
    *pd++ = WS_OP_TEXT | WS_CTLF_FIN;
    
    if(Bytes > 125)
    {
        *pd++ = 126 | WS_FLAG_MASKED;
        *((uiw*) pd) = bswap16((uiw) Bytes);
        pd += 2;
    }

    else
    {
        *pd++ = ((uib) Bytes) | WS_FLAG_MASKED;
    }

    *((uid*) pd) = 0xDEADBEEF;
    pd += 4;

    ws_mask(0xDEADBEEF, pData, Bytes, pd);

    return TlsSend(pNetIoBuffer, WsHdrSize + Bytes);
}


/*
 * Chat::Send
 *
 */
uib Chat::Send(const void* pData, uid Bytes)
{
    byte* pNetIoBuffer;

    if(!(pNetIoBuffer = (byte*) net_mm_alloc(tls.ctx.header_sz + tls.ctx.trailer_sz + Bytes)))
        return FALSE;

    memcpy(pNetIoBuffer + tls.ctx.header_sz, pData, Bytes);

    return TlsSend(pNetIoBuffer, Bytes);
}

void Chat::SendChatMessage(const char* pMessage)
{
    static uid NextRequestId;
    static char CAPI_SEND_MESSAGE_REQUEST[] =
        "{"
        "\"command\": \"Botapichat.SendMessageRequest\","
        "\"requestId\": %u,"
        "\"payload\": {"
        "\"message\": \"%s\""
        "}"
        "}";

    if(State != CHAT_STATE_READY)
        return;

    uid Len = strfmt(Scratch, sizeof(Scratch), CAPI_SEND_MESSAGE_REQUEST, ++NextRequestId, pMessage);

    for(char* p = Scratch; *p; p++)
    {
        if(*p < 0x20 || *p > 0x7E)
            *p = ' ';
    }

    WsSend(Scratch, Len);
}


/*
 * CAPI encoding wrappers
 *
 */
uib Chat::CAPI_Auth(const char* pKey)
{
    static const char CAPI_AUTH_FMT[] = "{\"command\":\"Botapiauth.AuthenticateRequest\",\"request_id\":1,\"payload\":{\"api_key\":\"%s\"}}";

    return WsSend(Scratch, strfmt(Scratch, sizeof(Scratch), CAPI_AUTH_FMT, pKey));
}

uib Chat::CAPI_Connect()
{
    static const char CAPI_CONNECT_FMT[] = "{\"command\":\"Botapichat.ConnectRequest\",\"request_id\":1,\"payload\":{}}";

    return WsSend(CAPI_CONNECT_FMT, sizeof(CAPI_CONNECT_FMT)-1);
}
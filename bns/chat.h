/*
 * chat.h
 *
 */
#ifndef CHAT_H
#define CHAT_H
#include <netlib/netlib.h>
#include "tls.h"
#include "ringbuffer.h"
#include "bns.h"



// Chat states
#define CHAT_STATE_READY  1

struct chat_cfg
{
    char auth_key[64];
};

struct Chat
{
    uiw s;
    uib State;
    chat_cfg* pCfg;


    struct
    {
        tls_ctx ctx;
        ring_buffer rb;
    } tls;

    struct
    {
        ring_buffer rb;
        uid handshake_status_response;
    } ws;

    // Methods
    uib Initialize(chat_cfg* pCfg);
    void SendChatMessage(const char* pMessage);

    // Internal
    void Connect();
    void Disconnect();
    uib TlsProcess(byte* pData, uid Bytes);
    uib TlsOnConnect();
    uib WsProcess(byte* pData, uid Bytes);
    uib OnConnect();
    uib OnReceive(char* pData, uid Length);
    uib TlsSend(void* pIoBuffer, uid Bytes);
    uib WsSend(const void* pData, uid Bytes);
    uib Send(const void* pData, uid Bytes);

    // CAPI
    uib CAPI_Auth(const char* pKey);
    uib CAPI_Connect();
};



#endif
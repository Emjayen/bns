/*
 * prx.cpp
 *
 */
#include <netlib/netlib.h>
#include "prx.h"
#include "bns.h"
#include <pxp.h>




// Constants
#define PXP_USERNAME  "bns"
#define RECV_BUFF_SZ      512
#define RECV_QUEUE_DEPTH  2
#define RECONNECT_TIMEOUT  8000
#define MAX_PROXIES  128


// Globals
static uiw s;
static HTIMER htReconnect;
static HTIMER htPing;
static proxy Proxies[MAX_PROXIES]; // = { 0, 0, inet_addr("9.9.9.9"), 111, 0, 10, 0 };
static uid ProxyCount; // = 1;



/*
 * GetUserMaximumConnections
 *
 */
static uib GetUserMaximumConnections(char* pPrxUsername)
{
#ifndef DEPLOY_LIVE
    return 1;
#else
    if(cmpstri(pPrxUsername, "dim"))
        return 0;

    else if(cmpstri(pPrxUsername, "matt"))
        return 6;

    return 6;
#endif
}


/*
 * LookupProxy
 *
 */
static void RegisterProxy(uid Addr, uiw Port, char* pUsername)
{
    uid ts = GetTickCount();
    proxy* p = NULL;

    for(uid i = 0; i < ProxyCount; i++)
    {
        if(Proxies[i].addr == Addr)
        {
            p = &Proxies[i];
            break;
        }
    }

    if(!p)
    {
        if(ProxyCount >= ARRAYSIZE(Proxies))
        {
            LWARN("Reached proxy limit");
            return;
        }

        p = &Proxies[ProxyCount++];
    }

    p->addr = Addr;
    p->port = Port;
    strcpyn(p->username, sizeof(p->username), pUsername);
    p->max_connections = GetUserMaximumConnections(p->username);

    LOG("Proxy list updated:");

    for(uid i = 0; i < ProxyCount; i++)
    {
        LOG("  %a %u/%u (user:%s next_ready:%u next_acquire:%u failures:%u ts:%u)",
            Proxies[i].addr,
            Proxies[i].connection_count,
            Proxies[i].max_connections,
            Proxies[i].username,
            Proxies[i].ts_next_acquire,
            Proxies[i].ts_next_ready,
            Proxies[i].failure_count,
            ts);
    }
}




/*
 * AcquireProxy
 *
 */
proxy* AcquireProxy()
{
    proxy* pBest = NULL;
    uid ts = GetTickCount();
    
    for(uid i = 0; i < ProxyCount; i++)
    {
        proxy& rp = Proxies[i];

        if(rp.connection_count >= rp.max_connections || ts < rp.ts_next_ready)
            continue;

        if(!pBest || rp.connection_count < pBest->connection_count)
            pBest = &rp;
    }

    if(!pBest || ts < pBest->ts_next_acquire)
        return NULL;

    pBest->connection_count++;
    pBest->ts_next_acquire = ts + CFG_PROXY_CONNECT_DELAY;

    return pBest;
}


/*
 * ReleaseProxy
 *
 */
void ReleaseProxy(proxy* pProxy)
{
    pProxy->connection_count--;
}


/*
 * NotifyProxyFailure
 *
 */
void NotifyProxyFailure(proxy* pProxy)
{
    uid ts = GetTickCount();

    if(ts > pProxy->ts_next_ready)
    {
        pProxy->failure_count++;
        pProxy->failure_seq++;

        if(pProxy->failure_seq == CFG_PROXY_FAIL_SEQUENCE_RESET)
            pProxy->failure_seq = 1;

        pProxy->ts_next_ready = ts + (pProxy->failure_seq * CFG_PROXY_FAIL_BASE_TIMEOUT);

        LWARN("Proxy %a (%s) failure occured; failure_seq:%u failure_count:%u", pProxy->addr, pProxy->username, pProxy->failure_seq, pProxy->failure_count);
    }
}


/*
 * Disconnect
 *
 */
static void Disconnect()
{
    LOG("PXM disconnected");

    if(s != NET_BAD_SOCKET && !net_close(s))
    {
        LERR("Failed to close PXM socket");
    }

    timer_set(htReconnect, 0, RECONNECT_TIMEOUT);
    timer_set(htPing, NULL, NULL);
}


/*
 * OnConnect
 *
 */
static void NETLIBCALLBACK OnConnect(void*, void* _1, uid _2, uid Error, net_addr*)
{
    if(Error)
    {
        SetLastError(Error);
        LERR("Failed to connect to PXM server");
        goto FAIL;
    }

    for(uib i = 0; i < RECV_QUEUE_DEPTH; i++)
    {
        if(!net_recv(s, NULL, RECV_BUFF_SZ))
        {
            LERR("Failed to initiate PXM receive I/O");
            goto FAIL;
        }
    }

    LOG("Connected to PXM server");

    union
    {
        pxp_helo helo;
        byte _pad[sizeof(pxp_helo) + 16];
    };

    helo.magic = PXP_MAGIC;
    helo.mid = PXP_HELO;
    helo.len = sizeof(pxp_helo) + sizeof(PXP_USERNAME);
    helo.reserved[0] = 0;
    helo.reserved[1] = 0;
    helo.reserved[2] = 0;
    helo.auth_key = 0x81FA9810;
    memcpy(helo.username, PXP_USERNAME, sizeof(PXP_USERNAME));

    if(!net_send(s, &helo, helo.len))
    {
        LERR("Failed to send PXM HELO");
        goto FAIL;
    }

    timer_set(htPing, NULL, 20000);

    // Success
    return;

FAIL:
    Disconnect();
}


/*
 * OnReceive
 *
 */
static void NETLIBCALLBACK OnReceive(void*, byte* pBuffer, uid Bytes, uid Error, net_addr*)
{
    if(Error || !Bytes)
    {
        SetLastError(Error);
        LERR("PXM I/O failure");
        goto FAIL;
    }

    union
    {
        byte* p;
        pxp_hdr* pHdr;
        pxp_query* pQuery;
    };

    p = pBuffer;

    if(Bytes < sizeof(pxp_hdr) || Bytes < pHdr->len)
    {
        LWARN("Partial PXM message received");
    }

    else if(pHdr->magic != PXP_MAGIC)
    {
        LWARN("Received corrupt PXM message");
    }

    else if(pHdr->mid != PXP_QUERY)
    {
        LWARN("Received unknown PXP message: 0x%X", pHdr->mid);
    }

    else
    {
        uid count = (pQuery->len - sizeof(pxp_hdr)) / sizeof(pxp_query::_proxy);

        LOG("Received PXM proxy update; %u entries", count);

        for(uid i = 0; i < count; i++)
        {
            RegisterProxy(pQuery->proxies[i].addr, bswap16(1590), pQuery->proxies[i].username);
        }
    }


    if(!net_recv(s, NULL, RECV_BUFF_SZ))
    {
        LERR("Failed to initiate PXM receive I/O");
        goto FAIL;
    }

    // Success
    return;

FAIL:
    Disconnect();
}


/*
 * Connect
 *   Connect to PXM server
 *
 */
static void Connect(void*)
{
    hostent* ph;
    uid Addr;

    timer_set(htReconnect, NULL, NULL);

    if(!(ph = gethostbyname("lbs.laserblue.org")))
    {
        LERR("Failed to resolve pxm server");
        goto FAIL;
    }

    Addr = *((uid*) ph->h_addr_list[0]);

    LOG("Connecting to PXM server %a", Addr);

    if((s = net_create(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL)) == NET_BAD_SOCKET || !net_bind(s, NULL, NULL) || !net_connect(s, Addr, PXP_PORT))
    {
        LERR("Failed to initiate connection to PXM server");
        goto FAIL;
    }

    net_event_filter(s, NETEV_RECV|NETEV_CONN);
    net_event_callback(s, &OnReceive, NULL, &OnConnect, NULL);

    // Success
    return;

FAIL:
    Disconnect();
}


/*
 * Ping
 *
 */
static void Ping(void*)
{
    pxp_ping msg;

    msg.magic = PXP_MAGIC;
    msg.mid = PXP_PING;
    msg.len = sizeof(msg);
    msg.token = 'BNS ';

    net_send(s, &msg, msg.len);
}


/*
 * PrxStartup()
 *
 */
uib PrxStartup()
{
    if(!(htReconnect = timer_create(&Connect, NULL)))
        return FALSE;

    if(!(htPing = timer_create(&Ping, NULL)))
        return FALSE;

    Connect(NULL);

    return TRUE;
}

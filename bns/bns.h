/*
 * bns.h
 *
 */
#ifndef BNS_H
#define BNS_H
#include "helper.h"
#include "syslog.h"





// Game types
union gt
{
    uid v;

    struct
    {
        uid is_nightmare : 1;
        uid is_hell : 1;
        uid is_ladder : 1;
        uid is_hardcore : 1;
        uid is_expansion : 1;
        uid realm : 2;
    };
};


#define GT_MODE_MASK       0x7F
#define GT_TYPE_IDX_MASK   0x7F
#define GT_TOTAL_REALMS    4

#define GT_MASK_REALM      0x60
#define GT_REALM_USEAST    0x0
#define GT_REALM_USWEST    0x1
#define GT_REALM_EUROPE    0x2
#define GT_REALM_ASIA      0x3

#define GT_FLAG_NORMAL     (0)
#define GT_FLAG_NIGHTMARE  (1<<0)
#define GT_FLAG_HELL       (1<<1)
#define GT_FLAG_LADDER     (1<<2)
#define GT_FLAG_HARDCORE   (1<<3)
#define GT_FLAG_EXPANSION  (1<<4)

// Internal flags
#define GT_FLAG_OPEN (1<<31) /* We've had authoritive confirmation this game exists */
#define GT_FLAG_INFO (1<<30) /* We've received extended (query/info) on this game */



/*
 * bn_acc_desc
 *
 */
struct bn_acc_desc
{
    uib realm;
    const char* name;
    const char* password;
    const char* character;
    const char* channel;
};


/*
 * bn_key_desc
 *
 */
struct bn_key_desc
{
    const char* d2dv;
    const char* d2xp;
};


/*
 * Various system configuration settings (that should probably be exposed)
 *
 */

// General system
#define CFG_LOG_FILE          "bns.log"
#define CFG_PROCESS_PRIORITY  ABOVE_NORMAL_PRIORITY_CLASS
#define CFG_THREAD_PRIORITY   THREAD_PRIORITY_HIGHEST
#define CFG_EVTMSG_FLUSH_INT  1000
#define CFG_GAME_POOL_SZ      0x2000
#define CFG_MAX_GAME_ID_MASK  0x7FF /* Must be power of 2-1 */
#define CFG_MAX_SESSIONS      0x8000
#define CFG_WEBSOCKET_PORT    1504
#define CFG_BSP_KEEPALIVE_INT 15000

// Client (BSP) rate limiting
#define CFG_BSP_MAX_CONNECTIONS 4 /* Maximum concurrent websocket connections */
#define CFG_BSP_TKNBKT_TCP_COST 1 /* Cost per TCP connection attempt, in tokens */
#define CFG_BSP_TKNBKT_REQ_COST 1 /* Cost per game list request, in tokens */
#define CFG_BSP_TKNBKT_RX_COST  1 /* Cost per byte */
#define CFG_BSP_TKNBKT_MAX_SIZE 12 /* Maximum bucket size, in tokens */
#define CFG_BSP_TKNBKT_RATE     8 /* Tokens/1024th second */

// netlib
#define CFG_NETLIB_HEAP_SZ  0xFFFFFF /* 16MB */
#define CFG_NETLIB_SOCKETS  0x2000 /* 8K */
#define CFG_NETLIB_IOPS     (CFG_NETLIB_SOCKETS * 8)

// Maximum userland timers
#define CFG_MAX_TIMERS  512

// Game query rate control (delay)
#define CFG_MCP_QUERY_DELAY 1000

// Game list rate control (delay)
#define CFG_MCP_LIST_DELAY 1000

// Timeout for MCP game query/info requests.
#define CFG_MCP_QUERY_REQUEST_TIMEOUT  3000

// Timeout for MCP game list requests.
#define CFG_MCP_LIST_REQUEST_TIMEOUT  3000

// Simple timeout period between Battle.net connections from a particular address/proxy.
#define CFG_PROXY_CONNECT_DELAY  25000

// Proxy time outs.
#define CFG_PROXY_FAIL_SEQUENCE_RESET  5
#define CFG_PROXY_FAIL_BASE_TIMEOUT    35000 

// Minimum of the default system tick interval (16*2)
#define CFG_MIN_TIMER_ACCURACY   64

// Timers that expire with an accuracy less than this will generate a warning in the syslog.
#define CFG_TIMER_ACCURACY_WARN  100

// Battle.net client identity, etc.
#define CFG_BNET_GAME_VERSION   ":1.14.3.71:"
#define CFG_BNET_PRODUCT_OWNER  "bns"

// prx/pxm related
#define CFG_PRX_SOCKS4_IDENT    "prxmagicx86"

// DNS lookup cache time-to-live, in milliseconds.
#define CFG_BNET_SVR_CACHE_TTL  (15 * 60 * 1000)

// Configuration defaults
#define CFG_DEFAULT_PASSWORD "dolphin&552"
#define CFG_DEFAULT_CHANNEL  "bns-backstage"

// cfg
extern const bn_acc_desc cfg_accounts[];
extern const bn_key_desc cfg_keys[];
extern const uid cfg_accounts_sz;
extern const uid cfg_keys_sz;



/*
 * Logging
 *
 */
 // Assertion
#define ASSERT(_exp) if(!(_exp)) { LERR("ASSERTION! " #_exp); ExitProcess(0xDEADBEEF); }

// Debugging
//#define DBG_CAPI_DUMP_RX
//#define DBG_TLS_REPORT_STATUS


void Log(uib Level, const char* pFormat, ...);
void Fatal(const char* pFormat, ...);
uid GetTimeUTC();

#define LERR(_pFormat, ...) Log(LOG_ERR, _pFormat, __VA_ARGS__)
#define LOG(_pFormat, ...) Log(LOG_INFO, _pFormat, __VA_ARGS__)
#define LWARN(_pFormat, ...) Log(LOG_WARN, _pFormat, __VA_ARGS__)
#define FATAL(_pFormat, ...) Fatal(_pFormat, __VA_ARGS__)

#ifdef DEBUG
#define LDBG(_pFormat, ...) Log(LOG_DBG, _pFormat, __VA_ARGS__)
#else
#define LDBG(_pFormat, ...)
#endif


#endif
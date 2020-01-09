/*
 * bns
 *
 */
#include "bns.h"
#include <netlib\netlib.h>
#include "bnc.h"
#include "chat.h"
#include "prx.h"
#include "web.h"
#include "iptbl.h"
#include "server.h"


// Forwards
struct Game;


// Globals
static uid st_systime_cache; // Cached UNIX timestamp of current time.
static uid ts_tick_cache; // Cached current tick.


static Chat east;
static Chat west;
static Chat euro;
static BnClient bnc[32];



void SystemHealthCheck()
{
    uid ts = GetTickCount();

    for(uid i = 0; i < cfg_accounts_sz; i++)
    {
        if(bnc[i].State != BNC_STATE_MCP_LOGGED)
            LDBG("!!! Client not online: %s (%s)", bnc[i].pAccount->name, bnc[i].pKey->d2dv);

        else
        {
#ifdef DEBUG
            if((ts - bnc[i].dbg_ts_last_succesful_query) > 5000)
                LWARN("[%s] No successful query in %u seconds", bnc[i].pAccount->name, (ts - bnc[i].dbg_ts_last_succesful_query)/1000);

            if((ts - bnc[i].dbg_ts_last_successful_list) > 5000)
                LWARN("[%s] No successful list in %u seconds", bnc[i].pAccount->name, (ts - bnc[i].dbg_ts_last_successful_list)/1000);
#endif
        }
    }
}



/*
 * UpdateSystemTimeCache
 *
 */
static void UpdateSystemTimeCache()
{
    uiq ft;
    GetSystemTimeAsFileTime((FILETIME*) &ft);
    st_systime_cache = (uid) ((ft / 10000000) - 11644473600LL);
}


/*
 * Startup
 *
 */
uib Startup()
{
    UpdateSystemTimeCache();

    srnd(GetTickCount64());

    if(!init_timers(CFG_MAX_TIMERS))
    {
        LERR("init_timers() failed");
        return FALSE;
    }

    if(!net_init(CFG_NETLIB_HEAP_SZ, CFG_NETLIB_SOCKETS, CFG_NETLIB_IOPS))
    {
        LERR("net_init() failed");
        return FALSE;
    }

    if(!tls_startup())
    {
        LERR("tls_startup() failed");
        return FALSE;
    }

    if(!WebStartup())
    {
        LERR("WebStartup() failed");
        return FALSE;
    }

    if(!SetPriorityClass(GetCurrentProcess(), CFG_PROCESS_PRIORITY) || !SetThreadPriority(GetCurrentThread(), CFG_THREAD_PRIORITY))
    {
        LERR("Failed to adjust process/thread priority");
        return FALSE;
    }

    if(!PrxStartup())
    {
        LERR("PrxStartup() failed");
        return FALSE;
    }

    return TRUE;
}






uib AppEntry()
{
    if(!Startup())
        return FALSE;

    if(!ServerStartup())
        return FALSE;


    for(uid i = 0; i < cfg_accounts_sz && i < cfg_keys_sz; i++)
    {
        bnc[i].Initialize(&cfg_accounts[i], &cfg_keys[i]);
        bnc[i].Connect();
    }


    chat_cfg chat_east =
    {
        "04ef54b6be4247f7cfc626a5b0cb5dbeffaf5abbab707a2fee86e97a"
    };

    chat_cfg chat_west =
    {
        "cb0916ba84ac9db1ef378f3484f557047926477dbc2d5b8c6eaa4811"
    };
    
    chat_cfg chat_euro =
    {
        "ab34d841c28a29de009e14cebbee14e102afd446d7af3b2232a45a68"
    };


    //east.Initialize(&chat_east);
    //west.Initialize(&chat_west);
    //euro.Initialize(&chat_euro);

   // east.Connect();
    //west.Connect();
    //euro.Connect();
   

    for(;;)
    {
        // Wait for events.
        net_wait(CFG_MIN_TIMER_ACCURACY / 2);

        // Update our system time cache.
        UpdateSystemTimeCache();

        // Fire off timers.
        fire_timers();

        uid ts = GetTickCount();

        static uid ts_last_dbg;

        if(ts > ts_last_dbg + 5000)
        {
            SystemHealthCheck();
            ts_last_dbg = ts;
        }
    }
}



uid GetTimeUTC()
{
    return st_systime_cache;
}






void Fatal(const char* pFormat, ...)
{
    char str[512];
    uid len;

    len = strfmtv(str, sizeof(str), pFormat, __valist(pFormat));
    LERR("Fatal exit: %s", str);

    ExitProcess(FALSE);
}
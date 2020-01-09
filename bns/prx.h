/*
 * prx.h
 *
 */
#ifndef PRX_H
#define PRX_H
#include <pce\pce.h>


/*
 * PrxStartup()
 *
 */
uib PrxStartup();

// Proxy
struct proxy
{
    uid ts_next_acquire; /* Time at which this proxy will be acquirable next. */
    uid ts_next_ready; /* Time at which this proxy will be ready again. */
    uid addr;
    uiw port;
    uib connection_count; /* Current number of connections via this proxy. */
    uib max_connections;
    uib failure_count; /* Total failures */
    uib failure_seq; /* Failures in sequence count */
    char username[32];
};


/*
 * AcquireProxy
 *
 */
proxy* AcquireProxy();


/*
 * ReleaseProxy
 *
 */
void ReleaseProxy(proxy* pProxy);


/*
 * NotifyProxyFailure
 *
 */
void NotifyProxyFailure(proxy* pProxy);


#endif
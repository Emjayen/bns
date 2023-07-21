/*
 * iptbl.h
 *
 */
#ifndef IPTBL_H
#define IPTBL_H
#include <pce\pce.h>



#define IPTBL_ENTRY  host
#define IPTBL_TBL_SIZE  0x100000
#define IPTBL_MAX_ENTRIES  256000


 /*
  * host
  *
  */
struct host
{
    uib conns; /* Current number of websocket connections */
    uid bkt_tokens; /* Current number of tokens */
    uid ts_last_add; /* Last time tokens were added */
};



/*
 * iptbl_lookup
 *
 */
IPTBL_ENTRY* pce_fastcall iptbl_lookup(uid addr);

/*
 * iptbl_init
 *
 */
void iptbl_init();


#ifdef DEBUG
extern uid iptbl_collisions;
extern uid iptbl_worst_probe;
#endif


#endif
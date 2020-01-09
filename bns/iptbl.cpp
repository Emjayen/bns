/*
 * iptbl.cpp
 *
 */
#include "iptbl.h"








struct tbl_idx
{
    uid addr;
    IPTBL_ENTRY* data;
};

static tbl_idx hash_tbl[IPTBL_TBL_SIZE];
static IPTBL_ENTRY entries[IPTBL_MAX_ENTRIES];
static freelist fl_entries;

#ifdef DEBUG
uid iptbl_collisions;
uid iptbl_worst_probe;
#endif





 /*
  * iptbl_lookup
  *
  */
IPTBL_ENTRY* pce_fastcall iptbl_lookup(uid addr)
{
    uid hash = (addr & (IPTBL_TBL_SIZE-1));

    while(hash_tbl[hash].addr != addr && hash_tbl[hash].data)
    {
        hash = (hash+1) & (IPTBL_TBL_SIZE-1);
    }

    if(!hash_tbl[hash].data)
    {
        hash_tbl[hash].addr = addr;
        hash_tbl[hash].data = (IPTBL_ENTRY*) freelist_alloc(&fl_entries);
    }
 
    return hash_tbl[hash].data;
}



/*
 * iptbl_init
 *
 */
void iptbl_init()
{
    freelist_init(&fl_entries, entries, sizeof(IPTBL_ENTRY), IPTBL_MAX_ENTRIES);
}
/*
 * bzstrmap.h
 *
 */
#ifndef BZSTRMAP_H
#define BZSTRMAP_H
#include <pce\pce.h>



template<typename T, uint TableSz> class bzstrmap
{
public:
    T* Lookup(const char* pstrKey, uib Length);
    void Insert(T* pKey);

private:
    T* kv_tbl[TableSz];
    uid hash_tbl[TableSz];

    uid crc32hash(const void* key);
    bool keycmp(const void* k1, const void* k2);
};


template<typename T, uint TableSz> uid bzstrmap<T, TableSz>::crc32hash(const void* key)
{
    uid hash = 0xFFFFFFFF;

    for(uib i = 0; i < sizeof(T::bzh_key)/sizeof(uid); i++)
    {
        hash = _mm_crc32_u32(hash, ((uid*) key)[i] & 0xDFDFDFDF);
    }

    return hash;
}


template<typename T, uint TableSz> bool bzstrmap<T, TableSz>::keycmp(const void* k1, const void* k2)
{
    for(uib i = 0; i < sizeof(T::bzh_key)/sizeof(uid); i++)
    {
        if((((uid*) k1)[i] & 0xDFDFDFDF) != (((uid*) k2)[i] & 0xDFDFDFDF))
            return false;
    }

    return true;
}


template<typename T, uint TableSz> void bzstrmap<T, TableSz>::Insert(T* key)
{
    uid hash = crc32hash((const void*) key->bzh_key);
    uid idx = hash & (TableSz-1);
    uid probe_dist = 0;


    for(;;)
    {
        if(!kv_tbl[idx])
        {
            kv_tbl[idx] = key;
            hash_tbl[idx] = hash;
            return;
        }

        uid dst_dist = (TableSz + probe_dist - (hash_tbl[idx] & (TableSz-1)));

        if(probe_dist > dst_dist)
        {
            SWAP(hash, hash_tbl[idx]);
            SWAP((*((uid*) key)), (*((uid*) kv_tbl[idx])));

            probe_dist = dst_dist;
        }

        probe_dist++;
        idx = (idx+1) & (TableSz-1);
    }
}


template<typename T, uint TableSz> T* bzstrmap<T, TableSz>::Lookup(const char* pstrKey, uib Length)
{
    byte key[sizeof(T::bzh_key)] = { 0 };
    uid idx;
    uid hash;


    memcpy(key, pstrKey, Length);
    hash = crc32hash(key);
    idx = hash & (TableSz-1);

    for(uid probe_dist = 0;;)
    {
        if(!kv_tbl[idx] || probe_dist > (TableSz + probe_dist - (hash_tbl[idx] & (TableSz-1))))
            return NULL;

        else if(hash_tbl[idx] == hash && keycmp(kv_tbl[idx]->bzh_key, key))
            return kv_tbl[idx];

        probe_dist++;
        idx = (idx+1) & (TableSz-1);
    }
}



#endif
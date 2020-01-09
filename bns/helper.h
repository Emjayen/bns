/*
 * helper.h
 *
 */
#ifndef HELPER_H
#define HELPER_H
#include <pce\pce.h>





// General purpose memory allocation
#define MALLOC(_bytes) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, _bytes)
#define FREE(_p) HeapFree(GetProcessHeap(), NULL, _p)

// list helpers
#define LIST_ITERATE(_lst, _type, _node) \
	for(_type* ple = (_type*) _lst.head, *_ns = NULL; ple ? _ns = (_type*) ((list_node*) ple)->next, (ple = CONTAINING_RECORD(ple, _type, _node)) : 0; ple = _ns) \

// Timers
struct timer;
typedef timer* HTIMER;
typedef void (*PFTIMERCALLBACK)(void*);

void timer_set(HTIMER hTimer, uid Due, uid Period);
HTIMER timer_create(PFTIMERCALLBACK pfCallback, void* pContext);
void timer_destroy(HTIMER hTimer);
void fire_timers();
uib init_timers(uid MaxTimers);



/*
 * rnd
 *
 */
void srnd(uiq seed);
uiq rnd();



/*
 * DecodeKey
 *
 */
struct bnp_key;

uib DecodeKey(const char* pKey, uid ClientToken, uid ServerToken, bnp_key* pOut);



/*
 * HashPassword
 *
 */
void HashPassword(const char* pPassword, uid ClientToken, uid ServerToken, void* pHash);


/*
 * DumpFile
 *
 */
void DumpFile(void* pData, uid Bytes, const char* pFilename, ...);



/*
 * LogHex
 *
 */
void LogHex(void* pData, uid Bytes);


/*
 * ws_mask
 *
 */
void ws_mask(uid Key, const void* pData, uid Length, void* pOut);


/*
 * http_parse_response
 *   Parses a HTTP response, returning the HTTP status code in 'pStatus'. The return value
 *   specifies the length of the response, in bytes and zero if there's insufficient data.
 *
 */
uid http_parse_response(const char* pHttp, uid Bytes, uid* pStatus);



/*
 * pearson_hash
 *
 */
uib pearson_hash(const char* p, const byte* const T);


/*
 * pearson_perfect
 *
 */
uid pearson_perfect(const char** ppstr, uid Count, uib Mask, byte* pResultTable);


/*
 * GeneratePerfectHashTable
 *
 */
void GeneratePerfectHashTable(const char** ppStrings, uid Count, uib Mask, uib bNoZero);


/*
 * StatstringToMode
 *
 */
uib StatstringToMode(const byte* pStatstring);


/*
 * McpToType
 *
 */
uib McpToType(uid status);


/*
 * HttpComputeWebsocketServerKey
 *
 */
uid HttpComputeWebsocketServerKey(byte* pRequest, uid RequestLength, uid BufferSz, char* pb64KeyOut);


/*
 * fmt_gt
 *
 */
char* fmt_gt(uid type);


/*
 * str_gt
 *
 */
uid str_gt(const char* pstrType);


/*
 * MapPersistantArray
 *
 */
void* MapPersistantArray(const char* pstrFilename, uid ElementSz, uid MaxElements, uid* pElementCount, uib bTransient = FALSE);


/*
 * bzstrcmp
 *
 */
uib bzstrcmp(const char* p1, const char* p2);


#endif
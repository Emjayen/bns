/*
 * helper.cpp
 *
 */
#include "helper.h"
#include "bns.h"
#include <bzl\bzl.h>
#include <stdint.h>
#include <Windows.h>










/*
 * xoroshiro128+ implementation
 *
 */
static uiq rand_state[2];

static inline uiq rotl(const uiq x, int k) 
{
	return (x << k) | (x >> (64 - k));
}

static uiq splitmix64(uiq& x) 
{
	uiq z = (x += UINT64_C(0x9E3779B97F4A7C15));
	z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
	z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
	return z ^ (z >> 31);
}

void srnd(uiq seed)
{
	rand_state[0] = splitmix64(seed);
	rand_state[1] = splitmix64(seed);
}

uiq rnd()
{
	const uiq s0 = rand_state[0];
	uiq s1 = rand_state[1];
	const uiq result = s0 + s1;

	s1 ^= s0;
	rand_state[0] = rotl(s0, 55) ^ s1 ^ (s1 << 14); // a, b
	rand_state[1] = rotl(s1, 36); // c

	return result;
}



/*
 * Timers
 *
 */
static freelist flTimers;
static timer* pTimerPool;
static List lstTimers;

struct timer
{
	timer* next;
	timer* prev;

	uid due;
	uid period;
	PFTIMERCALLBACK pfcb;
	void* userdata;
};


void timer_set(HTIMER hTimer, uid Due, uid Period)
{
	hTimer->period = Period;
	hTimer->due = Due ? Due : (Period ? GetTickCount() + Period : 0);
}

HTIMER timer_create(PFTIMERCALLBACK pfCallback, void* pContext)
{
	timer* pt;

	if(!(pt = (timer*) freelist_alloc(&flTimers)))
		return NULL;

	pt->due = 0;
	pt->pfcb = pfCallback;
	pt->userdata = pContext;

	ListAppend(&lstTimers, (Node*) pt);

	return pt;
}


void timer_destroy(HTIMER hTimer)
{
	if(!hTimer)
		return;

	ListRemove(&lstTimers, (Node*) hTimer);
	freelist_free(&flTimers, hTimer);
}


void fire_timers()
{
	uid ts = GetTickCount();

	for(timer* p = (timer*) lstTimers.pHead, *pn; p; p = pn)
	{
		pn = p->next;

		if(p->due && ts >= p->due)
		{
			uid delta = ts - p->due;

			if(delta > CFG_TIMER_ACCURACY_WARN)
				LWARN("Timer late by %u ms", delta);

			p->due = p->period ? ts + p->period : 0;
			p->pfcb(p->userdata);
		}
	}
}


uib init_timers(uid MaxTimers)
{
	if(!(pTimerPool = (timer*) VirtualAlloc(NULL, MaxTimers * sizeof(timer), MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE)) || !VirtualLock(pTimerPool, MaxTimers * sizeof(timer)))
		return FALSE;

	freelist_init(&flTimers, pTimerPool, sizeof(timer), MaxTimers);

	return TRUE;
}





/*
 * DecodeKey
 *
 */
uib DecodeKey(const char* pKey, uid ClientToken, uid ServerToken, bnp_key* pOut)
{
	pOut->length = strlen(pKey);

	if(pOut->length == 16)
	{
		if(decode_key16((const uint8_t*) pKey, (uint32_t*) &pOut->product, (uint32_t*) &pOut->serial, (uint32_t*) &pOut->unused) < 0)
			return FALSE;

		hash_key16(ClientToken, ServerToken, pOut->product, pOut->serial, pOut->unused, (uint32_t*) pOut->hash);
	}

	else if(pOut->length == 26)
	{
		uint8_t secret[10];

		if(decode_key26(pKey, (uint32_t*) &pOut->product, (uint32_t*) &pOut->serial, secret))
			return FALSE;

		hash_key26(ClientToken, ServerToken, pOut->product, pOut->serial, secret, (uint32_t*) pOut->hash);

		((uid*) pOut->hash)[0] = bswap32(((uid*) pOut->hash)[0]);
		((uid*) pOut->hash)[1] = bswap32(((uid*) pOut->hash)[1]);
		((uid*) pOut->hash)[2] = bswap32(((uid*) pOut->hash)[2]);
		((uid*) pOut->hash)[3] = bswap32(((uid*) pOut->hash)[3]);
		((uid*) pOut->hash)[4] = bswap32(((uid*) pOut->hash)[4]);
	}

	else
		return FALSE;

	pOut->unused = 0;

	return TRUE;
}




/*
 * HashPassword
 *
 */
void HashPassword(const char* pPassword, uid ClientToken, uid ServerToken, void* pHash)
{
	union
	{
		struct
		{
			uid client_token;
			uid server_token;
			byte hash[20];
		};

		byte Buffer[4+4+20];
	};

	char Tmp[32];
	char* p = Tmp;

	while(*pPassword)
	{
		*p++ = *pPassword >= 'A' && *pPassword <= 'Z' ? *pPassword + 0x20 : *pPassword;

		pPassword++;
	}

	bsha(Tmp, p-Tmp, (uint32_t*) hash);

	client_token = ClientToken;
	server_token = ServerToken;

	bsha((char*) Buffer, sizeof(Buffer), (uint32_t*) pHash);
}



/*
 * DumpFile
 *
 */
void DumpFile(void* pData, uid Bytes, const char* pFilename, ...)
{
	HANDLE hFile;
	char Filename[MAX_PATH];

	strfmtv(Filename, sizeof(Filename), pFilename, __valist(pFilename));

	if((hFile = CreateFile(Filename, GENERIC_ALL, NULL, NULL, CREATE_ALWAYS, NULL, NULL)) == INVALID_HANDLE_VALUE)
	{
		LERR("DumpFile(): Failed to create file: %s", Filename);
		return;
	}

	WriteFile(hFile, pData, Bytes, &Bytes, NULL);
	CloseHandle(hFile);
}



/*
 * fmt_hex
 *
 */
void fmt_hex(char* pDst, const void* pSrc, uid SrcLen, uid Width)
{
	const char tbl[] = "0123456789ABCDEF";
	uid i = 0;


	while(i < SrcLen)
	{
		uid x = i%Width;

		pDst[x*3+0] = tbl[(((byte*) pSrc)[i]>>4)];
		pDst[x*3+1] = tbl[((byte*) pSrc)[i] & 0x0F];
		pDst[x*3+2] = ' ';

		pDst[Width*3+x] = ((byte*) pSrc)[i] >= 0x20 && ((byte*) pSrc)[i] <= 0x7E ? ((byte*) pSrc)[i] : '.';

		if((i+1) % Width == 0)
		{
			pDst += Width*4;
			*pDst++ = '\n';
		}

		i++;
	}

	for(uid x = i%Width; x < Width; x++)
	{
		pDst[x*3+0] = ' ';
		pDst[x*3+1] = ' ';
		pDst[x*3+2] = ' ';
		pDst[Width*3+x] = ' ';
	}

	pDst[Width*4+0] = '\n';
	pDst[Width*4+1] = '\0';

}


/*
 * LogHex
 *
 */
void LogHex(void* pData, uid Bytes)
{
	static const uid width = 16;
	char tmp[0x10000]; // TODO:

	//char* pTmp = (char*) alloca((Bytes*4) + ((Bytes/width)+1) + 8);
	char* pTmp = tmp;

	fmt_hex(pTmp, pData, Bytes, width);

	LOG("%s", pTmp);
}



/*
 * ws_mask
 *
 */
void ws_mask(uid Key, const void* pData, uid Length, void* pOut)
{
	uid* ps = (uid*) pData;
	uid* pd = (uid*) pOut;

	while(((sid) Length) > 0)
	{
		*pd++ = (*ps++ ^ Key);
		Length -= 4;
	}
}


/*
 * http_parse_response
 *
 */
uid http_parse_response(const char* pHttp, uid Bytes, uid* pStatus)
{
	uid i = 3;
	
	// Linear search for end of repsonse (\r\n\r\n)
	while(i < Bytes)
	{
		if(pHttp[i] == '\n' && pHttp[i-2] == '\n')
			break;

		i++;
	}

	if(i >= Bytes)
		return 0;

	// Response size.
	Bytes = i+1;

	// Have complete response; extract status code.
	if(pStatus)
	{
		*pStatus = 0;
		i = 0;

		while(i < Bytes && pHttp[i] != ' ')
			i++;

		if(i < Bytes)
		{
			union
			{
				char HttpStatus[4];
				uid _dummy;
			}; _dummy = 0;

			pHttp = pHttp+i+1;
			uid x;

			for(x = 0; i < Bytes && pHttp[x] != ' ' && x < sizeof(HttpStatus); x++, i++)
				HttpStatus[x] = pHttp[x];

			if(i < Bytes && x < sizeof(HttpStatus))
				*pStatus = sxui(HttpStatus);
		}
	}

	return Bytes;
}



/*
 * pearson_hash
 *
 */
uib pearson_hash(const char* p, const byte* const T)
{
	uib h = 0;

	while(*p)
	{
		h = T[h ^ *p++];
	}

	return h;
}


/*
 * pearson_perfect
 *
 */
uid pearson_perfect(const char** ppstr, uid Count, uib Mask, byte* pResultTable, uib bNoZero)
{
	uib rand_tbl[0x100];
	uib hash_tbl[0x100];
	uid attempts = 0;

NEXT:
	attempts++;

	// Permutate random permutations of the permutation table.
	for(uid i = 0; i < sizeof(rand_tbl); i++)
	{
		rand_tbl[i] = (uib) rnd();
	}

	// Clear hash table.
	memzero(hash_tbl, sizeof(hash_tbl));

	// Try them all.
	for(uid i = 0; i < Count; i++)
	{
		uib h = pearson_hash(ppstr[i], rand_tbl) & Mask;

		if(bNoZero && !h)
			goto NEXT;

		if(hash_tbl[h]++)
			goto NEXT;
	}

	// Found.
	memcpy(pResultTable, rand_tbl, sizeof(rand_tbl));
	
	return attempts;
}



/*
 * GeneratePerfectHashTable
 *
 */
void GeneratePerfectHashTable(const char** ppStrings, uid Count, uib Mask, uib bNoZero)
{
	byte rand_tbl[0x100];
	const char* hash_tbl[0x100];

	pearson_perfect(ppStrings, Count, Mask, rand_tbl, bNoZero);

	memzero(hash_tbl, sizeof(hash_tbl));

	for(uid i = 0; i < Count; i++)
	{
		uib h = pearson_hash(ppStrings[i], rand_tbl) & Mask;

		if(hash_tbl[h])
			__asm int 3

		hash_tbl[h] = ppStrings[i];
	}

	LOG("Hash table entries:");

	for(uid i = 0; i <= Mask; i++)
	{
		LOG("/* 0x%X */ %s", i, hash_tbl[i] ? hash_tbl[i] : "<NULL>");
	}

	LOG("Pearson table:");

	for(uib y = 0; y < 16; y++)
	{
		for(uib x = 0; x < 16; x++)
			Log(LOG_DBG,"0x%02X, ", rand_tbl[(y*16)+x]);

		LOG("");
	}
}



/*
 * StatstringToMode
 *
 */
uib StatstringToMode(const byte* pStatstring)
{
	uib gt = 0;

	gt |= (pStatstring[26] & 0x20) >> 1; // Expansion flag.
	gt |= (pStatstring[26] & 0x04) << 1; // Hardcore flag.

	const byte tbl[2][2] =
	{
		{ 0x88, 0x90 },
		{ 0x8A, 0x94 },
	};

	gt |= (pStatstring[27] >= tbl[(gt & GT_FLAG_EXPANSION) ? 1 : 0][1]) ? GT_FLAG_HELL : (pStatstring[27] >= tbl[(gt & GT_FLAG_EXPANSION) ? 1 : 0][0]) ? GT_FLAG_NIGHTMARE : GT_FLAG_NORMAL;

	if(pStatstring[30] != ((byte) 0xFF))
		gt |= GT_FLAG_LADDER;

	return gt;
}


/*
 * McpToType
 *
 */
uib McpToType(uid status)
{
	uib result = 0;

	result |= (status & 0x00003000) >> 12; // Difficulty
	result |= (status & 0x00200000) >> 19; // Ladder
	result |= (status & 0x00100000) >> 16; // Expansion
	result |= (status & 0x00000800) >> 8;  // Hardcore

	return result;
}



/*
 * HttpComputeWebsocketServerKey
 *
 */
uid HttpComputeWebsocketServerKey(byte* pRequest, uid RequestLength, uid BufferSz, char* pb64KeyOut)
{
	static const char WS_MAGIC[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";


	if(RequestLength <= 4)
		return FALSE;

	/*
	 * Properly formed requests will always have atleast 1 byte of trailing slack space (\n and more likely: \r\n)
	 *
	 */
	pRequest[RequestLength-1] = '\0';

	byte* ps = pRequest+4;

	// Locate the "Sec-WebSocket-Key" header.
	while(*ps++)
	{
		if(*ps == ':' && (*((uid*) (ps-4)) & 0xDFDFDFFF) == 'YEK-')
			goto FOUND_KEY;
	}

	// Couldn't find the key. Probably malformed and/or malicious.
	return FALSE;

FOUND_KEY:
	// Redundant leading whitespace is [of course] common in HTTP.
	while(*++ps == ' ');

	// We also need minimally enough trailing slack 
	if((ps-pRequest) + 24 + sizeof(WS_MAGIC) >= BufferSz)
		return FALSE;

	memcpy(ps + 24, WS_MAGIC, sizeof(WS_MAGIC)-1);
	sha1(ps, 24 + sizeof(WS_MAGIC)-1, pRequest);
	
	return b64enc(pb64KeyOut, 28, pRequest, 20);
}


// Hackery for converting game types to strings
static const char REALM[4] = { 'e', 'w', 'u', 'a' };
static const char PROD[2] = { 'c', 'x' };
static const char CORE[2] = { 's', 'h' };
static const char LAD[2] = { 'n', 'l' };
static const char DIFF[4] = { 'n', 'x', 'h', '?' };

/*
 * fmt_gt
 *
 */
char* fmt_gt(uid type)
{
	static char tmp[8][8];
	static uid next;

	if((next += 1) >= 8)
		next = 0;

	char* pd = tmp[next];
	
	gt t = *((gt*) &type);

	pd[0] = REALM[t.realm];
	pd[1] = PROD[t.is_expansion];
	pd[2] = CORE[t.is_hardcore];
	pd[3] = LAD[t.is_ladder];
	pd[4] = DIFF[t.v & 3];

	return pd;
}


/*
 * str_gt
 *
 */
uid str_gt(const char* pstrType)
{
	gt t;
	t.v = 0;

	if(*pstrType == REALM[0]) t.realm = 0;
	else if(*pstrType == REALM[1]) t.realm = 1;
	else if(*pstrType == REALM[2]) t.realm = 2;
	else if(*pstrType == REALM[3]) t.realm = 3;
	else
		return ~0;

	pstrType++;

	if(*pstrType != PROD[0] && *pstrType != PROD[1])
		return ~0;

	t.is_expansion = *pstrType == PROD[0] ? 0 : 1;
	pstrType++;

	if(*pstrType != CORE[0] && *pstrType != CORE[1])
		return ~0;

	t.is_hardcore = *pstrType == CORE[0] ? 0 : 1;
	pstrType++;

	if(*pstrType != LAD[0] && *pstrType != LAD[1])
		return ~0;

	t.is_ladder = *pstrType == LAD[0] ? 0 : 1;
	pstrType++;

	if(*pstrType == DIFF[0]) t.v |= GT_FLAG_NORMAL;
	else if(*pstrType == DIFF[1]) t.v |= GT_FLAG_NIGHTMARE;
	else if(*pstrType == DIFF[2]) t.v |= GT_FLAG_HELL;
	else
		return ~0;

	return t.v;
}



/*
 * MapPersistantArray
 *
 */
void* MapPersistantArray(const char* pstrFilename, uid ElementSz, uid MaxElements, uid* pElementCount, uib bTransient)
{
	HANDLE hFile;
	HANDLE hSection;
	byte* pData;


	*pElementCount = 0;

	if(bTransient)
	{
		return VirtualAlloc(NULL, MaxElements * ElementSz, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
	}

	if((hFile = CreateFile(pstrFilename, GENERIC_ALL, NULL, NULL, OPEN_ALWAYS, NULL, NULL)) == INVALID_HANDLE_VALUE)
		return FALSE;

	if(!(hSection = CreateFileMapping(hFile, NULL, PAGE_READWRITE, NULL, (MaxElements*ElementSz), NULL)))
		return FALSE;

	if(!(pData = (byte*) MapViewOfFile(hSection, FILE_MAP_WRITE, 0, 0, (MaxElements*ElementSz))))
		return FALSE;

	// Valid elements must have non-null dword-size data at offset zero.
	for(uid i = 0; i < MaxElements; i++)
	{
		if(*((uid*) (pData + (i * ElementSz))) == NULL)
			break;

		++*pElementCount;
	}

	return pData;
}


/*
 * bzstrcmp
 *
 */
uib bzstrcmp(const char* p1, const char* p2)
{
	while(*p1 && *p2)
	{
		if((*p1++ & 0xDF) != (*p2++ & 0xDF))
			return FALSE;
	}

	return (*p1 == 0) && (*p2 == 0);
}
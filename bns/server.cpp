/*
 * server.cpp
 *
 */
#include "server.h"
#include "web.h"
#include "bnc.h"
#include "bzstrmap.h"
#include <Windows.h>




// Prototypes
Character* FindCharacter(uib Mode, const char* pstrName, uib bCreateIfNotExist);

// Pools
Game* plGames; // Game object pool.
freelist flGames; // 'plGames' freelist.

// Globals
Game* IdIdxTbl[GT_TOTAL_REALMS][CFG_MAX_GAME_ID_MASK+1]; // Translate game_id to Game; hint only.
list lstGameType[GT_TYPE_IDX_MASK+1]; // Game::ln_type. All games sorted by base game type (low bits).
list lstGameRealm[GT_TOTAL_REALMS]; // Game::ln_realm. All games sorted by realm.
list lstListeners[GT_TYPE_IDX_MASK+1]; // Session::ln_listen. Sessions sorted by game type they are listening for.
uib GameModeStatus[GT_TYPE_IDX_MASK+1]; // For each mode: 0=We dont have a client at all for it; 1=We have a client for it; 1..n; we have n-1 clients currently available.

// Query scheduling
uid sched_counter;


// Persistent data.
struct
{
    User* p;
    uid count;
} Users;

struct
{
    Character* p;
    uid count;
} Characters;


// Helpers
#define GetGameID(_pGame) ((uiw) (_pGame - plGames))
#define GetCharID(_pChar) ((uid) (_pChar - Characters.p))

struct EMQ
{
    byte* p;
    byte _padnethdr[BSP_FRAME_HDR_SZ];
    byte data[MAX_EVTMSG_QBUF - BSP_FRAME_HDR_SZ - sizeof(byte*)];
} EvtMsgQueue[GT_TYPE_IDX_MASK+1];

#define GetEMQ(_gametype) (EvtMsgQueue[_gametype & GT_TYPE_IDX_MASK])
#define EMQGetSize(_emq) (_emq.p - _emq.data)





/*
 * Sesssion:OnFetchList
 *
 */
uib Session::OnFetchList(bspc_fetch_list* pMsg, uid Length)
{
    static byte tmp[0x10000];

    union
    {
        byte* pd;
        bsps_game_state* pStateMsg;
        bsps_fetch_list* pFetchMsg;
    };

    pd = tmp+BSP_FRAME_HDR_SZ;


    // Mask off mode just in case.
    uib Type = (pMsg->type & GT_MODE_MASK);

    // Remove from the current list if in one.
    if(Flags & SX_FLAG_LISTENING)
    {
        LDBG("[Sx0x%X:%a] Removing from listen list 0x%X", this, this->Addr, ListenGameType);
        list_remove(&lstListeners[ListenGameType], &ln_listen);
        Flags ^= SX_FLAG_LISTENING;
        ListenGameType = NULL;
    }

    // Write out the status response.
    pFetchMsg->mid = BSP_FETCH_LIST;
    pFetchMsg->status = GameModeStatus[Type] >= 2 ? BSP_STATUS_SUCCESS : (GameModeStatus[Type] ? BSP_STATUS_TRY_AGAIN : BSP_STATUS_NOT_FOUND);
    pFetchMsg->mode = (uib) Type;
    pFetchMsg++;

    // Only if we may have data at some point.
    if(GameModeStatus[Type])
    {
        LOG("[0x%X] Registered listener of type 0x%X", this, Type);

        // Add to the mode listener list.
        ListenGameType = Type;
        Flags |= SX_FLAG_LISTENING;
        list_append(&lstListeners[ListenGameType], &ln_listen);

        LDBG("[Sx0x%X:%a] Adding to listen list 0x%X", this, this->Addr, ListenGameType);

        // HACK: We need to do this elsewise we may get stale delta updates, predating our full-state update.
        pEvtMsgQueueTmp = GetEMQ(Type).p;

        // Send full game-state data for the selected mode.
        LIST_ITERATE(lstGameType[Type], Game, ln_type)
        {
            pStateMsg->mid = BSP_GAME_STATE;
            pStateMsg->game_id = GetGameID(ple);
            ple->EncExtended(&pStateMsg->desc_ex);
            pd += sizeof(bsps_game_state) + ple->EncBasic((bsp_game_desc*) pStateMsg->etc);
            pd += ple->EncCharlist((bsp_char_desc*) pd);
        }
    }

    if((pd-tmp) > BSP_FRAME_HDR_SZ)
        Send(tmp+BSP_FRAME_HDR_SZ, (pd-tmp)-BSP_FRAME_HDR_SZ);

    return TRUE;
}


/*
 * Session::CleanupCb
 *
 */
void Session::CleanupCb()
{
    if(Flags & SX_FLAG_LISTENING)
    {
        LDBG("[Sx0x%X:%a] Removing from listen list 0x%X", this, this->Addr, ListenGameType);
        list_remove(&lstListeners[ListenGameType], &ln_listen);
        Flags ^= SX_FLAG_LISTENING;
        ListenGameType = NULL;
    }
}


/*
 * FlushEventMessages
 *
 */
void FlushEventMessages(void*)
{
    for(uib type = 0; type <= GT_TYPE_IDX_MASK; type++)
    {
        EMQ& rEMQ = GetEMQ(type);

        if(lstListeners[type].count && EMQGetSize(rEMQ))
        {
            LDBG("Flushing event messages (type:0x%X size:%u) for %u listeners", type, EMQGetSize(rEMQ), lstListeners[type].count);

            LIST_ITERATE(lstListeners[type], Session, ln_listen)
            {
                LDBG("[Sx0x%X:%a] Flushing event messages (%u bytes)", ple, ple->Addr, EMQGetSize(rEMQ));

                if(ple->pEvtMsgQueueTmp)
                {
                    if(ple->pEvtMsgQueueTmp >= rEMQ.p)
                        LWARN("EvtMsgQueueTmp >= p (%u)", ple->pEvtMsgQueueTmp - rEMQ.p);

                    else if(!ple->Send(ple->pEvtMsgQueueTmp, rEMQ.p - ple->pEvtMsgQueueTmp))
                    {
                        LERR("Failed to send event message buffer (partial)");
                    }

                    ple->pEvtMsgQueueTmp = NULL;
                }

                else
                {
                    if(!ple->Send(rEMQ.data, EMQGetSize(rEMQ)))
                        LERR("Failed to send event message buffer");
                }
            }
        }

        rEMQ.p = rEMQ.data;
    }
}


/*
 * CreateGame
 *
 */
Game* CreateGame(const char* pName, uid Type)
{
    Game* p;


    ASSERT(strlen(pName) < sizeof(Game::name));

    if(!(p = (Game*) freelist_alloc(&flGames)))
    {
        LERR("Failed to allocate game");
        return NULL;
    }

    memzero(p, sizeof(Game));
    p->type = (Type & GT_MODE_MASK);
    p->name_len = (uib) strlen(pName);
    memcpy(p->name, pName, p->name_len);

    list_append(&lstGameType[p->type & GT_TYPE_IDX_MASK], &p->ln_type);
    list_append(&lstGameRealm[((gt*) &p->type)->realm], &p->ln_realm);

    LOG("Game created: %s (0x%X)", p->name, p->type);


    return p;
}


/*
 * Game state encoding
 *
 */
uib Game::EncBasic(bsp_game_desc* pd)
{
    byte* p = (byte*) pd;

    *p++ = name_len;
    memcpy(p, name, name_len);
    p += name_len;
    *p++ = desc_len;
    memcpy(p, description, desc_len);
    p += desc_len;

    return (uib) (p - ((byte*) pd));
}

void Game::EncExtended(bsp_game_desc_ex* pd)
{
    pd->max_players = max_players;
    pd->cur_players = cur_players;
    pd->clvl_diff = clvl_diff;
    pd->uptime = (uib) (GetTickCount() - ts_hosted)/1000;
}

uib Game::EncCharlist(bsp_char_desc* pd)
{
    union
    {
        byte* p;
        bsp_char_desc* pcd;
    };

    pcd = pd;
    *p++ = cur_players;

    for(uib i = 0; i < 8; i++)
    {
        if(!(bv_chars & (1<<i)))
            continue;

        p += pCharlist[i]->Enc(pcd);
    }

    return (p - ((byte*) pd));
}


/*
 * Game::Destroy
 *
 */
void Game::Destroy()
{
    LOG("Destroy game: %s", name);

    if(type & GT_FLAG_OPEN)
        OnClose();

    if(pbncQuery)
        pbncQuery->CancelQueryRequest();

    list_remove(&lstGameType[type & GT_TYPE_IDX_MASK], &ln_type);
    list_remove(&lstGameRealm[((gt*) &type)->realm], &ln_realm);

    IdIdxTbl[((gt*) &type)->realm][game_id] = NULL;

    freelist_free(&flGames, this);
}


/*
 * CmpGamePri
 *
 */
uib CmpGamePri(Game* pg1, Game* pg2, uid& ts)
{
    if(pg1->ts_dirty || pg2->ts_dirty)
        return pg1->ts_dirty-1 < pg2->ts_dirty-1;

    return __max(pg1->ts_query_completed, pg1->ts_listed) < __max(pg2->ts_query_completed, pg2->ts_listed);
}


/*
 * Game::OnOpen
 *   The game has been opened.
 *
 */
void Game::OnOpen()
{
    ts_discovered = GetTickCount();


    // Queue message describing this event.
    union
    {
        byte* pd;
        bsps_game_open* pMsg;
    };

    pd = GetEMQ(type).p;
    pMsg->mid = BSP_GAME_OPEN;
    pMsg->game_id = GetGameID(this);

    GetEMQ(type).p += sizeof(bsps_game_open) + EncBasic((bsp_game_desc*) pMsg->etc);
}


/*
 * Game::OnInfo
 *   First query complete; extended game information is now available.
 *
 */
void Game::OnInfo()
{
    // Queue message.
    union
    {
        byte* pd;
        bsps_game_update* pMsg;
    };

    pd = GetEMQ(type).p;
    pMsg->mid = BSP_GAME_UPDATE;
    pMsg->game_id = GetGameID(this);
    EncExtended(&pMsg->desc_ex);
    GetEMQ(type).p += sizeof(bsps_game_update);
}


/*
 * Game::OnClose
 *
 */
void Game::OnClose()
{

    // Queue state change message.
    union
    {
        bsps_game_close* pMsg;
        byte* pd;
    };

    pd = GetEMQ(type).p;
    pMsg->mid = BSP_GAME_CLOSE;
    pMsg->game_id = GetGameID(this);
    GetEMQ(type).p += sizeof(bsps_game_close);
}


/*
 * Character::Enc
 *
 */
uib Character::Enc(bsp_char_desc* pd)
{
    pd->char_id = GetCharID(this);
    pd->clvl = this->clvl;
    pd->cls = this->cls;
    pd->dt_join = (uib) (GetTickCount() - this->ts_join)/1000;
    pd->name_len = this->name_len;
    memcpy(pd->name, this->name, this->name_len);

    return sizeof(bsp_char_desc) + this->name_len;
}


/*
 * Character::OnJoinGame
 *
 */
void Character::OnJoinGame(Game* pGame)
{
    LDBG("%s joins %s", this->name, pGame->name);

    union
    {
        bsps_game_padd* pMsg;
        byte* pd;
    };

    pd = GetEMQ(pGame->type).p;
    pMsg->mid = BSP_GAME_PADD;
    pMsg->game_id = GetGameID(pGame);
    GetEMQ(pGame->type).p += sizeof(bsps_game_padd) + Enc((bsp_char_desc*) pMsg->ect);
}


/*
 * Character::OnLeaveGame
 *
 */
void Character::OnLeaveGame(Game* pGame)
{
    LDBG("%s leaves %s", this->name, pGame->name);


    union
    {
        bsps_game_prem* pMsg;
        byte* pd;
    };

    pd = GetEMQ(pGame->type).p;
    pMsg->mid = BSP_GAME_PREM;
    pMsg->game_id = GetGameID(pGame);
    pMsg->char_id = GetCharID(this);
    GetEMQ(pGame->type).p += sizeof(bsps_game_prem);
}



/*
 * LookupGame
 *
 */
Game* LookupGame(const char* pName, uid Type, uid GameID)
{
    Game* p;


    // First try the hint table.
    if((p = IdIdxTbl[((gt*) &Type)->realm][GameID & CFG_MAX_GAME_ID_MASK]))
    {
        if(strcmp(pName, p->name) == 0)
            return p;
    }

    // Fallback
    LIST_ITERATE(lstGameRealm[((gt*) &Type)->realm], Game, ln_realm)
    {
        if(strcmp(pName, ple->name) == 0)
        {
            return ple;
        }
    }

    return NULL;
}


/*
 * BnReadyQuery
 *    A client is available to send a query.
 *
 */
void BnReadyQuery(BnClient* pbnc)
{
    Game* pBest = NULL;
    uid ts = GetTickCount();

    LIST_ITERATE(lstGameRealm[((gt*) &pbnc->Mode)->realm], Game, ln_realm)
    {
        if(!ple->pbncQuery && (!pBest || CmpGamePri(ple, pBest, ts)))
        {
            pBest = ple;
        }
    }

    if(!pBest)
    {
        LWARN("No game to query!");
        return;
    }

    if(pbnc->SubmitQueryRequest(pBest->name, pBest))
    {
        LOG("[SUBMIT QUERY]: %s | last submit:%us last complete:%us last listed:%u)",
            pBest->name,
            (ts-pBest->ts_query_submitted)/1000,
            (ts-pBest->ts_query_completed)/1000,
            (ts-pBest->ts_listed)/1000);

        pBest->pbncQuery = pbnc;
        pBest->ts_query_submitted = GetTickCount();
        pBest->last_sched = ++sched_counter;

    }

    else
        LERR("failed to submit query");
}


/*
 * BnQueryComplete
 *   A game query request has completed.
 *
 */
void BnQueryComplete(BnClient* pbnc, mcps_query_game* pMsg, void* pContext)
{
    Game* pGame = (Game*) pContext;
    uid Type;


    // Operation complete; clear reference.
    pGame->pbncQuery = NULL;

    // Failed (timeout)?
    if(!pMsg)
    {
        LWARN("Query operation failed on %s", pGame->name);
        return;
    }

    // Temporary failure; try again later.
    if(pMsg->status == MCP_QUERY_UNAVAILABLE)
    {
        LWARN("Query request returned no data %s", pGame->name);
        return;
    }

    // Returned when the specified game does not exist.
    if(pMsg->status == MCP_QUERY_INVALID_GAME)
    {
        LOG("Game closed: %s", pGame->name);
        pGame->Destroy();
        return;
    }

    Type = McpToType(pMsg->status) | (pbnc->Mode & GT_MASK_REALM);

    // We missed detecting the game being closed.
    if(Type != (pGame->type & GT_MODE_MASK))
    {
        LWARN("!!x (Query) game type mismatch: 0x%X does not match our game 0x%X (%s)", Type, pGame->type, pGame->name);

        char tmp[16];
        strcpyn(tmp, sizeof(tmp), pGame->name);

        pGame->Destroy();

        if(!(pGame = CreateGame(tmp, Type)))
            return;
    }

    pGame->ts_query_completed = GetTickCount();
    

    if(!(pGame->type & GT_FLAG_OPEN))
    {
        pGame->type |= GT_FLAG_OPEN;
        pGame->OnOpen();
    }

    pGame->host_clvl = pMsg->creator_lvl;
    pGame->clvl_diff = pMsg->diff_lvl;
    pGame->max_players = pMsg->max_players;

    if(!(pGame->type & GT_FLAG_INFO))
    {
        pGame->type |= GT_FLAG_INFO;

        pGame->ts_hosted = GetTickCount() - (pMsg->uptime*1000);
        pGame->OnInfo();
    }


    /*
     * Update the character list; build list new characters and a bit vector
     * of old/left charactes.
     *
     */
    Character* pJoin[8];
    uib JoinLen = 0;
    uib LeaveMask = 0;
    uib CharCount = pMsg->cur_players;
    char* ps = pMsg->ect + 1 + (*pMsg->ect ? strlen(pMsg->ect) : 0);

    for(uib i = 0, x = 0; i < CharCount && i < 8; i++, ps++, x=0)
    {
        if(!*ps)
            continue;

        for(; x < 8; x++)
        {
            if(!(pGame->bv_chars & (1<<x)))
                continue;

            if(bzstrcmp(ps, pGame->pCharlist[x]->name))
                break;
        }

        Character* pc = pGame->pCharlist[x];

        // Character exists; just an update. 
        if(x < 8)
        {
            pc = pGame->pCharlist[x];
            LeaveMask |= (1<<x);
        }

        // Character doesn't exist; joined the game.
        else
            pJoin[JoinLen++] = (pc = FindCharacter(Type, ps, TRUE));

        // Update character information.
        pc->clvl = pMsg->char_levels[i];
        pc->cls = pMsg->char_levels[i];

        ps += strlen(ps);
    }

    // Remove and add left/joining players.
    LeaveMask = ((~LeaveMask) & pGame->bv_chars);

    uib redundant = 1;

    for(uib i = 0; i < 8; i++)
    {
        if((LeaveMask & (1<<i)))
        {
            pGame->bv_chars ^= (1<<i); 
            pGame->pCharlist[i]->OnLeaveGame(pGame);
            pGame->pCharlist[i] = NULL;

            redundant = 0;
        }

        if(JoinLen && !(pGame->bv_chars & (1<<i)))
        {
            pGame->bv_chars ^= (1<<i);
            pGame->pCharlist[i] = pJoin[--JoinLen];
            pGame->pCharlist[i]->OnJoinGame(pGame);

            redundant = 0;
        }
    }

    pGame->cur_players = __popcnt((unsigned int) pGame->bv_chars);

    if(redundant)
    {
        pGame->wasted_queries++;
    }

    LDBG("[COMPLETE QUERY]: %s | Redundant? %s (%u)", pGame->name, redundant ? "YES" : "No", pGame->wasted_queries);

    if(pGame->ts_dirty && redundant)
        LDBG("----------- Dirty game was not dirty");

    pGame->ts_dirty = NULL;
        
}


/*
 * BnListComplete
 *    Game list results received.
 *
 */
void BnListComplete(BnClient* pClient, mcps_game_list* pMsg)
{
    Game* pGame;
    uid Type = McpToType(pMsg->status) | (pClient->Mode & GT_MASK_REALM);


    if(!(pGame = LookupGame(pMsg->etc, Type, pMsg->game_id)) && !(pGame = CreateGame(pMsg->etc, Type)))
        return;

    //LDBG("Got listing: %s game (Type: 0x%X (%s)) on client %s (Type: 0x%X (%s))",
    //     pGame->name,
    //     pGame->type, fmt_gt(pGame->type),
    //     pClient->pAccount->name,
    //     pClient->Mode, fmt_gt(pClient->Mode));

    if(Type != (pGame->type & GT_MODE_MASK))
    {
        LWARN("!!x (List) game type mismatch: 0x%X does not match our game 0x%X (%s)", Type, pGame->type, pGame->name);

        pGame->Destroy();

        if(!(pGame = CreateGame(pMsg->etc, Type)))
            return;
    }

    pGame->ts_listed = GetTickCount();

    if(pMsg->player_count != pGame->cur_players && !pGame->ts_dirty)
    {
        pGame->ts_dirty = GetTickCount();
        LDBG("DIRTY: %s", pGame->name);
    }

    if(!(pGame->type & GT_FLAG_OPEN))
    {
        pGame->type |= GT_FLAG_OPEN;

        if(pMsg->etc[pGame->name_len+1])
            pGame->desc_len = strcpyn(pGame->description, sizeof(pGame->description), pMsg->etc+pGame->name_len+1);

        pGame->game_id = pMsg->game_id;
        IdIdxTbl[((gt*) &pGame->type)->realm][pGame->game_id & CFG_MAX_GAME_ID_MASK] = pGame;

        pGame->OnOpen();
    }
}


void BnReadyList(BnClient* pClient)
{
    pClient->SubmitListRequest(NULL);
}


void BnNotifyStatus(uib Mode, uib bAvailable)
{
    if(bAvailable)
        GameModeStatus[Mode & GT_MODE_MASK]++;

    else
        GameModeStatus[Mode & GT_MODE_MASK]--;

    LOG("Data for mode 0x%X (%s) changed (%u) -- current mode status: %u", Mode, fmt_gt(Mode), bAvailable, GameModeStatus[Mode & GT_MODE_MASK]);
}


/*
 * FindUser
 *   Looks up a user by username, optionally creating if it does not exist.
 *
 */
User* FindUser(uib Mode, const char* pstrUsername, uib bCreateIfNotExist)
{
    static bzstrmap<User, 0x10000> Map[GT_TOTAL_REALMS]; // Maps User->name to Users for each realm.
    User* p = NULL;
    uib len = (uib) strlen(pstrUsername);
    
    
    if(len >= sizeof(User::name))
        return NULL;

    if(!(p = (User*) Map[((gt*) &Mode)->realm].Lookup(pstrUsername, len)) && bCreateIfNotExist)
    {
        p = &Users.p[Users.count++];
        p->mode = Mode;
        p->st_created = p->st_created ? p->st_created : GetTimeUTC();
        memcpy(p->name, pstrUsername, len);
        Map[((gt*) &p->mode)->realm].Insert(p);
    }

    return p;
}


/*
 * FindCharacter
 *   Looks up a character by name, optionally creating if it does not exist.
 *
 */
Character* FindCharacter(uib Mode, const char* pstrName, uib bCreateIfNotExist)
{
    static bzstrmap<Character, 0x10000> Map[GT_TOTAL_REALMS]; // Maps Character->name to Characters for each realm.
    Character* p = NULL;
    uib len = (uib) strlen(pstrName);


    if(len >= sizeof(Character::name))
        return NULL;

    if(!(p = (Character*) Map[((gt*) &Mode)->realm].Lookup(pstrName, len)) && bCreateIfNotExist)
    {
        p = &Characters.p[Characters.count++];
        p->mode = Mode;
        p->st_created = p->st_created ? p->st_created : GetTimeUTC();
        p->name_len = len;
        memcpy(p->name, pstrName, p->name_len);
        Map[((gt*) &p->mode)->realm].Insert(p);

        LDBG("Create character: p->name: '%s' (%u) pstrName: '%s'", p->name, p->name_len, pstrName);
    }

    return p;
}


/*
 * ServerStartup
 *
 */
uib ServerStartup()
{
    if(!(plGames = (Game*) VirtualAlloc(NULL, CFG_GAME_POOL_SZ * sizeof(Game), MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE)))
        return FALSE;

    freelist_init(&flGames, plGames, sizeof(Game), CFG_GAME_POOL_SZ);

    for(uib i = 0; i < ARRAYSIZE(EvtMsgQueue); i++)
        EvtMsgQueue[i].p = EvtMsgQueue[i].data;

    iptbl_init();

    for(uib i = 0; i < cfg_accounts_sz; i++)
    {
        uib mode = (uib) (str_gt(cfg_accounts[i].name+4) & GT_MODE_MASK);

        GameModeStatus[mode] = 1;

        LOG("Mode data 0x%X (%s) is marked as existing.", mode, fmt_gt(mode));
    }


    HTIMER ht = timer_create(&FlushEventMessages, NULL);
    timer_set(ht, 0, CFG_EVTMSG_FLUSH_INT);


    if(!(Users.p = (User*) MapPersistantArray("users", USER_DATA_SIZE, 0x10000, &Users.count, TRUE)))
    {
        LERR("Failed to load users from disk");
        return FALSE;
    }

    LOG("Loaded users: %u", Users.count);

    // Fixup.
    for(uid i = 0; i < Users.count; i++)
        FindUser(Users.p[i].mode, Users.p[i].name, TRUE);

    if(!(Characters.p = (Character*) MapPersistantArray("characters", CHARACTER_DATA_SIZE, 0x10000, &Characters.count, TRUE)))
    {
        LERR("Failed to load characters from disk");
        return FALSE;
    }

    LOG("Loaded characters: %u", Characters.count);

    // Fixup
    for(uid i = 0; i < Characters.count; i++)
        FindCharacter(Characters.p[i].mode, Characters.p[i].name, TRUE);

    return TRUE;
}
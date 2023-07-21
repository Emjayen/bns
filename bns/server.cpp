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
Run* LookupRun(const char* pRunName, uid Type);
uib GetRunType(const char* pName);
uib ParseRunName(const char* pName, char* pRunName, char* pFormat, uid* pSequence);
uid GetRunTypeMaxDuration(uib RunType);

// Constants
#define CFG_JOIN_ACCURACY_THRESH 18000

// Pools
Game* plGames; // Game object pool.
Run* plRuns; // Runs pool.
freelist flGames; // 'plGames' freelist.
freelist flRuns; // 'plRuns' freelist.

// Globals
Game* IdIdxTbl[GT_TOTAL_REALMS][CFG_MAX_GAME_ID_MASK+1]; // Translate game_id to Game; hint only.
list lstGameType[GT_TYPE_IDX_MASK+1]; // Game::ln_type. All games sorted by base game type (low bits).
list lstRunType[GT_TYPE_IDX_MASK+1]; // Run::ln_type. All runs sorted by game type.
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
#define GetRunID(_pRun) ((uiw) (_pRun - plRuns))

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
        bsps_run_state* pRunMsg;
    };

    pd = tmp+BSP_FRAME_HDR_SZ;


    if(!ChargeHost(this->pHost, CFG_BSP_TKNBKT_FETCH_COST))
    {
        LWARN("[ABUSE] Exceeded rate-limit (resolve)");
        return FALSE;
    }

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
            pd += ple->EncPlayerList((bsp_pdesc*) pd);
        }

        // Send full run-state data for the selected mode.
        LIST_ITERATE(lstRunType[Type], Run, ln_type)
        {
            if(ple->flags & RUN_FLAG_VALID)
            {
                pRunMsg->mid = BSP_RUN_STATE;
                pRunMsg->run_id = GetRunID(ple);
                pd += sizeof(bsps_run_state) + ple->Enc((bsp_run_desc*) pRunMsg->ect);
            }
        }
    }

    if((pd-tmp) > BSP_FRAME_HDR_SZ)
        Send(tmp+BSP_FRAME_HDR_SZ, (pd-tmp)-BSP_FRAME_HDR_SZ);

    return TRUE;
}


/*
 * Session::OnResolveChar
 *
 */
uib Session::OnResolveChar(bspc_resolve_char* pMsg, uid Length)
{
    static byte tmp[sizeof(bsps_resolve_char) + (32 * CFG_BSP_MAX_RESOLVE_COUNT)];

    union
    {
        bsps_resolve_char* pResultMsg;
        byte* p;
    };

    p = tmp+BSP_FRAME_HDR_SZ;

    uid count = (Length - sizeof(bspc_resolve_char)) / sizeof(uid); /* char_id */

    LDBG("Received resolve request; %u count", count);

    count = __min(count, CFG_BSP_MAX_RESOLVE_COUNT);

    if(!ChargeHost(this->pHost, CFG_BSP_TKNBKT_RESOLVE_BASE_COST + (CFG_BSP_TKNBKT_RESOLVE_N_COST * count)))
    {
        LWARN("[ABUSE] Exceeded rate-limit (resolve)");
        return FALSE;
    }

    pResultMsg->mid = BSP_RESOLVE_CHAR;
    p += sizeof(bsps_resolve_char);

    for(uid i = 0; i < count; i++)
    {
        if(pMsg->char_id[i] >= Characters.count)
        {
            LWARN("[ABUSE] Received request for invalid character: %u", pMsg->char_id[i]);
            return FALSE;
        }

        p += Characters.p[pMsg->char_id[i]].Enc((bsp_char_desc*) p);
    }

    Send(tmp+BSP_FRAME_HDR_SZ, (p-tmp)-BSP_FRAME_HDR_SZ);

    return TRUE;
}


/*
 * Session::OnFeedback
 *
 */
uib Session::OnFeedback(bspc_feedback* pMsg, uid Length)
{
    LOG("Received feedback (host:%a bytes:%u)", this->Addr, Length);

    DumpFile(pMsg->text, Length-sizeof(bspc_feedback), "feedback\\%a-%u.txt", this->Addr, GetTickCount());
    
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
 * TcbRunExpire
 *
 */
void TcbRunExpire(Run* pRun)
{
    LOG("(%s) Expiring run", pRun->Name);

    pRun->Destroy();
}


/*
 * CreateRun
 *
 */
Run* CreateRun(const char* pRunName, uid Mode, uib RunType)
{
    Run* p;


    ASSERT(strlen(pRunName) < sizeof(Run::Name));

    if(!(p = (Run*) freelist_alloc(&flRuns)))
    {
        LERR("Failed to allocate run");
        return NULL;
    }

    memzero(p, sizeof(Run));

    if(!(p->htExpire = timer_create((PFTIMERCALLBACK) &TcbRunExpire, p)))
    {
        LERR("Failed to create run expire timer");
        freelist_free(&flRuns, p);
        return NULL;
    }

    p->mode = ((uib) Mode) & GT_MODE_MASK;
    p->run_type = RunType;
    p->name_len = strlen(pRunName);
    memcpy(p->Name, pRunName, p->name_len);

    timer_set(p->htExpire, GetTickCount()+GetRunTypeMaxDuration(p->run_type), NULL);

    list_append(&lstRunType[p->mode], &p->ln_type);

    LOG("Run created: %s (0x%X) run type: %u", p->Name, p->mode, p->run_type);

    return p;
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

    p->AddRef();

    return p;
}


/*
 * Game state encoding
 *
 */
uib Game::EncBasic(bsp_game_desc* pd)
{
    pd->flags = NULL;
    
    if((flags & GAME_FLAG_RUN) && (run_type != RUN_MISC || (flags & GAME_FLAG_VALID_RUN)))
        pd->flags |= BSP_GAME_FLAG_RUN;

    byte* p = (byte*) pd->name_and_desc;

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
    pd->clvl_diff = clvl_diff;
    pd->uptime = (uiw) ((GetTickCount() - ts_hosted)/1000);
}

uib Game::EncPlayer(uib pid, bsp_pdesc* pd)
{
    pd->pid = pid;
    pd->char_id = GetCharID(Charlist[pid].pc) | (Charlist[pid].pc == pHost ? BSP_PFLAG_HOST : 0);
    pd->dt_join = (uiw) ((GetTickCount() - Charlist[pid].ts_joined)/1000);

    return sizeof(bsp_pdesc);
}

uib Game::EncPlayerList(bsp_pdesc* pd)
{
    union
    {
        byte* p;
        bsp_pdesc* ppd;
    };

    ppd = pd;
    *p++ = cur_players;

    for(uib i = 0; i < 8; i++)
    {
        if(!(bv_chars & (1<<i)))
            continue;

        p += EncPlayer(i, ppd);
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

    if(flags & GAME_FLAG_DESTROYED)
    {
        LERR("Tried to destroy game %s when destroyed", name);
        return;
    }

    flags |= GAME_FLAG_DESTROYED;

    if(flags & GAME_FLAG_OPEN)
        OnClose();

    if(pbncQuery)
        pbncQuery->CancelQueryRequest();

    list_remove(&lstGameType[type & GT_TYPE_IDX_MASK], &ln_type);
    list_remove(&lstGameRealm[((gt*) &type)->realm], &ln_realm);

    IdIdxTbl[((gt*) &type)->realm][game_id] = NULL;

    Release();
}


/*
 * Game reference counting
 *
 */
void Game::AddRef()
{
    this->refs++;
}

void Game::Release()
{
    if((refs -= 1) <= 0)
    {
        LDBG("%s released (%d)", name, refs);
        freelist_free(&flGames, this);
    }
}


/*
 * CmpGamePri
 *
 */
uib CmpGamePri(Game* pg1, Game* pg2, uid& ts, uib* pCondition)
{
    if(!pg1->ts_query_completed)
    {
        *pCondition = 1;
        return TRUE;
    }

    if((pg1->flags & GAME_FLAG_RUN) && (pg1->flags & GAME_FLAG_INFO) && !(pg1->flags & GAME_FLAG_POP_SAMP) && (ts-pg1->ts_hosted) >= CFG_RUN_POP_SAMP_TIME)
    {
        *pCondition = 2;
        return TRUE;
    }

    if(pg1->ts_dirty || pg2->ts_dirty)
    {
        *pCondition = 3;
        return pg1->ts_dirty-1 < pg2->ts_dirty-1;
    }

    *pCondition = 4;

    return (__max(pg1->ts_query_completed, pg1->ts_listed) < __max(pg2->ts_query_completed, pg2->ts_listed));
}


/*
 * Game::OnOpen
 *   The game has been opened.
 *
 */
void Game::OnOpen()
{
    // Note discovery time.
    ts_discovered = GetTickCount();

    // Check if this game is a run.
    if((this->run_type = GetRunType(name)))
    {
        char RunName[MAX_GAME_NAME];
        char RunFormat[MAX_GAME_NAME+8];
        uid Sequence;
        Run* pRun;

        this->flags |= GAME_FLAG_RUN;

        if(ParseRunName(this->name, RunName, RunFormat, &Sequence))
        {
            if((pRun = LookupRun(RunName, this->type)))
                pRun->OnGameOpen(this, RunFormat, Sequence);
        }

        else
            LWARN("Failed to parse run name");
     
        LDBG("'%s' marked as run (type: %u)", name, run_type);
    }

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
    uid ts = GetTickCount();

    LOG("[FIRST QUERY] (%s) %s discovery delay: %us first query delay: %us", fmt_gt(type), name, (ts_discovered-ts_hosted)/1000, (ts_query_submitted-ts_discovered)/1000);

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

    if(this->flags & GAME_FLAG_RUN && (ts-ts_hosted) < CFG_CREATE_RUN_THRESH)
    {
        char RunName[MAX_GAME_NAME];
        char RunFormat[MAX_GAME_NAME+8];
        uid Sequence;
        Run* pRun;

        if(ParseRunName(name, RunName, RunFormat, &Sequence))
        {
            if(!LookupRun(RunName, type))
            {
                if((pRun = CreateRun(RunName, type, this->run_type)))
                    pRun->OnGameOpen(this, RunFormat, Sequence);
            }

            else
                LWARN("Run already existed: %s", RunName);
        }

        else
            LWARN("Failed to parse run game name");
    }

    
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
 * Run::OnGameOpen
 *   A game matching our run name has been opened.
 *
 */
void Run::OnGameOpen(Game* pGame, const char* pFormat, uid Sequence)
{
    if(pGame == pPreviousGame)
    {
        LWARN("(%s) Received game open for previous game: %s", Name, pGame->name);
        return;
    }

    if(pGame == pCurrentGame)
    {
        LWARN("(%s) Received game open for current game: %s", Name, pGame->name);
        return;
    }

    LOG("(%s) Got next run: %s; run_count:%u current run: %s; previous run: %s", Name, pGame->name, run_count, pCurrentGame ? pCurrentGame->name : "<NULL>", pPreviousGame ? pPreviousGame->name : "<NULL>");

    // Integrate data from the previous run; this run is not the current, it's 2 runs behind the new run.
    if(pPreviousGame)
    {
        ProcessRunDataSample((pCurrentGame->ts_hosted - pPreviousGame->ts_hosted) / 1000, pPreviousGame->pop_sample);

        pPreviousGame->Release();
        pPreviousGame = NULL;
    }

    if(pCurrentGame)
    {
        // Set the current game as the previous; this will be used to calculate stats.
        pPreviousGame = pCurrentGame;
    }

    // Assign our new current game.
    pCurrentGame = pGame;
    pCurrentGame->AddRef();

    if(!(this->flags & RUN_FLAG_VALID))
    {
        if(run_type != RUN_MISC || (run_type == RUN_MISC && run_count >= 2))
        {
            LOG("Run %s is marked as a valid run", Name);

            this->flags |= RUN_FLAG_VALID;
        }
    }

    // Reset expiration timer.
    timer_set(htExpire, GetTickCount()+GetRunTypeMaxDuration(this->run_type), NULL);

    if(this->flags & RUN_FLAG_VALID)
    {
        pCurrentGame->flags |= GAME_FLAG_VALID_RUN;

        // Update state.
        union
        {
            bsps_run_state* pMsg;
            byte* pd;
        };

        pd = GetEMQ(this->mode).p;
        pMsg->mid = BSP_RUN_STATE;
        pMsg->run_id = GetRunID(this);
        GetEMQ(this->mode).p += sizeof(bsps_run_state) + Enc((bsp_run_desc*) pMsg->ect);
    }
}


/*
 * Run::ComputeStats
 *
 */
void Run::ProcessRunDataSample(uid Duration, uid Popcount)
{
    uid idx = run_count++ % CFG_RUN_SAMPLE_COUNT;

    LOG("(%s) Processing new run data; duration:%u popcount:%u (idx: %u)", Name, Duration, Popcount, idx);

    stats.samples_duration[idx] = Duration;
    stats.samples_popcount[idx] = Popcount;

    uid sam_count = __min(run_count, CFG_RUN_SAMPLE_COUNT);

    if(sam_count > CFG_RUN_MIN_SAMPL_CALC)
    {
        stddev(stats.samples_duration, sam_count, &stats.duration_avg, &stats.duration_stddev);
        stddev(stats.samples_popcount, sam_count, &stats.population_avg, &stats.population_stddev);

        LOG("Computed stats (avg/stddev): duration: %u/%u population: %u/%u",
            stats.duration_avg, stats.duration_stddev,
            stats.population_avg, stats.population_stddev);
    }
}


/*
 * Run::Enc
 *
 */
uib Run::Enc(bsp_run_desc* pd)
{
    pd->cur_game_id = GetGameID(pCurrentGame);
    pd->run_type = run_type;
    pd->duration_avg = (uiw) stats.duration_avg;
    pd->duration_stddev = (uib) stats.duration_stddev;
    pd->popcount_avg = (uib) stats.population_avg;
    pd->rating = stats.rating;

    return sizeof(bsp_run_desc);
}


/*
 * Run::Destroy
 *
 */
void Run::Destroy()
{
    LOG("Destroy run %s", Name);

    timer_destroy(htExpire);

    if(pPreviousGame)
    {
        pPreviousGame->Release();
        pPreviousGame = NULL;
    }

    if(pCurrentGame)
    {
        pCurrentGame->Release();
        pCurrentGame = NULL;
    }

    union
    {
        bsps_run_close* pMsg;
        byte* pd;
    };

    pd = GetEMQ(this->mode).p;
    pMsg->mid = BSP_RUN_CLOSE;
    pMsg->run_id = GetRunID(this);
    GetEMQ(this->mode).p += sizeof(bsps_run_close);

    list_remove(&lstRunType[mode & GT_TYPE_IDX_MASK], &ln_type);
    freelist_free(&flRuns, this);
}


/*
 * Character::OnJoinGame
 *
 */
void Character::OnJoinGame(Game* pGame, uib pid)
{
    LDBG("%s (pid:%u) joins %s", this->name, pid, pGame->name);

    union
    {
        bsps_game_padd* pMsg;
        byte* pd;
    };

    pd = GetEMQ(pGame->type).p;
    pMsg->mid = BSP_GAME_PADD;
    pMsg->game_id = GetGameID(pGame);
    GetEMQ(pGame->type).p += sizeof(bsps_game_padd) + pGame->EncPlayer(pid, (bsp_pdesc*) pMsg->etc);
}


/*
 * Character::Enc
 *
 */
uib Character::Enc(bsp_char_desc* pd)
{
    pd->clvl = this->clvl;
    pd->cls = this->cls;
    pd->name_len = this->name_len;
    memcpy(pd->name, this->name, this->name_len);

    return sizeof(bsp_char_desc) + this->name_len;
}


/*
 * Character::OnLeaveGame
 *
 */
void Character::OnLeaveGame(Game* pGame, uib pid)
{
    LDBG("%s (pid:%u) leaves %s", this->name, pid, pGame->name);


    union
    {
        bsps_game_prem* pMsg;
        byte* pd;
    };

    pd = GetEMQ(pGame->type).p;
    pMsg->mid = BSP_GAME_PREM;
    pMsg->game_id = GetGameID(pGame);
    pMsg->pid = pid;
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
            return ple;
    }

    return NULL;
}


/*
 * LookupRun
 *
 */
Run* LookupRun(const char* pRunName, uid Type)
{
    // Brute search.
    LIST_ITERATE(lstRunType[Type & GT_TYPE_IDX_MASK], Run, ln_type)
    {
        if(cmpstri(pRunName, ple->Name))
            return ple;
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
    uib Condition;

    LIST_ITERATE(lstGameRealm[((gt*) &pbnc->Mode)->realm], Game, ln_realm)
    {
        if(ple->pbncQuery)
            continue;

        if(!pBest)
        {
            pBest = ple;
            pBest->dbg_condition = 0xFF;
            continue;
        }
        
        uib Result = CmpGamePri(ple, pBest, ts, &(Condition=0xFF));

        //LDBG("%s (last-q:%us last-l:%us dirty:%u) | %s | %s (last-q:%us last-l:%us dirty:%u) -- cond %u",
        //        ple->name, (ts-ple->ts_query_submitted)/1000, (ts-ple->ts_listed)/1000, ple->ts_dirty != 0,
        //        Result ? "GREATER THAN" : "LESS THAN",
        //        pBest->name, (ts-pBest->ts_query_submitted)/1000, (ts-pBest->ts_listed)/1000, pBest->ts_dirty != 0,
        //        Condition);

        if(Result)
        {
            pBest = ple;
            pBest->dbg_condition = Condition;

            if(Condition < 3)
                break;
        }
    }

    if(!pBest)
    {
        LWARN("No game to query!");
        return;
    }

    if(pbnc->SubmitQueryRequest(pBest->name, pBest))
    {
        LOG("[SUBMIT QUERY] (%s) %s | last submit:%us last complete:%us last listed:%u dirty:%u condition:%u)",
            fmt_gt(pBest->type),
            pBest->name,
            (ts-pBest->ts_query_submitted)/1000,
            (ts-pBest->ts_query_completed)/1000,
            (ts-pBest->ts_listed)/1000,
            pBest->ts_dirty != 0,
            pBest->dbg_condition);

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

    pGame->ts_query_completed = GetTickCount();
    pGame->total_queries++;

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

    if(!(pGame->flags & GAME_FLAG_OPEN))
    {
        pGame->flags|= GAME_FLAG_OPEN;
        pGame->OnOpen();
    }

    pGame->host_clvl = pMsg->creator_lvl;
    pGame->clvl_diff = pMsg->diff_lvl;
    pGame->max_players = pMsg->max_players;

    if(!(pGame->flags & GAME_FLAG_INFO))
    {
        pGame->flags |= GAME_FLAG_INFO;
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
    uib bAccurate = (GetTickCount() - pGame->ts_hosted) < CFG_JOIN_ACCURACY_THRESH;

    for(uib i = 0, x = 0; i < CharCount && i < 8; i++, ps++, x=0)
    {
        if(!*ps)
            continue;

        for(; x < 8; x++)
        {
            if(!(pGame->bv_chars & (1<<x)))
                continue;

            if(bzstrcmp(ps, pGame->Charlist[x].pc->name))
                break;
        }

        Character* pc = pGame->Charlist[x].pc;

        // Character exists; just an update. 
        if(x < 8)
        {
            pc = pGame->Charlist[x].pc;
            LeaveMask |= (1<<x);
        }

        // Character doesn't exist; joined the game.
        else
            pJoin[JoinLen++] = (pc = FindCharacter((uib) Type, ps, TRUE));

        // Update character information.
        pc->clvl = pMsg->char_levels[i];
        pc->cls = pMsg->char_classes[i];

        if(bAccurate && !pGame->pHost && pc->clvl == pGame->host_clvl)
        {
            pGame->pHost = pc;

            LDBG("[%s] Game host set as: %s", pGame->name, pc->name);
        }

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
            pGame->Charlist[i].pc->OnLeaveGame(pGame, i);
            pGame->Charlist[i].pc = NULL;

            redundant = 0;
        }

        if(JoinLen && !(pGame->bv_chars & (1<<i)))
        {
            pGame->bv_chars ^= (1<<i);
            pGame->Charlist[i].pc = pJoin[--JoinLen];
            pGame->Charlist[i].pc->OnJoinGame(pGame, i);

            redundant = 0;
        }
    }

    pGame->cur_players = __popcnt((unsigned int) pGame->bv_chars);

    if((pGame->flags & GAME_FLAG_RUN) && !(pGame->flags & GAME_FLAG_POP_SAMP) && (GetTickCount() - pGame->ts_hosted) >= CFG_RUN_POP_SAMP_TIME)
    {
        pGame->pop_sample = __max(pGame->pop_sample, pGame->cur_players);
        pGame->flags |= GAME_FLAG_POP_SAMP;

        LOG("(G:%s) Got pop sample: %u", pGame->name, pGame->pop_sample);
    }

    if(redundant)
    {
        pGame->wasted_queries++;
    }

    LDBG("[COMPLETE QUERY] (%s) %s | Redundant? %s (%u of %u)", fmt_gt(pGame->type), pGame->name, redundant ? "YES" : "No", pGame->wasted_queries, pGame->total_queries);

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

    if(!(pGame->flags & GAME_FLAG_OPEN))
    {
        pGame->flags |= GAME_FLAG_OPEN;

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

        LDBG("Create character: p->name: '%s' (%u) pstrName: '%s' -- total: %u", p->name, p->name_len, pstrName, Characters.count);
    }

    return p;
}



uib GetRunType(const char* pName)
{
    struct
    {
        const uib run_type;
        const char* term;
    } static const KEYWORDS[] =
    {
        { RUN_CBAAL, "cbaal" },
        { RUN_CBAAL, "dbaal" },
        { RUN_CBAAL, "dbal" },
        { RUN_CBAAL, "diaba" },
        { RUN_CBAAL, "db" },
        { RUN_CBAAL, "dib" },
        { RUN_CBAAL, "cbal" },
        { RUN_COW, "cow" },
        { RUN_TOMB, "tomb" },
        { RUN_TRIST, "trist" },
        { RUN_CHANT, "chant" },
        { RUN_CHAOS, "dia" },
        { RUN_CHAOS, "cha" },
        { RUN_CHAOS, "khao" },
        { RUN_CHAOS, "cs" },
        { RUN_BAAL, "baal" },
        { RUN_BAAL, "ball" },
        { RUN_BAAL, "bal" },
    };

    uib len = (uib) strlen(pName);
    uib result = NULL;

    // Runs must have a trailing number.
    if(((pName[len-1]) & 0xF0) != 0x30)
        return NULL;

    // Try to determine the run type.
    for(uib i = 0; i < ARRAYSIZE(KEYWORDS); i++)
    {
        const char* pd = pName;

        while(*pd)
        {
            while(*pd && !bzchrcmp(*pd, KEYWORDS[i].term[0]))
                pd++;

            if(!*pd)
                break;

            const char* ps1 = pd;
            const char* ps2 = KEYWORDS[i].term;

            while(bzchrcmp(*ps1, *ps2) && *ps1)
            {
                ps1++;
                ps2++;
            }

            if(!*ps2)
            {
                LDBG("Matched '%s' at '%s'", KEYWORDS[i].term, pd);

                result += KEYWORDS[i].run_type;

                if(result >= RUN_CBAAL)
                    goto DONE;
            }

            pd++;
        }
    }

    // Some unrecognized run.
    if(!result)
    {
        LWARN("Unknown run game: %s", pName);
        result = RUN_MISC;
    }

DONE:
    // Shouldnt happen
    if(result > RUN_MAX)
    {
        LWARN("Bad run identification; %u", result);
        return NULL;
    }

    return result;
}


uib ParseRunName(const char* pName, char* pRunName, char* pFormat, uid* pSequence)
{
    uib len = strlen(pName);
    uib digits = 0;

    // Sequence number is comprised of trailing digits.
    pName += len-1;

    while(digits < len && ((*pName >= '0') && (*pName <= '9')))
    {
        pName--;
        digits++;
    }

    if(!digits || digits == len)
        return FALSE;

    // Length excluding the sequence.
    len -= digits;
    pName++;

    // Extract sequence number and build run name and static format prefix.
    *pSequence = sxui(pName);
    memcpy(pRunName, pName-len, len);
    memcpy(pFormat, pName-len, len);
    pRunName[len] = '\0';

    // Build the sequence number format.
    pFormat += len;
    *pFormat++ = '%';
    *pFormat++ = '0';
    *pFormat++ = '0' + digits;
    *pFormat++ = 'u';
    *pFormat = '\0';

    return TRUE;
}


uid GetRunTypeMaxDuration(uib RunType)
{
    static const uid TBL[] =
    {
        (10*60*1000),
        (8*60*1000),
        (10*60*1000),
        (15*60*1000),
        (30*60*1000),
        (25*60*1000),
        (35*60*1000),
        (10*60*1000),
        (30*60*1000),
    };

    return TBL[RunType];
}


/*
 * ServerStartup
 *
 */
uib ServerStartup()
{
    if(!(plGames = (Game*) VirtualAlloc(NULL, CFG_GAME_POOL_SZ * sizeof(Game), MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE)) || !(plRuns = (Run*) VirtualAlloc(NULL, CFG_RUN_POOL_SZ * sizeof(Run), MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE)))
        return FALSE;

    freelist_init(&flGames, plGames, sizeof(Game), CFG_GAME_POOL_SZ);
    freelist_init(&flRuns, plRuns, sizeof(Run), CFG_RUN_POOL_SZ);

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


    if(!(Users.p = (User*) MapPersistantArray("users", USER_DATA_SIZE, CFG_MAX_BN_USERS, &Users.count, TRUE)))
    {
        LERR("Failed to load users from disk");
        return FALSE;
    }

    LOG("Loaded users: %u", Users.count);

    // Fixup.
    for(uid i = 0; i < Users.count; i++)
        FindUser(Users.p[i].mode, Users.p[i].name, TRUE);

    if(!(Characters.p = (Character*) MapPersistantArray("characters", CHARACTER_DATA_SIZE, CFG_MAX_BN_CHARACTERS, &Characters.count, TRUE)))
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
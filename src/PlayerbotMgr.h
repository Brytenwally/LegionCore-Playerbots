/*
 * mod-playerbots: bot lifecycle (login, update, logout) on the core's bot sessions.
 *
 * A bot is a character logged in on a session created by WorldSession::CreateBotSession: no
 * client, not in World::m_sessions. This manager holds those sessions, updates them while they
 * have no map (the maps update them afterwards, like any session) and drops them once the core
 * reports them ended (character logged out).
 */

#ifndef MOD_PLAYERBOTS_PLAYERBOTMGR_H
#define MOD_PLAYERBOTS_PLAYERBOTMGR_H

#include "Define.h"
#include "ObjectGuid.h"
#include "BotFactory.h"
#include "BotGear.h"
#include "BotFormation.h"
#include "BotAvoidAoe.h"
#include "LFG.h"
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class Player;
class PlayerbotAI;
class WorldSession;

class PlayerbotMgr
{
public:
    static PlayerbotMgr* instance();

    void LoadConfig();
    bool IsEnabled() const { return _enabled; }

    enum class SummonRevive : uint8
    {
        Never,              // a dead bot is not summoned
        OutOfCombat,        // revived by "summon" in party chat while the owner is out of combat
        Always              // also while the owner is in combat
    };
    SummonRevive GetSummonRevive() const { return _summonRevive; }
    // combat: 0 = no combat log, 1 = engage/stop and each cast, 2 = also why each rotation step is skipped
    uint32 GetCombatLog() const { return _combatLog; }
    float GetEngageRange() const { return _engageRange; }
    uint32 GetEngageDelay() const { return _engageDelay; }

    // plan step 4.5
    struct SurvivalOptions
    {
        bool defensives = true;
        float potionHealthPct = 30.0f;
        float potionManaPct = 20.0f;
        bool potionsOnAlts = false;     // potions are the player's items: off for alts unless set
        bool rest = true;
        float restHealthPct = 60.0f;
        float restManaPct = 50.0f;
    };
    SurvivalOptions const& GetSurvivalOptions() const { return _survival; }
    BotFormation::Shape GetDefaultFormation() const { return _defaultFormation; }
    // SimC rotations (Playerbots.Rotation.*)
    bool UseApl() const { return _useApl; }
    std::string const& GetAplDirectory() const { return _aplDirectory; }

    // loot rolls and equipping from the bags (owner's rules, 2026-11-05)
    struct LootOptions
    {
        bool randomBotsRoll = true;     // false: only alt bots roll; random bots pass
        float needUpgradePct = 5.0f;    // need when the item beats the worn piece by this much
        bool autoEquip = true;
        bool autoEquipAlts = false;     // alt rule: nothing changes an alt unless ordered
        float equipUpgradePct = 5.0f;
        bool autoSellGray = true;       // sell gray items when standing at a vendor
        bool autoSellGrayAlts = false;  // alt rule: nothing changes an alt unless ordered
    };
    LootOptions const& GetLootOptions() const { return _loot; }

    // strategies every bot starts with (Playerbots.Strategies.*), and the loot / aggressive settings
    std::string const& GetDefaultCombatStrategies() const { return _defaultCo; }
    std::string const& GetDefaultNonCombatStrategies() const { return _defaultNc; }
    struct StrategyOptions
    {
        uint32 lootMinQuality = 0;      // items below this quality stay on the corpse (quest items always taken)
        float lootRange = 30.0f;
        float aggressiveRange = 25.0f;
        uint32 aggressiveLevelsAbove = 6;
    };
    StrategyOptions const& GetStrategyOptions() const { return _strategy; }
    // co avoidaoe: Playerbots.AvoidAoe.* (radius limit, step, never-avoided spells, whisper)
    BotAvoidAoe::Options const& GetAvoidAoeOptions() const { return _avoidAoe; }
    bool GetAvoidAoeTell() const { return _avoidAoeTell; }

    // ".playerbots self" (player's thread): turns the AI on or off for his own character; reply = what to tell him
    bool ToggleSelfBot(Player* player, std::string& reply);
    bool IsSelfGMOnly() const { return _selfGMOnly; }
    // a client's character, every update on his own thread: runs his self-bot AI and reports his dungeon
    // finder queues to the fill
    void OnClientUpdate(Player* player, uint32 diff);
    // Any thread: a dungeon finder fill bot whose queuer left the queue is free again (no master, back to
    // GM Island)
    void RequestRelease(ObjectGuid const& bot);

    // Starts logging the character in as a bot. notifyGuid (may be empty) gets "Bot 'X' logged in."
    // when the character is in the world, or the reason it failed.
    bool AddBot(std::string name, ObjectGuid const& notifyGuid, std::string& error);
    // Any thread: queues ".playerbots create"; the world thread creates the character, logs it in a few
    // seconds later (its rows must be in the database) and has it initialized once in the world.
    void RequestCreate(BotFactory::CreateRequest const& request);
    // Any thread: a masterless random bot accepted a player's invite; the world thread makes the player its
    // master (new AI) and has it set up to his level and summoned to him.
    void RequestAssign(ObjectGuid const& bot, BotFactory::CreateRequest const& request);
    // a bot account (<AccountPrefix>...): its bots are random bots, not alts
    bool IsBotAccountName(std::string const& accountName) const;
    BotGear::Options GetGearOptions() const { return _gearOptions; }

    // Logs the bot out (saved); its entry goes once the session has ended.
    bool RemoveBot(std::string name, std::string& error);
    void RemoveAllBots();

    struct BotInfo
    {
        std::string name;
        std::string state;
    };
    std::vector<BotInfo> ListBots() const;

    // a bot character (not a self-bot), from any thread
    bool IsBot(ObjectGuid const& guid) const;
    // the bot's AI, from any thread (the AI itself is used on the bot's own map thread only)
    std::shared_ptr<PlayerbotAI> GetAI(ObjectGuid const& bot) const;
    // the bots this player owns (PlayerbotAI::IsMaster), from any thread
    std::vector<std::pair<ObjectGuid, std::shared_ptr<PlayerbotAI>>> GetBotsOwnedBy(ObjectGuid const& player, uint32 accountId) const;

    // Startup (SkyFire's / AzerothCore's DeleteRandomBotAccounts): with Playerbots.DeleteBotAccounts = 1,
    // every account named <AccountPrefix>... and its characters are deleted, and no pool is built this boot.
    void DeleteBotAccountsAtStartup();

    // World thread, every world update.
    void Update(uint32 diff);
    // Any thread: a client's character logged out.
    void OnPlayerLogout(uint32 accountId, ObjectGuid const& player);
    void Shutdown();

private:
    friend class PlayerbotAI;                               // BgMember reports from inside battlegrounds
    PlayerbotMgr() = default;

    // Dungeon finder fill (owner's request, 2026-11-05): a client queued for a dungeon (alone or as his group's
    // leader) is reported from his thread; after Playerbots.LFG.FillDelay the world thread sets up idle pool
    // bots of his faction one by one (his level, a spec of a missing role) and queues them for the same
    // dungeons, so other players may queue first.
    struct QueueReport
    {
        ObjectGuid queuer;                                  // the reporting player: the fill bots' master
        ObjectGuid key;                                     // his group (one fill per group), or himself
        uint32 queueId = 0;
        uint8 level = 0;
        uint32 team = 0;
        bool queued = false;                                // LFG_STATE_QUEUED (fill only then)
        lfg::LfgDungeonSet dungeons;
        uint8 tanks = 0;                                    // roles his group already brings
        uint8 healers = 0;
        uint8 dps = 0;
    };
    struct LfgFill
    {
        QueueReport report;
        uint32 firstSeen = 0;                               // getMSTime
        uint32 lastSeen = 0;
        uint32 nextJoin = 0;
        struct FillBot
        {
            ObjectGuid guid;
            uint8 role = 0;                                 // lfg::PLAYER_ROLE_*
            uint32 started = 0;                             // getMSTime of its setup
        };
        std::vector<FillBot> bots;
        uint8 warned = 0;                                   // roles already logged as "no idle pool bot"
    };
    void UpdateLfgFill(uint32 diff);
    bool StartLfgBot(LfgFill& fill, uint8 role);
    void ReleaseBot(ObjectGuid const& guid);
    std::mutex _lfgLock;
    std::map<ObjectGuid, uint32> _lfgCheckTimers;           // client -> ms since his last queue report (same lock)
    std::vector<QueueReport> _queueReports;                 // same lock
    std::vector<ObjectGuid> _releaseRequests;               // same lock
    std::map<std::pair<ObjectGuid, uint32>, LfgFill> _lfgFills;  // world thread only
    uint32 _lfgTimer = 0;
    bool _lfgFillEnabled = true;
    uint32 _lfgFillDelay = 30000;
    uint32 _lfgJoinInterval = 2000;

    // Battleground fill (owner's rules, 2026-11-06 / 07), the dungeon finder fill's twin. Clients waiting in a normal
    // battleground queue are reported from their threads; per queue and level bracket, after Playerbots.BG.FillDelay,
    // idle pool bots of both factions join until each team is full (Playerbots.BG.BotsPerTeam, 0 = the battleground's
    // maximum) with a fixed role mix (10% tanks, 20% healers rounded down, the rest dps). Every bot and client inside a
    // battleground reports too (BgMember): a player queueing for a battleground whose team is full makes a fill bot of
    // his role leave it (the queue then invites him into the freed place), and a team short of players for 30 s with
    // nobody waiting gets a bot of the missing role (a refused invitation, a bot gone).
    struct BgReport
    {
        ObjectGuid queuer;
        uint8 queueType = 0;                                // MS::Battlegrounds::BattlegroundQueueTypeId
        uint16 bgTypeId = 0;                                // its BattlemasterList id (the random one included)
        uint8 bracket = 0;                                  // PVPDifficulty RangeIndex
        uint8 level = 0;
        uint32 team = 0;
        uint8 role = 0;                                     // ROLES_TANK / HEALER / DPS of his spec
        uint8 partyBots = 0;                                // bots of his group queued with him (leader only)
    };
    struct BgMember
    {
        ObjectGuid guid;
        uint32 instanceId = 0;
        uint8 queueType = 0;                                // of the battleground's queued type (random included)
        uint16 bgTypeId = 0;
        uint8 bracket = 0;
        uint8 status = 0;                                   // BattlegroundStatus
        uint32 maxPerTeam = 0;
        uint32 team = 0;
        uint8 role = 0;
        uint8 level = 0;
        bool fillBot = false;                               // a pool bot of the fill: it may give its place up
        uint32 lastSeen = 0;
    };
    struct BgFillBot
    {
        ObjectGuid guid;
        uint32 team = 0;
        uint8 role = 0;
        uint32 started = 0;
    };
    struct BgFill
    {
        uint8 queueType = 0;
        uint16 bgTypeId = 0;
        uint8 level = 0;                                    // the fill bots' level (the first queuer's)
        ObjectGuid master;                                  // the first queuer: the fill bots' master
        uint32 firstSeen = 0;
        uint32 nextJoin = 0;
        uint32 endedAt = 0;                                 // nobody waits any more: bots still queued get 60 s
        struct Queuer
        {
            uint32 team = 0;
            uint8 role = 0;
            uint8 partyBots = 0;
            uint32 lastSeen = 0;
        };
        std::map<ObjectGuid, Queuer> queuers;
        std::vector<BgFillBot> bots;
        uint8 warned = 0;                                   // 1 Alliance, 2 Horde: "no idle pool bot" logged
    };
    struct BgInstance
    {
        uint8 queueType = 0;
        uint16 bgTypeId = 0;
        uint8 bracket = 0;
        uint8 status = 0;
        uint32 maxPerTeam = 0;
        uint8 level = 0;
        uint32 lastSeen = 0;
        std::map<ObjectGuid, BgMember> members;
        std::vector<BgFillBot> pending;                     // top-up bots queued for it, not inside yet
        uint32 shortSince[2] = { 0, 0 };                    // getMSTime a team became short of players, 0 = full
        uint32 nextJoin = 0;
    };
    // Dungeon finder groups short of members (owner, builds #28-#31: after a kick or a leave the core's replacement
    // queue never matched the group with a bot, though a player's invite brought one in at once): the group's leader,
    // a player inside the dungeon, is reported; after Playerbots.LFG.FillDelay he invites an idle pool bot of a
    // missing role (his session sends the invite), and the bot teleports to him once in the group.
    struct ReplaceReport
    {
        ObjectGuid leader;
        uint32 leaderAccount = 0;
        ObjectGuid group;
        uint32 dungeonId = 0;
        uint8 level = 0;
        uint32 team = 0;
        uint8 members = 0;
        uint8 tanks = 0;
        uint8 healers = 0;
        uint8 dps = 0;
    };
    struct ReplaceFill
    {
        ReplaceReport report;
        uint32 firstSeen = 0;
        uint32 lastSeen = 0;
        uint32 nextJoin = 0;
        std::vector<LfgFill::FillBot> bots;                 // invited, not in the group yet
        uint8 warned = 0;
    };
    void ReportDungeonGroup(Player* player);
    void UpdateReplaceFill(std::vector<ReplaceReport> const& reports, uint32 now);
    bool StartReplaceBot(ReplaceFill& fill, uint8 role);
    std::vector<ReplaceReport> _replaceReports;             // _lfgLock
    std::map<ObjectGuid, ReplaceFill> _replaceFills;        // by group; world thread only
    void ReportBgQueues(Player* player);
    void UpdateBgFill(std::vector<BgReport> const& reports, std::vector<BgMember> const& members, uint32 now);
    // a team of the battleground needs: the role mix of perTeam players minus those it has
    static void RoleNeeds(uint32 perTeam, std::vector<uint8> const& roles, int32 need[3]);
    // an idle pool bot of that faction and role, at most `level`, set up and queued for bgTypeId; empty if none
    ObjectGuid StartBgBot(uint32 team, uint8 role, uint8 level, uint16 bgTypeId, ObjectGuid const& master, uint8& warned);
    bool MakeRoomFor(BgReport const& report, uint32 now);
public:
    // any thread: a bot or a client inside a battleground (every 2 s)
    void ReportBgMember(BgMember const& member);
private:
    std::vector<BgReport> _bgReports;                       // _lfgLock
    std::vector<BgMember> _bgMemberReports;                 // _lfgLock
    std::map<std::pair<uint8, uint8>, BgFill> _bgFills;     // (queue type, bracket) -> fill; world thread only
    std::map<uint32, BgInstance> _bgInstances;              // instance id -> who is in it; world thread only
    std::map<ObjectGuid, uint32> _bgRoomMade;               // queuer -> getMSTime a bot left for him
    bool _bgFillEnabled = true;
    uint32 _bgFillDelay = 30000;
    uint32 _bgJoinInterval = 1000;
    uint32 _bgBotsPerTeam = 0;

    struct Bot
    {
        std::shared_ptr<WorldSession> session;
        std::string name;
        ObjectGuid notifyGuid;
        bool inWorld = false;                               // "logged in" was reported
        bool removing = false;
        std::shared_ptr<BotFactory::CreateRequest> init;    // a created bot, initialized once in the world
        bool toGMIsland = false;                            // a pool bot logged in at startup: waits on GM Island
        int32 ownerGoneTimer = -1;                          // ms until logout after its owner logged out, -1 = none
        uint32 restUntil = 0;                               // getMSTime: a released bot is not used to fill a queue before
    };
    // ".playerbots create": an idle logged-in pool bot, else an offline bot character, of the class and the
    // creator's faction and at most his level; false when there is none (a new one is made)
    bool ReuseBot(BotFactory::CreateRequest const& request);
    // The startup pool is built a little each world update (a few accounts / characters / logins), not in one
    // go: 100 accounts (each a deliberately slow Argon2 hash) and 600 characters at once would hold the world
    // thread for minutes (MaxCoreStuckTime).
    void UpdatePool(uint32 diff);
    enum class PoolStage : uint8 { Start, Accounts, Characters, WaitLogins, Logins, Done };
    PoolStage _poolStage = PoolStage::Start;
    uint32 _poolNextAccount = 1;
    std::map<uint32, uint32> _poolCounts;                   // bot account -> characters
    std::map<uint32, std::string> _poolNames;               // bot account -> name
    uint32 _poolTotal = 0;
    uint32 _poolMade = 0;
    int32 _poolTimer = 0;
    std::vector<BotFactory::StoredBot> _poolToLogin;
    size_t _poolLoginIndex = 0;
    uint32 _poolLoggedIn = 0;

    struct PendingLogin
    {
        std::string name;
        BotFactory::CreateRequest request;
        uint32 delay = 0;                                   // ms until the login
    };
    void ProcessCreates(uint32 diff);

    void Notify(ObjectGuid const& guid, std::string const& text) const;
    void LogoutAndReset(Bot& bot);
    Bot* FindBotByName(std::string const& name);

    bool _enabled = false;
    SummonRevive _summonRevive = SummonRevive::OutOfCombat;
    uint32 _combatLog = 1;
    float _engageRange = 30.0f;
    uint32 _engageDelay = 1200;
    SurvivalOptions _survival;
    LootOptions _loot;
    std::string _defaultCo = "tank,defensives,potions";
    std::string _defaultNc = "follow,loot,rest,roll,autoequip,talents";
    StrategyOptions _strategy;
    BotAvoidAoe::Options _avoidAoe;
    bool _avoidAoeTell = false;
    std::map<ObjectGuid, Bot> _bots;                        // world thread only
    mutable std::mutex _aiLock;
    std::map<ObjectGuid, std::shared_ptr<PlayerbotAI>> _ais;  // written on the world thread, read from map threads
    std::map<ObjectGuid, std::shared_ptr<PlayerbotAI>> _selfAis;  // self-bots by player guid, same lock
    std::atomic<uint32> _selfCount{ 0 };                    // skips the lookup for every client update while 0
    bool _selfGMOnly = true;
    bool _useApl = true;
    bool _deleteBotAccounts = false;
    std::string _aplDirectory = "playerbots_apl";
    BotFormation::Shape _defaultFormation = BotFormation::Shape::Role;
    BotGear::Options _gearOptions;
    BotFactory::CreateConfig _createConfig;
    std::mutex _createLock;
    std::vector<BotFactory::CreateRequest> _createRequests;  // filled from any thread, read in Update
    std::vector<std::pair<ObjectGuid, BotFactory::CreateRequest>> _assignRequests;  // same lock
    std::vector<PendingLogin> _pendingLogins;               // world thread only
    std::mutex _logoutLock;
    std::vector<std::pair<uint32, ObjectGuid>> _loggedOutPlayers;  // filled from map threads, read in Update
};

#define sPlayerbotMgr PlayerbotMgr::instance()

#endif

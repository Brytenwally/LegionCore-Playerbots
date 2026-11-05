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
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

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

    // Starts logging the character in as a bot. notifyGuid (may be empty) gets "Bot 'X' logged in."
    // when the character is in the world, or the reason it failed.
    bool AddBot(std::string name, ObjectGuid const& notifyGuid, std::string& error);
    // Any thread: queues ".playerbots create"; the world thread creates the character, logs it in a few
    // seconds later (its rows must be in the database) and has it initialized once in the world.
    void RequestCreate(BotFactory::CreateRequest const& request);
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

    // the bot's AI, from any thread (the AI itself is used on the bot's own map thread only)
    std::shared_ptr<PlayerbotAI> GetAI(ObjectGuid const& bot) const;
    // the bots this player owns (PlayerbotAI::IsMaster), from any thread
    std::vector<std::pair<ObjectGuid, std::shared_ptr<PlayerbotAI>>> GetBotsOwnedBy(ObjectGuid const& player, uint32 accountId) const;

    // World thread, every world update.
    void Update(uint32 diff);
    // Any thread: a client's character logged out.
    void OnPlayerLogout(uint32 accountId);
    void Shutdown();

private:
    PlayerbotMgr() = default;

    struct Bot
    {
        std::shared_ptr<WorldSession> session;
        std::string name;
        ObjectGuid notifyGuid;
        bool inWorld = false;                               // "logged in" was reported
        bool removing = false;
        std::shared_ptr<BotFactory::CreateRequest> init;    // a created bot, initialized once in the world
        bool toGMIsland = false;                            // a pool bot logged in at startup: waits on GM Island
    };
    // ".playerbots create": an idle logged-in pool bot, else an offline bot character, of the class and the
    // creator's faction and at most his level; false when there is none (a new one is made)
    bool ReuseBot(BotFactory::CreateRequest const& request);
    void UpdatePool(uint32 diff);
    bool _poolMade = false;
    int32 _poolLoginDelay = -1;                             // ms until the startup logins, -1 = none pending

    struct PendingLogin
    {
        std::string name;
        BotFactory::CreateRequest request;
        uint32 delay = 0;                                   // ms until the login
    };
    void ProcessCreates(uint32 diff);

    void Notify(ObjectGuid const& guid, std::string const& text) const;
    Bot* FindBotByName(std::string const& name);

    bool _enabled = false;
    SummonRevive _summonRevive = SummonRevive::OutOfCombat;
    uint32 _combatLog = 1;
    float _engageRange = 30.0f;
    uint32 _engageDelay = 1200;
    std::map<ObjectGuid, Bot> _bots;                        // world thread only
    mutable std::mutex _aiLock;
    std::map<ObjectGuid, std::shared_ptr<PlayerbotAI>> _ais;  // written on the world thread, read from map threads
    BotGear::Options _gearOptions;
    BotFactory::CreateConfig _createConfig;
    std::mutex _createLock;
    std::vector<BotFactory::CreateRequest> _createRequests;  // filled from any thread, read in Update
    std::vector<PendingLogin> _pendingLogins;               // world thread only
    std::mutex _logoutLock;
    std::vector<uint32> _loggedOutAccounts;                 // filled from map threads, read in Update
};

#define sPlayerbotMgr PlayerbotMgr::instance()

#endif

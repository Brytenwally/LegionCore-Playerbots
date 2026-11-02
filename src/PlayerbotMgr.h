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

    // Starts logging the character in as a bot. notifyGuid (may be empty) gets "Bot 'X' logged in."
    // when the character is in the world, or the reason it failed.
    bool AddBot(std::string name, ObjectGuid const& notifyGuid, std::string& error);
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
    };

    void Notify(ObjectGuid const& guid, std::string const& text) const;
    Bot* FindBotByName(std::string const& name);

    bool _enabled = false;
    std::map<ObjectGuid, Bot> _bots;                        // world thread only
    mutable std::mutex _aiLock;
    std::map<ObjectGuid, std::shared_ptr<PlayerbotAI>> _ais;  // written on the world thread, read from map threads
    std::mutex _logoutLock;
    std::vector<uint32> _loggedOutAccounts;                 // filled from map threads, read in Update
};

#define sPlayerbotMgr PlayerbotMgr::instance()

#endif

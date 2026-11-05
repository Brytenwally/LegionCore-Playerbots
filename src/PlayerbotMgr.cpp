/*
 * mod-playerbots: bot lifecycle. See PlayerbotMgr.h.
 */

#include "PlayerbotMgr.h"
#include "PlayerbotAI.h"
#include "AccountMgr.h"
#include "Chat.h"
#include "Config.h"
#include "GlobalFunctional.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "World.h"
#include "WorldSession.h"
#include <algorithm>

PlayerbotMgr* PlayerbotMgr::instance()
{
    static PlayerbotMgr instance;
    return &instance;
}

void PlayerbotMgr::LoadConfig()
{
    _enabled = sConfigMgr->GetBoolDefault("Playerbots.Enable", false);
    _summonRevive = SummonRevive(std::min<uint32>(sConfigMgr->GetIntDefault("Playerbots.Summon.Revive", 1), 2));
    _combatLog = std::min<uint32>(sConfigMgr->GetIntDefault("Playerbots.Combat.Log", 1), 2);
    _engageRange = std::max(5.0f, sConfigMgr->GetFloatDefault("Playerbots.Combat.EngageRange", 30.0f));
    _engageDelay = sConfigMgr->GetIntDefault("Playerbots.Combat.EngageDelay", 1200);

    _gearOptions.minQuality = std::min<uint32>(sConfigMgr->GetIntDefault("Playerbots.AutoGear.MinQuality", 0), 5);
    _gearOptions.maxQuality = std::min<uint32>(sConfigMgr->GetIntDefault("Playerbots.AutoGear.MaxQuality", 4), 5);
    _gearOptions.maxItemLevel = sConfigMgr->GetIntDefault("Playerbots.AutoGear.MaxItemLevel", 0);

    _createConfig.accountPrefix = sConfigMgr->GetStringDefault("Playerbots.Create.AccountPrefix", "PLAYERBOT");
    _createConfig.accountPassword = sConfigMgr->GetStringDefault("Playerbots.Create.AccountPassword", "playerbot");
    _createConfig.charactersPerAccount = std::max<uint32>(1, sConfigMgr->GetIntDefault("Playerbots.Create.CharactersPerAccount", 10));
    if (_createConfig.accountPrefix.size() < 3)
        _createConfig.accountPrefix = "PLAYERBOT";
}

bool PlayerbotMgr::AddBot(std::string name, ObjectGuid const& notifyGuid, std::string& error)
{
    if (!_enabled)
    {
        error = "Playerbots are disabled (set Playerbots.Enable = 1 in playerbots.conf).";
        return false;
    }

    if (!normalizePlayerName(name))
    {
        error = "Invalid character name.";
        return false;
    }

    ObjectGuid const guid = ObjectMgr::GetPlayerGUIDByName(name);
    if (guid.IsEmpty())
    {
        error = "No character named '" + name + "'.";
        return false;
    }

    if (_bots.find(guid) != _bots.end())
    {
        error = "'" + name + "' is already a bot.";
        return false;
    }

    // a second Player with the same guid would collide in the object accessor
    if (ObjectAccessor::FindPlayer(guid, false))
    {
        error = "'" + name + "' is already online.";
        return false;
    }

    uint32 const accountId = ObjectMgr::GetPlayerAccountIdByGUID(guid);
    std::string accountName;
    if (!accountId || !AccountMgr::GetName(accountId, accountName))
    {
        error = "Could not find the account of '" + name + "'.";
        return false;
    }

    // bots always play at player security, whatever their account's level
    std::shared_ptr<WorldSession> session = WorldSession::CreateBotSession(accountId, std::move(accountName), SEC_PLAYER,
        uint8(sWorld->getIntConfig(CONFIG_EXPANSION)), LOCALE_enUS);
    if (!session->LoginBot(guid))
    {
        error = "Could not start loading '" + name + "'.";
        return false;
    }

    Bot& bot = _bots[guid];
    bot.session = session;
    bot.name = name;
    bot.notifyGuid = notifyGuid;

    TC_LOG_INFO("server.loading", "[mod-playerbots] Logging in bot '%s' (account %u).", name.c_str(), accountId);
    return true;
}

PlayerbotMgr::Bot* PlayerbotMgr::FindBotByName(std::string const& name)
{
    for (auto& pair : _bots)
        if (pair.second.name == name)
            return &pair.second;
    return nullptr;
}

bool PlayerbotMgr::RemoveBot(std::string name, std::string& error)
{
    if (!normalizePlayerName(name))
    {
        error = "Invalid character name.";
        return false;
    }

    Bot* bot = FindBotByName(name);
    if (!bot)
    {
        error = "'" + name + "' is not a bot.";
        return false;
    }

    bot->removing = true;
    bot->session->LogoutBot();
    return true;
}

void PlayerbotMgr::RemoveAllBots()
{
    for (auto& pair : _bots)
    {
        pair.second.removing = true;
        pair.second.session->LogoutBot();
    }
}

std::vector<PlayerbotMgr::BotInfo> PlayerbotMgr::ListBots() const
{
    std::vector<BotInfo> list;
    for (auto const& pair : _bots)
    {
        Bot const& bot = pair.second;
        BotInfo info;
        info.name = bot.name;
        if (bot.removing)
            info.state = "logging out";
        else if (!bot.inWorld)
            info.state = "logging in";
        else if (Player* player = bot.session->GetPlayer())
            info.state = "in world, map " + std::to_string(player->GetMapId()) + ", level " + std::to_string(player->getLevel());
        else
            info.state = "in world";
        list.push_back(info);
    }
    return list;
}

void PlayerbotMgr::Notify(ObjectGuid const& guid, std::string const& text) const
{
    if (guid.IsEmpty())
        return;

    if (Player* player = ObjectAccessor::FindPlayer(guid))
        ChatHandler(player).SendSysMessage(text.c_str());
}

void PlayerbotMgr::RequestCreate(BotFactory::CreateRequest const& request)
{
    std::lock_guard<std::mutex> lock(_createLock);
    _createRequests.push_back(request);
}

void PlayerbotMgr::ProcessCreates(uint32 diff)
{
    std::vector<BotFactory::CreateRequest> requests;
    {
        std::lock_guard<std::mutex> lock(_createLock);
        requests.swap(_createRequests);
    }

    for (BotFactory::CreateRequest const& request : requests)
    {
        std::string error;
        std::string const name = BotFactory::CreateCharacter(request, _createConfig, error);
        if (name.empty())
        {
            TC_LOG_ERROR("server.loading", "[mod-playerbots] .playerbots create failed: %s", error.c_str());
            Notify(request.creator, "Could not create a bot: " + error);
            continue;
        }

        Notify(request.creator, "Created '" + name + "', logging in...");
        PendingLogin pending;
        pending.name = name;
        pending.request = request;
        pending.delay = 3000;                               // SaveToDB is asynchronous: let the rows land first
        _pendingLogins.push_back(pending);
    }

    for (auto itr = _pendingLogins.begin(); itr != _pendingLogins.end();)
    {
        if (itr->delay > diff)
        {
            itr->delay -= diff;
            ++itr;
            continue;
        }

        std::string error;
        if (!AddBot(itr->name, itr->request.creator, error))
            Notify(itr->request.creator, "Could not log in '" + itr->name + "': " + error);
        else if (Bot* bot = FindBotByName(itr->name))
            bot->init = std::make_shared<BotFactory::CreateRequest>(itr->request);
        itr = _pendingLogins.erase(itr);
    }
}

void PlayerbotMgr::Update(uint32 diff)
{
    ProcessCreates(diff);

    // owner's rule (SkyFire): a player logging out takes the bots of his account with him, so his
    // alts are never left running while he logs into one of them
    std::vector<uint32> accounts;
    {
        std::lock_guard<std::mutex> lock(_logoutLock);
        accounts.swap(_loggedOutAccounts);
    }
    for (uint32 accountId : accounts)
        for (auto& pair : _bots)
            if (!pair.second.removing && pair.second.session->GetAccountId() == accountId)
            {
                pair.second.removing = true;
                pair.second.session->LogoutBot();
                TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' logs out with its account's player.", pair.second.name.c_str());
            }

    for (auto itr = _bots.begin(); itr != _bots.end();)
    {
        Bot& bot = itr->second;
        WorldSession* session = bot.session.get();

        // as World::UpdateSessions: a session without a map is updated here, the map updates it otherwise
        if (!session->GetMap() && !session->Update(diff))
        {
            if (!bot.inWorld)
            {
                TC_LOG_ERROR("server.loading", "[mod-playerbots] Bot '%s' could not be loaded into the world.", bot.name.c_str());
                Notify(bot.notifyGuid, "Could not add bot '" + bot.name + "': failed to load the character into the world.");
            }
            else
                TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' logged out.", bot.name.c_str());

            {
                std::lock_guard<std::mutex> lock(_aiLock);
                _ais.erase(itr->first);
            }
            itr = _bots.erase(itr);
            continue;
        }

        if (!bot.inWorld && !session->PlayerLoading())
            if (Player* player = session->GetPlayer())
                if (player->IsInWorld())
                {
                    bot.inWorld = true;
                    {
                        std::lock_guard<std::mutex> lock(_aiLock);
                        _ais[itr->first] = std::make_shared<PlayerbotAI>(bot.notifyGuid, session->GetAccountId());
                    }
                    TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' logged in (map %u).", bot.name.c_str(), player->GetMapId());
                    // SquidBots Lite (SkyFire) parses this exact text: keep the wording
                    Notify(bot.notifyGuid, "Bot '" + bot.name + "' logged in.");

                    // a created bot: set it up on its own thread
                    if (bot.init)
                    {
                        std::shared_ptr<BotFactory::CreateRequest> init = bot.init;
                        bot.init.reset();
                        std::shared_ptr<PlayerbotAI> ai = GetAI(itr->first);
                        ObjectAccessor::PostToPlayer(itr->first, [ai, init](Player* p) -> void
                        {
                            if (ai)
                                ai->InitializeNewBot(p, *init);
                        });
                    }
                }

        ++itr;
    }
}

std::shared_ptr<PlayerbotAI> PlayerbotMgr::GetAI(ObjectGuid const& bot) const
{
    std::lock_guard<std::mutex> lock(_aiLock);
    auto itr = _ais.find(bot);
    return itr != _ais.end() ? itr->second : nullptr;
}

std::vector<std::pair<ObjectGuid, std::shared_ptr<PlayerbotAI>>> PlayerbotMgr::GetBotsOwnedBy(ObjectGuid const& player, uint32 accountId) const
{
    std::vector<std::pair<ObjectGuid, std::shared_ptr<PlayerbotAI>>> owned;
    std::lock_guard<std::mutex> lock(_aiLock);
    for (auto const& pair : _ais)
        if (pair.first != player && pair.second->IsMaster(player, accountId))
            owned.emplace_back(pair.first, pair.second);
    return owned;
}

void PlayerbotMgr::OnPlayerLogout(uint32 accountId)
{
    std::lock_guard<std::mutex> lock(_logoutLock);
    _loggedOutAccounts.push_back(accountId);
}

void PlayerbotMgr::Shutdown()
{
    // the maps log the characters out when they unload; the sessions go with the last reference
    {
        std::lock_guard<std::mutex> lock(_aiLock);
        _ais.clear();
    }
    _bots.clear();
}

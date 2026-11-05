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

namespace
{
    int32 const OwnerGoneLogoutDelay = 60000;               // bot-account bots of a player who logged out
}

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
    _createConfig.poolAccounts = sConfigMgr->GetIntDefault("Playerbots.Pool.Accounts", 2);
    _createConfig.poolCharacters = sConfigMgr->GetIntDefault("Playerbots.Pool.Characters", 20);
    _createConfig.poolLogins = sConfigMgr->GetIntDefault("Playerbots.Pool.LoginAtStartup", 5);
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
        // owner's rule: a bot already in the database (logged in or not) is used before a new one is made
        if (ReuseBot(request))
            continue;

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

bool PlayerbotMgr::ReuseBot(BotFactory::CreateRequest const& request)
{
    std::vector<uint32> const accounts = BotFactory::BotAccountIds(_createConfig);
    auto onBotAccount = [&accounts](uint32 id) { return std::find(accounts.begin(), accounts.end(), id) != accounts.end(); };
    auto fits = [&request](uint8 race, uint8 cls, uint8 level)
    {
        return cls == request.cls && Player::TeamForRace(race) == request.team && level <= request.level;
    };

    // a logged-in bot of a bot account that nobody leads (the startup pool), the highest level first
    ObjectGuid bestGuid;
    Bot* best = nullptr;
    uint8 bestLevel = 0;
    for (auto& pair : _bots)
    {
        Bot& bot = pair.second;
        if (bot.removing || !bot.inWorld || !bot.notifyGuid.IsEmpty() || bot.init || !onBotAccount(bot.session->GetAccountId()))
            continue;
        CharacterInfo const* info = sWorld->GetCharacterInfo(pair.first);   // no touching the player off its thread
        if (!info || !fits(info->Race, info->Class, info->Level) || (best && info->Level <= bestLevel))
            continue;
        best = &bot;
        bestGuid = pair.first;
        bestLevel = info->Level;
    }

    if (best)
    {
        best->notifyGuid = request.creator;
        best->toGMIsland = false;
        // a new AI: its master (constant, read from any thread by IsMaster) is the creator now
        std::shared_ptr<PlayerbotAI> ai = std::make_shared<PlayerbotAI>(request.creator, best->session->GetAccountId());
        {
            std::lock_guard<std::mutex> lock(_aiLock);
            _ais[bestGuid] = ai;
        }
        std::shared_ptr<BotFactory::CreateRequest> init = std::make_shared<BotFactory::CreateRequest>(request);
        ObjectAccessor::PostToPlayer(bestGuid, [ai, init](Player* p) -> void { ai->InitializeNewBot(p, *init); });
        TC_LOG_INFO("server.loading", "[mod-playerbots] .playerbots create: reusing logged-in bot '%s'.", best->name.c_str());
        Notify(request.creator, "Bot '" + best->name + "' is coming.");
        return true;
    }

    // an offline bot character, the highest level first
    BotFactory::StoredBot found;
    for (BotFactory::StoredBot const& stored : BotFactory::OfflineBots(accounts, request.cls))
        if (fits(stored.race, stored.cls, stored.level) && _bots.find(stored.guid) == _bots.end() &&
            (found.guid.IsEmpty() || stored.level > found.level))
            found = stored;
    if (found.guid.IsEmpty())
        return false;

    std::string error;
    if (!AddBot(found.name, request.creator, error))
    {
        TC_LOG_ERROR("server.loading", "[mod-playerbots] .playerbots create: could not log in '%s': %s", found.name.c_str(), error.c_str());
        return false;
    }
    if (Bot* bot = FindBotByName(found.name))
        bot->init = std::make_shared<BotFactory::CreateRequest>(request);
    TC_LOG_INFO("server.loading", "[mod-playerbots] .playerbots create: logging in stored bot '%s'.", found.name.c_str());
    Notify(request.creator, "Logging in bot '" + found.name + "'...");
    return true;
}

// Startup pool (owner's request, 2026-11-05): Playerbots.Pool.Accounts bot accounts and
// Playerbots.Pool.Characters bot characters exist; Playerbots.Pool.LoginAtStartup of them log in and wait
// on GM Island until ".playerbots create" takes one.
void PlayerbotMgr::UpdatePool(uint32 diff)
{
    if (!_poolMade)
    {
        _poolMade = true;
        BotFactory::EnsurePool(_createConfig);
        if (_createConfig.poolLogins)
            _poolLoginDelay = 5000;                         // new characters' rows must reach the database first
        return;
    }

    if (_poolLoginDelay < 0)
        return;
    _poolLoginDelay -= int32(diff);
    if (_poolLoginDelay > 0)
        return;
    _poolLoginDelay = -1;

    uint32 started = 0;
    for (BotFactory::StoredBot const& stored : BotFactory::OfflineBots(BotFactory::BotAccountIds(_createConfig), 0))
    {
        if (started >= _createConfig.poolLogins)
            break;
        if (_bots.find(stored.guid) != _bots.end())
            continue;
        std::string error;
        if (!AddBot(stored.name, ObjectGuid::Empty, error))
            continue;
        if (Bot* bot = FindBotByName(stored.name))
            bot->toGMIsland = true;
        ++started;
    }
    TC_LOG_INFO("server.loading", "[mod-playerbots] Bot pool: logging in %u bot(s) to GM Island.", started);
}

void PlayerbotMgr::Update(uint32 diff)
{
    if (_enabled)
        UpdatePool(diff);
    ProcessCreates(diff);

    // Owner's rules when a player logs out or disconnects (2026-11-05; the first one from SkyFire):
    // - the bots of his account (alt bots) log out at once, so an alt is never left running while he
    //   logs into it;
    // - the other bots he leads (bot-account bots) log out after OwnerGoneLogoutDelay, unless he is back
    //   in the world by then.
    std::vector<std::pair<uint32, ObjectGuid>> players;
    {
        std::lock_guard<std::mutex> lock(_logoutLock);
        players.swap(_loggedOutPlayers);
    }
    for (auto const& player : players)
        for (auto& pair : _bots)
        {
            Bot& bot = pair.second;
            if (bot.removing)
                continue;
            if (bot.session->GetAccountId() == player.first)
            {
                bot.removing = true;
                bot.session->LogoutBot();
                TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' logs out with its account's player.", bot.name.c_str());
            }
            else if (!player.second.IsEmpty() && bot.notifyGuid == player.second && bot.ownerGoneTimer < 0)
            {
                bot.ownerGoneTimer = OwnerGoneLogoutDelay;
                TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s': its owner logged out, it logs out in %u s unless he returns.",
                    bot.name.c_str(), OwnerGoneLogoutDelay / 1000);
            }
        }

    for (auto& pair : _bots)
    {
        Bot& bot = pair.second;
        if (bot.ownerGoneTimer < 0 || bot.removing)
            continue;
        if (ObjectAccessor::FindPlayer(bot.notifyGuid))     // back in the world: only an existence check here
        {
            bot.ownerGoneTimer = -1;
            continue;
        }
        bot.ownerGoneTimer -= int32(diff);
        if (bot.ownerGoneTimer <= 0)
        {
            bot.ownerGoneTimer = -1;
            bot.removing = true;
            bot.session->LogoutBot();
            TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' logs out: its owner did not come back.", bot.name.c_str());
        }
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

                    if (bot.toGMIsland)
                    {
                        bot.toGMIsland = false;
                        ObjectAccessor::PostToPlayer(itr->first, [](Player* p) -> void
                        {
                            p->TeleportTo(1, 16222.1f, 16252.1f, 12.5872f, 1.35f);  // GM Island
                        });
                    }

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

void PlayerbotMgr::OnPlayerLogout(uint32 accountId, ObjectGuid const& player)
{
    std::lock_guard<std::mutex> lock(_logoutLock);
    _loggedOutPlayers.emplace_back(accountId, player);
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

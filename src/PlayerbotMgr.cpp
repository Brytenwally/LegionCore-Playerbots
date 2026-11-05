/*
 * mod-playerbots: bot lifecycle. See PlayerbotMgr.h.
 */

#include "PlayerbotMgr.h"
#include "PlayerbotAI.h"
#include "AccountMgr.h"
#include "Chat.h"
#include "Config.h"
#include "DB2Stores.h"
#include "GlobalFunctional.h"
#include "Group.h"
#include "LFGMgr.h"
#include "Timer.h"
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
    _selfGMOnly = sConfigMgr->GetBoolDefault("Playerbots.Self.GMOnly", true);
    _useApl = sConfigMgr->GetBoolDefault("Playerbots.Rotation.SimC", true);
    _aplDirectory = sConfigMgr->GetStringDefault("Playerbots.Rotation.Directory", "playerbots_apl");
    _loot.randomBotsRoll = sConfigMgr->GetBoolDefault("Playerbots.Loot.RandomBotsRoll", true);
    _defaultCo = sConfigMgr->GetStringDefault("Playerbots.Strategies.Combat", "tank,defensives,potions");
    _defaultNc = sConfigMgr->GetStringDefault("Playerbots.Strategies.NonCombat", "follow,loot,rest,roll,autoequip,talents");
    _strategy.lootMinQuality = std::min<uint32>(sConfigMgr->GetIntDefault("Playerbots.Loot.MinQuality", 0), 7);
    _strategy.lootRange = std::max(5.0f, sConfigMgr->GetFloatDefault("Playerbots.Loot.Range", 30.0f));
    _strategy.aggressiveRange = std::max(5.0f, sConfigMgr->GetFloatDefault("Playerbots.Aggressive.Range", 25.0f));
    _strategy.aggressiveLevelsAbove = sConfigMgr->GetIntDefault("Playerbots.Aggressive.MaxLevelAbove", 6);
    _loot.needUpgradePct = sConfigMgr->GetFloatDefault("Playerbots.Loot.NeedUpgradePct", 5.0f);
    _loot.autoEquip = sConfigMgr->GetBoolDefault("Playerbots.AutoEquip.Enable", true);
    _loot.autoEquipAlts = sConfigMgr->GetBoolDefault("Playerbots.AutoEquip.Alts", false);
    _loot.equipUpgradePct = sConfigMgr->GetFloatDefault("Playerbots.AutoEquip.UpgradePct", 5.0f);
    {
        std::string formation = sConfigMgr->GetStringDefault("Playerbots.Formation.Default", "role");
        std::transform(formation.begin(), formation.end(), formation.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (!BotFormation::ShapeFromName(formation, _defaultFormation))
        {
            TC_LOG_ERROR("server.loading", "[mod-playerbots] Playerbots.Formation.Default: unknown formation '%s', using role.", formation.c_str());
            _defaultFormation = BotFormation::Shape::Role;
        }
    }
    _lfgFillEnabled = sConfigMgr->GetBoolDefault("Playerbots.LFG.Fill", true);
    _lfgFillDelay = sConfigMgr->GetIntDefault("Playerbots.LFG.FillDelay", 30000);
    _lfgJoinInterval = std::max(500, sConfigMgr->GetIntDefault("Playerbots.LFG.JoinInterval", 2000));
    _survival.defensives = sConfigMgr->GetBoolDefault("Playerbots.Combat.Defensives", true);
    _survival.potionHealthPct = sConfigMgr->GetFloatDefault("Playerbots.Combat.PotionHealthPct", 30.0f);
    _survival.potionManaPct = sConfigMgr->GetFloatDefault("Playerbots.Combat.PotionManaPct", 20.0f);
    _survival.potionsOnAlts = sConfigMgr->GetBoolDefault("Playerbots.Combat.PotionsOnAlts", false);
    _survival.rest = sConfigMgr->GetBoolDefault("Playerbots.Rest.Enable", true);
    _survival.restHealthPct = sConfigMgr->GetFloatDefault("Playerbots.Rest.HealthPct", 60.0f);
    _survival.restManaPct = sConfigMgr->GetFloatDefault("Playerbots.Rest.ManaPct", 50.0f);

    _gearOptions.minQuality = std::min<uint32>(sConfigMgr->GetIntDefault("Playerbots.AutoGear.MinQuality", 0), 5);
    _gearOptions.maxQuality = std::min<uint32>(sConfigMgr->GetIntDefault("Playerbots.AutoGear.MaxQuality", 4), 5);
    _gearOptions.maxItemLevel = sConfigMgr->GetIntDefault("Playerbots.AutoGear.MaxItemLevel", 0);
    _gearOptions.choices = std::max(1, sConfigMgr->GetIntDefault("Playerbots.AutoGear.Choices", 5));
    _gearOptions.bagEntry = sConfigMgr->GetIntDefault("Playerbots.AutoGear.BagEntry", 0);
    _gearOptions.belowSixtyAdd = sConfigMgr->GetFloatDefault("Playerbots.AutoGear.IlvlCap.Below60Add", 6.0f);
    {
        BotGear::Options const defaults;
        for (uint32 i = 0; i < 10; ++i)
        {
            std::string const key = "Playerbots.AutoGear.IlvlCap." + std::to_string(60 + i * 5);
            _gearOptions.brackets[i] = uint32(std::max(0, sConfigMgr->GetIntDefault(key, int32(defaults.brackets[i]))));
        }
    }

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

    LogoutAndReset(*bot);
    return true;
}

// A random bot is reset first (level, inventory except bags) so it returns to the pool clean; then logged out.
void PlayerbotMgr::LogoutAndReset(Bot& bot)
{
    bot.removing = true;
    std::shared_ptr<WorldSession> session = bot.session;
    Player* player = session->GetPlayer();
    if (!bot.inWorld || !player || !IsBotAccountName(session->GetAccountName()))
    {
        session->LogoutBot();
        return;
    }
    if (!ObjectAccessor::PostToPlayer(player->GetGUID(), [session](Player* p) -> void
    {
        BotFactory::ResetBot(p, false);
        session->LogoutBot();
    }))
        session->LogoutBot();
}

void PlayerbotMgr::RemoveAllBots()
{
    for (auto& pair : _bots)
        if (!pair.second.removing)
            LogoutAndReset(pair.second);
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

void PlayerbotMgr::RequestAssign(ObjectGuid const& bot, BotFactory::CreateRequest const& request)
{
    std::lock_guard<std::mutex> lock(_createLock);
    _assignRequests.emplace_back(bot, request);
}

bool PlayerbotMgr::IsBotAccountName(std::string const& accountName) const
{
    std::string prefix = _createConfig.accountPrefix;
    std::transform(prefix.begin(), prefix.end(), prefix.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    std::string name = accountName;
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    return !prefix.empty() && name.rfind(prefix, 0) == 0;
}

void PlayerbotMgr::ProcessCreates(uint32 diff)
{
    std::vector<BotFactory::CreateRequest> requests;
    std::vector<std::pair<ObjectGuid, BotFactory::CreateRequest>> assigns;
    {
        std::lock_guard<std::mutex> lock(_createLock);
        requests.swap(_createRequests);
        assigns.swap(_assignRequests);
    }

    // a masterless random bot accepted a player's invite: he becomes its master
    for (auto const& assign : assigns)
    {
        auto itr = _bots.find(assign.first);
        if (itr == _bots.end() || itr->second.removing || !itr->second.notifyGuid.IsEmpty())
            continue;
        Bot& bot = itr->second;
        BotFactory::CreateRequest const& request = assign.second;
        bot.notifyGuid = request.creator;
        bot.toGMIsland = false;
        bot.ownerGoneTimer = -1;
        std::shared_ptr<PlayerbotAI> ai = std::make_shared<PlayerbotAI>(request.creator, bot.session->GetAccountId(), true);
        {
            std::lock_guard<std::mutex> lock(_aiLock);
            _ais[assign.first] = ai;
        }
        std::shared_ptr<BotFactory::CreateRequest> init = std::make_shared<BotFactory::CreateRequest>(request);
        ObjectAccessor::PostToPlayer(assign.first, [ai, init](Player* p) -> void { ai->InitializeNewBot(p, *init); });
        TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' accepted an invite: it has a new master.", bot.name.c_str());
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
        std::shared_ptr<PlayerbotAI> ai = std::make_shared<PlayerbotAI>(request.creator, best->session->GetAccountId(), true);
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
// on GM Island until ".playerbots create" or a player's invite takes one. Built in small steps per world
// update (see PlayerbotMgr.h).
void PlayerbotMgr::UpdatePool(uint32 diff)
{
    uint32 const AccountChecksPerUpdate = 20;
    uint32 const CharactersPerUpdate = 2;
    uint32 const LoginsPerUpdate = 5;

    switch (_poolStage)
    {
        case PoolStage::Start:
            _poolStage = PoolStage::Accounts;
            TC_LOG_INFO("server.loading", "[mod-playerbots] Bot pool: %u account(s), %u character(s), %u login(s) at startup.",
                _createConfig.poolAccounts, _createConfig.poolCharacters, _createConfig.poolLogins);
            break;

        case PoolStage::Accounts:
            // existing accounts are cheap to check; at most one new account (password hash) per update
            for (uint32 checks = 0; checks < AccountChecksPerUpdate && _poolNextAccount <= _createConfig.poolAccounts; ++checks)
            {
                uint32 id = 0;
                std::string name;
                bool const made = BotFactory::EnsureAccount(_createConfig, _poolNextAccount++, id, name);
                if (made)
                    break;
            }
            if (_poolNextAccount > _createConfig.poolAccounts)
            {
                std::vector<uint32> const ids = BotFactory::BotAccountIds(_createConfig);
                _poolCounts = BotFactory::CharacterCounts(ids);
                _poolTotal = 0;
                for (auto const& pair : _poolCounts)
                {
                    _poolTotal += pair.second;
                    std::string name;
                    if (AccountMgr::GetName(pair.first, name))
                        _poolNames[pair.first] = name;
                }
                _poolStage = PoolStage::Characters;
            }
            break;

        case PoolStage::Characters:
            for (uint32 made = 0; made < CharactersPerUpdate; ++made)
            {
                if (_poolTotal >= _createConfig.poolCharacters)
                {
                    TC_LOG_INFO("server.loading", "[mod-playerbots] Bot pool: %u character(s) made, %u in all.", _poolMade, _poolTotal);
                    _poolStage = PoolStage::WaitLogins;
                    _poolTimer = 5000;                      // new characters' rows must reach the database first
                    break;
                }

                // the first bot account with room; a new one (next update) when all are full
                uint32 accountId = 0;
                for (auto const& pair : _poolCounts)
                    if (pair.second < _createConfig.charactersPerAccount && _poolNames.count(pair.first))
                    {
                        accountId = pair.first;
                        break;
                    }
                if (!accountId)
                {
                    uint32 id = 0;
                    std::string name;
                    BotFactory::EnsureAccount(_createConfig, _poolNextAccount++, id, name);
                    if (id && !_poolCounts.count(id))
                    {
                        _poolCounts[id] = 0;
                        _poolNames[id] = name;
                    }
                    break;
                }

                static uint8 const classes[] = { CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER, CLASS_ROGUE, CLASS_PRIEST, CLASS_SHAMAN,
                    CLASS_MAGE, CLASS_WARLOCK, CLASS_MONK, CLASS_DRUID, CLASS_DEATH_KNIGHT, CLASS_DEMON_HUNTER };
                BotFactory::CreateRequest request;
                request.cls = classes[_poolTotal % std::size(classes)];
                request.team = (_poolTotal / std::size(classes)) % 2 ? HORDE : ALLIANCE;
                std::string error;
                if (BotFactory::CreateCharacter(request, _createConfig, error, accountId, _poolNames[accountId]).empty())
                {
                    TC_LOG_ERROR("server.loading", "[mod-playerbots] Bot pool: %s Stopping at %u character(s).", error.c_str(), _poolTotal);
                    _poolStage = PoolStage::WaitLogins;
                    _poolTimer = 5000;
                    break;
                }
                ++_poolCounts[accountId];
                ++_poolTotal;
                ++_poolMade;
            }
            break;

        case PoolStage::WaitLogins:
            if (!_createConfig.poolLogins)
            {
                _poolStage = PoolStage::Done;
                break;
            }
            _poolTimer -= int32(diff);
            if (_poolTimer > 0)
                break;
            _poolToLogin = BotFactory::OfflineBots(BotFactory::BotAccountIds(_createConfig), 0);
            _poolLoginIndex = 0;
            _poolStage = PoolStage::Logins;
            break;

        case PoolStage::Logins:
            for (uint32 started = 0; started < LoginsPerUpdate; ++started)
            {
                if (_poolLoggedIn >= _createConfig.poolLogins || _poolLoginIndex >= _poolToLogin.size())
                {
                    TC_LOG_INFO("server.loading", "[mod-playerbots] Bot pool: %u bot(s) logging in to GM Island.", _poolLoggedIn);
                    _poolToLogin.clear();
                    _poolStage = PoolStage::Done;
                    break;
                }
                BotFactory::StoredBot const& stored = _poolToLogin[_poolLoginIndex++];
                if (_bots.find(stored.guid) != _bots.end())
                    continue;
                std::string error;
                if (!AddBot(stored.name, ObjectGuid::Empty, error))
                    continue;
                if (Bot* bot = FindBotByName(stored.name))
                    bot->toGMIsland = true;
                ++_poolLoggedIn;
            }
            break;

        case PoolStage::Done:
            break;
    }
}

void PlayerbotMgr::Update(uint32 diff)
{
    if (_enabled)
        UpdatePool(diff);
    ProcessCreates(diff);
    UpdateLfgFill(diff);

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
                TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s': its owner logged out: dismissed (random bot) or logged out (alt) in %u s unless he returns.",
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
            // owner's rule: a random bot is dismissed (reset, back to GM Island) instead of logged out
            if (IsBotAccountName(bot.session->GetAccountName()))
            {
                if (std::shared_ptr<PlayerbotAI> ai = GetAI(pair.first))
                {
                    ObjectAccessor::PostToPlayer(pair.first, [ai](Player* p) -> void { ai->Dismiss(p, "its owner did not come back"); });
                    continue;
                }
            }
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
                        _ais[itr->first] = std::make_shared<PlayerbotAI>(bot.notifyGuid, session->GetAccountId(),
                            IsBotAccountName(session->GetAccountName()));
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

bool PlayerbotMgr::IsBot(ObjectGuid const& guid) const
{
    std::lock_guard<std::mutex> lock(_aiLock);
    return _ais.find(guid) != _ais.end();
}

std::shared_ptr<PlayerbotAI> PlayerbotMgr::GetAI(ObjectGuid const& bot) const
{
    std::lock_guard<std::mutex> lock(_aiLock);
    auto itr = _ais.find(bot);
    if (itr != _ais.end())
        return itr->second;
    itr = _selfAis.find(bot);
    return itr != _selfAis.end() ? itr->second : nullptr;
}

std::vector<std::pair<ObjectGuid, std::shared_ptr<PlayerbotAI>>> PlayerbotMgr::GetBotsOwnedBy(ObjectGuid const& player, uint32 accountId) const
{
    std::vector<std::pair<ObjectGuid, std::shared_ptr<PlayerbotAI>>> owned;
    std::lock_guard<std::mutex> lock(_aiLock);
    for (auto const& pair : _ais)
        if (pair.first != player && pair.second->IsMaster(player, accountId))
            owned.emplace_back(pair.first, pair.second);
    // his own character when it is a self-bot: party chat orders reach it too
    auto self = _selfAis.find(player);
    if (self != _selfAis.end())
        owned.emplace_back(self->first, self->second);
    return owned;
}

void PlayerbotMgr::OnPlayerLogout(uint32 accountId, ObjectGuid const& player)
{
    {
        std::lock_guard<std::mutex> lock(_aiLock);
        if (_selfAis.erase(player))
            --_selfCount;
    }
    {
        std::lock_guard<std::mutex> lock(_lfgLock);
        _lfgCheckTimers.erase(player);
    }
    std::lock_guard<std::mutex> lock(_logoutLock);
    _loggedOutPlayers.emplace_back(accountId, player);
}

// ---- Self-bot ------------------------------------------------------------------------------------------

bool PlayerbotMgr::ToggleSelfBot(Player* player, std::string& reply)
{
    ObjectGuid const guid = player->GetGUID();
    std::shared_ptr<PlayerbotAI> ai;
    {
        std::lock_guard<std::mutex> lock(_aiLock);
        auto itr = _selfAis.find(guid);
        if (itr != _selfAis.end())
        {
            ai = itr->second;
            _selfAis.erase(itr);
            --_selfCount;
        }
    }
    if (ai)
    {
        ai->Detach(player);
        reply = "Self-bot off: you control your character again.";
        TC_LOG_INFO("server.loading", "[mod-playerbots] %s: self-bot off.", player->GetName());
        return true;
    }

    WorldSession* session = player->GetSession();
    if (!session || session->IsBot())
    {
        reply = "Only a player's own character can be a self-bot.";
        return false;
    }

    ai = std::make_shared<PlayerbotAI>(guid, session->GetAccountId(), false);
    ai->SetSelfBot();
    {
        std::lock_guard<std::mutex> lock(_aiLock);
        _selfAis[guid] = ai;
        ++_selfCount;
    }
    reply = "Self-bot on: your character fights your target and whatever attacks the group with its spec's rotation, "
        "follows the group's leader and takes the bot orders (whisper yourself or use party chat). "
        "'.playerbots self' again to stop.";
    TC_LOG_INFO("server.loading", "[mod-playerbots] %s: self-bot on.", player->GetName());
    return true;
}

void PlayerbotMgr::OnClientUpdate(Player* player, uint32 diff)
{
    ObjectGuid const guid = player->GetGUID();
    if (_selfCount)
    {
        std::shared_ptr<PlayerbotAI> ai;
        {
            std::lock_guard<std::mutex> lock(_aiLock);
            auto itr = _selfAis.find(guid);
            if (itr != _selfAis.end())
                ai = itr->second;
        }
        if (ai)
            ai->Update(player, diff);
    }

    if (!_enabled || !_lfgFillEnabled)
        return;

    {
        std::lock_guard<std::mutex> lock(_lfgLock);
        uint32& timer = _lfgCheckTimers[guid];
        timer += diff;
        if (timer < 2000)
            return;
        timer = 0;
    }

    if (!sLFGMgr->HasQueue(guid))
        return;

    // a party's queue is its leader's; a dungeon finder group queueing again for a replacement (a member kicked
    // or gone; its leader may be a bot) is reported by its players, one fill per group (owner, build #26)
    Group* group = player->GetGroup();
    if (group && !group->isLFGGroup() && group->GetLeaderGUID() != guid)
        return;

    std::vector<ObjectGuid> members;
    if (group)
    {
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
            members.push_back(slot.Guid);
    }
    else
        members.push_back(guid);

    std::vector<QueueReport> reports;
    for (uint32 queueId : sLFGMgr->GetQueueIds(guid))
    {
        lfg::LfgState const state = sLFGMgr->GetState(guid, queueId);
        if (state != lfg::LFG_STATE_QUEUED && state != lfg::LFG_STATE_ROLECHECK && state != lfg::LFG_STATE_PROPOSAL)
            continue;

        QueueReport report;
        report.queuer = guid;
        report.key = group ? group->GetGUID() : guid;
        report.queueId = queueId;
        report.level = player->getLevel();
        report.team = player->GetTeam();
        report.queued = state == lfg::LFG_STATE_QUEUED;
        report.dungeons = sLFGMgr->GetSelectedDungeons(guid, queueId);
        // a dungeon finder group's replacement queue does not set the selection again: its queue's own dungeon
        if (report.dungeons.empty() && group && group->isLFGGroup())
            report.dungeons.insert(queueId);
        // each member counts once, for the scarcest role he offers
        for (ObjectGuid const& member : members)
        {
            uint8 const roles = sLFGMgr->GetRoles(member, queueId);
            if (roles & lfg::PLAYER_ROLE_TANK)
                ++report.tanks;
            else if (roles & lfg::PLAYER_ROLE_HEALER)
                ++report.healers;
            else
                ++report.dps;
        }
        if (!report.dungeons.empty())
            reports.push_back(report);
    }

    if (reports.empty())
        return;
    std::lock_guard<std::mutex> lock(_lfgLock);
    _queueReports.insert(_queueReports.end(), reports.begin(), reports.end());
}

// ---- Dungeon finder fill -------------------------------------------------------------------------------

void PlayerbotMgr::RequestRelease(ObjectGuid const& bot)
{
    std::lock_guard<std::mutex> lock(_lfgLock);
    _releaseRequests.push_back(bot);
}

void PlayerbotMgr::ReleaseBot(ObjectGuid const& guid)
{
    auto itr = _bots.find(guid);
    if (itr == _bots.end() || itr->second.removing)
        return;
    Bot& bot = itr->second;
    bot.notifyGuid.Clear();
    bot.ownerGoneTimer = -1;
    bot.restUntil = getMSTime() + 300000;               // 5 min before it fills a dungeon queue again
    std::shared_ptr<PlayerbotAI> ai = std::make_shared<PlayerbotAI>(ObjectGuid::Empty, bot.session->GetAccountId(), true);
    {
        std::lock_guard<std::mutex> lock(_aiLock);
        _ais[guid] = ai;
    }
    TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' is free again.", bot.name.c_str());
}

void PlayerbotMgr::UpdateLfgFill(uint32 diff)
{
    _lfgTimer += diff;
    if (_lfgTimer < 1000)
        return;
    _lfgTimer = 0;

    std::vector<QueueReport> reports;
    std::vector<ObjectGuid> releases;
    {
        std::lock_guard<std::mutex> lock(_lfgLock);
        reports.swap(_queueReports);
        releases.swap(_releaseRequests);
    }
    for (ObjectGuid const& guid : releases)
        ReleaseBot(guid);

    uint32 const now = getMSTime();
    for (QueueReport const& report : reports)
    {
        auto const fillKey = std::make_pair(report.key, report.queueId);
        if (_lfgFills.count(fillKey) && _lfgFills[fillKey].report.queuer != report.queuer &&
            getMSTimeDiff(_lfgFills[fillKey].lastSeen, now) < 5000)
        {
            _lfgFills[fillKey].lastSeen = now;              // another player of the same group: one fill per group
            continue;
        }
        LfgFill& fill = _lfgFills[fillKey];
        if (!fill.firstSeen)
        {
            fill.firstSeen = now;
            TC_LOG_INFO("server.loading", "[mod-playerbots] Dungeon finder: %s queued (%s); bots fill missing roles in %u s.",
                report.queuer.ToString().c_str(), lfg::ConcatenateDungeons(report.dungeons).c_str(), _lfgFillDelay / 1000);
        }
        fill.report = report;
        fill.lastSeen = now;
    }

    // a fill bot leaves the queue and is freed, unless it is grouped already (a dungeon was found)
    auto abandon = [](ObjectGuid const& guid)
    {
        std::shared_ptr<PlayerbotAI> ai = sPlayerbotMgr->GetAI(guid);
        if (!ai)
            return;
        ObjectAccessor::PostToPlayer(guid, [ai](Player* bot) -> void
        {
            if (ai->AbandonLfg(bot))
                ai->Dismiss(bot, "its queuer left the queue");
        });
    };

    for (auto itr = _lfgFills.begin(); itr != _lfgFills.end();)
    {
        LfgFill& fill = itr->second;
        uint32 const queueId = itr->first.second;

        // no report for a while: he left the queue, logged out, or the group was made (his state is no longer
        // queued / role check / proposal)
        if (getMSTimeDiff(fill.lastSeen, now) > 5000)
        {
            for (LfgFill::FillBot const& bot : fill.bots)
                abandon(bot.guid);
            itr = _lfgFills.erase(itr);
            continue;
        }

        // bots no longer in that queue (matched with other players, or the join failed) leave the fill
        for (auto bot = fill.bots.begin(); bot != fill.bots.end();)
        {
            lfg::LfgState const state = sLFGMgr->GetState(bot->guid, queueId);
            bool const inQueue = state == lfg::LFG_STATE_QUEUED || state == lfg::LFG_STATE_ROLECHECK || state == lfg::LFG_STATE_PROPOSAL;
            if (inQueue || getMSTimeDiff(bot->started, now) < 10000)
            {
                ++bot;
                continue;
            }
            if (state != lfg::LFG_STATE_DUNGEON)
                abandon(bot->guid);
            bot = fill.bots.erase(bot);
        }

        if (!fill.report.queued || getMSTimeDiff(fill.firstSeen, now) < _lfgFillDelay || int32(fill.nextJoin - now) > 0)
        {
            ++itr;
            continue;
        }

        // 5-man dungeons (random or specific); the role counts come from the dungeon's entry
        lfg::LFGDungeonData const* dungeon = sLFGMgr->GetLFGDungeon(*fill.report.dungeons.begin() & 0xFFFFF);
        if (!dungeon || !dungeon->dbc || dungeon->dbc->Substruct != LFG_QUEUE_DUNGEON)
        {
            ++itr;
            continue;
        }
        int32 tanks = dungeon->dbc->CountTank;
        int32 healers = dungeon->dbc->CountHealer;
        int32 dps = dungeon->dbc->CountDamage;
        if (!tanks && !healers && !dps)
        {
            tanks = 1;
            healers = 1;
            dps = 3;
        }
        tanks -= fill.report.tanks;
        healers -= fill.report.healers;
        dps -= fill.report.dps;
        for (LfgFill::FillBot const& bot : fill.bots)
        {
            if (bot.role & lfg::PLAYER_ROLE_TANK)
                --tanks;
            else if (bot.role & lfg::PLAYER_ROLE_HEALER)
                --healers;
            else
                --dps;
        }

        // one bot per JoinInterval, the scarce roles first
        for (uint8 role : { uint8(lfg::PLAYER_ROLE_TANK), uint8(lfg::PLAYER_ROLE_HEALER), uint8(lfg::PLAYER_ROLE_DAMAGE) })
        {
            int32 const need = role == lfg::PLAYER_ROLE_TANK ? tanks : role == lfg::PLAYER_ROLE_HEALER ? healers : dps;
            if (need <= 0)
                continue;
            if (StartLfgBot(fill, role))
            {
                fill.nextJoin = now + _lfgJoinInterval;
                break;
            }
        }
        ++itr;
    }
}

bool PlayerbotMgr::StartLfgBot(LfgFill& fill, uint8 role)
{
    uint8 const specRole = role == lfg::PLAYER_ROLE_TANK ? ROLES_TANK : role == lfg::PLAYER_ROLE_HEALER ? ROLES_HEALER : ROLES_DPS;
    auto specsFor = [specRole](uint8 cls)
    {
        std::vector<uint32> specs;
        for (uint32 spec : BotFactory::SpecsOfClass(cls))
            if (ChrSpecializationEntry const* entry = sChrSpecializationStore.LookupEntry(spec))
                if (entry->Role == specRole)
                    specs.push_back(spec);
        return specs;
    };

    std::vector<uint32> const accounts = BotFactory::BotAccountIds(_createConfig);
    auto onBotAccount = [&accounts](uint32 id) { return std::find(accounts.begin(), accounts.end(), id) != accounts.end(); };

    // an idle logged-in pool bot of his faction, at most his level, whose class has a spec of that role;
    // the highest level first
    ObjectGuid bestGuid;
    Bot* best = nullptr;
    CharacterInfo const* bestInfo = nullptr;
    for (auto& pair : _bots)
    {
        Bot& bot = pair.second;
        if (bot.removing || !bot.inWorld || !bot.notifyGuid.IsEmpty() || bot.init || !onBotAccount(bot.session->GetAccountId()))
            continue;
        // a bot just released (e.g. kicked from this very group) is not sent back at once
        if (bot.restUntil && int32(bot.restUntil - getMSTime()) > 0)
            continue;
        CharacterInfo const* info = sWorld->GetCharacterInfo(pair.first);
        if (!info || Player::TeamForRace(info->Race) != fill.report.team || info->Level > fill.report.level ||
            (bestInfo && info->Level <= bestInfo->Level) || specsFor(info->Class).empty())
            continue;
        best = &bot;
        bestGuid = pair.first;
        bestInfo = info;
    }

    if (!best)
    {
        if (!(fill.warned & role))
        {
            fill.warned |= role;
            TC_LOG_INFO("server.loading", "[mod-playerbots] Dungeon finder: no idle pool bot can fill a %s for %s.",
                role == lfg::PLAYER_ROLE_TANK ? "tank" : role == lfg::PLAYER_ROLE_HEALER ? "healer" : "dps",
                fill.report.queuer.ToString().c_str());
        }
        return false;
    }

    std::vector<uint32> const specs = specsFor(bestInfo->Class);
    BotFactory::CreateRequest request;
    request.cls = bestInfo->Class;
    request.specId = specs[urand(0, specs.size() - 1)];
    request.level = fill.report.level;
    request.team = fill.report.team;
    request.creator = fill.report.queuer;
    request.teleport = false;                               // the dungeon finder takes it to the dungeon

    best->notifyGuid = fill.report.queuer;
    best->toGMIsland = false;
    best->ownerGoneTimer = -1;
    std::shared_ptr<PlayerbotAI> ai = std::make_shared<PlayerbotAI>(fill.report.queuer, best->session->GetAccountId(), true);
    {
        std::lock_guard<std::mutex> lock(_aiLock);
        _ais[bestGuid] = ai;
    }
    lfg::LfgDungeonSet const dungeons = fill.report.dungeons;
    ObjectAccessor::PostToPlayer(bestGuid, [ai, request, dungeons, role](Player* p) -> void
    {
        ai->JoinLfgFor(p, request, dungeons, role);
    });

    LfgFill::FillBot fillBot;
    fillBot.guid = bestGuid;
    fillBot.role = role;
    fillBot.started = getMSTime();
    fill.bots.push_back(fillBot);
    TC_LOG_INFO("server.loading", "[mod-playerbots] Dungeon finder: bot '%s' fills a %s for %s.", best->name.c_str(),
        role == lfg::PLAYER_ROLE_TANK ? "tank" : role == lfg::PLAYER_ROLE_HEALER ? "healer" : "dps",
        fill.report.queuer.ToString().c_str());
    return true;
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

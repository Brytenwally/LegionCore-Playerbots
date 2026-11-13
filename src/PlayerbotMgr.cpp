/*
 * mod-playerbots: bot lifecycle. See PlayerbotMgr.h.
 */

#include "PlayerbotMgr.h"
#include "PlayerbotAI.h"
#include "AccountMgr.h"
#include "BattlegroundMgr.h"
#include "BattlegroundQueue.h"
#include "BotBattleground.h"
#include "Chat.h"
#include "Config.h"
#include "DB2Stores.h"
#include "GlobalFunctional.h"
#include "Group.h"
#include "GroupMgr.h"
#include "LFGMgr.h"
#include "Timer.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "World.h"
#include "WorldSession.h"
#include <algorithm>
#include <set>

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
    _deleteBotAccounts = sConfigMgr->GetBoolDefault("Playerbots.DeleteBotAccounts", false);
    _aplDirectory = sConfigMgr->GetStringDefault("Playerbots.Rotation.Directory", "playerbots_apl");
    _loot.randomBotsRoll = sConfigMgr->GetBoolDefault("Playerbots.Loot.RandomBotsRoll", true);
    _defaultCo = sConfigMgr->GetStringDefault("Playerbots.Strategies.Combat", "tank,defensives,potions");
    _defaultNc = sConfigMgr->GetStringDefault("Playerbots.Strategies.NonCombat", "follow,loot,rest,roll,autoequip,talents");
    _avoidAoe.maxRadius = std::max(1.0f, sConfigMgr->GetFloatDefault("Playerbots.AvoidAoe.MaxRadius", 15.0f));
    _avoidAoe.maxStep = std::max(2.0f, sConfigMgr->GetFloatDefault("Playerbots.AvoidAoe.MaxStep", 8.0f));
    _avoidAoeTell = sConfigMgr->GetBoolDefault("Playerbots.AvoidAoe.Tell", false);
    _avoidAoe.whitelist.clear();
    {
        // mod-playerbots' AiPlayerbot.AoeAvoidSpellWhitelist: comma separated spell ids
        std::string const list = sConfigMgr->GetStringDefault("Playerbots.AvoidAoe.Whitelist", "");
        std::string number;
        for (char c : list + ",")
        {
            if (c >= '0' && c <= '9')
                number += c;
            else if (!number.empty())
            {
                _avoidAoe.whitelist.insert(uint32(std::stoul(number)));
                number.clear();
            }
        }
    }
    _strategy.lootMinQuality = std::min<uint32>(sConfigMgr->GetIntDefault("Playerbots.Loot.MinQuality", 0), 7);
    _strategy.lootRange = std::max(5.0f, sConfigMgr->GetFloatDefault("Playerbots.Loot.Range", 30.0f));
    _strategy.aggressiveRange = std::max(5.0f, sConfigMgr->GetFloatDefault("Playerbots.Aggressive.Range", 25.0f));
    _strategy.aggressiveLevelsAbove = sConfigMgr->GetIntDefault("Playerbots.Aggressive.MaxLevelAbove", 6);
    _loot.needUpgradePct = sConfigMgr->GetFloatDefault("Playerbots.Loot.NeedUpgradePct", 5.0f);
    _loot.autoEquip = sConfigMgr->GetBoolDefault("Playerbots.AutoEquip.Enable", true);
    _loot.autoEquipAlts = sConfigMgr->GetBoolDefault("Playerbots.AutoEquip.Alts", false);
    _loot.equipUpgradePct = sConfigMgr->GetFloatDefault("Playerbots.AutoEquip.UpgradePct", 5.0f);
    _loot.autoSellGray = sConfigMgr->GetBoolDefault("Playerbots.Sell.AutoGray", true);
    _loot.autoSellGrayAlts = sConfigMgr->GetBoolDefault("Playerbots.Sell.AutoGrayAlts", false);
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
    _bgFillEnabled = sConfigMgr->GetBoolDefault("Playerbots.BG.Fill", true);
    _bgFillDelay = sConfigMgr->GetIntDefault("Playerbots.BG.FillDelay", 30000);
    _bgJoinInterval = std::max(250, sConfigMgr->GetIntDefault("Playerbots.BG.JoinInterval", 1000));
    _bgBotsPerTeam = uint32(std::max(0, sConfigMgr->GetIntDefault("Playerbots.BG.BotsPerTeam", 0)));
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
    _createConfig.alliedRaceLevel = uint8(std::min(110, std::max(1, sConfigMgr->GetIntDefault("Playerbots.Create.AlliedRaceLevel", 10))));
    BotFactory::SetAlliedRaceLevel(_createConfig.alliedRaceLevel);
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

    // a logged-in bot of a bot account that nobody leads (the startup pool), picked at random (owner, build #31: by
    // highest level, the level 10 allied races always won over the level 1 pool bots)
    ObjectGuid bestGuid;
    Bot* best = nullptr;
    uint32 seen = 0;
    for (auto& pair : _bots)
    {
        Bot& bot = pair.second;
        if (bot.removing || !bot.inWorld || !bot.notifyGuid.IsEmpty() || bot.init || !onBotAccount(bot.session->GetAccountId()))
            continue;
        CharacterInfo const* info = sWorld->GetCharacterInfo(pair.first);   // no touching the player off its thread
        if (!info || !fits(info->Race, info->Class, info->Level) || urand(0, seen++) != 0)
            continue;
        best = &bot;
        bestGuid = pair.first;
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
            BotFactory::FlagBotAccounts(_createConfig);     // allied races (also for accounts made below)
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
                request.pool = true;
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

void PlayerbotMgr::DeleteBotAccountsAtStartup()
{
    if (!_enabled || !_deleteBotAccounts)
        return;

    // SkyFire's guards: never let a short or generic prefix match real accounts
    std::string prefix;
    for (char c : _createConfig.accountPrefix)
        if (std::isalnum(static_cast<unsigned char>(c)))
            prefix += char(std::toupper(static_cast<unsigned char>(c)));
    if (prefix.size() < 3 || prefix == "ADMIN" || prefix == "GM" || prefix == "PLAYER" || prefix == "ACCOUNT" || prefix == "TEST")
    {
        TC_LOG_ERROR("server.loading", "[mod-playerbots] Playerbots.DeleteBotAccounts: account prefix '%s' is too short or looks unsafe; "
            "nothing deleted.", _createConfig.accountPrefix.c_str());
        return;
    }

    uint32 accounts = 0;
    uint32 characters = 0;
    for (uint32 accountId : BotFactory::BotAccountIds(_createConfig))
    {
        // the characters first, for good (DeleteAccount follows CharDelete.Method, which may only mark them deleted)
        if (QueryResult result = CharacterDatabase.PQuery("SELECT guid FROM characters WHERE account = %u", accountId))
        {
            do
            {
                Player::DeleteFromDB(ObjectGuid::Create<HighGuid::Player>((*result)[0].GetUInt64()), accountId, false, true);
                ++characters;
            } while (result->NextRow());
        }
        if (AccountMgr::DeleteAccount(accountId) == AccountOpResult::AOR_OK)
            ++accounts;
        else
            TC_LOG_ERROR("server.loading", "[mod-playerbots] Playerbots.DeleteBotAccounts: could not delete account %u.", accountId);
    }

    // the deletes are queued: building the pool now could meet the old accounts' rows
    _poolStage = PoolStage::Done;
    TC_LOG_INFO("server.loading", "[mod-playerbots] Playerbots.DeleteBotAccounts: deleted %u bot account(s) ('%s...') and %u character(s). "
        "No bot pool this boot: set Playerbots.DeleteBotAccounts = 0 and restart to build a new one.", accounts, prefix.c_str(), characters);
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

    if (!_enabled || (!_lfgFillEnabled && !_bgFillEnabled))
        return;

    {
        std::lock_guard<std::mutex> lock(_lfgLock);
        uint32& timer = _lfgCheckTimers[guid];
        timer += diff;
        if (timer < 2000)
            return;
        timer = 0;
    }

    if (_bgFillEnabled)
        ReportBgQueues(player);
    if (_lfgFillEnabled)
        ReportDungeonGroup(player);

    if (!_lfgFillEnabled || !sLFGMgr->HasQueue(guid))
        return;

    // a party's queue is its leader's; a dungeon finder group short of members is filled by invites instead
    // (ReportDungeonGroup): its replacement queue never matched a bot (builds #28-#31)
    Group* group = player->GetGroup();
    if (group && (group->isLFGGroup() || group->GetLeaderGUID() != guid))
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
    std::vector<BgReport> bgReports;
    std::vector<BgMember> bgMembers;
    std::vector<ReplaceReport> replaceReports;
    std::vector<ObjectGuid> releases;
    {
        std::lock_guard<std::mutex> lock(_lfgLock);
        reports.swap(_queueReports);
        bgReports.swap(_bgReports);
        bgMembers.swap(_bgMemberReports);
        replaceReports.swap(_replaceReports);
        releases.swap(_releaseRequests);
    }
    for (ObjectGuid const& guid : releases)
        ReleaseBot(guid);

    uint32 const now = getMSTime();
    UpdateBgFill(bgReports, bgMembers, now);
    UpdateReplaceFill(replaceReports, now);
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

    // an idle logged-in pool bot of his faction, at most his level, whose class has a spec of that role; picked at
    // random (owner, build #31: by highest level, the level 10 allied races always won)
    ObjectGuid bestGuid;
    Bot* best = nullptr;
    CharacterInfo const* bestInfo = nullptr;
    uint32 seen = 0;
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
            specsFor(info->Class).empty() || urand(0, seen++) != 0)
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

// ---- Dungeon finder groups short of members (2026-11-07) ------------------------------------------------

// the client's thread, every 2 s: he leads a dungeon finder group, inside its dungeon, with an empty place
void PlayerbotMgr::ReportDungeonGroup(Player* player)
{
    Group* group = player->GetGroup();
    if (!group || !group->isLFGGroup() || group->GetLeaderGUID() != player->GetGUID() || !player->GetMap()->IsDungeon())
        return;
    ObjectGuid const gguid = group->GetGUID();
    uint32 const queueId = sLFGMgr->GetQueueId(gguid);
    lfg::LfgState const state = sLFGMgr->GetState(gguid, queueId);
    // in the dungeon, or queued again for a replacement; not finished, not during a kick vote or a proposal
    if (state != lfg::LFG_STATE_DUNGEON && state != lfg::LFG_STATE_QUEUED)
        return;
    uint32 const dungeonId = sLFGMgr->GetDungeon(gguid);
    lfg::LFGDungeonData const* dungeon = sLFGMgr->GetLFGDungeon(dungeonId);
    if (!dungeon || !dungeon->dbc || dungeon->dbc->Substruct != LFG_QUEUE_DUNGEON)
        return;

    ReplaceReport report;
    report.leader = player->GetGUID();
    report.leaderAccount = player->GetSession()->GetAccountId();
    report.group = gguid;
    report.dungeonId = dungeonId;
    report.level = player->getLevel();
    report.team = player->GetTeam();
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
    {
        ++report.members;
        uint8 const roles = group->GetLfgRoles(slot.Guid);
        if (roles & lfg::PLAYER_ROLE_TANK)
            ++report.tanks;
        else if (roles & lfg::PLAYER_ROLE_HEALER)
            ++report.healers;
        else
            ++report.dps;
    }
    uint32 const size = std::max<uint32>(dungeon->dbc->CountTank + dungeon->dbc->CountHealer + dungeon->dbc->CountDamage, 5);
    if (report.members >= size)
        return;
    std::lock_guard<std::mutex> lock(_lfgLock);
    _replaceReports.push_back(report);
}

void PlayerbotMgr::UpdateReplaceFill(std::vector<ReplaceReport> const& reports, uint32 now)
{
    for (ReplaceReport const& report : reports)
    {
        ReplaceFill& fill = _replaceFills[report.group];
        if (!fill.firstSeen)
        {
            fill.firstSeen = now;
            TC_LOG_INFO("server.loading", "[mod-playerbots] Dungeon finder: the group of %s has %u member(s); bots join it in %u s.",
                report.leader.ToString().c_str(), uint32(report.members), _lfgFillDelay / 1000);
        }
        fill.report = report;
        fill.lastSeen = now;
    }

    // an invited bot that is not in the group (the invite failed, or the group is gone) goes back to GM Island
    auto release = [](ObjectGuid const& guid)
    {
        std::shared_ptr<PlayerbotAI> ai = sPlayerbotMgr->GetAI(guid);
        if (!ai)
            return;
        ObjectAccessor::PostToPlayer(guid, [ai](Player* bot) -> void
        {
            if (!bot->GetGroup())
                ai->Dismiss(bot, "it did not get into the dungeon group");
        });
    };

    for (auto itr = _replaceFills.begin(); itr != _replaceFills.end();)
    {
        ReplaceFill& fill = itr->second;
        Group* group = sGroupMgr->GetGroupByGUID(itr->first);

        // no report for a while: the group is full, gone, finished, or its leader left the dungeon
        if (getMSTimeDiff(fill.lastSeen, now) > 5000 || !group)
        {
            for (LfgFill::FillBot const& bot : fill.bots)
                if (!group || !group->IsMember(bot.guid))
                    release(bot.guid);
            itr = _replaceFills.erase(itr);
            continue;
        }

        // bots that made it count in the report from now on; one not in after 30 s is given up
        for (auto bot = fill.bots.begin(); bot != fill.bots.end();)
        {
            if (group->IsMember(bot->guid))
                bot = fill.bots.erase(bot);
            else if (getMSTimeDiff(bot->started, now) > 30000)
            {
                release(bot->guid);
                bot = fill.bots.erase(bot);
            }
            else
                ++bot;
        }

        if (getMSTimeDiff(fill.firstSeen, now) < _lfgFillDelay || int32(fill.nextJoin - now) > 0)
        {
            ++itr;
            continue;
        }

        lfg::LFGDungeonData const* dungeon = sLFGMgr->GetLFGDungeon(fill.report.dungeonId);
        int32 tanks = dungeon && dungeon->dbc ? dungeon->dbc->CountTank : 1;
        int32 healers = dungeon && dungeon->dbc ? dungeon->dbc->CountHealer : 1;
        int32 dps = dungeon && dungeon->dbc ? dungeon->dbc->CountDamage : 3;
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
        int32 const free = 5 - int32(fill.report.members) - int32(fill.bots.size());
        if (free > 0)
            for (uint8 role : { uint8(lfg::PLAYER_ROLE_TANK), uint8(lfg::PLAYER_ROLE_HEALER), uint8(lfg::PLAYER_ROLE_DAMAGE) })
            {
                int32 const need = role == lfg::PLAYER_ROLE_TANK ? tanks : role == lfg::PLAYER_ROLE_HEALER ? healers : dps;
                if (need > 0 && StartReplaceBot(fill, role))
                {
                    fill.nextJoin = now + _lfgJoinInterval;
                    break;
                }
            }
        ++itr;
    }
}

bool PlayerbotMgr::StartReplaceBot(ReplaceFill& fill, uint8 role)
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

    // an idle logged-in pool bot of his faction, at most his level, with a spec of that role, at random
    ObjectGuid bestGuid;
    Bot* best = nullptr;
    CharacterInfo const* bestInfo = nullptr;
    uint32 seen = 0;
    for (auto& pair : _bots)
    {
        Bot& bot = pair.second;
        if (bot.removing || !bot.inWorld || !bot.notifyGuid.IsEmpty() || bot.init || !onBotAccount(bot.session->GetAccountId()))
            continue;
        if (bot.restUntil && int32(bot.restUntil - getMSTime()) > 0)    // e.g. just kicked from this group
            continue;
        CharacterInfo const* info = sWorld->GetCharacterInfo(pair.first);
        if (!info || Player::TeamForRace(info->Race) != fill.report.team || info->Level > fill.report.level ||
            specsFor(info->Class).empty() || urand(0, seen++) != 0)
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
            TC_LOG_INFO("server.loading", "[mod-playerbots] Dungeon finder: no idle pool bot can join the group of %s as %s.",
                fill.report.leader.ToString().c_str(), role == lfg::PLAYER_ROLE_TANK ? "tank" : role == lfg::PLAYER_ROLE_HEALER ? "healer" : "dps");
        }
        return false;
    }

    std::vector<uint32> const specs = specsFor(bestInfo->Class);
    BotFactory::CreateRequest request;
    request.cls = bestInfo->Class;
    request.specId = specs[urand(0, specs.size() - 1)];
    request.level = fill.report.level;
    request.team = fill.report.team;
    request.creator = fill.report.leader;
    request.creatorAccount = fill.report.leaderAccount;
    request.teleport = false;                               // to him once it is in the group (the dungeon is the group's)

    best->notifyGuid = fill.report.leader;
    best->toGMIsland = false;
    best->ownerGoneTimer = -1;
    std::shared_ptr<PlayerbotAI> ai = std::make_shared<PlayerbotAI>(fill.report.leader, best->session->GetAccountId(), true);
    {
        std::lock_guard<std::mutex> lock(_aiLock);
        _ais[bestGuid] = ai;
    }
    ObjectAccessor::PostToPlayer(bestGuid, [ai, request, role](Player* p) -> void { ai->JoinDungeonGroupFor(p, request, role); });

    LfgFill::FillBot fillBot;
    fillBot.guid = bestGuid;
    fillBot.role = role;
    fillBot.started = getMSTime();
    fill.bots.push_back(fillBot);
    TC_LOG_INFO("server.loading", "[mod-playerbots] Dungeon finder: bot '%s' joins the group of %s as %s.", best->name.c_str(),
        fill.report.leader.ToString().c_str(), role == lfg::PLAYER_ROLE_TANK ? "tank" : role == lfg::PLAYER_ROLE_HEALER ? "healer" : "dps");
    return true;
}

// ---- Battleground fill (2026-11-06 / 07) ----------------------------------------------------------------

namespace
{
    uint8 SpecRoleOf(Player* player)
    {
        if (ChrSpecializationEntry const* spec = sChrSpecializationStore.LookupEntry(player->GetSpecializationId()))
            if (spec->Role <= ROLES_DPS)
                return spec->Role;
        return ROLES_DPS;
    }

    char const* RoleText(uint8 role)
    {
        return role == ROLES_TANK ? "tank" : role == ROLES_HEALER ? "healer" : "dps";
    }

    int TeamIndex(uint32 team)
    {
        return team == ALLIANCE ? 0 : 1;
    }
}

// the client's thread, every 2 s: each normal battleground queue he waits in (not invited yet), or the battleground
// he is in
void PlayerbotMgr::ReportBgQueues(Player* player)
{
    if (player->InBattleground())
    {
        Battleground* bg = player->GetBattleground();
        if (!bg || bg->IsArena())
            return;
        BgMember member;
        member.guid = player->GetGUID();
        member.instanceId = bg->GetInstanceID();
        member.bgTypeId = bg->GetTypeID();
        member.queueType = MS::Battlegrounds::GetBgQueueTypeIdByBgTypeID(member.bgTypeId);
        member.bracket = bg->GetBracketId();
        member.status = uint8(bg->GetStatus());
        member.maxPerTeam = bg->GetMaxPlayersPerTeam();
        member.team = player->GetBGTeam();
        member.role = SpecRoleOf(player);
        member.level = player->getLevel();
        ReportBgMember(member);
        return;
    }
    if (!BotBattleground::IsQueued(player))
        return;

    std::vector<BgReport> reports;
    for (uint32 i = 0; i < PLAYER_MAX_BATTLEGROUND_QUEUES; ++i)
    {
        uint8 const queue = player->GetBattlegroundQueueTypeId(i);
        if (!BotBattleground::IsFillableQueue(queue) || player->IsInvitedForBattlegroundQueueType(queue))
            continue;
        BgReport report;
        report.queuer = player->GetGUID();
        report.queueType = queue;
        report.bgTypeId = MS::Battlegrounds::GetBgTemplateIdByQueueTypeID(queue);
        report.level = player->getLevel();
        report.team = player->GetTeam();
        report.role = SpecRoleOf(player);
        Battleground* bg = report.bgTypeId ? sBattlegroundMgr->GetBattlegroundTemplate(report.bgTypeId) : nullptr;
        PVPDifficultyEntry const* bracket = bg ? sDB2Manager.GetBattlegroundBracketByLevel(bg->GetMapId(), report.level) : nullptr;
        if (!bracket)
            continue;
        report.bracket = bracket->RangeIndex;
        // the bots of his group queued with it (a group queues as one: its leader counts them)
        if (Group* group = player->GetGroup())
            if (group->GetLeaderGUID() == player->GetGUID())
                for (Group::MemberSlot const& slot : group->GetMemberSlots())
                    if (slot.Guid != player->GetGUID() && IsBot(slot.Guid))
                        ++report.partyBots;
        reports.push_back(report);
    }
    if (reports.empty())
        return;
    std::lock_guard<std::mutex> lock(_lfgLock);
    _bgReports.insert(_bgReports.end(), reports.begin(), reports.end());
}

void PlayerbotMgr::ReportBgMember(BgMember const& member)
{
    std::lock_guard<std::mutex> lock(_lfgLock);
    _bgMemberReports.push_back(member);
}

// owner's rule: 10% tanks, 20% healers (rounded down), the rest dps; need[] = what perTeam players lack after `roles`
void PlayerbotMgr::RoleNeeds(uint32 perTeam, std::vector<uint8> const& roles, int32 need[3])
{
    need[ROLES_TANK] = int32(perTeam / 10);
    need[ROLES_HEALER] = int32(perTeam / 5);
    need[ROLES_DPS] = int32(perTeam) - need[ROLES_TANK] - need[ROLES_HEALER];
    for (uint8 role : roles)
    {
        uint8 r = role <= ROLES_DPS ? role : ROLES_DPS;
        if (need[r] <= 0)                                   // an extra tank or healer takes a dps place
            r = ROLES_DPS;
        --need[r];
    }
}

// A player queues for a battleground whose team is full of fill bots: a bot of his role (else a dps) leaves it, the
// queue then invites him into that place instead of waiting for a new battleground
bool PlayerbotMgr::MakeRoomFor(BgReport const& report, uint32 now)
{
    auto made = _bgRoomMade.find(report.queuer);
    if (made != _bgRoomMade.end() && getMSTimeDiff(made->second, now) < 90000)
        return true;                                        // done for him already: the invitation is on its way

    for (auto& pair : _bgInstances)
    {
        BgInstance& instance = pair.second;
        if (instance.queueType != report.queueType || instance.bracket != report.bracket ||
            instance.status < STATUS_WAIT_JOIN || instance.status >= STATUS_WAIT_LEAVE)
            continue;
        uint32 inTeam = 0;
        BgMember const* sameRole = nullptr;
        BgMember const* anyBot = nullptr;
        for (auto const& m : instance.members)
        {
            if (m.second.team != report.team)
                continue;
            ++inTeam;
            if (!m.second.fillBot)
                continue;
            if (m.second.role == report.role && !sameRole)
                sameRole = &m.second;
            if (!anyBot || (m.second.role == ROLES_DPS && anyBot->role != ROLES_DPS))
                anyBot = &m.second;
        }
        if (inTeam < instance.maxPerTeam || !anyBot)
            continue;                                       // a free place already, or no bot to send away
        BgMember const* leaver = sameRole ? sameRole : anyBot;
        ObjectGuid const guid = leaver->guid;
        if (std::shared_ptr<PlayerbotAI> ai = GetAI(guid))
            ObjectAccessor::PostToPlayer(guid, [ai](Player* bot) -> void { ai->LeaveBattlegroundForPlayer(bot); });
        instance.members.erase(guid);
        _bgRoomMade[report.queuer] = now;
        TC_LOG_INFO("server.loading", "[mod-playerbots] Battleground: a %s bot leaves battleground %u (instance %u) for %s, who queued.",
            RoleText(leaver->role), uint32(instance.bgTypeId), pair.first, report.queuer.ToString().c_str());
        return true;
    }
    return false;
}

void PlayerbotMgr::UpdateBgFill(std::vector<BgReport> const& reports, std::vector<BgMember> const& members, uint32 now)
{
    // who is in which battleground
    for (BgMember const& report : members)
    {
        BgInstance& instance = _bgInstances[report.instanceId];
        instance.queueType = report.queueType;
        instance.bgTypeId = report.bgTypeId;
        instance.bracket = report.bracket;
        instance.status = report.status;
        instance.maxPerTeam = report.maxPerTeam;
        instance.level = std::max(instance.level, report.level);
        instance.lastSeen = now;
        BgMember& member = instance.members[report.guid];
        member = report;
        member.lastSeen = now;
        // a top-up bot that got in counts as a member from now on
        instance.pending.erase(std::remove_if(instance.pending.begin(), instance.pending.end(),
            [&report](BgFillBot const& b) { return b.guid == report.guid; }), instance.pending.end());
    }
    for (auto itr = _bgInstances.begin(); itr != _bgInstances.end();)
    {
        for (auto m = itr->second.members.begin(); m != itr->second.members.end();)
            m = getMSTimeDiff(m->second.lastSeen, now) > 5000 ? itr->second.members.erase(m) : std::next(m);
        if (itr->second.members.empty() && getMSTimeDiff(itr->second.lastSeen, now) > 5000)
            itr = _bgInstances.erase(itr);
        else
            ++itr;
    }
    for (auto itr = _bgRoomMade.begin(); itr != _bgRoomMade.end();)
        itr = getMSTimeDiff(itr->second, now) > 90000 ? _bgRoomMade.erase(itr) : std::next(itr);

    // queued players: a place in a running battleground first, else the fill of a new one
    std::set<std::pair<uint8, uint8>> waiting;              // queues with a player waiting
    for (BgReport const& report : reports)
    {
        waiting.insert(std::make_pair(report.queueType, report.bracket));
        if (MakeRoomFor(report, now))
            continue;
        BgFill& fill = _bgFills[std::make_pair(report.queueType, report.bracket)];
        if (!fill.firstSeen || fill.endedAt)
        {
            fill = BgFill();
            fill.firstSeen = now;
            fill.queueType = report.queueType;
            fill.bgTypeId = report.bgTypeId;
            fill.level = report.level;
            fill.master = report.queuer;
            TC_LOG_INFO("server.loading", "[mod-playerbots] Battleground: %s queued (battleground %u, bracket %u); bots fill both teams in %u s.",
                report.queuer.ToString().c_str(), uint32(report.bgTypeId), uint32(report.bracket), _bgFillDelay / 1000);
        }
        BgFill::Queuer& queuer = fill.queuers[report.queuer];
        queuer.team = report.team;
        queuer.role = report.role;
        queuer.partyBots = report.partyBots;
        queuer.lastSeen = now;
    }

    // a bot no longer needed leaves the queue and goes back to GM Island, unless it got in (or was invited)
    auto abandon = [](ObjectGuid const& guid)
    {
        std::shared_ptr<PlayerbotAI> ai = sPlayerbotMgr->GetAI(guid);
        if (!ai)
            return;
        ObjectAccessor::PostToPlayer(guid, [ai](Player* bot) -> void
        {
            if (ai->AbandonBattleground(bot))
                ai->Dismiss(bot, "nobody waits in its battleground queue any more");
        });
    };

    for (auto itr = _bgFills.begin(); itr != _bgFills.end();)
    {
        BgFill& fill = itr->second;
        for (auto queuer = fill.queuers.begin(); queuer != fill.queuers.end();)
            queuer = getMSTimeDiff(queuer->second.lastSeen, now) > 5000 ? fill.queuers.erase(queuer) : std::next(queuer);

        BattlegroundQueue& queue = sBattlegroundMgr->GetBattlegroundQueue(fill.queueType);
        auto queued = [&queue](ObjectGuid const& guid) { GroupQueueInfo info; return queue.GetPlayerGroupInfoData(guid, &info); };

        // nobody waits any more (they got in, or left the queue): the bots still queued get 60 s to be invited into
        // the battleground being filled, then leave
        if (fill.queuers.empty())
        {
            if (!fill.endedAt)
                fill.endedAt = now;
            bool const expired = getMSTimeDiff(fill.endedAt, now) > 60000;
            for (auto bot = fill.bots.begin(); bot != fill.bots.end();)
            {
                if (queued(bot->guid) && !expired)
                {
                    ++bot;
                    continue;
                }
                if (queued(bot->guid))
                    abandon(bot->guid);
                bot = fill.bots.erase(bot);
            }
            if (fill.bots.empty())
                itr = _bgFills.erase(itr);
            else
                ++itr;
            continue;
        }

        // a bot no longer in the queue (it got in, or the join failed) leaves the fill
        for (auto bot = fill.bots.begin(); bot != fill.bots.end();)
        {
            if (queued(bot->guid) || getMSTimeDiff(bot->started, now) < 10000)
            {
                ++bot;
                continue;
            }
            abandon(bot->guid);
            bot = fill.bots.erase(bot);
        }

        Battleground* bg = sBattlegroundMgr->GetBattlegroundTemplate(fill.bgTypeId);
        if (!bg || getMSTimeDiff(fill.firstSeen, now) < _bgFillDelay || int32(fill.nextJoin - now) > 0)
        {
            ++itr;
            continue;
        }
        uint32 const maxPerTeam = std::max<uint32>(1, bg->GetMaxPlayersPerTeam());
        uint32 const perTeam = _bgBotsPerTeam ? std::min(_bgBotsPerTeam, maxPerTeam) : maxPerTeam;

        std::vector<uint8> roles[2];
        for (auto const& pair : fill.queuers)
        {
            roles[TeamIndex(pair.second.team)].push_back(pair.second.role);
            for (uint8 i = 0; i < pair.second.partyBots; ++i)
                roles[TeamIndex(pair.second.team)].push_back(ROLES_DPS);
        }
        for (BgFillBot const& bot : fill.bots)
            roles[TeamIndex(bot.team)].push_back(bot.role);

        // one bot per JoinInterval, to the team that lacks the most, its scarcest role first
        int32 need[2][3];
        RoleNeeds(perTeam, roles[0], need[0]);
        RoleNeeds(perTeam, roles[1], need[1]);
        int32 const total0 = std::max(0, need[0][0]) + std::max(0, need[0][1]) + std::max(0, need[0][2]);
        int32 const total1 = std::max(0, need[1][0]) + std::max(0, need[1][1]) + std::max(0, need[1][2]);
        for (int side : { total0 >= total1 ? 0 : 1, total0 >= total1 ? 1 : 0 })
        {
            bool started = false;
            for (uint8 role : { uint8(ROLES_TANK), uint8(ROLES_HEALER), uint8(ROLES_DPS) })
            {
                if (need[side][role] <= 0)
                    continue;
                uint32 const team = side == 0 ? ALLIANCE : HORDE;
                ObjectGuid const guid = StartBgBot(team, role, fill.level, fill.bgTypeId, fill.master, fill.warned);
                if (guid.IsEmpty())
                    continue;
                BgFillBot fillBot;
                fillBot.guid = guid;
                fillBot.team = team;
                fillBot.role = role;
                fillBot.started = now;
                fill.bots.push_back(fillBot);
                started = true;
                break;
            }
            if (started)
            {
                fill.nextJoin = now + _bgJoinInterval;
                break;
            }
        }
        ++itr;
    }

    // top-up (owner's rule): a running battleground's team short of players for FillDelay with nobody waiting in its
    // queue (a refused invitation, a bot or player gone) gets a bot of the missing role
    for (auto& pair : _bgInstances)
    {
        BgInstance& instance = pair.second;
        if (instance.status < STATUS_WAIT_JOIN || instance.status >= STATUS_WAIT_LEAVE || !instance.maxPerTeam ||
            waiting.count(std::make_pair(instance.queueType, instance.bracket)) || int32(instance.nextJoin - now) > 0)
            continue;
        // a top-up bot not inside after 60 s leaves the queue (AbandonBattleground keeps it if it got in meanwhile)
        for (auto bot = instance.pending.begin(); bot != instance.pending.end();)
        {
            if (getMSTimeDiff(bot->started, now) <= 60000)
            {
                ++bot;
                continue;
            }
            abandon(bot->guid);
            bot = instance.pending.erase(bot);
        }
        uint32 const perTeam = _bgBotsPerTeam ? std::min(_bgBotsPerTeam, instance.maxPerTeam) : instance.maxPerTeam;
        std::vector<uint8> roles[2];
        bool hasBots = false;
        for (auto const& m : instance.members)
        {
            roles[TeamIndex(m.second.team)].push_back(m.second.role);
            hasBots |= m.second.fillBot;
        }
        if (!hasBots)
            continue;                                       // only battlegrounds the bots are filling
        for (BgFillBot const& bot : instance.pending)
            roles[TeamIndex(bot.team)].push_back(bot.role);
        for (int side = 0; side < 2; ++side)
        {
            if (roles[side].size() >= perTeam)
            {
                instance.shortSince[side] = 0;
                continue;
            }
            if (!instance.shortSince[side])
                instance.shortSince[side] = now;
            if (getMSTimeDiff(instance.shortSince[side], now) < _bgFillDelay)
                continue;
            int32 need[3];
            RoleNeeds(perTeam, roles[side], need);
            uint8 role = need[ROLES_TANK] > 0 ? ROLES_TANK : need[ROLES_HEALER] > 0 ? ROLES_HEALER : ROLES_DPS;
            uint32 const team = side == 0 ? ALLIANCE : HORDE;
            uint8 warned = 0;
            ObjectGuid const guid = StartBgBot(team, role, instance.level, instance.bgTypeId, ObjectGuid::Empty, warned);
            if (guid.IsEmpty())
                continue;
            BgFillBot bot;
            bot.guid = guid;
            bot.team = team;
            bot.role = role;
            bot.started = now;
            instance.pending.push_back(bot);
            instance.shortSince[side] = now;                // the next one, if still short, after another delay
            instance.nextJoin = now + _bgJoinInterval;
            TC_LOG_INFO("server.loading", "[mod-playerbots] Battleground: instance %u lacks a %s %s; a bot queues for it.",
                pair.first, team == ALLIANCE ? "Alliance" : "Horde", RoleText(role));
            break;
        }
    }
}

ObjectGuid PlayerbotMgr::StartBgBot(uint32 team, uint8 role, uint8 level, uint16 bgTypeId, ObjectGuid const& master, uint8& warned)
{
    auto specsFor = [role](uint8 cls)
    {
        std::vector<uint32> specs;
        for (uint32 spec : BotFactory::SpecsOfClass(cls))
            if (ChrSpecializationEntry const* entry = sChrSpecializationStore.LookupEntry(spec))
                if (entry->Role == role)
                    specs.push_back(spec);
        return specs;
    };

    std::vector<uint32> const accounts = BotFactory::BotAccountIds(_createConfig);
    auto onBotAccount = [&accounts](uint32 id) { return std::find(accounts.begin(), accounts.end(), id) != accounts.end(); };

    // an idle logged-in pool bot of that faction, at most that level, whose class has a spec of the role; at random
    ObjectGuid bestGuid;
    Bot* best = nullptr;
    CharacterInfo const* bestInfo = nullptr;
    uint32 seen = 0;
    for (auto& pair : _bots)
    {
        Bot& bot = pair.second;
        if (bot.removing || !bot.inWorld || !bot.notifyGuid.IsEmpty() || bot.init || !onBotAccount(bot.session->GetAccountId()))
            continue;
        if (bot.restUntil && int32(bot.restUntil - getMSTime()) > 0)
            continue;
        CharacterInfo const* info = sWorld->GetCharacterInfo(pair.first);
        if (!info || Player::TeamForRace(info->Race) != team || info->Level > level || specsFor(info->Class).empty() ||
            urand(0, seen++) != 0)
            continue;
        best = &bot;
        bestGuid = pair.first;
        bestInfo = info;
    }

    uint8 const bit = uint8((team == ALLIANCE ? 1 : 8) << role);
    if (!best)
    {
        if (!(warned & bit))
        {
            warned |= bit;
            TC_LOG_INFO("server.loading", "[mod-playerbots] Battleground: no idle %s pool bot of level %u or less can be a %s.",
                team == ALLIANCE ? "Alliance" : "Horde", uint32(level), RoleText(role));
        }
        return ObjectGuid::Empty;
    }

    std::vector<uint32> const specs = specsFor(bestInfo->Class);
    BotFactory::CreateRequest request;
    request.cls = bestInfo->Class;
    request.specId = specs[urand(0, specs.size() - 1)];
    request.level = level;
    request.team = team;
    request.creator = master;
    request.teleport = false;                               // the battleground takes it there

    best->notifyGuid = master;
    best->toGMIsland = false;
    best->ownerGoneTimer = -1;
    std::shared_ptr<PlayerbotAI> ai = std::make_shared<PlayerbotAI>(master, best->session->GetAccountId(), true);
    {
        std::lock_guard<std::mutex> lock(_aiLock);
        _ais[bestGuid] = ai;
    }
    ObjectAccessor::PostToPlayer(bestGuid, [ai, request, bgTypeId](Player* p) -> void
    {
        ai->JoinBattlegroundFor(p, request, bgTypeId);
    });
    TC_LOG_INFO("server.loading", "[mod-playerbots] Battleground: bot '%s' queues for battleground %u as an %s %s.", best->name.c_str(),
        uint32(bgTypeId), team == ALLIANCE ? "Alliance" : "Horde", RoleText(role));
    return bestGuid;
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

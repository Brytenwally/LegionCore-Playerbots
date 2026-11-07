/*
 * mod-playerbots: per-bot AI. See PlayerbotAI.h.
 */

#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "DB2Stores.h"
#include "Bag.h"
#include "Group.h"
#include "Item.h"
#include "LootMgr.h"
#include "Creature.h"
#include "World.h"
#include "CellImpl.h"
#include "Creature.h"
#include "GameObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "BotCombat.h"
#include "BotMovement.h"
#include "BotRotations.h"
#include "BotFactory.h"
#include "BotGear.h"
#include "BotMaintenance.h"
#include "BotConsumables.h"
#include "BotFormation.h"
#include "Chat.h"
#include "GameTime.h"
#include "LFGMgr.h"
#include "LFGPackets.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "QuestData.h"
#include "QuestDef.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <algorithm>
#include <map>
#include <mutex>
#include <vector>
#include <cctype>

namespace
{
    uint32 const FollowCheckInterval = 500;                 // ms between follow checks
    uint32 const CombatCheckInterval = 250;                 // ms between combat checks (one cast at most each)
    uint32 const TalentCheckInterval = 10000;               // ms between checks for empty talent rows
    uint32 const LfgCheckInterval = 1000;                   // ms between dungeon finder checks (role check, proposal)
    uint32 const LootCheckInterval = 1000;                  // ms between loot roll / bag upgrade checks
    uint32 const ChaosInterval = 3000;                      // ms between new chaos formation offsets
    int32 const GroupLeftLogoutDelay = 3000;                // owner's rule: logout this long after leaving his group
    float const RangedDistance = 25.0f;                     // ranged roles chase to this distance
    float const RangedStopDistance = 30.0f;                 // ... and stop chasing within this (and in sight)
    float const HealBelowPct = 90.0f;                       // healers heal allies below this
    float const TankFirstBelowPct = 75.0f;                  // ... the tank first once it is below this
    float const CriticalHealthPct = 40.0f;                  // triage: healed before interrupts and dispels
    float const HealRange = 35.0f;                          // beyond it (or out of sight) a healer walks closer
    float const HealApproachDistance = 15.0f;
    float const BattlegroundRange = 40.0f;                  // enemy players this close are fought in a battleground

    std::string Lower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return text;
    }
}

PlayerbotAI::PlayerbotAI(ObjectGuid const& master, uint32 accountId, bool randomBot)
    : _master(master), _accountId(accountId), _formation(sPlayerbotMgr->GetDefaultFormation()), _randomBot(randomBot)
{
    auto fill = [](std::set<std::string>& set, std::string const& list)
    {
        std::string name;
        for (char c : list + ",")
        {
            if (c == ',' || c == ' ')
            {
                if (!name.empty())
                    set.insert(name);
                name.clear();
            }
            else
                name += char(std::tolower((unsigned char)c));
        }
    };
    fill(_co, sPlayerbotMgr->GetDefaultCombatStrategies());
    fill(_nc, sPlayerbotMgr->GetDefaultNonCombatStrategies());
    // alt rule: looting changes a player's character (its bags), so alts loot only when ordered (nc +loot)
    if (!_randomBot)
        _nc.erase("loot");
    ApplyFlags();
}

bool PlayerbotAI::IsMaster(ObjectGuid const& guid, uint32 accountId) const
{
    return (!_master.IsEmpty() && guid == _master) || (accountId && accountId == _accountId);
}

void PlayerbotAI::Update(Player* bot, uint32 diff)
{
    if (!bot->IsInWorld() || bot->IsBeingTeleported())
        return;

    if (_dismissed)
    {
        FinishDismiss(bot, diff);
        return;
    }

    if (_selfBot)
    {
        // the player's own character: only the fight and the follow (no invites, logout rules or talents)
        _combatTimer += diff;
        if (_combatTimer >= CombatCheckInterval)
        {
            _combatTimer = 0;
            UpdateCombat(bot);
        }
        _followTimer += diff;
        if (_followTimer >= FollowCheckInterval)
        {
            _followTimer = 0;
            UpdateFollow(bot);
        }
        return;
    }

    if (!_inviteCreator.IsEmpty())
    {
        if (_inviteTimer > diff)
            _inviteTimer -= diff;
        else
        {
            SendCreatorInvite(bot);
            _inviteCreator.Clear();
        }
    }

    HandleGroupInvite(bot);
    if (_joinTeleport)
        UpdateJoinTeleport(bot);
    CheckOwnerGroup(bot, diff);
    CheckPlayerlessGroup(bot, diff);

    // owner's decision: talents are learned automatically, alts included, into empty rows only
    _talentTimer += diff;
    if (_talentTimer >= TalentCheckInterval)
    {
        _talentTimer = 0;
        if (_talentsOn && bot->IsAlive() && !bot->isInCombat())
            BotFactory::FillTalents(bot);
    }

    _lfgTimer += diff;
    if (_lfgTimer >= LfgCheckInterval)
    {
        _lfgTimer = 0;
        UpdateLfg(bot);
        _leadTimer += LfgCheckInterval;
        if (_leadTimer >= 3000)
        {
            _leadTimer = 0;
            PassLfgLead(bot);
        }
    }

    _bgTimer += diff;
    if (_bgTimer >= 500)
    {
        uint32 const elapsed = _bgTimer;
        _bgTimer = 0;
        UpdateBattleground(bot, elapsed);
    }

    _lootTimer += diff;
    if (_lootTimer >= LootCheckInterval)
    {
        _lootTimer = 0;
        UpdateLootRolls(bot);
        UpdateBagEquip(bot);
    }

    _corpseTimer += diff;
    if (_corpseTimer >= 500)
    {
        _corpseTimer = 0;
        UpdateLoot(bot);
    }

    _combatTimer += diff;
    if (_combatTimer >= CombatCheckInterval)
    {
        _combatTimer = 0;
        UpdateCombat(bot);
    }
    UpdatePrecombat(bot, diff);

    _followTimer += diff;
    if (_followTimer >= FollowCheckInterval)
    {
        _followTimer = 0;
        UpdateFollow(bot);
    }
}

// Answers a pending group invite as a client would, with CMSG_PARTY_INVITE_RESPONSE: the core's own
// handler then joins or declines on the right threads. Invites from the bot's master (or its account's
// player) are accepted, others declined.
void PlayerbotAI::HandleGroupInvite(Player* bot)
{
    Group* invite = bot->GetGroupInvite();
    if (!invite)
    {
        _answeredInvite = nullptr;                          // a later invite of the same group is a new one
        return;
    }
    if (invite == _answeredInvite)
        return;

    _answeredInvite = invite;
    ObjectGuid const leader = invite->GetLeaderGUID();
    bool accept = IsMaster(leader, ObjectMgr::GetPlayerAccountIdByGUID(leader));

    // Owner's rule (2026-11-05): a random bot with no master accepts any player's invite and becomes his bot
    // (set up to his level and summoned to him, as by ".playerbots create"); one with a master says so.
    if (!accept && _randomBot)
    {
        if (_master.IsEmpty() && _pendingMaster.IsEmpty())
        {
            // the inviter may stand on another map: read what is needed under the accessor lock
            BotFactory::CreateRequest request;
            bool isPlayer = false;
            ObjectAccessor::WithPlayer(leader, [&](Player* inviter)
            {
                WorldSession* session = inviter->GetSession();
                if (!session || session->IsBot())
                    return;
                isPlayer = true;
                request.level = inviter->getLevel();
                request.team = inviter->GetTeam();
                request.location = WorldLocation(inviter->GetMapId(), inviter->GetPositionX(), inviter->GetPositionY(),
                    inviter->GetPositionZ(), inviter->GetOrientation());
                request.creator = inviter->GetGUID();
                request.creatorAccount = session->GetAccountId();
            });
            if (isPlayer)
            {
                accept = true;
                request.cls = bot->getClass();
                _pendingMaster = leader;
                sPlayerbotMgr->RequestAssign(bot->GetGUID(), request);
            }
        }
        else
            Say(bot, leader, "I already have a master, try another bot.");
    }

    // PartyInviteResponse::Read: int8 PartyIndex, bit Accept, bit HasRolesDesired
    WorldPacket* packet = new WorldPacket(CMSG_PARTY_INVITE_RESPONSE, 2);
    *packet << int8(0);
    packet->WriteBit(accept);
    packet->WriteBit(false);
    packet->FlushBits();
    bot->GetSession()->QueuePacket(packet);

    TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' %s the group invite of %s.", bot->GetName(),
        accept ? "accepts" : "declines", leader.ToString().c_str());
}

// Owner's rule (2026-11-07): a bot in a party mounts when its leader mounts (a flier when he flies and its riding allows,
// else a ground mount) and dismounts when he dismounts or a fight starts; only outdoors, with a mount and riding
void PlayerbotAI::UpdateMount(Player* bot)
{
    bool const mounted = bot->HasAuraType(SPELL_AURA_MOUNTED);
    if (_selfBot || !bot->IsAlive())
        return;
    if (_engaged || _groupInFight || bot->isInCombat())
    {
        if (mounted)
            bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
        return;
    }
    Group* group = bot->GetGroup();
    if (!group || bot->InBattleground() || bot->InArena())
        return;
    ObjectGuid leaderGuid = group->GetLeaderGUID();
    if (!_master.IsEmpty() && group->IsMember(_master))
        leaderGuid = _master;
    if (leaderGuid == bot->GetGUID())
        return;
    Player* leader = ObjectAccessor::GetPlayer(*bot, leaderGuid);
    if (!leader || !leader->IsInWorld())
        return;

    if (!leader->HasAuraType(SPELL_AURA_MOUNTED))
    {
        if (mounted)
            bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
        return;
    }
    if (mounted || bot->IsNonMeleeSpellCast(false) || _resting || !_lootTarget.IsEmpty())
        return;
    if (bot->IsInWater() || !bot->GetMap()->IsOutdoors(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()))
        return;
    bool const flying = leader->IsFlying() || leader->HasAuraType(SPELL_AURA_FLY);
    uint32 const spell = BotMaintenance::MountSpellFor(bot, flying);
    if (!spell)
        return;
    // the mount has a cast time: stand still for it (BotCast's rule: no moving flags before a cast)
    bot->StopMoving();
    bot->RemoveUnitMovementFlag(MOVEMENTFLAG_MASK_MOVING);
    bot->CastSpell(bot, spell, false);
}

void PlayerbotAI::UpdateFollow(Player* bot)
{
    UpdateMount(bot);

    // combat owns the movement while engaged (or a healer walks to its heal target); moving would also
    // break a cast; a resting bot stays seated (UpdateRest stands it up when its leader walks off)
    if (_engaged || _resting || !_healMoveGuid.IsEmpty() || !_lootTarget.IsEmpty() || bot->IsNonMeleeSpellCast(false))
        return;

    // in a battleground or arena the tactics move the bot (BotBattleground::Update); a self-bot is the player's
    if (!_selfBot && (bot->InBattleground() || bot->InArena()))
        return;

    // owner's rule (build #24): no following while the group fights (healers kept running after the player);
    // combat moves the bot (chase, heal range) until the fight is over
    if (_groupInFight)
    {
        if (!_followGuid.IsEmpty())
            StopFollowing(bot);
        return;
    }

    Group* group = bot->GetGroup();
    if (_stay || !group || !bot->IsAlive())
    {
        if (!group)
            StopFollowing(bot);
        return;
    }

    // the master when he is in the group, the group's leader otherwise; only on the bot's own map (a self-bot's
    // master is himself: he follows the leader)
    ObjectGuid leaderGuid = group->GetLeaderGUID();
    if (!_selfBot && !_master.IsEmpty() && group->IsMember(_master))
        leaderGuid = _master;
    if (leaderGuid == bot->GetGUID())
        return;

    Player* leader = ObjectAccessor::GetPlayer(*bot, leaderGuid);
    if (!leader || !leader->IsInWorld() || leader->IsBeingTeleported())
        return;

    // the formation slot (SkyFire / mod-playerbots formations); the chaos offset changes every few seconds
    _jitterTimer += FollowCheckInterval;
    if (_jitterTimer >= ChaosInterval)
    {
        _jitterTimer = 0;
        _jitterForward = frand(-1.5f, 1.5f);
        _jitterLeft = frand(-1.5f, 1.5f);
    }
    BotFormation::Slot const slot = BotFormation::SlotFor(bot, leader, BotCombat::GroupMembersOnMap(bot), _formation,
        _jitterForward, _jitterLeft);
    Player* target = slot.anchor == leaderGuid ? leader : ObjectAccessor::GetPlayer(*bot, slot.anchor);
    if (!target || !target->IsInWorld() || target->IsBeingTeleported())
        target = leader;

    MotionMaster* motion = bot->GetMotionMaster();
    if (_followGuid == target->GetGUID() && motion->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE &&
        std::fabs(_followDistance - slot.distance) < 0.3f && std::fabs(_followAngle - slot.angle) < 0.15f)
        return;

    motion->Clear();
    motion->MoveFollow(target, slot.distance, slot.angle);
    _followGuid = target->GetGUID();
    _followDistance = slot.distance;
    _followAngle = slot.angle;
}

// "formation [name]": shows or sets the follow formation (SkyFire's role slots, mod-playerbots' shapes)
bool PlayerbotAI::FormationOrder(Player* bot, OrderSender const& sender, std::string const& args)
{
    if (args.empty())
    {
        Say(bot, sender.guid, std::string("Formation: ") + BotFormation::ShapeName(_formation) + ". Options: " + BotFormation::ShapeList() + ".");
        return true;
    }

    BotFormation::Shape shape;
    if (!BotFormation::ShapeFromName(args, shape))
    {
        Say(bot, sender.guid, "Unknown formation '" + args + "'. Options: " + BotFormation::ShapeList() + ".");
        return true;
    }

    _formation = shape;
    _followGuid.Clear();                                    // take the new slot at the next check
    _followTimer = FollowCheckInterval;
    Say(bot, sender.guid, std::string("Formation set to ") + BotFormation::ShapeName(shape) + ".");
    return true;
}

void PlayerbotAI::StopFollowing(Player* bot)
{
    if (_followGuid.IsEmpty())
        return;

    _followGuid.Clear();
    bot->GetMotionMaster()->Clear();
    bot->GetMotionMaster()->MoveIdle();
}

void PlayerbotAI::HandleWhisper(Player* bot, OrderSender const& sender, std::string const& text)
{
    if (!IsMaster(sender.guid, sender.accountId))
        return;

    if (!HandleOrder(bot, sender, text))
        Say(bot, sender.guid, "Orders I know: follow, stay, summon, leave, attack, passive, assist [full|defend|passive], formation [name], co ?|+name|-name, nc ?|+name|-name, player tank, bot tank, talents [spec], autogear, maintenance, quests [all|in|co|summary|available], quest take <name>, quest complete <name>.");
}

void PlayerbotAI::HandlePartyChat(Player* bot, OrderSender const& sender, ObjectGuid const& group, std::string const& text)
{
    if (!IsMaster(sender.guid, sender.accountId))
        return;

    // the bot may have left that group since the message was sent; in a battleground the bot's group is the
    // battleground raid while /party still reaches its original group (owner: every kind of group chat)
    Group* botGroup = bot->GetGroup();
    Group* original = bot->GetOriginalGroup();
    if ((!botGroup || botGroup->GetGUID() != group) && (!original || original->GetGUID() != group))
        return;

    HandleOrder(bot, sender, text);
}

bool PlayerbotAI::HandleOrder(Player* bot, OrderSender const& sender, std::string const& text)
{
    std::string const order = Lower(text);
    if (order == "co" || order.rfind("co ", 0) == 0)
        return StrategyOrder(bot, sender, true, order.size() > 2 ? order.substr(3) : std::string());
    if (order == "nc" || order.rfind("nc ", 0) == 0)
        return StrategyOrder(bot, sender, false, order.size() > 2 ? order.substr(3) : std::string());

    if (order == "follow")
    {
        _nc.insert("follow");
        ApplyFlags();                                       // owner's rule: follow does not end a fight
        _followGuid.Clear();                                // follow again at the next check
        _followTimer = FollowCheckInterval;
        Say(bot, sender.guid, bot->GetGroup() ? "Following." : "Following once I am in your group.");
        return true;
    }

    if (order == "stay")
    {
        _nc.erase("follow");
        ApplyFlags();
        _healMoveGuid.Clear();
        _followGuid.Clear();
        bot->GetMotionMaster()->Clear();
        bot->GetMotionMaster()->MoveIdle();
        Say(bot, sender.guid, "Staying here.");
        return true;
    }

    if (order == "summon")
    {
        Summon(bot, sender);
        return true;
    }

    if (order == "attack")
    {
        AttackOrder(bot, sender);
        return true;
    }

    if (order == "autogear")
    {
        AutoGearOrder(bot, sender);
        return true;
    }

    if (order == "maintenance")
    {
        // an order, so it may change an alt (owner's alt rule); an alt's enchants are only added, never replaced
        if (bot->isInCombat())
            Say(bot, sender.guid, "Not in combat.");
        else
            Say(bot, sender.guid, BotMaintenance::Maintain(bot, _randomBot, sPlayerbotMgr->GetGearOptions().bagEntry));
        return true;
    }
    if (order == "talents" || order.rfind("talents ", 0) == 0)
    {
        TalentsOrder(bot, sender, order.size() > 8 ? text.substr(8) : std::string());
        return true;
    }

    if (order == "player tank")
    {
        _co.erase("tank");
        ApplyFlags();
        Say(bot, sender.guid, BotRotations::RoleOf(bot) == BotRotations::CombatRole::Tank ?
            "You tank: I won't taunt, I fight with you." : "You tank (only matters to tank bots).");
        return true;
    }
    if (order == "bot tank")
    {
        _co.insert("tank");
        ApplyFlags();
        Say(bot, sender.guid, BotRotations::RoleOf(bot) == BotRotations::CombatRole::Tank ?
            "I tank: I taunt whatever attacks the group." : "Bot tanks tank again.");
        return true;
    }

    if (order == "formation")
        return FormationOrder(bot, sender, "");
    if (order.rfind("formation ", 0) == 0)
        return FormationOrder(bot, sender, order.substr(10));

    if (order == "passive")
        return AssistOrder(bot, sender, "passive");
    if (order == "assist")
        return AssistOrder(bot, sender, "");
    if (order.rfind("assist ", 0) == 0)
        return AssistOrder(bot, sender, order.substr(7));

    if (order == "leave")
    {
        LeaveGroup(bot, sender);
        return true;
    }

    if (order == "quests available")
    {
        ListAvailableQuests(bot, sender.guid);
        return true;
    }

    // the name keeps its case for the replies; matching is done in lower case
    if (order.rfind("quest take ", 0) == 0)
    {
        TakeQuest(bot, sender.guid, text.substr(11));
        return true;
    }
    if (order.rfind("quest complete ", 0) == 0)
    {
        CompleteQuestOrder(bot, sender.guid, text.substr(15));
        return true;
    }

    if (order == "quests")
        return ListQuests(bot, sender.guid, "summary");
    if (order.rfind("quests ", 0) == 0)
        return ListQuests(bot, sender.guid, order.substr(7));

    return false;
}

// Teleports the bot to where the owner stood when he gave the order, on any map; in an instance, the
// map picks the instance of the bot's group, as for any member entering.
// Owner's rule (2026-11-03): a dead bot is revived only by "summon" in party chat, and only while the
// owner is out of combat unless Playerbots.Summon.Revive = 2. Bots are never revived on their own.
void PlayerbotAI::Summon(Player* bot, OrderSender const& sender)
{
    if (!bot->IsAlive())
    {
        PlayerbotMgr::SummonRevive const revive = sPlayerbotMgr->GetSummonRevive();
        if (revive == PlayerbotMgr::SummonRevive::Never)
        {
            Say(bot, sender.guid, "I can't come, I'm dead.");
            return;
        }
        if (!sender.partyChat)
        {
            Say(bot, sender.guid, "I'm dead. Say summon in party chat to revive me.");
            return;
        }
        if (sender.inCombat && revive != PlayerbotMgr::SummonRevive::Always)
        {
            Say(bot, sender.guid, "I'm dead. Summon me again once you are out of combat.");
            return;
        }

        bot->ResurrectPlayer(1.0f);
        bot->SpawnCorpseBones();
        TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' revived by its owner's summon.", bot->GetName());
    }

    if (bot->isInFlight())
    {
        Say(bot, sender.guid, "I can't come, I'm on a flight.");
        return;
    }

    _nc.insert("follow");                                   // a summoned bot follows again
    ApplyFlags();
    _followGuid.Clear();
    bot->GetMotionMaster()->Clear();
    if (!bot->TeleportTo(sender.location))
    {
        Say(bot, sender.guid, "I can't get there.");
        return;
    }
    Say(bot, sender.guid, "Coming.");
}

// as a client leaving the group: CMSG_LEAVE_GROUP (LeaveGroup::Read: int8 PartyIndex)
void PlayerbotAI::LeaveGroup(Player* bot, OrderSender const& sender)
{
    if (!bot->GetGroup())
    {
        Say(bot, sender.guid, "I'm not in a group.");
        return;
    }

    WorldPacket* packet = new WorldPacket(CMSG_LEAVE_GROUP, 1);
    *packet << int8(0);
    bot->GetSession()->QueuePacket(packet);

    _nc.insert("follow");
    ApplyFlags();
    _leftByOrder = true;                                    // leaving by order keeps the bot online
    StopFollowing(bot);
    Say(bot, sender.guid, "Leaving the group.");
}

bool PlayerbotAI::ListQuests(Player* bot, ObjectGuid const& to, std::string const& args)
{
    bool showIncomplete = false;
    bool showComplete = false;
    if (args == "all")
        showIncomplete = showComplete = true;
    else if (args == "completed" || args == "co")
        showComplete = true;
    else if (args == "incompleted" || args == "in")
        showIncomplete = true;
    else if (args != "summary")
        return false;

    // a quest in progress also gets its objectives, "3/8 Kobold Vermin"
    auto questLine = [bot](Quest const* quest, bool complete) -> std::string
    {
        std::string line = "[" + quest->LogTitle + "]";
        if (complete)
            return line;

        std::string objectives;
        for (QuestObjective const& objective : quest->GetObjectives())
        {
            if (objective.Amount <= 0 || (objective.Flags & QUEST_OBJECTIVE_FLAG_HIDDEN))
                continue;

            int32 const amount = objective.Amount;
            int32 done = objective.Type == QUEST_OBJECTIVE_ITEM ? int32(bot->GetItemCount(objective.ObjectID))
                : bot->GetQuestObjectiveData(quest, objective.StorageIndex);
            done = std::max(0, std::min(done, amount));

            std::string label = objective.Description;
            if (label.empty())
            {
                if (objective.Type == QUEST_OBJECTIVE_MONSTER)
                {
                    if (CreatureTemplate const* creature = sObjectMgr->GetCreatureTemplate(objective.ObjectID))
                        label = creature->Name[0];
                }
                else if (objective.Type == QUEST_OBJECTIVE_GAMEOBJECT)
                {
                    if (GameObjectTemplate const* go = sObjectMgr->GetGameObjectTemplate(objective.ObjectID))
                        label = go->name;
                }
            }
            if (label.empty())
                label = "objective";

            std::string const piece = std::to_string(done) + "/" + std::to_string(amount) + " " + label;
            // the client cuts whispers past ~255 characters
            if (line.size() + objectives.size() + piece.size() > 230)
                break;
            objectives += objectives.empty() ? " - " : ", ";
            objectives += piece;
        }
        return line + objectives;
    };

    uint32 incomplete = 0;
    uint32 complete = 0;
    for (int pass = 0; pass < 2; ++pass)
    {
        bool const completedPass = pass == 1;
        bool const show = completedPass ? showComplete : showIncomplete;
        if (show)
            Say(bot, to, completedPass ? "--- Completed quests ---" : "--- Incompleted quests ---");

        for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        {
            uint32 const questId = bot->GetQuestSlotQuestId(slot);
            Quest const* quest = questId ? sQuestDataStore->GetQuestTemplate(questId) : nullptr;
            if (!quest)
                continue;

            bool const isComplete = bot->GetQuestStatus(questId) == QUEST_STATUS_COMPLETE;
            if (isComplete != completedPass)
                continue;

            ++(isComplete ? complete : incomplete);
            if (show)
                Say(bot, to, questLine(quest, isComplete));
        }
    }

    Say(bot, to, "Quests: " + std::to_string(incomplete + complete) + " / " + std::to_string(MAX_QUEST_LOG_SIZE)
        + " (in progress: " + std::to_string(incomplete) + ", completed: " + std::to_string(complete) + ")");
    return true;
}

void PlayerbotAI::Say(Player* bot, ObjectGuid const& to, std::string const& text)
{
    // a self-bot talks to its own player as system messages
    if (_selfBot && to == bot->GetGUID())
    {
        ChatHandler(bot->GetSession()).SendSysMessage(text.c_str());
        return;
    }
    // Player::Whisper by guid reaches the receiver wherever he stands
    bot->Whisper(text, LANG_UNIVERSAL, to);
}

bool PlayerbotAI::InOwnersGroup(Player* bot, ObjectGuid const& group) const
{
    Group* botGroup = bot->GetGroup();
    return botGroup && botGroup->GetGUID() == group;
}

namespace
{
    // every mirrored quest step is logged: a bot skipping a quest must be explainable from the console
    void LogQuest(Player* bot, uint32 questId, char const* what)
    {
        TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s', quest %u: %s", bot->GetName(), questId, what);
    }

    // Many quests exist once per class (or race): the Northshire starting quests, for instance, are nine
    // quests titled "Beating Them Back!", each limited to one class by AllowableClasses and leading to that
    // class's next quest. A bot of another class than its owner takes its own copy: the quest with the same
    // title in the same zone (QuestSortID) that its class and race can take.
    typedef std::map<std::pair<int32, std::string>, std::vector<uint32>> QuestCopyIndex;

    QuestCopyIndex const& GetQuestCopyIndex()
    {
        static QuestCopyIndex index;
        static std::once_flag built;
        std::call_once(built, []()
        {
            for (auto const& pair : sQuestDataStore->GetQuestTemplates())
                if (Quest const* quest = pair.second)
                    if (!quest->LogTitle.empty())
                        index[std::make_pair(quest->QuestSortID, quest->LogTitle)].push_back(quest->GetQuestId());
        });
        return index;
    }

    std::vector<uint32> const* GetQuestCopies(Quest const* quest)
    {
        QuestCopyIndex const& index = GetQuestCopyIndex();
        auto itr = index.find(std::make_pair(quest->QuestSortID, quest->LogTitle));
        return itr != index.end() && itr->second.size() > 1 ? &itr->second : nullptr;
    }

    // The bot's own copy of a quest it cannot take. Copies are told apart by class (AllowableClasses) or
    // only by their previous quest: Northshire's "They Sent Assassins" is nine quests, eight without a
    // class, each following one class's "Join the Battle!". So the copy the bot can take now comes first
    // (its previous quest done), a class-specific one before an unrestricted one; failing that, the same
    // order among those its class and race allow, so the refusal names the real reason.
    Quest const* FindCopyToTake(Player* bot, Quest const* quest)
    {
        std::vector<uint32> const* copies = GetQuestCopies(quest);
        if (!copies)
            return nullptr;

        Quest const* best = nullptr;
        int bestRank = -1;
        for (uint32 id : *copies)
        {
            Quest const* copy = id != quest->GetQuestId() ? sQuestDataStore->GetQuestTemplate(id) : nullptr;
            if (!copy || !bot->SatisfyQuestClass(copy, false) || !bot->SatisfyQuestRace(copy, false))
                continue;
            int const rank = (bot->CanTakeQuest(copy, false) ? 2 : 0) + (copy->AllowableClasses ? 1 : 0);
            if (rank > bestRank)
            {
                best = copy;
                bestRank = rank;
            }
        }
        return best;
    }

    // the copy of the owner's quest that the bot has in its log
    Quest const* FindCopyInLog(Player* bot, Quest const* quest)
    {
        std::vector<uint32> const* copies = GetQuestCopies(quest);
        if (!copies)
            return nullptr;

        for (uint32 id : *copies)
        {
            if (id == quest->GetQuestId())
                continue;
            QuestStatus const status = bot->GetQuestStatus(id);
            if ((status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_COMPLETE) && !bot->GetQuestRewardStatus(id))
                return sQuestDataStore->GetQuestTemplate(id);
        }
        return nullptr;
    }
}

void PlayerbotAI::MirrorQuestAccept(Player* bot, ObjectGuid const& owner, ObjectGuid const& group, uint32 questId)
{
    Quest const* quest = sQuestDataStore->GetQuestTemplate(questId);
    if (!quest)
        return;

    if (!InOwnersGroup(bot, group))
    {
        LogQuest(bot, questId, "accept skipped, not in the owner's group");
        return;
    }

    // the owner's copy is for another class or race: the bot takes its own copy of the same quest
    if (!bot->SatisfyQuestClass(quest, false) || !bot->SatisfyQuestRace(quest, false))
    {
        Quest const* copy = FindCopyToTake(bot, quest);
        if (!copy)
        {
            LogQuest(bot, questId, "accept skipped, another class or race and no copy for the bot's");
            return;
        }

        TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s', quest %u: takes its own class/race copy, quest %u",
            bot->GetName(), questId, copy->GetQuestId());
        quest = copy;
        questId = copy->GetQuestId();
    }

    // already on it or done with it, or a quest of another class / race: nothing to say
    QuestStatus const status = bot->GetQuestStatus(questId);
    if (status != QUEST_STATUS_NONE || bot->GetQuestRewardStatus(questId))
    {
        TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s', quest %u: accept skipped, already has it (status %u, rewarded %u)",
            bot->GetName(), questId, uint32(status), uint32(bot->GetQuestRewardStatus(questId)));
        return;
    }
    if (!bot->SatisfyQuestClass(quest, false) || !bot->SatisfyQuestRace(quest, false))
    {
        LogQuest(bot, questId, "accept skipped, another class or race");
        return;
    }

    if (bot->CanTakeQuest(quest, false) && bot->CanAddQuest(quest, false))
    {
        // no quest giver: the giver's script hooks stay with the owner's own accept
        bot->AddQuestAndCheckCompletion(quest, nullptr);
        TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s', quest %u: accepted with its owner (status now %u)",
            bot->GetName(), questId, uint32(bot->GetQuestStatus(questId)));
        return;
    }

    // tell the owner, so a bot falling behind on a chain is not a mystery
    char const* reason = "I can't take it";
    if (bot->FindQuestSlot(0) >= MAX_QUEST_LOG_SIZE)
        reason = "my quest log is full";
    else if (!bot->SatisfyQuestLevel(quest, false))
        reason = "my level is too low";
    else if (!bot->SatisfyQuestPreviousQuest(quest, false))
        reason = "I haven't done the quest before it";
    else if (!bot->CanAddQuest(quest, false))
        reason = "no room in my bags for its item";
    LogQuest(bot, questId, (std::string("accept refused: ") + reason).c_str());
    Say(bot, owner, "Can't take [" + quest->LogTitle + "]: " + reason + ".");
}

void PlayerbotAI::MirrorQuestReward(Player* bot, ObjectGuid const& owner, ObjectGuid const& group, uint32 questId)
{
    Quest const* quest = sQuestDataStore->GetQuestTemplate(questId);
    if (!quest)
        return;

    if (!InOwnersGroup(bot, group))
    {
        LogQuest(bot, questId, "turn-in skipped, not in the owner's group");
        return;
    }

    QuestStatus status = bot->GetQuestStatus(questId);
    if ((status != QUEST_STATUS_INCOMPLETE && status != QUEST_STATUS_COMPLETE) || bot->GetQuestRewardStatus(questId))
    {
        // the bot may have its own class/race copy of the owner's quest
        Quest const* copy = FindCopyInLog(bot, quest);
        if (!copy)
        {
            TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s', quest %u: turn-in skipped, not in its log (status %u, rewarded %u)",
                bot->GetName(), questId, uint32(status), uint32(bot->GetQuestRewardStatus(questId)));
            return;
        }

        TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s', quest %u: turns in its own class/race copy, quest %u",
            bot->GetName(), questId, copy->GetQuestId());
        quest = copy;
        questId = copy->GetQuestId();
        status = bot->GetQuestStatus(questId);
    }

    TurnInQuest(bot, quest, nullptr, owner);
}

bool PlayerbotAI::TurnInQuest(Player* bot, Quest const* quest, WorldObject* giver, ObjectGuid const& to)
{
    uint32 const questId = quest->GetQuestId();
    if (!bot->SatisfyQuestDay(quest, false) || !bot->SatisfyQuestWeek(quest) || !bot->SatisfyQuestSeasonal(quest))
    {
        LogQuest(bot, questId, "turn-in refused, daily/weekly/seasonal limit");
        Say(bot, to, "Can't turn in [" + quest->LogTitle + "]: I've reached the limit for this quest.");
        return false;
    }

    uint32 const reward = ChooseQuestReward(bot, quest);
    if (!QuestRewardsFit(bot, quest, reward))
    {
        LogQuest(bot, questId, "turn-in refused, no bag room for the reward");
        Say(bot, to, "Can't turn in [" + quest->LogTitle + "]: no room in my bags for the reward.");
        return false;
    }

    // its own objectives do not matter (owner's rule): mark it complete, then hand it in. A quest that
    // rewards itself on completion is done after CompleteQuest.
    if (bot->GetQuestStatus(questId) != QUEST_STATUS_COMPLETE)
    {
        bot->CompleteQuest(questId);
        if (bot->GetQuestRewardStatus(questId))
        {
            LogQuest(bot, questId, "turned in (rewarded on completion)");
            return true;
        }
    }

    // without a giver (the hook does not name the owner's) the bot stands in, as for a quest turned in
    // from the quest log
    bot->RewardQuest(quest, reward, giver ? static_cast<Object*>(giver) : static_cast<Object*>(bot), true);
    TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s', quest %u: turned in (reward item %u, rewarded %u)",
        bot->GetName(), questId, reward, uint32(bot->GetQuestRewardStatus(questId)));
    return true;
}

uint32 PlayerbotAI::ChooseQuestReward(Player* bot, Quest const* quest) const
{
    std::vector<uint32> candidates;
    for (uint32 i = 0; i < quest->m_rewChoiceItemsCount && i < QUEST_REWARD_CHOICES_COUNT; ++i)
        if (quest->RewardChoiceItemId[i])
            candidates.push_back(quest->RewardChoiceItemId[i]);

    // Legion's class/spec rewards (QuestPackageItem.db2), as the client offers them
    if (quest->PackageID)
    {
        bool selectable = false;
        if (std::vector<QuestPackageItemEntry const*> const* items = sDB2Manager.GetQuestPackageItems(quest->PackageID))
            for (QuestPackageItemEntry const* item : *items)
                if (bot->CanSelectQuestPackageItem(item))
                {
                    candidates.push_back(uint32(item->ItemID));
                    selectable = true;
                }

        if (!selectable)
            if (std::vector<QuestPackageItemEntry const*> const* items = sDB2Manager.GetQuestPackageItemsFallback(quest->PackageID))
                for (QuestPackageItemEntry const* item : *items)
                    candidates.push_back(uint32(item->ItemID));
    }

    // SkyFire's scoring: an item the bot can use, by item level, armor and weapons first, sell price last
    uint32 best = 0;
    int64 bestScore = -1;
    for (uint32 itemId : candidates)
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
        if (!proto)
            continue;

        int64 score = int64(proto->GetSellPrice());
        if (bot->CanUseItem(proto) == EQUIP_ERR_OK)
        {
            score += int64(proto->GetBaseItemLevel()) * 1000;
            if (proto->GetClass() == ITEM_CLASS_WEAPON || proto->GetClass() == ITEM_CLASS_ARMOR)
                score += 5000;
        }

        if (score > bestScore)
        {
            bestScore = score;
            best = itemId;
        }
    }
    return best;
}

// room in the bags for what the quest hands out: the chosen item and the fixed rewards
bool PlayerbotAI::QuestRewardsFit(Player* bot, Quest const* quest, uint32 reward) const
{
    auto fits = [bot](uint32 item, uint32 count)
    {
        if (!item || !count)
            return true;
        ItemPosCountVec dest;
        return bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, item, count) == EQUIP_ERR_OK;
    };

    if (reward)
    {
        uint32 count = 1;
        for (uint32 i = 0; i < quest->m_rewChoiceItemsCount && i < QUEST_REWARD_CHOICES_COUNT; ++i)
            if (quest->RewardChoiceItemId[i] == reward)
                count = quest->RewardChoiceItemCount[i];
        if (!fits(reward, count))
            return false;
    }

    for (uint32 i = 0; i < quest->m_rewItemsCount && i < QUEST_ITEM_COUNT; ++i)
        if (!fits(quest->RewardItemId[i], quest->RewardItemCount[i]))
            return false;

    return true;
}

namespace
{
    float const QuestGiverRange = 30.0f;                    // the owner stands at the NPC, the bot follows him

    // every object in range: creatures and gameobjects whose entry starts or ends quests are kept below
    struct AnyGameObjectInRange
    {
        WorldObject const* me;
        float range;
        bool operator()(GameObject* go) const { return me->IsWithinDistInMap(go, range); }
    };

    struct QuestGiver
    {
        WorldObject* object;
        QuestRelationBounds starts;
        QuestRelationBounds ends;
    };

    // the quest givers around the bot, nearest first
    std::vector<QuestGiver> FindQuestGivers(Player* bot)
    {
        std::vector<QuestGiver> givers;

        std::list<Creature*> creatures;
        bot->GetCreatureListInGrid(creatures, QuestGiverRange);
        for (Creature* creature : creatures)
        {
            if (!creature->IsAlive())
                continue;
            QuestRelationBounds starts = sQuestDataStore->GetCreatureQuestRelationBounds(creature->GetEntry());
            QuestRelationBounds ends = sQuestDataStore->GetCreatureQuestInvolvedRelationBounds(creature->GetEntry());
            if (starts.first != starts.second || ends.first != ends.second)
                givers.push_back({ creature, starts, ends });
        }

        std::list<GameObject*> objects;
        AnyGameObjectInRange check{ bot, QuestGiverRange };
        Trinity::GameObjectListSearcher<AnyGameObjectInRange> searcher(bot, objects, check);
        CellCoord pair(Trinity::ComputeCellCoord(bot->GetPositionX(), bot->GetPositionY()));
        Cell cell(pair);
        cell.SetNoCreate();
        cell.Visit(pair, Trinity::makeGridVisitor(searcher), *bot->GetMap(), *bot, QuestGiverRange);
        for (GameObject* go : objects)
        {
            QuestRelationBounds starts = sQuestDataStore->GetGOQuestRelationBounds(go->GetEntry());
            QuestRelationBounds ends = sQuestDataStore->GetGOQuestInvolvedRelationBounds(go->GetEntry());
            if (starts.first != starts.second || ends.first != ends.second)
                givers.push_back({ go, starts, ends });
        }

        std::sort(givers.begin(), givers.end(), [bot](QuestGiver const& a, QuestGiver const& b)
        {
            return bot->GetDistance(a.object) < bot->GetDistance(b.object);
        });
        return givers;
    }

    // "Encrypted Letter", a quest link pasted in chat ("|Hquest:...|h[Encrypted Letter]|h|r") or a quest id
    std::string QuestNameFrom(std::string text)
    {
        std::string::size_type open = text.find('[');
        std::string::size_type close = text.find(']', open == std::string::npos ? 0 : open);
        if (open != std::string::npos && close != std::string::npos)
            text = text.substr(open + 1, close - open - 1);
        return Lower(text);
    }

    // Exact title (or id) before a title containing the name. Several quests may share a title (class
    // copies, all offered by the same NPC): `preferred` picks among them, e.g. the one the bot can take.
    template<class Candidates, class Preferred>
    Quest const* MatchQuest(Candidates const& questIds, std::string const& name, Preferred preferred)
    {
        Quest const* best = nullptr;
        int bestRank = -1;
        for (uint32 id : questIds)
        {
            Quest const* quest = sQuestDataStore->GetQuestTemplate(id);
            if (!quest)
                continue;

            std::string const title = Lower(quest->LogTitle);
            int rank;
            if (std::to_string(id) == name)
                rank = 4;                                   // a quest id names exactly one quest
            else if (title == name)
                rank = 2;
            else if (!name.empty() && title.find(name) != std::string::npos)
                rank = 0;
            else
                continue;

            if (rank < 4 && preferred(quest))
                ++rank;
            if (rank > bestRank)
            {
                best = quest;
                bestRank = rank;
            }
        }
        return best;
    }

    std::string GiverName(WorldObject* object)
    {
        return object->GetName();
    }
}

// quests available: the quests the nearest quest giver offers that the bot can take, and the ones of
// its log that the giver takes back
void PlayerbotAI::ListAvailableQuests(Player* bot, ObjectGuid const& to)
{
    std::vector<QuestGiver> const givers = FindQuestGivers(bot);
    if (givers.empty())
    {
        Say(bot, to, "There is no quest giver near me.");
        return;
    }

    QuestGiver const& giver = givers.front();
    std::string available;
    for (auto itr = giver.starts.first; itr != giver.starts.second; ++itr)
    {
        Quest const* quest = sQuestDataStore->GetQuestTemplate(itr->second);
        if (!quest || bot->GetQuestStatus(quest->GetQuestId()) != QUEST_STATUS_NONE || !bot->CanTakeQuest(quest, false))
            continue;
        available += (available.empty() ? "[" : ", [") + quest->LogTitle + "]";
    }

    std::string turnIn;
    for (auto itr = giver.ends.first; itr != giver.ends.second; ++itr)
    {
        QuestStatus const status = bot->GetQuestStatus(itr->second);
        if (status != QUEST_STATUS_INCOMPLETE && status != QUEST_STATUS_COMPLETE)
            continue;
        if (Quest const* quest = sQuestDataStore->GetQuestTemplate(itr->second))
            turnIn += (turnIn.empty() ? "[" : ", [") + quest->LogTitle + "]";
    }

    Say(bot, to, GiverName(giver.object) + ": " + (available.empty() ? std::string("no quest I can take") : "I can take " + available)
        + (turnIn.empty() ? std::string(".") : "; I can turn in " + turnIn + "."));
}

// quest take <name>: from a quest giver near the bot, as a client accepting it there
void PlayerbotAI::TakeQuest(Player* bot, ObjectGuid const& to, std::string const& text)
{
    std::string const name = QuestNameFrom(text);
    for (QuestGiver const& giver : FindQuestGivers(bot))
    {
        std::vector<uint32> offered;
        for (auto itr = giver.starts.first; itr != giver.starts.second; ++itr)
            offered.push_back(itr->second);

        // among copies of the same title the giver offers, the one the bot can take now
        Quest const* quest = MatchQuest(offered, name, [bot](Quest const* q)
        {
            return bot->GetQuestStatus(q->GetQuestId()) == QUEST_STATUS_NONE && bot->CanTakeQuest(q, false);
        });
        if (!quest)
            continue;

        uint32 const questId = quest->GetQuestId();
        if (bot->GetQuestStatus(questId) != QUEST_STATUS_NONE || bot->GetQuestRewardStatus(questId))
        {
            Say(bot, to, "I already have or did [" + quest->LogTitle + "].");
            return;
        }

        if (!bot->CanTakeQuest(quest, false) || !bot->CanAddQuest(quest, false))
        {
            char const* reason = "I can't take it";
            if (bot->FindQuestSlot(0) >= MAX_QUEST_LOG_SIZE)
                reason = "my quest log is full";
            else if (!bot->SatisfyQuestClass(quest, false) || !bot->SatisfyQuestRace(quest, false))
                reason = "it is not for my class or race";
            else if (!bot->SatisfyQuestLevel(quest, false))
                reason = "my level is too low";
            else if (!bot->SatisfyQuestPreviousQuest(quest, false))
                reason = "I haven't done the quest before it";
            else if (!bot->CanAddQuest(quest, false))
                reason = "no room in my bags for its item";
            LogQuest(bot, questId, (std::string("manual take refused: ") + reason).c_str());
            Say(bot, to, "Can't take [" + quest->LogTitle + "]: " + reason + ".");
            return;
        }

        // with its giver, as a client: the giver's scripts run for the bot's own accept
        bot->AddQuestAndCheckCompletion(quest, giver.object);
        LogQuest(bot, questId, "taken by order");
        Say(bot, to, "Took [" + quest->LogTitle + "].");
        return;
    }

    Say(bot, to, "No quest giver near me offers \"" + text + "\". Try: quests available");
}

// quest complete <name>: turns a quest of its log in to its turn-in NPC or object near the bot
void PlayerbotAI::CompleteQuestOrder(Player* bot, ObjectGuid const& to, std::string const& text)
{
    std::string const name = QuestNameFrom(text);
    std::vector<uint32> inLog;
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        if (uint32 questId = bot->GetQuestSlotQuestId(slot))
            inLog.push_back(questId);

    Quest const* quest = MatchQuest(inLog, name, [](Quest const*) { return false; });
    if (!quest)
    {
        Say(bot, to, "I have no quest \"" + text + "\". Try: quests all");
        return;
    }

    for (QuestGiver const& giver : FindQuestGivers(bot))
        for (auto itr = giver.ends.first; itr != giver.ends.second; ++itr)
            if (itr->second == quest->GetQuestId())
            {
                if (TurnInQuest(bot, quest, giver.object, to))
                    Say(bot, to, "Turned in [" + quest->LogTitle + "].");
                return;
            }

    Say(bot, to, "I'm not near the one who takes [" + quest->LogTitle + "] back.");
}

// ---- Combat (plan steps 4.1 and 4.2) ----------------------------------------------------------------
// From LegionBotAI (targeting rules) and AshamaneCore-Legion735 (rotations), see the core's CLAUDE.md.

Player* PlayerbotAI::FindOwner(std::vector<Player*> const& members) const
{
    for (Player* member : members)
    {
        WorldSession* session = member->GetSession();
        if (session && !session->IsBot() && IsMaster(member->GetGUID(), session->GetAccountId()))
            return member;
    }
    return nullptr;
}

void PlayerbotAI::UpdateCombat(Player* bot)
{
    if (!bot->IsAlive())
    {
        if (_engaged)
            StopCombat(bot, "dead");
        _orderedTarget.Clear();
        _healMoveGuid.Clear();
        return;
    }

    std::vector<Player*> const members = BotCombat::GroupMembersOnMap(bot);
    BotRotations::CombatRole const role = BotRotations::RoleOf(bot);

    bool ordered = false;
    Unit* enemy = SelectTarget(bot, members, ordered);

    // owner's rule (build #24): following is an out-of-combat tactic; while the group fights, nobody follows
    _groupInFight = enemy != nullptr || std::any_of(members.begin(), members.end(), [](Player* m) { return m->isInCombat(); });

    // tanks (plan step 4.4): an enemy hitting someone else is theirs at once, no engage delay (it is
    // already in the fight); unless the owner tanks ("player tank") or the bot is passive
    Unit* loose = nullptr;
    if (!ordered && role == BotRotations::CombatRole::Tank && !_playerTank && _assist != AssistMode::Passive)
        loose = FindLooseEnemy(bot, members);
    if (loose)
        enemy = loose;

    if (enemy && !ordered && !loose && !_engageTimer.Ready(enemy->GetGUID(), getMSTime(), sPlayerbotMgr->GetEngageDelay()))
    {
        // LegionBotAI's engage delay: the player swings first; a fight already going on goes on
        Unit* current = bot->getVictim();
        enemy = (_engaged && current && current->IsAlive()) ? current : nullptr;
    }

    if (!_selfBot)                                          // the player sits down himself
    {
        UpdateRest(bot, members, enemy);
        if (_resting)
            return;
    }

    if (enemy)
        Engage(bot, enemy, role);
    else if (_engaged)
        StopCombat(bot, "no enemy left");

    if (bot->IsNonMeleeSpellCast(false))
        return;

    if (bot->isInCombat() && TrySurvival(bot, enemy))
        return;

    if (loose && TryTaunt(bot, loose))
        return;

    RunRotation(bot, members, role, enemy);
}

// LegionBotAI's tank: the first enemy hitting an ally other than the bot (the owner's first) within reach.
// Never a pull: it must already be attacking one of them.
Unit* PlayerbotAI::FindLooseEnemy(Player* bot, std::vector<Player*> const& members) const
{
    float const range = sPlayerbotMgr->GetEngageRange();
    Player* owner = FindOwner(members);
    std::vector<Player*> allies;
    if (owner && owner != bot)
        allies.push_back(owner);
    for (Player* member : members)
        if (member != bot && member != owner)
            allies.push_back(member);

    for (Player* ally : allies)
    {
        if (!ally->IsAlive())
            continue;
        for (Unit* attacker : *ally->getAttackers())
            if (attacker && attacker->IsInWorld() && attacker->IsAlive() && attacker->getVictim() == ally &&
                attacker->GetMap() == bot->GetMap() && bot->IsValidAttackTarget(attacker) &&
                bot->GetDistance(attacker) <= range)
                return attacker;
    }
    return nullptr;
}

// the spec's taunt on an enemy hitting someone else, its second taunt when the first is not ready
bool PlayerbotAI::TryTaunt(Player* bot, Unit* enemy)
{
    BotRotations::SpecRotation const* rotation = BotRotations::GetRotation(bot->GetSpecializationId());
    if (!rotation || enemy->getVictim() == bot)
        return false;

    for (char const* name : { rotation->taunt, rotation->tauntAlt })
    {
        uint32 const spellId = BotRotations::KnownSpell(bot, name, rotation->family);
        if (!spellId)
            continue;
        BotCombat::CastOutcome const outcome = BotCombat::BotCast(bot, enemy, spellId, _throttle);
        if (outcome)
        {
            if (sPlayerbotMgr->GetCombatLog() >= 1)
                TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' taunts %s off %s (%s).", bot->GetName(),
                    enemy->GetName(), enemy->getVictim() ? enemy->getVictim()->GetName() : "?", name);
            return true;
        }
    }
    return false;
}

// LegionBotAI's targeting: a bot never pulls. It fights an ordered target, or a unit attacking the group
// (assist full) / the owner or the bot (assist defend), or, in any mode, whatever attacks the bot.
Unit* PlayerbotAI::SelectTarget(Player* bot, std::vector<Player*> const& members, bool& ordered)
{
    ordered = false;
    float const range = sPlayerbotMgr->GetEngageRange();

    if (!_orderedTarget.IsEmpty())
    {
        Unit* target = ObjectAccessor::GetUnit(*bot, _orderedTarget);
        if (target && target->IsInWorld() && target->IsAlive() && bot->IsValidAttackTarget(target))
        {
            ordered = true;
            return target;
        }
        _orderedTarget.Clear();
    }

    // a self-bot fights the player's own target, as an ordered one: hostile, or already in a fight
    if (_selfBot)
    {
        Unit* target = ObjectAccessor::GetUnit(*bot, bot->GetSelection());
        if (target && target != bot && target->IsInWorld() && target->IsAlive() && bot->IsValidAttackTarget(target) &&
            (bot->IsHostileTo(target) || target->isInCombat()))
        {
            ordered = true;
            return target;
        }
    }

    auto usable = [bot, range](Unit* unit)
    {
        return unit && unit->IsInWorld() && unit->IsAlive() && unit->GetMap() == bot->GetMap() &&
            bot->IsValidAttackTarget(unit) && bot->GetDistance(unit) <= range;
    };

    // battlegrounds and arenas (AzerothCore's "enemy player target"): the never-pull rule is for creatures; an enemy
    // player in reach is fought at once, the current one kept while it stays in reach. A flag carrier does not stop
    // to fight: it runs home.
    if (!_selfBot && _assist != AssistMode::Passive && (bot->InBattleground() || bot->InArena()))
    {
        if (BotBattleground::CarriesFlag(bot))
            return nullptr;
        Unit* current = bot->getVictim();
        if (current && current->IsPlayer() && current->IsInWorld() && current->IsAlive() && current->GetMap() == bot->GetMap() &&
            bot->IsValidAttackTarget(current) && bot->GetDistance(current) <= BattlegroundRange + 5.0f)
        {
            ordered = true;
            return current;
        }
        if (Unit* enemy = BotBattleground::SelectEnemy(bot, BattlegroundRange))
        {
            ordered = true;                                 // no engage delay against players
            return enemy;
        }
    }

    Player* owner = FindOwner(members);
    std::vector<Player*> allies;                            // the owner first: his fight is the one to join
    if (owner)
        allies.push_back(owner);
    if (_assist == AssistMode::Full)
    {
        for (Player* member : members)
            if (member != owner)
                allies.push_back(member);
    }
    else if (_assist == AssistMode::Defend)
        allies.push_back(bot);

    if (_assist != AssistMode::Passive)
    {
        Unit* current = bot->getVictim();
        if (usable(current) && BotCombat::IsAggressorOf(current, allies))
            return current;

        if (owner)
        {
            Unit* ownerTarget = owner->getVictim();
            if (usable(ownerTarget) && BotCombat::IsAggressorOf(ownerTarget, allies))
                return ownerTarget;
        }

        if (Unit* attacker = BotCombat::FindAttackerOf(bot, allies, range))
            return attacker;
    }
    else
        return nullptr;                                     // passive: not even self-defence

    // self-defence, in every mode
    Unit* attacker = bot->getAttackerForHelper();
    if (usable(attacker))
        return attacker;

    // co aggressive (owner's request): out of combat only, a new fight with a nearby enemy; in combat the bot
    // helps with what already attacks the group (above) instead of pulling more
    if (_aggressive)
    {
        bool fighting = bot->isInCombat();
        for (Player* member : members)
            if (member->isInCombat())
                fighting = true;
        if (!fighting)
            if (Unit* pull = FindPullTarget(bot))
            {
                ordered = true;                             // no engage delay: the bot starts it
                return pull;
            }
        if (fighting)
            _pullTarget.Clear();
    }
    return nullptr;
}

void PlayerbotAI::Engage(Player* bot, Unit* enemy, BotRotations::CombatRole role)
{
    // healers stay with the group and heal; their rotation's enemy steps still use this enemy
    if (role == BotRotations::CombatRole::Healer)
        return;

    bool const melee = role != BotRotations::CombatRole::Ranged;
    bool const newTarget = _combatTarget != enemy->GetGUID();
    if (newTarget)
    {
        if (sPlayerbotMgr->GetCombatLog() >= 1)
            TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' (%s) engages %s.", bot->GetName(),
                BotRotations::RoleName(role), enemy->GetName());
        _lastSkip.clear();
    }

    _combatTarget = enemy->GetGUID();
    _engaged = true;
    _followGuid.Clear();
    if (bot->HasAuraType(SPELL_AURA_MOUNTED))
        bot->RemoveAurasByType(SPELL_AURA_MOUNTED);

    if (bot->getVictim() != enemy)
        bot->Attack(enemy, melee);

    MotionMaster* motion = bot->GetMotionMaster();
    if (_stay)
    {
        // "stay": fight from where it stands
        if (motion->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
        {
            motion->Clear();
            motion->MoveIdle();
        }
        return;
    }

    if (!melee)
    {
        // ranged: stand still once in range and in sight, chase only to get there; a chase that keeps
        // re-pathing after a moving target leaves the bot "moving", which breaks its casts
        bool const inReach = bot->GetDistance(enemy) <= RangedStopDistance && bot->IsWithinLOSInMap(enemy);
        if (inReach)
        {
            if (motion->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
            {
                motion->Clear();
                motion->MoveIdle();
                bot->StopMoving();
            }
            return;
        }
    }

    if (newTarget || motion->GetCurrentMovementGeneratorType() != CHASE_MOTION_TYPE)
    {
        motion->Clear();
        motion->MoveChase(enemy, melee ? 0.0f : RangedDistance);
    }
}

void PlayerbotAI::StopCombat(Player* bot, char const* reason)
{
    if (sPlayerbotMgr->GetCombatLog() >= 1)
        TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' stops fighting: %s.", bot->GetName(), reason);

    bot->AttackStop();
    if (bot->IsAlive())
    {
        bot->GetMotionMaster()->Clear();
        bot->GetMotionMaster()->MoveIdle();
    }

    _engaged = false;
    _combatTarget.Clear();
    _engageTimer.Reset();
    _lastSkip.clear();
    _followGuid.Clear();                                    // follow again at once
    _followTimer = FollowCheckInterval;
}

// AshamaneCore's ProcessBotCombatAI with LegionBotAI's healer triage (plan step 4.3): an ally below
// CriticalHealthPct is healed before anything else; otherwise interrupt, then dispel, then the first
// rotation step whose spell the bot knows, whose condition holds and that BotCast accepts. One cast per
// check.
void PlayerbotAI::RunRotation(Player* bot, std::vector<Player*> const& members, BotRotations::CombatRole role, Unit* enemy)
{
    BotRotations::SpecRotation const* rotation = BotRotations::GetRotation(bot->GetSpecializationId());
    if (!rotation)
        return;

    uint32 const log = sPlayerbotMgr->GetCombatLog();
    bool needsHealTarget = role == BotRotations::CombatRole::Healer;
    for (BotRotations::RotationStep const& step : rotation->steps)
        if (step.target == BotRotations::StepTarget::HealTarget)
            needsHealTarget = true;
    Player* healTarget = needsHealTarget ? BotCombat::FindLowestHpAlly(bot, members, HealBelowPct, TankFirstBelowPct, HealBelowPct) : nullptr;

    if (role == BotRotations::CombatRole::Healer)
        MoveToHealTarget(bot, healTarget);

    if (!enemy && !healTarget && !rotation->dispel)
        return;

    auto logCast = [&](uint32 spellId, Unit* target, char const* what)
    {
        if (log < 1)
            return;
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' casts %s (%u)%s%s%s.", bot->GetName(),
            spellInfo && spellInfo->SpellName ? spellInfo->SpellName : "?", spellId, target ? " on " : "",
            target ? target->GetName() : "", what);
    };

    // the group's tank on the bot's map (healers keep their beacons / Prayer of Mending on it), else the owner
    Player* owner = FindOwner(members);
    Player* tank = nullptr;
    for (Player* member : members)
        if (member != bot && member->IsAlive() && BotCombat::RoleOf(member) == BotCombat::BotRole::Tank)
        {
            tank = member;
            break;
        }
    if (!tank && owner && owner->IsAlive())
        tank = owner;

    auto runSteps = [&]() -> bool
    {
        Unit* const roleTarget = role == BotRotations::CombatRole::Healer ? static_cast<Unit*>(healTarget) : enemy;
        for (size_t i = 0; i < rotation->steps.size(); ++i)
        {
            BotRotations::RotationStep const& step = rotation->steps[i];
            Unit* target = roleTarget;
            switch (step.target)
            {
                case BotRotations::StepTarget::Enemy:      target = enemy; break;
                case BotRotations::StepTarget::HealTarget: target = healTarget; break;
                // buffs on the tank / owner only while there is something to fight or heal
                case BotRotations::StepTarget::Tank:       target = (enemy || healTarget) ? tank : nullptr; break;
                case BotRotations::StepTarget::Owner:      target = (enemy || healTarget) && owner && owner->IsAlive() ? owner : nullptr; break;
                // defensives: only while fighting
                case BotRotations::StepTarget::Self:       target = (enemy || bot->isInCombat()) ? static_cast<Unit*>(bot) : nullptr; break;
                default: break;
            }
            if (!target)
                continue;

            uint32 const spellId = BotRotations::KnownSpell(bot, step.spell, rotation->family);
            if (!spellId)
            {
                LogSkip(bot, i, step.spell, "not known");
                continue;
            }

            if (!BotRotations::CheckCondition(bot, target, step, members))
            {
                LogSkip(bot, i, step.spell, "condition not met: " + BotRotations::DescribeCondition(step));
                continue;
            }

            BotCombat::CastOutcome const outcome = BotCombat::BotCast(bot, target, spellId, _throttle);
            if (outcome)
            {
                _lastSkip.erase(i);
                logCast(spellId, target, "");
                return true;
            }

            std::string reason = BotCombat::CastRefusalName(outcome.refusal);
            if (outcome.refusal == BotCombat::CastRefusal::CastFailed)
                reason += " (result " + std::to_string(uint32(outcome.result)) + ")";
            LogSkip(bot, i, step.spell, reason);
        }
        return false;
    };

    // triage: a dying ally comes before interrupts and dispels
    bool const critical = healTarget && healTarget->GetHealthPct() < CriticalHealthPct;
    if (critical && runSteps())
        return;

    if (!critical && enemy && rotation->interrupt && BotCombat::GetInterruptibleCast(enemy))
        if (uint32 spellId = BotRotations::KnownSpell(bot, rotation->interrupt, rotation->family))
            if (BotCombat::BotCast(bot, enemy, spellId, _throttle))
            {
                logCast(spellId, enemy, ", interrupt");
                return;
            }

    if (rotation->dispel)
        if (uint32 spellId = BotRotations::KnownSpell(bot, rotation->dispel, rotation->family))
            if (BotCombat::TryDispel(bot, members, spellId, _throttle))
            {
                logCast(spellId, nullptr, ", dispel");
                return;
            }

    if (critical)
        return;

    // the spec's SimC profile (owner, 2026-11-05) instead of the step list; healers keep their lists
    if (enemy && role != BotRotations::CombatRole::Healer && sPlayerbotMgr->UseApl() && BotApl::HasProfile(bot->GetSpecializationId()))
    {
        std::string fired;
        if (BotApl::Run(bot, enemy, _apl, _throttle, BotApl::Normalize(rotation->interrupt ? rotation->interrupt : ""), fired) && log >= 1)
            TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' casts %s on %s.", bot->GetName(),
                fired.size() > 200 ? (fired.substr(0, 200) + "...").c_str() : fired.c_str(), enemy->GetName());
        return;
    }

    runSteps();
}

void PlayerbotAI::UpdatePrecombat(Player* bot, uint32 diff)
{
    _aplPrecombatTimer += diff;
    if (_aplPrecombatTimer < 2000)
        return;
    _aplPrecombatTimer = 0;
    if (!sPlayerbotMgr->UseApl() || !BotApl::HasProfile(bot->GetSpecializationId()) || !bot->IsAlive() || bot->isInCombat() ||
        _groupInFight || _resting || !_lootTarget.IsEmpty() || bot->IsMounted() || bot->IsNonMeleeSpellCast(false))
        return;
    std::string fired;
    if (BotApl::Run(bot, nullptr, _apl, _throttle, std::string(), fired) && sPlayerbotMgr->GetCombatLog() >= 1)
        TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' casts %s (out of combat).", bot->GetName(), fired.c_str());
}

// A healer whose heal target is out of reach (beyond HealRange or out of sight) walks towards it; follow
// waits meanwhile and takes over again once the target is in reach or healed.
void PlayerbotAI::MoveToHealTarget(Player* bot, Player* healTarget)
{
    bool const outOfReach = healTarget && healTarget != bot &&
        (bot->GetDistance(healTarget) > HealRange || !bot->IsWithinLOSInMap(healTarget));

    if (!outOfReach)
    {
        if (!_healMoveGuid.IsEmpty())
        {
            _healMoveGuid.Clear();
            _followGuid.Clear();                            // follow again at the next check
            _followTimer = FollowCheckInterval;
        }
        return;
    }

    if (_stay || bot->IsNonMeleeSpellCast(false))
        return;

    MotionMaster* motion = bot->GetMotionMaster();
    if (_healMoveGuid == healTarget->GetGUID() && motion->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE)
        return;

    _healMoveGuid = healTarget->GetGUID();
    _followGuid.Clear();
    motion->Clear();
    motion->MoveFollow(healTarget, HealApproachDistance, 0.0f);
    if (sPlayerbotMgr->GetCombatLog() >= 2)
        TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' moves to heal %s.", bot->GetName(), healTarget->GetName());
}

// log level 2: why a rotation step was skipped, once per change of reason
void PlayerbotAI::LogSkip(Player* bot, size_t step, char const* spell, std::string const& reason)
{
    if (sPlayerbotMgr->GetCombatLog() < 2)
        return;

    std::string& last = _lastSkip[step];
    if (last == reason)
        return;
    last = reason;
    TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' skips %s: %s.", bot->GetName(), spell, reason.c_str());
}

// "attack": the owner's target, fought until it dies (or "passive"); no engage delay and no never-pull
// check, since the owner chose it
void PlayerbotAI::AttackOrder(Player* bot, OrderSender const& sender)
{
    Unit* target = sender.selection.IsEmpty() ? nullptr : ObjectAccessor::GetUnit(*bot, sender.selection);
    if (!target || !target->IsInWorld() || !target->IsAlive() || !bot->IsValidAttackTarget(target))
    {
        Say(bot, sender.guid, "Target something I can attack first.");
        return;
    }

    _orderedTarget = target->GetGUID();
    Say(bot, sender.guid, std::string("Attacking ") + target->GetName() + ".");
}

bool PlayerbotAI::AssistOrder(Player* bot, OrderSender const& sender, std::string const& mode)
{
    if (mode == "full")
    {
        _co.erase("passive");
        _co.erase("defend");
        ApplyFlags();
    }
    else if (mode == "defend")
        SetStrategy(bot, true, "defend", true);
    else if (mode == "passive")
        SetStrategy(bot, true, "passive", true);            // owner's rule: drops the fight at once
    else if (!mode.empty())
        return false;

    char const* name = _assist == AssistMode::Full ? "full: I fight whatever attacks the group" :
        _assist == AssistMode::Defend ? "defend: I fight only what attacks you or me" :
        "passive: I fight nothing, not even what attacks me, until you order attack or set another mode";
    Say(bot, sender.guid, std::string(mode.empty() ? "Assist mode " : "Assist mode set to ") + name + ".");
    return true;
}

// ---- Created bots, talents and gear ------------------------------------------------------------------

void PlayerbotAI::InitializeNewBot(Player* bot, BotFactory::CreateRequest const& request)
{
    std::string const summary = BotFactory::InitializeNewBot(bot, request);
    Say(bot, request.creator, summary + " Say 'talents' to see or change my spec, 'autogear' after a change.");

    // owner's request: a created bot joins its creator's party
    _inviteCreator = request.creator;
    _inviteAccount = request.creatorAccount;
    _inviteTimer = 3000;
}

// as if the creator typed /invite <bot>: CMSG_PARTY_INVITE queued on his session (PartyInviteClient::Read: int8
// PartyIndex, int32 ProposedRoles, packed guid, 9 bits name length, 9 bits realm length, name, realm), so the
// core's HandlePartyInvite does every check on his thread; the bot then accepts (HandleGroupInvite)
void PlayerbotAI::SendCreatorInvite(Player* bot)
{
    if (Group* group = bot->GetGroup())
        if (group->IsMember(_inviteCreator))
            return;

    WorldSessionPtr session = sWorld->FindSession(_inviteAccount);
    Player* creator = session ? session->GetPlayer() : nullptr;
    if (!creator || creator->GetGUID() != _inviteCreator)
        return;                                             // he logged out or switched character

    std::string const name = bot->GetName();
    WorldPacket* packet = new WorldPacket(CMSG_PARTY_INVITE, 32 + name.size());
    *packet << int8(0);
    *packet << int32(0);
    *packet << bot->GetGUID();
    packet->WriteBits(name.size(), 9);
    packet->WriteBits(0, 9);
    packet->FlushBits();
    packet->WriteString(name);
    session->QueuePacket(packet);
}

// "autogear": the best gear for the current spec within playerbots.conf's limits; replaced pieces go to the
// bags (an order, so it may change an alt)
void PlayerbotAI::AutoGearOrder(Player* bot, OrderSender const& sender)
{
    if (bot->isInCombat())
    {
        Say(bot, sender.guid, "Not in combat.");
        return;
    }
    if (!bot->GetSpecializationId())
    {
        Say(bot, sender.guid, "I have no spec yet.");
        return;
    }

    BotGear::Result const result = BotGear::AutoGear(bot, sPlayerbotMgr->GetGearOptions());
    std::string text = result.equipped ? "Equipped " + std::to_string(result.equipped) + " item(s)" :
        "Nothing better found (" + std::to_string(result.candidates) + " items allowed for me, item level cap " +
        (result.itemLevelCap ? std::to_string(result.itemLevelCap) : std::string("none")) + ")";
    if (result.bagsAdded)
        text = "Added " + std::to_string(result.bagsAdded) + " bag(s). " + text;
    for (size_t i = 0; i < result.pieces.size() && i < 6; ++i)
        text += (i ? ", " : ": ") + result.pieces[i];
    if (result.pieces.size() > 6)
        text += "...";
    if (result.noBagRoom)
        text += ". " + std::to_string(result.noBagRoom) + " upgrade(s) skipped: no room in my bags for the old piece";
    Say(bot, sender.guid, text + ".");
    if (result.equipped || result.bagsAdded)
        bot->SaveToDB();
}

// "talents": spec and talents; "talents <spec>": switch spec (name or 1-4) and fill the talent rows
void PlayerbotAI::TalentsOrder(Player* bot, OrderSender const& sender, std::string const& args)
{
    std::string wanted = args;
    while (!wanted.empty() && wanted.front() == ' ')
        wanted.erase(wanted.begin());

    if (!wanted.empty())
    {
        if (bot->isInCombat())
        {
            Say(bot, sender.guid, "Not in combat.");
            return;
        }
        uint32 const spec = BotFactory::SpecFromName(bot->getClass(), wanted);
        if (!spec)
        {
            Say(bot, sender.guid, "I have no spec called '" + wanted + "'.");
            return;
        }
        if (spec != bot->GetSpecializationId())
        {
            bot->ForceChangeTalentGroup(spec);
            _throttle.Clear();
        }
        BotFactory::FillTalents(bot);
        bot->SaveToDB();
    }

    Say(bot, sender.guid, BotFactory::DescribeTalents(bot));
}

// Owner's rule (2026-11-05): when the owner leaves the group or removes the bot from it, the bot (alt bot or
// bot-account bot) logs out 3 s later. Only after it has been in a group with its master, and not when it left
// by the "leave" order. LogoutBot only raises a flag: the session logs the character out on its own thread.
void PlayerbotAI::CheckOwnerGroup(Player* bot, uint32 diff)
{
    // in a battleground GetGroup() is the battleground's raid of the bot's team (the party is the original group):
    // the party rules wait until it leaves (a fill bot's master may even play for the other team)
    if (bot->InBattleground() || bot->InArena())
        return;

    Group* group = bot->GetGroup();
    if (!group)
        _leaveQueued = false;
    // still in the group 5 s after the leave was sent: send it again (owner, build #26: bots of older builds
    // stayed in dead groups, the single leave having been lost)
    if (_leaveQueued && group)
    {
        _leaveRetryTimer += diff;
        if (_leaveRetryTimer >= 5000)
        {
            _leaveRetryTimer = 0;
            // what HandleLeaveGroup calls, on this (the bot's) thread
            if (_randomBot && !group->isLFGGroup() && !group->IsMember(_master.IsEmpty() ? _pendingMaster : _master))
            {
                TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' leaves a party without its master (directly).", bot->GetName());
                bot->RemoveFromGroup(GROUP_REMOVEMETHOD_LEAVE);
                group = bot->GetGroup();
            }
            if (!group)
                _leaveQueued = false;
        }
    }

    // the master, or the player whose invite a masterless random bot just accepted
    ObjectGuid const owner = !_master.IsEmpty() ? _master : _pendingMaster;
    if (group && !owner.IsEmpty() && group->IsMember(owner))
    {
        _wasWithOwner = !_master.IsEmpty();
        _leftByOrder = false;
        _groupLogoutTimer = -1;
        return;
    }

    // Owner's rule (2026-11-05): a random bot never stays in a party without its master (a party of bots
    // left behind by a leader who left): it leaves, so it is free for another player.
    // A dungeon finder group is the exception: it may join a bot to other players than its master.
    if (_randomBot && group && !_leaveQueued && !group->isLFGGroup())
    {
        _leaveQueued = true;
        WorldPacket* packet = new WorldPacket(CMSG_LEAVE_GROUP, 1);
        *packet << int8(0);
        bot->GetSession()->QueuePacket(packet);
        TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' leaves a party without its master.", bot->GetName());
    }

    if (_master.IsEmpty() || !_wasWithOwner || _leftByOrder)
        return;

    if (_groupLogoutTimer < 0)
    {
        _groupLogoutTimer = GroupLeftLogoutDelay;
        TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' is no longer in its owner's group: %s in %d s.",
            bot->GetName(), _randomBot ? "back to GM Island" : "logging out", GroupLeftLogoutDelay / 1000);
        return;
    }

    _groupLogoutTimer -= int32(diff);
    if (_groupLogoutTimer > 0)
        return;

    _groupLogoutTimer = -1;
    _wasWithOwner = false;
    if (_randomBot)
    {
        Dismiss(bot, "it left its owner's group");
        return;
    }
    // out of a dungeon finder group first, so the group does not keep an offline member in the dungeon
    if (group && group->isLFGGroup())
    {
        WorldPacket* packet = new WorldPacket(CMSG_LEAVE_GROUP, 1);
        *packet << int8(0);
        bot->GetSession()->QueuePacket(packet);
    }
    TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' logs out: it left its owner's group.", bot->GetName());
    bot->GetSession()->LogoutBot();
}

// ---- Defensives, potions, rest (plan step 4.5) -----------------------------------------------------------

bool PlayerbotAI::TrySurvival(Player* bot, Unit* enemy)
{
    PlayerbotMgr::SurvivalOptions const& options = sPlayerbotMgr->GetSurvivalOptions();
    float const health = bot->GetHealthPct();

    if (options.defensives && _defensivesOn)
    {
        uint32 const family = BotRotations::ClassFamily(bot->getClass());
        for (BotRotations::Defensive const* defensive : BotRotations::DefensivesFor(bot->getClass(), bot->GetSpecializationId()))
        {
            if (health > defensive->belowPct)
                continue;
            Unit* target = defensive->onEnemy ? enemy : static_cast<Unit*>(bot);
            if (!target || (!defensive->onEnemy && BotRotations::HasAuraNamed(bot, defensive->spell, bot->GetGUID())))
                continue;
            uint32 const spellId = BotRotations::KnownSpell(bot, defensive->spell, family);
            if (!spellId)
                continue;
            if (BotCombat::BotCast(bot, target, spellId, _throttle))
            {
                if (sPlayerbotMgr->GetCombatLog() >= 1)
                    TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' uses %s at %.0f%% health.", bot->GetName(), defensive->spell, health);
                return true;
            }
        }
    }

    // potions are the bot's own items: alts only when the conf allows it (owner's alt rule)
    bool const mayUseItems = _potionsOn && (_randomBot || options.potionsOnAlts);
    if (mayUseItems && health <= options.potionHealthPct && BotConsumables::UsePotion(bot, false))
    {
        if (sPlayerbotMgr->GetCombatLog() >= 1)
            TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' drinks a healing potion at %.0f%% health.", bot->GetName(), health);
        return true;
    }
    if (mayUseItems && bot->GetMaxPower(POWER_MANA) > 0 && bot->GetPowerPct(POWER_MANA) <= options.potionManaPct &&
        BotConsumables::UsePotion(bot, true))
    {
        if (sPlayerbotMgr->GetCombatLog() >= 1)
            TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' drinks a mana potion.", bot->GetName());
        return true;
    }
    return false;
}

// SkyFire's rest: sit with food/drink auras of the bot's level when low out of combat; stand when topped
// up, when it or its group fights, or when its leader is more than RestLeaderDistance away
void PlayerbotAI::UpdateRest(Player* bot, std::vector<Player*> const& members, Unit* enemy)
{
    PlayerbotMgr::SurvivalOptions const& options = sPlayerbotMgr->GetSurvivalOptions();
    float const RestLeaderDistance = 20.0f;

    bool groupFighting = enemy || bot->isInCombat() || !bot->getAttackers()->empty();
    for (Player* member : members)
        if (member->isInCombat())
            groupFighting = true;

    Player* leader = nullptr;
    if (Group* group = bot->GetGroup())
    {
        ObjectGuid const leaderGuid = (!_master.IsEmpty() && group->IsMember(_master)) ? _master : group->GetLeaderGUID();
        for (Player* member : members)
            if (member->GetGUID() == leaderGuid && member != bot)
                leader = member;
    }
    bool const leaderAway = leader && bot->GetDistance(leader) > RestLeaderDistance;

    bool const usesMana = bot->GetMaxPower(POWER_MANA) > 0;
    float const health = bot->GetHealthPct();
    float const mana = usesMana ? bot->GetPowerPct(POWER_MANA) : 100.0f;

    if (_resting)
    {
        bool const full = health >= 99.0f && mana >= 99.0f;
        if (groupFighting || full || leaderAway || !bot->IsAlive())
        {
            _resting = false;
            BotConsumables::StopRest(bot);
            _followGuid.Clear();
            _followTimer = FollowCheckInterval;
        }
        else if (!BotConsumables::HasRestAura(bot))
            BotConsumables::StartRest(bot, health < 99.0f, mana < 99.0f);   // an aura ran out before full
        return;
    }

    if (!options.rest || !_restOn || groupFighting || leaderAway || !bot->IsAlive() || bot->IsNonMeleeSpellCast(false) || _engaged)
        return;
    bool const needHealth = health < options.restHealthPct;
    bool const needMana = usesMana && mana < options.restManaPct;
    if (!needHealth && !needMana)
        return;

    _resting = true;
    bot->GetMotionMaster()->Clear();
    bot->GetMotionMaster()->MoveIdle();
    // eat and drink together once sitting, as SkyFire
    BotConsumables::StartRest(bot, health < 99.0f, mana < 99.0f);
    if (sPlayerbotMgr->GetCombatLog() >= 1)
        TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' rests (health %.0f%%, mana %.0f%%).", bot->GetName(), health, mana);
}

// ---- Self-bot and dungeon finder -------------------------------------------------------------------------

void PlayerbotAI::Detach(Player* bot)
{
    if (_engaged)
        StopCombat(bot, "self-bot off");
    if (!_followGuid.IsEmpty())
        StopFollowing(bot);
    _healMoveGuid.Clear();
}

namespace
{
    uint8 LfgRoleOf(Player* bot)
    {
        switch (BotCombat::RoleOf(bot))
        {
            case BotCombat::BotRole::Tank:   return lfg::PLAYER_ROLE_TANK;
            case BotCombat::BotRole::Healer: return lfg::PLAYER_ROLE_HEALER;
            default:                         return lfg::PLAYER_ROLE_DAMAGE;
        }
    }

    char const* LfgRoleName(uint8 role)
    {
        if (role & lfg::PLAYER_ROLE_TANK)
            return "tank";
        if (role & lfg::PLAYER_ROLE_HEALER)
            return "healer";
        return "dps";
    }
}

// As a client: the group's role check gets the bot's spec role (the leader's queue, his order to the party),
// and a dungeon proposal is accepted. Both through the dungeon finder's own entry points, on the bot's thread
// like the packet handlers (HandleLfgCompleteRoleCheck, HandleLfgProposalResponse).
void PlayerbotAI::UpdateLfg(Player* bot)
{
    ObjectGuid const guid = bot->GetGUID();
    if (Group* group = bot->GetGroup())
    {
        uint8 chosen = 0;
        if (sLFGMgr->GetRoleCheckRoles(group->GetGUID(), guid, chosen) &&
            !(chosen & (lfg::PLAYER_ROLE_TANK | lfg::PLAYER_ROLE_HEALER | lfg::PLAYER_ROLE_DAMAGE)))
        {
            uint8 const role = LfgRoleOf(bot);
            TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' answers the dungeon finder role check: %s.", bot->GetName(), LfgRoleName(role));
            sLFGMgr->UpdateRoleCheck(group->GetGUID(), guid, role);
        }

        // owner's rule (2026-11-06): a player's vote to kick a bot gets every other bot's yes; other kicks are left to
        // the players (the bot does not vote, as before)
        ObjectGuid victim;
        std::vector<ObjectGuid> agreed;
        if (sLFGMgr->GetPendingBoot(group->GetGUID(), guid, victim, agreed) && victim != guid && sPlayerbotMgr->IsBot(victim))
        {
            bool playerAgrees = false;
            for (ObjectGuid const& voter : agreed)
                if (!sPlayerbotMgr->IsBot(voter))
                    playerAgrees = true;
            if (playerAgrees)
            {
                TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' votes yes to kick %s from its dungeon finder group.",
                    bot->GetName(), victim.ToString().c_str());
                sLFGMgr->UpdateBoot(group->GetGUID(), guid, true);
            }
        }
    }

    uint32 const proposal = sLFGMgr->GetPendingProposal(guid);
    if (!proposal)
    {
        _answeredProposal = 0;
        return;
    }
    if (proposal == _answeredProposal)
        return;

    _answeredProposal = proposal;
    WorldPackets::LFG::ProposalResponse response;
    response.ProposalID = proposal;
    response.Accepted = true;
    response.Ticket.RequesterGuid = guid;
    response.Ticket.Time = int32(GameTime::GetGameTime());
    for (uint32 queueId : sLFGMgr->GetQueueIds(guid))
        if (Optional<WorldPackets::LFG::RideTicket> ticket = sLFGMgr->GetTicket(guid, queueId))
        {
            response.Ticket = *ticket;
            break;
        }
    TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' accepts dungeon finder proposal %u.", bot->GetName(), proposal);
    sLFGMgr->UpdateProposal(response, guid);
}

void PlayerbotAI::PassLfgLead(Player* bot)
{
    Group* group = bot->GetGroup();
    if (!group || !group->isLFGGroup() || !group->IsLeader(bot->GetGUID()))
        return;

    // the owner first, else any player of the group who is online and not a bot
    ObjectGuid target;
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
    {
        if (slot.Guid == bot->GetGUID() || sPlayerbotMgr->IsBot(slot.Guid) || !ObjectAccessor::FindPlayer(slot.Guid))
            continue;
        if (target.IsEmpty() || slot.Guid == _master)
            target = slot.Guid;
    }
    if (target.IsEmpty())
        return;

    // as /promote: CMSG_SET_PARTY_LEADER (SetPartyLeader::Read: int8 PartyIndex, guid), so the core's handler and its
    // OnChangeLeader hook (the dungeon finder's leader) run; a dungeon finder group is the instance party (index 1)
    WorldPacket* packet = new WorldPacket(CMSG_SET_PARTY_LEADER, 1 + 18);
    *packet << int8(1);
    *packet << target;
    bot->GetSession()->QueuePacket(packet);
    TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' gives the lead of its dungeon finder group to %s.", bot->GetName(),
        target.ToString().c_str());
}

void PlayerbotAI::JoinLfgFor(Player* bot, BotFactory::CreateRequest const& request, lfg::LfgDungeonSet dungeons, uint8 role)
{
    std::string const summary = BotFactory::InitializeNewBot(bot, request);
    if (bot->GetGroup())
    {
        TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' is in a group: no dungeon finder fill.", bot->GetName());
        return;
    }
    TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' queues for the dungeon finder as %s (%s): %s",
        bot->GetName(), LfgRoleName(role), lfg::ConcatenateDungeons(dungeons).c_str(), summary.c_str());
    sLFGMgr->JoinLfg(bot, role, dungeons);
}

void PlayerbotAI::UpdateBattleground(Player* bot, uint32 diff)
{
    if (bot->InBattleground())
    {
        _bgEntered = true;
        BotBattleground::Update(bot, _bg, _engaged, diff);

        // who is in which battleground, for the fill (room for a queued player, top-ups)
        _bgReportTimer += diff;
        Battleground* bg = bot->GetBattleground();
        if (_bgReportTimer >= 2000 && bg && !bg->IsArena())
        {
            _bgReportTimer = 0;
            PlayerbotMgr::BgMember member;
            member.guid = bot->GetGUID();
            member.instanceId = bg->GetInstanceID();
            member.bgTypeId = bg->GetTypeID();
            member.queueType = MS::Battlegrounds::GetBgQueueTypeIdByBgTypeID(member.bgTypeId);
            member.bracket = bg->GetBracketId();
            member.status = uint8(bg->GetStatus());
            member.maxPerTeam = bg->GetMaxPlayersPerTeam();
            member.team = bot->GetBGTeam();
            member.role = ROLES_DPS;
            if (ChrSpecializationEntry const* spec = sChrSpecializationStore.LookupEntry(bot->GetSpecializationId()))
                if (spec->Role <= ROLES_DPS)
                    member.role = spec->Role;
            member.level = bot->getLevel();
            member.fillBot = _bgFill;
            sPlayerbotMgr->ReportBgMember(member);
        }
        return;
    }

    // invited: in (party bots queued with their group by its leader, fill bots)
    if (BotBattleground::AcceptInvites(bot))
        return;

    // a fill bot back from its battleground: dismissed (reset, GM Island, free again)
    if (_bgFill && _bgEntered && !BotBattleground::IsQueued(bot) && !bot->IsBeingTeleported())
    {
        _bgFill = false;
        _bgEntered = false;
        Dismiss(bot, "its battleground is over");
    }
}

void PlayerbotAI::JoinBattlegroundFor(Player* bot, BotFactory::CreateRequest const& request, uint16 bgTypeId)
{
    std::string const summary = BotFactory::InitializeNewBot(bot, request);
    if (bot->GetGroup())
    {
        TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' is in a group: no battleground fill.", bot->GetName());
        return;
    }
    _bgFill = true;
    _bgEntered = false;
    uint8 const roles = LfgRoleOf(bot);
    std::string why;
    if (!BotBattleground::JoinQueue(bot, bgTypeId, roles, why))
    {
        TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' could not queue for battleground %u: %s.", bot->GetName(),
            uint32(bgTypeId), why.c_str());
        return;
    }
    TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' queues for battleground %u as %s: %s", bot->GetName(),
        uint32(bgTypeId), LfgRoleName(roles), summary.c_str());
}

void PlayerbotAI::LeaveBattlegroundForPlayer(Player* bot)
{
    if (!bot->InBattleground() || !_bgFill)
        return;
    TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' leaves its battleground: a player queued for its place.", bot->GetName());
    bot->LeaveBattleground();                               // UpdateBattleground dismisses it once out
}

bool PlayerbotAI::AbandonBattleground(Player* bot)
{
    if (bot->InBattleground() || BotBattleground::IsInvited(bot))
        return false;
    if (BotBattleground::IsQueued(bot))
    {
        BotBattleground::LeaveQueues(bot);
        TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' leaves its battleground queue: not needed any more.", bot->GetName());
    }
    _bgFill = false;
    return true;
}

void PlayerbotAI::JoinDungeonGroupFor(Player* bot, BotFactory::CreateRequest const& request, uint8 role)
{
    std::string const summary = BotFactory::InitializeNewBot(bot, request);
    if (bot->GetGroup())
    {
        TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' is in a group: it cannot join a dungeon group.", bot->GetName());
        return;
    }
    TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' waits for the invite of its dungeon group as %s: %s", bot->GetName(),
        LfgRoleName(role), summary.c_str());
    // the leader's session invites it as a client would (SendCreatorInvite); HandleGroupInvite accepts its master
    _inviteCreator = request.creator;
    _inviteAccount = request.creatorAccount;
    _inviteTimer = 1000;
    _joinTeleport = true;
    _joinRole = role;
}

void PlayerbotAI::UpdateJoinTeleport(Player* bot)
{
    Group* group = bot->GetGroup();
    if (!group || !group->IsMember(_master))
        return;
    _joinTeleport = false;
    if (_joinRole)
        group->SetLfgRoles(bot->GetGUID(), _joinRole);

    // where its master stands (another map: read under the accessor lock), inside the group's dungeon
    bool found = false;
    uint32 mapId = 0;
    Position pos;
    ObjectAccessor::WithPlayer(_master, [&](Player* master)
    {
        found = true;
        mapId = master->GetMapId();
        pos = master->GetPosition();
    });
    if (!found)
        return;
    TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' joined its dungeon group: teleports to its leader (map %u).", bot->GetName(), mapId);
    bot->TeleportTo(mapId, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), pos.GetOrientation());
}

bool PlayerbotAI::AbandonLfg(Player* bot)
{
    if (bot->GetGroup())
        return false;
    // a proposal or a dungeon already under way: the group is being made, the bot stays
    std::vector<uint32> const queues = sLFGMgr->GetQueueIds(bot->GetGUID());
    for (uint32 queueId : queues)
    {
        lfg::LfgState const state = sLFGMgr->GetState(bot->GetGUID(), queueId);
        if (state == lfg::LFG_STATE_PROPOSAL || state == lfg::LFG_STATE_DUNGEON)
            return false;
    }
    if (!queues.empty())
        sLFGMgr->LeaveLfg(bot->GetGUID());
    TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' leaves the dungeon finder: its queuer left the queue.", bot->GetName());
    return true;
}

void PlayerbotAI::Dismiss(Player* bot, char const* why)
{
    if (_dismissed)
        return;

    if (!_randomBot)
    {
        TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' logs out: %s.", bot->GetName(), why);
        bot->GetSession()->LogoutBot();
        return;
    }

    // Out of the group first, and only then reset and teleported: the leave packet queued in the same tick as
    // the far teleport to GM Island was handled while the bot was between maps and lost, so dismissed bots stayed
    // in their group (owner's report, build #24: "x is already part of a group" when invited again).
    _dismissed = true;
    _dismissWhy = why;
    _dismissTimer = 0;
    _dismissTries = 0;
    TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' dismissed (%s): leaving its group, then reset and back to GM Island.",
        bot->GetName(), why);
    FinishDismiss(bot, 0);
}

void PlayerbotAI::FinishDismiss(Player* bot, uint32 diff)
{
    if (_released)
        return;

    if (bot->GetGroup())
    {
        _dismissTimer += diff;
        if (_dismissTries == 0 || _dismissTimer >= 3000)
        {
            if (_dismissTries >= 2)
            {
                // the packets did not make it: what HandleLeaveGroup itself calls, on this (the bot's) thread
                TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' leaves its group directly.", bot->GetName());
                bot->RemoveFromGroup(GROUP_REMOVEMETHOD_LEAVE);
                if (bot->GetGroup() && _dismissTries < 5)
                {
                    ++_dismissTries;
                    _dismissTimer = 0;
                    return;
                }
                if (bot->GetGroup())
                    TC_LOG_ERROR("server.loading", "[mod-playerbots] Bot '%s' could not leave its group; reset anyway.", bot->GetName());
            }
            else
            {
                WorldPacket* packet = new WorldPacket(CMSG_LEAVE_GROUP, 1);
                *packet << int8(0);
                bot->GetSession()->QueuePacket(packet);
                ++_dismissTries;
                _dismissTimer = 0;
                return;
            }
        }
        else
            return;
    }

    _released = true;
    BotFactory::ResetBot(bot, true);
    sPlayerbotMgr->RequestRelease(bot->GetGUID());
}

// ---- Loot rolls and equipping from the bags (2026-11-05) -------------------------------------------------

void PlayerbotAI::UpdateLootRolls(Player* bot)
{
    Group* group = bot->GetGroup();
    if (!group)
        return;

    PlayerbotMgr::LootOptions const& options = sPlayerbotMgr->GetLootOptions();
    for (Group::PendingRoll const& roll : group->GetPendingRolls(bot->GetGUID()))
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(roll.itemId);
        uint8 choice = ROLL_PASS;
        char const* why = _rollOn ? "random bots do not roll (Playerbots.Loot.RandomBotsRoll)" : "roll strategy off (nc -roll)";
        if (_rollOn && (!_randomBot || options.randomBotsRoll))
        {
            why = "not an upgrade for my spec";
            if (proto && (roll.rollVoteMask & ROLL_FLAG_TYPE_NEED))
            {
                BotGear::Comparison const comparison = BotGear::CompareToWorn(bot, proto,
                    BotGear::ItemLevelWithBonuses(proto, roll.bonusListIds));
                if (comparison.usable && comparison.gain * 100.0f >= options.needUpgradePct)
                    choice = ROLL_NEED;
                else if (comparison.usable)
                    why = "too small an upgrade";
            }
            if (choice != ROLL_NEED && (roll.rollVoteMask & ROLL_FLAG_TYPE_GREED))
                choice = ROLL_GREED;
        }

        TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' rolls %s on %s%s%s.", bot->GetName(),
            choice == ROLL_NEED ? "need" : choice == ROLL_GREED ? "greed" : "pass",
            proto && proto->GetName() ? proto->GetName()->Str[LOCALE_enUS] : "?",
            choice == ROLL_NEED ? "" : ": ", choice == ROLL_NEED ? "" : why);
        // as WorldSession::HandleLootRoll, on the voter's own thread
        group->CountRollVote(bot, roll.aoeSlot, choice);
    }
}

void PlayerbotAI::UpdateBagEquip(Player* bot)
{
    PlayerbotMgr::LootOptions const& options = sPlayerbotMgr->GetLootOptions();
    if (!options.autoEquip || !_autoEquipOn || (!_randomBot && !options.autoEquipAlts) || !bot->IsAlive())
        return;

    // the bag contents (items and where they are): a change means something was gained (loot, quest, mail,
    // vendor, roll, crafting...)
    std::vector<Item*> items;
    uint64 signature = 0;
    auto add = [&items, &signature](Item* item, uint32 pos)
    {
        items.push_back(item);
        signature = signature * 1000003ULL + item->GetGUID().GetCounter() * 131ULL + pos;
    };
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            add(item, slot);
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
        if (Bag* container = bot->GetBagByPos(bag))
            for (uint32 slot = 0; slot < container->GetBagSize(); ++slot)
                if (Item* item = container->GetItemByPos(uint8(slot)))
                    add(item, (bag << 8) | slot);

    if (signature == _bagSignature && !_equipPending)
        return;
    _bagSignature = signature;

    if (bot->isInCombat())
    {
        _equipPending = true;                               // after the fight
        return;
    }
    _equipPending = false;

    // the best upgrade first, then look again (a ring may now go to the other finger), a few per check
    for (uint32 round = 0; round < 6; ++round)
    {
        Item* bestItem = nullptr;
        BotGear::Comparison best;
        for (Item* item : items)
        {
            if (!item || item->IsInTrade())
                continue;
            BotGear::Comparison const comparison = BotGear::CompareToWorn(bot, item->GetTemplate(), item->GetItemLevel(bot->getLevel()));
            if (comparison.usable && comparison.gain * 100.0f >= options.equipUpgradePct && comparison.gain > best.gain)
            {
                best = comparison;
                bestItem = item;
            }
        }
        if (!bestItem || !EquipFromBag(bot, bestItem, best))
            break;
        items.erase(std::remove(items.begin(), items.end(), bestItem), items.end());
    }
}

bool PlayerbotAI::EquipFromBag(Player* bot, Item* item, BotGear::Comparison const& comparison)
{
    // a two-hander needs a free off hand: the off-hand piece goes to the bags first
    if (comparison.twoHanded)
        if (Item* off = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND))
        {
            ItemPosCountVec dest;
            if (bot->CanStoreItem(NULL_BAG, NULL_SLOT, dest, off, false) != EQUIP_ERR_OK)
                return false;
            bot->RemoveItem(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND, true);
            bot->StoreItem(dest, off, true);
        }

    std::string const name = item->GetTemplate()->GetName() ? item->GetTemplate()->GetName()->Str[LOCALE_enUS] : "?";
    // as CMSG_SWAP_INV_ITEM: the worn piece takes the new one's bag slot
    bot->SwapItem(item->GetPos(), uint16((INVENTORY_SLOT_BAG_0 << 8) | comparison.slot));
    if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, comparison.slot) != item)
    {
        TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' could not equip %s.", bot->GetName(), name.c_str());
        return false;
    }
    TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' equips %s (+%.0f%%).", bot->GetName(), name.c_str(), comparison.gain * 100.0f);
    if (!_master.IsEmpty())
        Say(bot, _master, "Equipped " + name + " (+" + std::to_string(int32(comparison.gain * 100.0f + 0.5f)) + "% for my spec).");
    return true;
}

// ---- Strategies (co / nc), aggressive pulls, looting --------------------------------------------------------

namespace
{
    struct StrategyInfo
    {
        char const* name;
        char const* what;
    };
    StrategyInfo const CombatStrategies[] =
    {
        { "aggressive", "start fights with nearby enemies when out of combat" },
        { "passive", "fight nothing, not even what attacks me" },
        { "defend", "fight only what attacks you or me" },
        { "tank", "tank bots taunt and take enemies off the group" },
        { "defensives", "use defensive cooldowns when hurt" },
        { "potions", "drink potions from my bags when low" },
    };
    StrategyInfo const NonCombatStrategies[] =
    {
        { "follow", "follow the group's leader (off = stay)" },
        { "loot", "loot the corpses of our kills" },
        { "rest", "eat and drink when low" },
        { "roll", "roll need / greed on group loot (off = pass)" },
        { "autoequip", "put on upgrades from my bags" },
        { "talents", "fill empty talent rows" },
    };
    // only one of these at a time
    bool IsAssistStrategy(std::string const& name)
    {
        return name == "aggressive" || name == "passive" || name == "defend";
    }
}

void PlayerbotAI::ApplyFlags()
{
    _assist = _co.count("passive") ? AssistMode::Passive : _co.count("defend") ? AssistMode::Defend : AssistMode::Full;
    _aggressive = _co.count("aggressive") != 0;
    _playerTank = !_co.count("tank");
    _defensivesOn = _co.count("defensives") != 0;
    _potionsOn = _co.count("potions") != 0;
    _stay = !_nc.count("follow");
    _lootCorpses = _nc.count("loot") != 0;
    _restOn = _nc.count("rest") != 0;
    _rollOn = _nc.count("roll") != 0;
    _autoEquipOn = _nc.count("autoequip") != 0;
    _talentsOn = _nc.count("talents") != 0;
}

void PlayerbotAI::SetStrategy(Player* bot, bool combat, std::string const& name, bool on)
{
    std::set<std::string>& set = combat ? _co : _nc;
    if (on)
    {
        if (combat && IsAssistStrategy(name))
        {
            _co.erase("aggressive");
            _co.erase("passive");
            _co.erase("defend");
        }
        set.insert(name);
    }
    else
        set.erase(name);
    ApplyFlags();

    // what a change does at once
    if (combat && name == "passive" && on)
    {
        _orderedTarget.Clear();
        _pullTarget.Clear();
        if (_engaged)
            StopCombat(bot, "passive");
    }
    if (combat && name == "aggressive" && !on)
        _pullTarget.Clear();
    if (!combat && name == "follow")
    {
        _followGuid.Clear();
        _followTimer = FollowCheckInterval;
        if (!on)
        {
            _healMoveGuid.Clear();
            bot->GetMotionMaster()->Clear();
            bot->GetMotionMaster()->MoveIdle();
        }
    }
    if (!combat && name == "loot" && !on)
        _lootTarget.Clear();
}

// "co ?" / "nc ?": the bot's strategies; "co +a,-b" (or "co + a", "co -b") adds / removes
bool PlayerbotAI::StrategyOrder(Player* bot, OrderSender const& sender, bool combat, std::string const& args)
{
    std::set<std::string>& set = combat ? _co : _nc;
    char const* kind = combat ? "Combat" : "Non-combat";
    auto known = [combat](std::string const& name) -> bool
    {
        if (combat)
        {
            for (StrategyInfo const& info : CombatStrategies)
                if (name == info.name)
                    return true;
        }
        else
            for (StrategyInfo const& info : NonCombatStrategies)
                if (name == info.name)
                    return true;
        return false;
    };
    auto list = [&]() -> std::string
    {
        std::string text = std::string(kind) + " strategies:";
        if (set.empty())
            text += " none";
        bool first = true;
        for (std::string const& name : set)
        {
            text += (first ? " " : ", ") + name;
            first = false;
        }
        text += ". Available:";
        first = true;
        if (combat)
        {
            for (StrategyInfo const& info : CombatStrategies)
            {
                text += std::string(first ? " " : ", ") + info.name + " (" + info.what + ")";
                first = false;
            }
        }
        else
            for (StrategyInfo const& info : NonCombatStrategies)
            {
                text += std::string(first ? " " : ", ") + info.name + " (" + info.what + ")";
                first = false;
            }
        return text + ".";
    };

    std::string rest = args;
    while (!rest.empty() && rest.front() == ' ')
        rest.erase(rest.begin());
    if (rest.empty() || rest == "?")
    {
        Say(bot, sender.guid, list());
        return true;
    }

    // tokens: +name / -name, separated by commas or spaces; a lone sign applies to the next name
    std::vector<std::string> changed, unknown;
    char sign = 0;
    std::string token;
    auto flush = [&]()
    {
        if (token.empty())
            return;
        if (token[0] == '+' || token[0] == '-')
        {
            sign = token[0];
            token.erase(token.begin());
        }
        if (!token.empty())
        {
            if (!sign || !known(token))
                unknown.push_back(token);
            else
            {
                SetStrategy(bot, combat, token, sign == '+');
                changed.push_back(std::string(1, sign) + token);
            }
            sign = 0;
        }
        token.clear();
    };
    for (char c : rest + ",")
    {
        if (c == ',' || c == ' ')
            flush();
        else
            token += c;
    }

    std::string text;
    if (!changed.empty())
    {
        text = std::string(kind) + " strategies changed (";
        for (size_t i = 0; i < changed.size(); ++i)
            text += (i ? ", " : "") + changed[i];
        text += "). ";
    }
    if (!unknown.empty())
    {
        text += "Unknown: ";
        for (size_t i = 0; i < unknown.size(); ++i)
            text += (i ? ", " : "") + unknown[i];
        text += " (use +name or -name). ";
    }
    Say(bot, sender.guid, text + list());
    return true;
}

Unit* PlayerbotAI::FindPullTarget(Player* bot)
{
    if (!_lootTarget.IsEmpty() || _resting || !bot->IsAlive())
        return nullptr;

    // no new fight while it should rest (it would only stand up again)
    PlayerbotMgr::SurvivalOptions const& survival = sPlayerbotMgr->GetSurvivalOptions();
    if (_restOn && survival.rest && (bot->GetHealthPct() < survival.restHealthPct ||
        (bot->GetMaxPower(POWER_MANA) > 0 && bot->GetPowerPct(POWER_MANA) < survival.restManaPct)))
        return nullptr;

    PlayerbotMgr::StrategyOptions const& options = sPlayerbotMgr->GetStrategyOptions();
    auto pullable = [bot, &options](Creature* creature) -> bool
    {
        return creature && creature->IsInWorld() && creature->IsAlive() && !creature->IsInEvadeMode() &&
            creature->getLevel() <= bot->getLevel() + options.aggressiveLevelsAbove &&
            creature->GetCreatureType() != CREATURE_TYPE_CRITTER && !creature->isCivilian() && !creature->isTotem() &&
            !creature->GetOwnerGUID().IsPlayer() && bot->IsHostileTo(creature) && bot->IsValidAttackTarget(creature) &&
            (!creature->hasLootRecipient() || creature->isTappedBy(bot)) &&
            bot->GetDistance(creature) <= options.aggressiveRange && bot->IsWithinLOSInMap(creature);
    };

    if (!_pullTarget.IsEmpty())
    {
        Creature* kept = ObjectAccessor::GetCreature(*bot, _pullTarget);
        if (pullable(kept))
            return kept;
        _pullTarget.Clear();
    }

    std::list<Creature*> creatures;
    bot->GetCreatureListInGrid(creatures, options.aggressiveRange);
    Creature* nearest = nullptr;
    for (Creature* creature : creatures)
        if (pullable(creature) && (!nearest || bot->GetDistance(creature) < bot->GetDistance(nearest)))
            nearest = creature;
    if (!nearest)
        return nullptr;

    _pullTarget = nearest->GetGUID();
    if (sPlayerbotMgr->GetCombatLog() >= 1)
        TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' (aggressive) pulls %s (level %u).", bot->GetName(), nearest->GetName(),
            uint32(nearest->getLevel()));
    return nearest;
}

void PlayerbotAI::UpdateLoot(Player* bot)
{
    if (!_lootCorpses || _selfBot || !bot->IsAlive() || bot->isInCombat() || _groupInFight || _engaged || _resting ||
        bot->IsNonMeleeSpellCast(false))
    {
        if (!_lootTarget.IsEmpty() && (!_lootCorpses || _groupInFight || bot->isInCombat()))
            _lootTarget.Clear();
        return;
    }

    float const LootReach = 4.0f;
    uint32 const LootTimeout = 15000;

    if (!_lootTarget.IsEmpty())
    {
        Creature* corpse = ObjectAccessor::GetCreature(*bot, _lootTarget);
        if (!corpse || corpse->IsAlive() || getMSTimeDiff(_lootStarted, getMSTime()) > LootTimeout)
        {
            _lootedCorpses.insert(_lootTarget);
            _lootTarget.Clear();
            _followGuid.Clear();                            // follow again
            return;
        }
        if (bot->GetDistance(corpse) > LootReach)
        {
            if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE)
            {
                float x, y, z;
                corpse->GetPosition(x, y, z);
                bot->GetMotionMaster()->Clear();
                bot->GetMotionMaster()->MovePoint(0, x, y, z);
            }
            return;
        }

        bot->StopMoving();
        LootCorpse(bot, corpse);
        _lootedCorpses.insert(_lootTarget);
        _lootTarget.Clear();
        _followGuid.Clear();
        return;
    }

    // a corpse of a kill the bot or its group may loot, not tried yet, the nearest first
    if (_lootedCorpses.size() > 300)
        _lootedCorpses.clear();
    std::list<Creature*> corpses;
    bot->GetCorpseCreatureInGrid(corpses, sPlayerbotMgr->GetStrategyOptions().lootRange);
    Creature* nearest = nullptr;
    for (Creature* corpse : corpses)
    {
        if (!corpse || corpse->IsAlive() || _lootedCorpses.count(corpse->GetGUID()))
            continue;
        if (!corpse->isTappedBy(bot) && !corpse->HasInLootList(bot->GetGUID()))
            continue;
        if (!nearest || bot->GetDistance(corpse) < bot->GetDistance(nearest))
            nearest = corpse;
    }
    if (!nearest)
        return;

    _lootTarget = nearest->GetGUID();
    _lootStarted = getMSTime();
    StopFollowing(bot);
}

// As the client: CMSG_LOOT_UNIT (WorldSession::LootCorps, which fills the corpse's loot and the bot's personal
// loot of it), then each item it may take (CMSG_LOOT_ITEM: StoreLootItem by slot, the rolled slots of group loot
// skipped), the gold (CMSG_LOOT_MONEY: GetGoldFromLoot) and CMSG_LOOT_RELEASE (DoLootRelease). All on the bot's own
// thread, in one go, as the handlers would.
void PlayerbotAI::LootCorpse(Player* bot, Creature* corpse)
{
    ObjectGuid const guid = corpse->GetGUID();
    WorldSession* session = bot->GetSession();
    session->LootCorps(guid);

    uint32 const minQuality = sPlayerbotMgr->GetStrategyOptions().lootMinQuality;
    std::vector<Loot*> loots;
    loots.push_back(&corpse->loot);
    {
        auto itr = bot->personalLoot.find(guid);
        if (itr != bot->personalLoot.end())
            loots.push_back(&itr->second);
    }

    uint32 taken = 0, left = 0;
    Group* group = bot->GetGroup();
    for (Loot* loot : loots)
    {
        uint32 const slots = loot->GetMaxSlotInLootFor(bot);
        for (uint32 slot = 0; slot < slots; ++slot)
        {
            QuestItem* questItem = nullptr;
            QuestItem* ffaItem = nullptr;
            QuestItem* condItem = nullptr;
            QuestItem* currency = nullptr;
            LootItem* item = loot->LootItemInSlot(slot, bot, &questItem, &ffaItem, &condItem, &currency);
            if (!item || item->is_looted || item->is_blocked)
                continue;
            if (group && !loot->personal && group->isRolledSlot(uint8(slot)))
                continue;                                   // being rolled for: the roll decides
            // quest items and currencies always; other items from the configured quality up
            bool const always = questItem || item->needs_quest || item->currency || currency;
            if (!always && uint32(item->quality) < minQuality)
            {
                ++left;
                continue;
            }
            bot->StoreLootItem(uint8(slot), loot);
            ++taken;
        }
    }
    bot->GetGoldFromLoot();
    session->DoLootRelease(bot->GetLootGUID().IsEmpty() ? guid : bot->GetLootGUID());

    TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' loots %s: %u item(s) taken, %u below the minimum quality left.",
        bot->GetName(), corpse->GetName(), taken, left);
}

// Owner's request (build #26): once a minute a bot looks at its group; two checks in a row (so a player has a
// minute to come back) without any player in it, online and not a bot, and it leaves: a random bot is dismissed
// (reset, GM Island), an alt just leaves. Catches every way a group can end up with bots only, beyond the
// owner / master rules. Cheap: the member list and one lookup per member, once a minute per bot.
void PlayerbotAI::CheckPlayerlessGroup(Player* bot, uint32 diff)
{
    uint32 const Interval = 60000;
    _groupCheckTimer += diff;
    if (_groupCheckTimer < Interval)
        return;
    _groupCheckTimer = 0;

    Group* group = bot->GetGroup();
    if (!group || bot->InBattleground() || bot->InArena())
    {
        _playerlessChecks = 0;
        return;
    }

    bool hasPlayer = false;
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
    {
        if (slot.Guid == bot->GetGUID() || sPlayerbotMgr->IsBot(slot.Guid))
            continue;
        if (ObjectAccessor::FindPlayer(slot.Guid))          // online: an existence check only
        {
            hasPlayer = true;
            break;
        }
    }
    if (hasPlayer)
    {
        _playerlessChecks = 0;
        return;
    }

    if (++_playerlessChecks < 2)
        return;
    _playerlessChecks = 0;

    if (_randomBot)
    {
        Dismiss(bot, "its group has no player");
        return;
    }
    TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' leaves a group with no player in it.", bot->GetName());
    WorldPacket* packet = new WorldPacket(CMSG_LEAVE_GROUP, 1);
    *packet << int8(0);
    bot->GetSession()->QueuePacket(packet);
}

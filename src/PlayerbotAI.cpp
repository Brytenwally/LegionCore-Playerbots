/*
 * mod-playerbots: per-bot AI. See PlayerbotAI.h.
 */

#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "DB2Stores.h"
#include "Group.h"
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
    float const FollowDistance = 2.0f;
    float const FollowAngle = float(M_PI);                  // behind the leader
    uint32 const CombatCheckInterval = 250;                 // ms between combat checks (one cast at most each)
    float const RangedDistance = 25.0f;                     // ranged roles chase to this distance
    float const RangedStopDistance = 30.0f;                 // ... and stop chasing within this (and in sight)
    float const HealBelowPct = 90.0f;                       // healers heal allies below this
    float const TankFirstBelowPct = 75.0f;                  // ... the tank first once it is below this
    float const CriticalHealthPct = 40.0f;                  // triage: healed before interrupts and dispels
    float const HealRange = 35.0f;                          // beyond it (or out of sight) a healer walks closer
    float const HealApproachDistance = 15.0f;

    std::string Lower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return text;
    }
}

bool PlayerbotAI::IsMaster(ObjectGuid const& guid, uint32 accountId) const
{
    return (!_master.IsEmpty() && guid == _master) || (accountId && accountId == _accountId);
}

void PlayerbotAI::Update(Player* bot, uint32 diff)
{
    if (!bot->IsInWorld() || bot->IsBeingTeleported())
        return;

    HandleGroupInvite(bot);

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
    bool const accept = IsMaster(leader, ObjectMgr::GetPlayerAccountIdByGUID(leader));

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

void PlayerbotAI::UpdateFollow(Player* bot)
{
    // combat owns the movement while engaged (or a healer walks to its heal target); moving would also
    // break a cast
    if (_engaged || !_healMoveGuid.IsEmpty() || bot->IsNonMeleeSpellCast(false))
        return;

    Group* group = bot->GetGroup();
    if (_stay || !group || !bot->IsAlive())
    {
        if (!group)
            StopFollowing(bot);
        return;
    }

    // the master when he is in the group, the group's leader otherwise; only on the bot's own map
    ObjectGuid targetGuid = group->GetLeaderGUID();
    if (!_master.IsEmpty() && group->IsMember(_master))
        targetGuid = _master;
    if (targetGuid == bot->GetGUID())
        return;

    Player* target = ObjectAccessor::GetPlayer(*bot, targetGuid);
    if (!target || !target->IsInWorld() || target->IsBeingTeleported())
        return;

    MotionMaster* motion = bot->GetMotionMaster();
    if (_followGuid == targetGuid && motion->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE)
        return;

    motion->Clear();
    motion->MoveFollow(target, FollowDistance, FollowAngle);
    _followGuid = targetGuid;
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
        Say(bot, sender.guid, "Orders I know: follow, stay, summon, leave, attack, passive, assist [full|defend|passive], quests [all|in|co|summary|available], quest take <name>, quest complete <name>.");
}

void PlayerbotAI::HandlePartyChat(Player* bot, OrderSender const& sender, ObjectGuid const& group, std::string const& text)
{
    if (!IsMaster(sender.guid, sender.accountId))
        return;

    // the bot may have left that group since the message was sent
    Group* botGroup = bot->GetGroup();
    if (!botGroup || botGroup->GetGUID() != group)
        return;

    HandleOrder(bot, sender, text);
}

bool PlayerbotAI::HandleOrder(Player* bot, OrderSender const& sender, std::string const& text)
{
    std::string const order = Lower(text);
    if (order == "follow")
    {
        _stay = false;                                      // owner's rule: follow does not end a fight
        _followGuid.Clear();                                // follow again at the next check
        _followTimer = FollowCheckInterval;
        Say(bot, sender.guid, bot->GetGroup() ? "Following." : "Following once I am in your group.");
        return true;
    }

    if (order == "stay")
    {
        _stay = true;
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

    _stay = false;                                          // a summoned bot follows again
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

    _stay = false;
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
    if (enemy && !ordered && !_engageTimer.Ready(enemy->GetGUID(), getMSTime(), sPlayerbotMgr->GetEngageDelay()))
    {
        // LegionBotAI's engage delay: the player swings first; a fight already going on goes on
        Unit* current = bot->getVictim();
        enemy = (_engaged && current && current->IsAlive()) ? current : nullptr;
    }

    if (enemy)
        Engage(bot, enemy, role);
    else if (_engaged)
        StopCombat(bot, "no enemy left");

    if (bot->IsNonMeleeSpellCast(false))
        return;

    RunRotation(bot, members, role, enemy);
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

    auto usable = [bot, range](Unit* unit)
    {
        return unit && unit->IsInWorld() && unit->IsAlive() && unit->GetMap() == bot->GetMap() &&
            bot->IsValidAttackTarget(unit) && bot->GetDistance(unit) <= range;
    };

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
    return usable(attacker) ? attacker : nullptr;
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

    if (!critical)
        runSteps();
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
        _assist = AssistMode::Full;
    else if (mode == "defend")
        _assist = AssistMode::Defend;
    else if (mode == "passive")
    {
        // owner's rule: passive drops the fight at once, ordered target included
        _assist = AssistMode::Passive;
        _orderedTarget.Clear();
        if (_engaged)
            StopCombat(bot, "passive order");
    }
    else if (!mode.empty())
        return false;

    char const* name = _assist == AssistMode::Full ? "full: I fight whatever attacks the group" :
        _assist == AssistMode::Defend ? "defend: I fight only what attacks you or me" :
        "passive: I fight nothing, not even what attacks me, until you order attack or set another mode";
    Say(bot, sender.guid, std::string(mode.empty() ? "Assist mode " : "Assist mode set to ") + name + ".");
    return true;
}

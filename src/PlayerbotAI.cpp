/*
 * mod-playerbots: per-bot AI. See PlayerbotAI.h.
 */

#include "PlayerbotAI.h"
#include "DB2Stores.h"
#include "Group.h"
#include "CellImpl.h"
#include "Creature.h"
#include "GameObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "ItemTemplate.h"
#include "Log.h"
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
        Say(bot, sender.guid, "Orders I know: follow, stay, summon, leave, quests [all|in|co|summary|available], quest take <name>, quest complete <name>.");
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
        _stay = false;
        _followGuid.Clear();                                // follow again at the next check
        _followTimer = FollowCheckInterval;
        Say(bot, sender.guid, bot->GetGroup() ? "Following." : "Following once I am in your group.");
        return true;
    }

    if (order == "stay")
    {
        _stay = true;
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
void PlayerbotAI::Summon(Player* bot, OrderSender const& sender)
{
    if (!bot->IsAlive())
    {
        Say(bot, sender.guid, "I can't come, I'm dead.");
        return;
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

    // the bot's own copy of a quest it cannot take; a class-specific copy first, an unrestricted one after
    Quest const* FindCopyToTake(Player* bot, Quest const* quest)
    {
        std::vector<uint32> const* copies = GetQuestCopies(quest);
        if (!copies)
            return nullptr;

        Quest const* unrestricted = nullptr;
        for (uint32 id : *copies)
        {
            Quest const* copy = id != quest->GetQuestId() ? sQuestDataStore->GetQuestTemplate(id) : nullptr;
            if (!copy || !bot->SatisfyQuestClass(copy, false) || !bot->SatisfyQuestRace(copy, false))
                continue;
            if (copy->AllowableClasses)
                return copy;
            if (!unrestricted)
                unrestricted = copy;
        }
        return unrestricted;
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

    // exact title (or id) first, then the first title containing the name
    template<class Candidates>
    Quest const* MatchQuest(Candidates const& questIds, std::string const& name)
    {
        Quest const* partial = nullptr;
        for (uint32 id : questIds)
        {
            Quest const* quest = sQuestDataStore->GetQuestTemplate(id);
            if (!quest)
                continue;
            std::string const title = Lower(quest->LogTitle);
            if (title == name || std::to_string(id) == name)
                return quest;
            if (!partial && !name.empty() && title.find(name) != std::string::npos)
                partial = quest;
        }
        return partial;
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

        Quest const* quest = MatchQuest(offered, name);
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

    Quest const* quest = MatchQuest(inLog, name);
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

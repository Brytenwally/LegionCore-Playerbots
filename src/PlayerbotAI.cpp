/*
 * mod-playerbots: per-bot AI. See PlayerbotAI.h.
 */

#include "PlayerbotAI.h"
#include "DB2Stores.h"
#include "Group.h"
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

void PlayerbotAI::HandleWhisper(Player* bot, ObjectGuid const& sender, uint32 senderAccount, std::string const& text)
{
    if (!IsMaster(sender, senderAccount))
        return;

    if (!HandleOrder(bot, sender, text))
        Say(bot, sender, "Orders I know: follow, stay.");
}

void PlayerbotAI::HandlePartyChat(Player* bot, ObjectGuid const& sender, uint32 senderAccount, ObjectGuid const& group, std::string const& text)
{
    if (!IsMaster(sender, senderAccount))
        return;

    // the bot may have left that group since the message was sent
    Group* botGroup = bot->GetGroup();
    if (!botGroup || botGroup->GetGUID() != group)
        return;

    HandleOrder(bot, sender, text);
}

bool PlayerbotAI::HandleOrder(Player* bot, ObjectGuid const& sender, std::string const& text)
{
    std::string const order = Lower(text);
    if (order == "follow")
    {
        _stay = false;
        _followGuid.Clear();                                // follow again at the next check
        _followTimer = FollowCheckInterval;
        Say(bot, sender, bot->GetGroup() ? "Following." : "Following once I am in your group.");
        return true;
    }

    if (order == "stay")
    {
        _stay = true;
        _followGuid.Clear();
        bot->GetMotionMaster()->Clear();
        bot->GetMotionMaster()->MoveIdle();
        Say(bot, sender, "Staying here.");
        return true;
    }

    return false;
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

void PlayerbotAI::MirrorQuestAccept(Player* bot, ObjectGuid const& owner, ObjectGuid const& group, uint32 questId)
{
    Quest const* quest = sQuestDataStore->GetQuestTemplate(questId);
    if (!quest || !InOwnersGroup(bot, group))
        return;

    // already on it or done with it, or a quest of another class / race: nothing to say
    if (bot->GetQuestStatus(questId) != QUEST_STATUS_NONE || bot->GetQuestRewardStatus(questId)
        || !bot->SatisfyQuestClass(quest, false) || !bot->SatisfyQuestRace(quest, false))
        return;

    if (bot->CanTakeQuest(quest, false) && bot->CanAddQuest(quest, false))
    {
        // no quest giver: the giver's script hooks stay with the owner's own accept
        bot->AddQuestAndCheckCompletion(quest, nullptr);
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
    Say(bot, owner, "Can't take [" + quest->LogTitle + "]: " + reason + ".");
}

void PlayerbotAI::MirrorQuestReward(Player* bot, ObjectGuid const& owner, ObjectGuid const& group, uint32 questId)
{
    Quest const* quest = sQuestDataStore->GetQuestTemplate(questId);
    if (!quest || !InOwnersGroup(bot, group))
        return;

    QuestStatus const status = bot->GetQuestStatus(questId);
    if ((status != QUEST_STATUS_INCOMPLETE && status != QUEST_STATUS_COMPLETE) || bot->GetQuestRewardStatus(questId))
        return;

    if (!bot->SatisfyQuestDay(quest, false) || !bot->SatisfyQuestWeek(quest) || !bot->SatisfyQuestSeasonal(quest))
    {
        Say(bot, owner, "Can't turn in [" + quest->LogTitle + "]: I've reached the limit for this quest.");
        return;
    }

    uint32 const reward = ChooseQuestReward(bot, quest);
    if (!QuestRewardsFit(bot, quest, reward))
    {
        Say(bot, owner, "Can't turn in [" + quest->LogTitle + "]: no room in my bags for the reward.");
        return;
    }

    // its own objectives do not matter (owner's rule): mark it complete, then hand it in. A quest that
    // rewards itself on completion is done after CompleteQuest.
    if (status != QUEST_STATUS_COMPLETE)
    {
        bot->CompleteQuest(questId);
        if (bot->GetQuestRewardStatus(questId))
            return;
    }

    // the hook does not say where the owner turned it in: the bot stands in as the quest giver, as for a
    // quest turned in from the quest log
    bot->RewardQuest(quest, reward, bot, true);
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

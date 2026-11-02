/*
 * mod-playerbots: per-bot AI. See PlayerbotAI.h.
 */

#include "PlayerbotAI.h"
#include "Group.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
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
        _answeredInvite.Clear();                            // a later invite of the same group is a new one
        return;
    }
    if (invite->GetGUID() == _answeredInvite)
        return;

    _answeredInvite = invite->GetGUID();
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

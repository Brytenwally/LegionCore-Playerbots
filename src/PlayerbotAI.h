/*
 * mod-playerbots: per-bot AI.
 *
 * Plan step 3, first piece: a bot answers group invites, follows its group, and takes orders from
 * its owner by whisper or party chat. Everything here runs on the bot's own map thread
 * (PlayerScript::OnUpdate, or an order posted there with ObjectAccessor::PostToPlayer), so the
 * bot's Player may be used freely; the sender of an order is known by guid only.
 */

#ifndef MOD_PLAYERBOTS_PLAYERBOTAI_H
#define MOD_PLAYERBOTS_PLAYERBOTAI_H

#include "Define.h"
#include "ObjectGuid.h"
#include <string>

class Player;
class Quest;

class PlayerbotAI
{
public:
    // master: the player who added the bot (may be empty, e.g. added from the console).
    PlayerbotAI(ObjectGuid const& master, uint32 accountId) : _master(master), _accountId(accountId) { }

    void Update(Player* bot, uint32 diff);
    // a whisper to the bot, from anywhere; only its owner is obeyed
    void HandleWhisper(Player* bot, ObjectGuid const& sender, uint32 senderAccount, std::string const& text);
    // party/raid chat of the group `group`; only the owner's known orders are acted on, other talk is ignored
    void HandlePartyChat(Player* bot, ObjectGuid const& sender, uint32 senderAccount, ObjectGuid const& group, std::string const& text);

    // Quests, as on SkyFire / AzerothCore: a bot in its owner's group takes the quests the owner takes,
    // and turns a quest in when the owner does, whatever its own objectives; only bag space for the
    // rewards and daily/weekly limits stop it. group = the owner's group when he acted.
    void MirrorQuestAccept(Player* bot, ObjectGuid const& owner, ObjectGuid const& group, uint32 questId);
    void MirrorQuestReward(Player* bot, ObjectGuid const& owner, ObjectGuid const& group, uint32 questId);

    // the bot's owner: its master (who added it), or a player of the bot's own account (its alts' player).
    // Reads only constant members: safe from any thread.
    bool IsMaster(ObjectGuid const& guid, uint32 accountId) const;

private:
    // carries out a known order and confirms it to the sender; false for an unknown order
    bool HandleOrder(Player* bot, ObjectGuid const& sender, std::string const& text);
    void HandleGroupInvite(Player* bot);
    void UpdateFollow(Player* bot);
    void StopFollowing(Player* bot);
    void Say(Player* bot, ObjectGuid const& to, std::string const& text);
    // the reward the bot takes: the best usable item among the quest's choices and its class/spec
    // package items (both are what a client may send as ItemChoiceID); 0 when there is nothing to choose
    uint32 ChooseQuestReward(Player* bot, Quest const* quest) const;
    bool QuestRewardsFit(Player* bot, Quest const* quest, uint32 reward) const;
    bool InOwnersGroup(Player* bot, ObjectGuid const& group) const;

    ObjectGuid _master;
    uint32 _accountId;
    bool _stay = false;                                     // "stay" order: hold position until "follow"
    ObjectGuid _answeredInvite;                             // the group invite already answered
    ObjectGuid _followGuid;                                 // who the current follow movement targets
    uint32 _followTimer = 0;
};

#endif

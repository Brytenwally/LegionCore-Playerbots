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
#include "Position.h"
#include "BotCombat.h"
#include "BotRotations.h"
#include <map>
#include <string>
#include <vector>

class Player;
class Quest;
class WorldObject;

// Who gave an order, read in his own thread when he gave it: the bot runs the order in its own thread and
// must not touch another map's player.
struct OrderSender
{
    ObjectGuid guid;
    uint32 accountId = 0;
    WorldLocation location;                                 // where he stood (summon)
    bool inCombat = false;                                  // he was in combat (summon revive)
    bool partyChat = false;                                 // given in party/raid chat, not whispered
    ObjectGuid selection;                                   // his target (attack order)
};

class PlayerbotAI
{
public:
    // master: the player who added the bot (may be empty, e.g. added from the console).
    PlayerbotAI(ObjectGuid const& master, uint32 accountId) : _master(master), _accountId(accountId) { }

    void Update(Player* bot, uint32 diff);
    // a whisper to the bot, from anywhere; only its owner is obeyed
    void HandleWhisper(Player* bot, OrderSender const& sender, std::string const& text);
    // party/raid chat of the group `group`; only the owner's known orders are acted on, other talk is ignored
    void HandlePartyChat(Player* bot, OrderSender const& sender, ObjectGuid const& group, std::string const& text);

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
    bool HandleOrder(Player* bot, OrderSender const& sender, std::string const& text);
    void Summon(Player* bot, OrderSender const& sender);
    void LeaveGroup(Player* bot, OrderSender const& sender);
    // quests [all|incompleted|in|completed|co|summary]: whispers the quest log, as SkyFire's ListQuestsAction
    bool ListQuests(Player* bot, ObjectGuid const& to, std::string const& args);
    // Manual quests, for quests whose class versions are not copies of the owner's (Northshire's
    // "Encrypted Letter"): from the quest givers within QuestGiverRange of the bot.
    void ListAvailableQuests(Player* bot, ObjectGuid const& to);
    void TakeQuest(Player* bot, ObjectGuid const& to, std::string const& name);
    void CompleteQuestOrder(Player* bot, ObjectGuid const& to, std::string const& name);
    // Turns a quest of the bot's log in to giver (the bot itself when there is none), whatever its
    // own objectives (owner's rule); refuses on bag space or daily/weekly limits, telling `to`.
    bool TurnInQuest(Player* bot, Quest const* quest, WorldObject* giver, ObjectGuid const& to);
    void HandleGroupInvite(Player* bot);
    // Combat (plan steps 4.1 / 4.2): target by the never-pull rule and the assist mode, engage after the
    // engage delay, then one rotation step per check. Healers do not melee; they heal and cast the
    // rotation's enemy steps.
    void UpdateCombat(Player* bot);
    Unit* SelectTarget(Player* bot, std::vector<Player*> const& members, bool& ordered);
    void Engage(Player* bot, Unit* enemy, BotRotations::CombatRole role);
    void StopCombat(Player* bot, char const* reason);
    void RunRotation(Player* bot, std::vector<Player*> const& members, BotRotations::CombatRole role, Unit* enemy);
    void MoveToHealTarget(Player* bot, Player* healTarget);
    // the owner among the members (his master, or a client player of his account), null if not on the map
    Player* FindOwner(std::vector<Player*> const& members) const;
    void LogSkip(Player* bot, size_t step, char const* spell, std::string const& reason);
    void AttackOrder(Player* bot, OrderSender const& sender);
    bool AssistOrder(Player* bot, OrderSender const& sender, std::string const& mode);
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
    // the group invite already answered: the group itself, not its guid, since a group being created by the
    // invite has no guid until someone accepts (an empty guid matched "nothing answered yet")
    void const* _answeredInvite = nullptr;
    ObjectGuid _followGuid;                                 // who the current follow movement targets
    uint32 _followTimer = 0;

    enum class AssistMode : uint8
    {
        Full,                                               // whatever attacks the group
        Defend,                                             // only what attacks the owner or the bot
        Passive                                             // nothing but an ordered target, no self-defence
    };
    AssistMode _assist = AssistMode::Full;
    ObjectGuid _orderedTarget;                              // "attack" order: fought until it dies or "passive"
    ObjectGuid _combatTarget;                               // the enemy engaged (melee/ranged chase)
    bool _engaged = false;
    ObjectGuid _healMoveGuid;                               // a healer walking to an out-of-reach heal target
    uint32 _combatTimer = 0;
    BotCombat::SpellThrottle _throttle;
    BotCombat::EngageTimer _engageTimer;
    std::map<size_t, std::string> _lastSkip;                // rotation step -> last logged skip reason (log level 2)
};

#endif

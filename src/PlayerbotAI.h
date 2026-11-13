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
#include "BotFactory.h"
#include "BotGear.h"
#include "BotFormation.h"
#include "BotApl.h"
#include "BotAvoidAoe.h"
#include "BotBattleground.h"
#include "LFG.h"
#include <map>
#include <set>
#include <string>
#include <vector>

class Creature;
class Item;
class Unit;
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
    // randomBot: a bot of a bot account (not anyone's alt)
    PlayerbotAI(ObjectGuid const& master, uint32 accountId, bool randomBot);

    // ".playerbots self": the AI runs a client's own character (master = himself). No invites, no logout rules,
    // no automatic talents or rest; his own target is fought as an ordered one. Set before the AI is shared.
    void SetSelfBot() { _selfBot = true; _nc.erase("loot"); ApplyFlags(); }
    bool IsSelfBot() const { return _selfBot; }
    // the self-bot is switched off: give the movement and the fight back to the player (his thread)
    void Detach(Player* bot);

    // Dungeon finder fill (PlayerbotMgr::UpdateLfgFill): a pool bot set up to the queuer's level in a spec of the
    // missing role, then queued for the same dungeons (random or specific). On the bot's thread.
    void JoinLfgFor(Player* bot, BotFactory::CreateRequest const& request, lfg::LfgDungeonSet dungeons, uint8 role);
    // the fill was not needed any more (the queuer left the queue): leave the queue if still in it, alone
    // (not grouped yet); true when the bot is free again (no group), so the manager may release it
    bool AbandonLfg(Player* bot);
    // a dungeon finder group short of members (PlayerbotMgr::UpdateReplaceFill): set up to the leader's level in a spec
    // of the missing role, invited by the leader, then teleported to him once in the group
    void JoinDungeonGroupFor(Player* bot, BotFactory::CreateRequest const& request, uint8 role);
    // Battleground fill (PlayerbotMgr::UpdateBgFill): a pool bot set up to the fill's level, then queued alone for
    // that battleground (bgTypeId, the random one included). Once its battleground is over it is dismissed.
    void JoinBattlegroundFor(Player* bot, BotFactory::CreateRequest const& request, uint16 bgTypeId);
    // the fill is not needed any more: leave the queue unless invited or inside; true when the bot is free again
    bool AbandonBattleground(Player* bot);
    // a player queued for this battleground and its team is full: the fill bot leaves, so the queue invites him
    // (owner's rule, 2026-11-07); it is dismissed once out
    void LeaveBattlegroundForPlayer(Player* bot);
    // Owner's rule (2026-11-05): a dismissed random bot (left its master's group, master gone, fill no longer
    // needed) leaves its group, is reset (level, inventory except bags) and goes back to GM Island without a
    // master; an alt bot logs out as before. On the bot's thread.
    void Dismiss(Player* bot, char const* why);

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

    // a bot made by ".playerbots create", just logged in: level, spec, talents, gear, then to its creator
    void InitializeNewBot(Player* bot, BotFactory::CreateRequest const& request);

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
    // owner's rule: once the bot has been in its owner's group, leaving it (he left, or removed the bot)
    // logs the bot out GroupLeftLogoutDelay later
    void CheckOwnerGroup(Player* bot, uint32 diff);
    // Combat (plan steps 4.1 / 4.2): target by the never-pull rule and the assist mode, engage after the
    // engage delay, then one rotation step per check. Healers do not melee; they heal and cast the
    // rotation's enemy steps.
    void UpdateCombat(Player* bot);
    Unit* SelectTarget(Player* bot, std::vector<Player*> const& members, bool& ordered);
    void Engage(Player* bot, Unit* enemy, BotRotations::CombatRole role);
    void StopCombat(Player* bot, char const* reason);
    void RunRotation(Player* bot, std::vector<Player*> const& members, BotRotations::CombatRole role, Unit* enemy);
    // plan step 4.5: a defensive cooldown or a potion when hurt in combat (true = something was used)
    bool TrySurvival(Player* bot, Unit* enemy);
    // out of combat: sit and eat/drink when low, stand when topped up, attacked or left behind
    void UpdateRest(Player* bot, std::vector<Player*> const& members, Unit* enemy);
    void MoveToHealTarget(Player* bot, Player* healTarget);
    // the owner among the members (his master, or a client player of his account), null if not on the map
    Player* FindOwner(std::vector<Player*> const& members) const;
    Unit* FindLooseEnemy(Player* bot, std::vector<Player*> const& members) const;
    bool TryTaunt(Player* bot, Unit* enemy);
    void LogSkip(Player* bot, size_t step, char const* spell, std::string const& reason);
    void AttackOrder(Player* bot, OrderSender const& sender);
    void AutoGearOrder(Player* bot, OrderSender const& sender);
    void TalentsOrder(Player* bot, OrderSender const& sender, std::string const& args);
    bool AssistOrder(Player* bot, OrderSender const& sender, std::string const& mode);
    void UpdateFollow(Player* bot);
    // mounts / dismounts with its leader (owner's rule, 2026-11-07); called by UpdateFollow
    void UpdateMount(Player* bot);
    // following a flying leader (owner, build #33), as SkyFire / AzerothCore: fly flags set by hand, straight splines
    // at his height; a bot that cannot fly walks beneath him; true when it moved the bot (no ground follow then)
    bool UpdateFlyingFollow(Player* bot, Player* leader, float slotDistance, float slotAngle);
    void SetFlying(Player* bot, bool enable);
    // removes the mount; a flying bot loses its fly flags and falls to the ground
    void Dismount(Player* bot);
    // dungeon finder: answers its group's role check by its spec and accepts proposals, as a client would
    void UpdateLfg(Player* bot);
    // a bot leading a dungeon finder group gives the lead to a real player of it (owner, build #28: the core offers
    // "find another player" to the leader only, and a bot leader's offer reached nobody)
    void PassLfgLead(Player* bot);
    // owner's request (2026-11-13), as SkyFire's bots: the party role (tank / healer / damage on the frames) follows the spec
    void UpdatePartyRole(Player* bot);
    uint32 _leadTimer = 0;
    // battlegrounds: accepts invitations (party bots queued with their group, fill bots), then in a battleground its
    // tactics, death and leaving (BotBattleground::Update)
    void UpdateBattleground(Player* bot, uint32 diff);
    BotBattleground::State _bg;
    uint32 _bgTimer = 0;
    bool _bgFill = false;                                   // a pool bot filling a battleground queue
    bool _bgEntered = false;                                // ... that got into its battleground
    uint32 _bgReportTimer = 0;                              // PlayerbotMgr::ReportBgMember every 2 s inside
    // group loot: need on a Playerbots.Loot.NeedUpgradePct upgrade for its spec, greed otherwise (random bots
    // pass when Playerbots.Loot.RandomBotsRoll = 0)
    void UpdateLootRolls(Player* bot);
    // an item that reached the bags and beats the worn piece by Playerbots.AutoEquip.UpgradePct is put on;
    // in combat it waits for the end of the fight
    void UpdateBagEquip(Player* bot);
    // "sell <gray|*|vendor|quality [all]|item links|name>" (AzerothCore's SellAction), at a vendor within reach
    void SellOrder(Player* bot, OrderSender const& sender, std::string const& args);
    // AzerothCore's "c" (bag inspection) and "t" (trade) orders, and the answer to a trade someone opens (BotTrade.h)
    void InventoryOrder(Player* bot, OrderSender const& sender, std::string const& args);
    void TradeOrder(Player* bot, OrderSender const& sender, std::string const& args);
    void UpdateTrade(Player* bot);
    ObjectGuid _tradeTrader;                                // the trade under way, as last seen
    bool _tradeBegun = false;                               // its window opened on our side (CMSG_BEGIN_TRADE)
    bool _tradeOpenedByMe = false;                          // the next trade is one the bot itself opened ("t")
    // gray items sold as soon as the bot stands at a vendor (Playerbots.Sell.AutoGray)
    void UpdateAutoSell(Player* bot);
    bool EquipFromBag(Player* bot, Item* item, BotGear::Comparison const& comparison);
    bool FormationOrder(Player* bot, OrderSender const& sender, std::string const& args);

    // Strategies (owner's request, as mod-playerbots' co / nc): named switches in two sets, combat and
    // non-combat; "co ?" / "nc ?" lists them, "co +name,-name" / "nc +name" changes them. The older orders
    // (follow, stay, passive, assist, player tank, bot tank) change the same sets. ApplyFlags caches them.
    std::set<std::string> _co;
    std::set<std::string> _nc;
    void ApplyFlags();
    bool StrategyOrder(Player* bot, OrderSender const& sender, bool combat, std::string const& args);
    void SetStrategy(Player* bot, bool combat, std::string const& name, bool on);
    bool _aggressive = false;                               // co aggressive: picks fights out of combat
    bool _lootCorpses = false;                              // nc loot
    bool _restOn = true;                                    // nc rest
    bool _defensivesOn = true;                              // co defensives
    bool _potionsOn = true;                                 // co potions
    bool _avoidAoeOn = true;                                // co avoidaoe

    // co avoidaoe (mod-playerbots' "avoid aoe"): in a fight, step out of a harmful ground effect before
    // anything else; while the flee move runs, chase, heal moves and the rotation wait
    bool UpdateAvoidAoe(Player* bot, Unit* enemy, BotRotations::CombatRole role);
    bool AvoidingAoe(Player* bot) const;
    BotAvoidAoe::FleeMemory _fleeMemory;
    uint32 _avoidUntil = 0;                                 // getMSTime() until which the flee move has priority
    uint32 _avoidTellTime = 0;
    uint32 _avoidCheckTime = 0;
    bool _rollOn = true;                                    // nc roll
    bool _autoEquipOn = true;                               // nc autoequip
    bool _talentsOn = true;                                 // nc talents

    // co aggressive: the nearest hostile creature in reach, at most Playerbots.Aggressive.MaxLevelAbove above
    // the bot, nobody else's; kept until the fight starts
    Unit* FindPullTarget(Player* bot);
    ObjectGuid _pullTarget;
    // nc loot: walks to a corpse it or its group may loot, out of combat, and loots it as a client would
    void UpdateLoot(Player* bot);
    void LootCorpse(Player* bot, Creature* corpse);
    ObjectGuid _lootTarget;
    BotApl::State _apl;                                     // the SimC rotation's per-bot state
    uint32 _aplPrecombatTimer = 0;
    // out of combat: the SimC profile's precombat self buffs and pet summons (every 2 s)
    void UpdatePrecombat(Player* bot, uint32 diff);
    uint32 _lootStarted = 0;
    uint32 _corpseTimer = 0;
    std::set<ObjectGuid> _lootedCorpses;
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
    bool _flying = false;                                   // fly flags set by SetFlying
    Position _airPoint;                                     // last point given while following in the air / beneath
    bool _airMove = false;                                  // _airPoint is the movement in progress
    uint32 _followTimer = 0;
    BotFormation::Shape _formation = BotFormation::Shape::Role;
    float _followDistance = 0.0f;                           // the slot the current follow movement keeps
    float _followAngle = 0.0f;
    float _jitterForward = 0.0f;                            // chaos formation offset
    float _jitterLeft = 0.0f;
    uint32 _jitterTimer = 0;
    bool _selfBot = false;
    bool _dismissed = false;                                // leaving its group, then reset and released
    bool _released = false;                                 // reset done, waiting for the manager's new AI
    uint32 _dismissTimer = 0;
    uint32 _dismissTries = 0;
    std::string _dismissWhy;
    void FinishDismiss(Player* bot, uint32 diff);
    bool _groupInFight = false;                             // someone of the group on this map is fighting
    uint32 _lfgTimer = 0;
    uint32 _answeredProposal = 0;
    uint32 _lootTimer = 0;
    uint64 _bagSignature = 0;                               // the bag contents last checked for upgrades
    bool _equipPending = false;                             // bags changed during a fight

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
    bool _resting = false;
    bool _playerTank = false;                               // "player tank": the owner holds aggro, tank bots do not taunt
    ObjectGuid _healMoveGuid;                               // a healer walking to an out-of-reach heal target
    uint32 _combatTimer = 0;
    uint32 _talentTimer = 0;
    bool _randomBot = false;
    ObjectGuid _pendingMaster;                              // a random bot that accepted a player's invite, until its new AI takes over
    bool _leaveQueued = false;                              // a random bot leaving a party without its master
    uint32 _leaveRetryTimer = 0;                            // re-sends that leave: a packet can be lost (teleport)
    uint32 _groupCheckTimer = 0;                            // the playerless group check (every minute)
    uint32 _playerlessChecks = 0;
    void CheckPlayerlessGroup(Player* bot, uint32 diff);
    uint32 _ungroupedTime = 0;                              // a random bot with a master but no group: ms so far
    bool _everGrouped = false;                              // has been in a group since this AI was made
    void CheckUngrouped(Player* bot, uint32 diff);
    bool _wasWithOwner = false;                             // has been in a group with its master
    bool _leftByOrder = false;                              // the "leave" order: no logout for that
    int32 _groupLogoutTimer = -1;                           // ms until logout, -1 = none
    // a created bot invites itself into its creator's party: his session sends the invite a few seconds after
    // the setup (the bot is teleporting to him meanwhile), and the bot accepts it as any master's invite
    ObjectGuid _inviteCreator;
    uint32 _inviteAccount = 0;
    uint32 _inviteTimer = 0;
    void SendCreatorInvite(Player* bot);
    bool _joinTeleport = false;                             // in its master's group: teleport to him (dungeon replacement)
    uint8 _joinRole = 0;                                    // ... with this dungeon finder role
    void UpdateJoinTeleport(Player* bot);
    BotCombat::SpellThrottle _throttle;
    BotCombat::EngageTimer _engageTimer;
    std::map<size_t, std::string> _lastSkip;                // rotation step -> last logged skip reason (log level 2)
};

#endif

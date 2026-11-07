/*
 * mod-playerbots: battlegrounds (owner's request, 2026-11-06).
 *
 * The references are AzerothCore's mod-playerbots (BattleGroundJoinAction, BGStatusAction, BGTactics: WSG flags, base
 * captures, roles attacker / defender) and the dungeon finder fill of this module; SkyFire-playerbots has no
 * battleground code. Everything is rewritten for this core's battlegrounds, which differ from 3.3.5:
 *  - queues: WorldSession::JoinBattlegroundQueue (the battlemaster join) and HandleBattleFieldPort (accept / leave),
 *    called on the bot's thread as the client's packets would be (this core handles them on the session's thread);
 *  - flags (Warsong Gulch, Twin Peaks): picked up by using the flag object; a capture happens on the server when the
 *    carrier stands in his base's area trigger (Battleground*::_CheckPositions), no client area trigger needed;
 *  - bases (Arathi Basin, Battle for Gilneas, Deepwind Gorge and any other capture-point battleground): banners of type
 *    GAMEOBJECT_TYPE_CAPTURE_POINT, opened with their lock's spell; who holds one is its spell visual
 *    (Battleground::UpdateCapturePoint: SpellVisual1 neutral, 2 / 3 assaulted by Horde / Alliance, 4 / 5 held);
 *  - the battleground's objects are Battleground::BgObjects (public).
 * Everything here runs on the bot's own (battleground) map thread.
 */

#ifndef MOD_PLAYERBOTS_BOTBATTLEGROUND_H
#define MOD_PLAYERBOTS_BOTBATTLEGROUND_H

#include "Define.h"
#include "ObjectGuid.h"
#include "Position.h"
#include "BotBattlegroundPaths.h"
#include <string>
#include <vector>

class Battleground;
class Player;
class Unit;

namespace BotBattleground
{
    // what a bot remembers between checks inside a battleground
    struct State
    {
        uint32 instanceId = 0;                              // the battleground these belong to
        ObjectGuid objective;                               // the flag / banner it goes for
        Position target;                                    // where it is heading
        bool hasTarget = false;
        Position hop;                                       // the step being walked towards it (pathfinding, <= 50 yd)
        std::vector<PathPoint> route;                       // waypoints towards a far target (BotBattlegroundPaths)
        size_t routeIndex = 0;
        bool routeTried = false;
        bool failLogged = false;
        uint32 selectTimer = 0;                             // ms until the objective is chosen again
        Position lastPos;                                   // stuck check
        uint32 stuckTime = 0;
        uint32 deadTime = 0;                                // ms dead (spirit released after a moment)
        uint32 rezTimer = 0;
        uint32 leaveTimer = 0;                              // ms after the end before leaving
        bool defender = false;                              // roles as on AzerothCore: a few stay home
        bool logged = false;
    };

    // battleground queue slots with an invitation: accepted, as CMSG_BATTLEFIELD_PORT; true when one was
    bool AcceptInvites(Player* bot);
    // leaves every battleground queue it waits in (not one it is invited to or fighting in)
    void LeaveQueues(Player* bot);
    bool IsQueued(Player* bot);
    bool IsInvited(Player* bot);
    // as the battlemaster join, alone (bgTypeId = BattlemasterList id, random included); false + why when refused
    bool JoinQueue(Player* bot, uint16 bgTypeId, uint8 roles, std::string& why);
    // a normal battleground queue (not arena, rated, brawl or Ashran)
    bool IsFillableQueue(uint8 queueTypeId);

    // in a battleground or arena that has started: the enemy player to fight, the carrier of its team's flag first,
    // then the nearest; null when none is in range
    Unit* SelectEnemy(Player* bot, float range);
    // the bot carries a flag (it runs home, it does not stop to fight)
    bool CarriesFlag(Player* bot);

    // In a battleground, every few hundred ms: releases the spirit and queues at the spirit healer when dead, leaves
    // once it is over, and otherwise moves to its objective when it is not fighting (engaged = the combat code owns the
    // movement). True while the bot is in a battleground (the follow code then stays out).
    bool Update(Player* bot, State& state, bool engaged, uint32 diff);
}

#endif

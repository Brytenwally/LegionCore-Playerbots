/*
 * mod-playerbots: battlegrounds. See BotBattleground.h.
 */

#include "BotBattleground.h"
#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "BattlegroundPackets.h"
#include "Creature.h"
#include "DB2Stores.h"
#include "GameObject.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Opcodes.h"
#include "PathGenerator.h"
#include "Pet.h"
#include "Player.h"
#include "PlayerDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Spell.h"
#include "Timer.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <cmath>
#include <map>
#include <mutex>
#include <vector>

namespace
{
    namespace BG = MS::Battlegrounds;

    uint32 const MoveId = 0xB670;                           // point movement id of the tactics
    float const FlagUseRange = 4.0f;                        // EventPlayerClickedOnFlag wants 10 yd; closer is safer
    float const CaptureRange = 4.0f;                        // a banner is opened from close by
    float const DroppedFlagRange = 40.0f;                   // a flag on the ground this close is worth a detour
    float const CarrierChaseRange = 120.0f;
    float const EscortRange = 60.0f;

    // Warsong Gulch and Twin Peaks (Legion entries, BattlegroundWarsongGulch.h / BattlegroundTwinPeaks.h)
    uint32 const AllianceFlagEntry = 227741;                // stands in the Alliance base
    uint32 const HordeFlagEntry = 227740;
    uint32 const AllianceFlagGroundEntry = 227745;          // a dropped flag
    uint32 const HordeFlagGroundEntry = 227744;
    uint32 const FlagAuraHorde = 156618;                    // SPELL_BG_HORDE_FLAG: carries the Horde flag
    uint32 const FlagAuraAlliance = 156621;                 // SPELL_BG_ALLIANCE_FLAG
    uint32 const FlagAuraNetherstorm = 34976;               // Eye of the Storm

    bool IsFlagBattleground(uint16 type)
    {
        return type == BG::BattlegroundTypeId::BattlegroundWarsongGulch || type == BG::BattlegroundTypeId::BattlegroundTwinPeaks;
    }

    // the area trigger where a team's carrier captures (its own flag room), as _CheckPositions tests it
    uint32 CaptureTrigger(uint16 type, TeamId team)
    {
        if (type == BG::BattlegroundTypeId::BattlegroundTwinPeaks)
            return team == TEAM_ALLIANCE ? 5904 : 5905;
        return team == TEAM_ALLIANCE ? 3646 : 3647;
    }

    // ---- objects of the battleground ----------------------------------------------------------------------------

    GameObject* FindBgObject(Battleground* bg, uint32 entry)
    {
        Map* map = bg->GetBgMap();
        if (!map)
            return nullptr;
        for (ObjectGuid const& guid : bg->BgObjects)
            if (!guid.IsEmpty() && guid.GetEntry() == entry)
                if (GameObject* go = map->GetGameObject(guid))
                    return go;
        return nullptr;
    }

    std::vector<GameObject*> CapturePoints(Battleground* bg)
    {
        std::vector<GameObject*> points;
        Map* map = bg->GetBgMap();
        if (!map)
            return points;
        for (ObjectGuid const& guid : bg->BgObjects)
        {
            if (guid.IsEmpty())
                continue;
            GameObject* go = map->GetGameObject(guid);
            if (go && go->isSpawned() && go->GetGOInfo()->type == GAMEOBJECT_TYPE_CAPTURE_POINT)
                points.push_back(go);
        }
        return points;
    }

    // who holds a banner: Battleground::UpdateCapturePoint sets its spell visual
    enum class PointState : uint8 { Neutral, HordeAssault, AllianceAssault, Horde, Alliance };

    PointState StateOf(GameObject* go)
    {
        auto const& info = go->GetGOInfo()->capturePoint;
        uint32 const visual = go->GetUInt32Value(GAMEOBJECT_FIELD_SPELL_VISUAL_ID);
        if (visual && visual == info.SpellVisual2)
            return PointState::HordeAssault;
        if (visual && visual == info.SpellVisual3)
            return PointState::AllianceAssault;
        if (visual && visual == info.SpellVisual4)
            return PointState::Horde;
        if (visual && visual == info.SpellVisual5)
            return PointState::Alliance;
        return PointState::Neutral;
    }

    // the spell a client casts to open an object with this lock (a banner): any spell whose OPEN_LOCK effect has the
    // lock's type, a cast-time one first. Built once from the spell store.
    std::mutex openSpellLock;
    std::map<uint32, uint32> openSpells;                    // lock type -> spell id
    bool openSpellsBuilt = false;

    uint32 OpenSpellFor(GameObject* go)
    {
        LockEntry const* lock = sLockStore.LookupEntry(go->GetGOInfo()->GetLockId());
        if (!lock)
            return 0;

        std::lock_guard<std::mutex> guard(openSpellLock);
        if (!openSpellsBuilt)
        {
            openSpellsBuilt = true;
            std::map<uint32, std::pair<bool, uint32>> best;  // lock type -> (has a cast time, spell)
            uint32 const size = sSpellMgr->GetSpellInfoStoreSize();
            for (uint32 id = 1; id < size; ++id)
            {
                SpellInfo const* info = sSpellMgr->GetSpellInfo(id);
                if (!info || info->IsPassive())
                    continue;
                for (SpellEffectInfo const* effect : info->Effects)
                {
                    if (!effect || effect->Effect != SPELL_EFFECT_OPEN_LOCK)
                        continue;
                    uint32 const type = uint32(effect->MiscValue);
                    bool const castTime = info->GetMisc()->CastTimes.Base > 0;
                    auto itr = best.find(type);
                    if (itr == best.end() || (castTime && !itr->second.first))
                        best[type] = { castTime, id };
                }
            }
            for (auto const& pair : best)
                openSpells[pair.first] = pair.second.second;
        }

        for (uint8 i = 0; i < 8; ++i)
        {
            if (lock->Type[i] != LOCK_KEY_SKILL)
                continue;
            auto itr = openSpells.find(uint32(lock->Index[i]));
            if (itr != openSpells.end())
                return itr->second;
        }
        return 0;
    }

    // ---- movement -----------------------------------------------------------------------------------------------

    void StandStill(Player* bot)
    {
        if (bot->IsMounted())
            bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
        bot->GetMotionMaster()->Clear();
        bot->GetMotionMaster()->MoveIdle();
        bot->StopMoving();
        bot->RemoveUnitMovementFlag(MOVEMENTFLAG_MASK_MOVING);
    }

    Position NearPoint(Player* bot, Position const& center, float radius);

    // One guarded step towards dest (owner, build #31: bots walked through walls and up cliffs). The path is computed
    // first; no path or a straight-line "shortcut" (PathGenerator's fallback through walls and terrain) is refused, and
    // a long path is walked in steps of HopLength yards along it (one path holds at most MAX_PATH_LENGTH polygons, so a
    // trip across the map came back incomplete and ended in that same shortcut). Without movement maps (or swimming) only
    // a short step in sight is allowed. False when no such step exists.
    float const HopLength = 50.0f;

    bool Step(Player* bot, BotBattleground::State& state, Position const& dest)
    {
        PathGenerator path(bot);
        path.CalculatePath(dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ(), false);
        PathType const type = path.GetPathType();
        Position hop = dest;
        if (type & PATHFIND_NOT_USING_PATH)
        {
            if (bot->GetExactDist(&dest) > 40.0f || !bot->IsWithinLOS(dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ()))
                return false;
        }
        else if (type & (PATHFIND_NOPATH | PATHFIND_SHORTCUT))
            return false;
        else
        {
            Movement::PointsArray const& points = path.GetPath();
            if (points.size() < 2)
                return false;
            float walked = 0.0f;
            size_t i = 1;
            for (; i < points.size(); ++i)
            {
                walked += (points[i] - points[i - 1]).length();
                if (walked >= HopLength)
                    break;
            }
            G3D::Vector3 const& end = points[std::min(i, points.size() - 1)];
            hop.Relocate(end.x, end.y, end.z);
        }
        state.hop = hop;
        MotionMaster* motion = bot->GetMotionMaster();
        motion->Clear();
        motion->MovePoint(MoveId, hop.GetPositionX(), hop.GetPositionY(), hop.GetPositionZ(), true);
        return true;
    }

    // Towards pos: far away on a map with waypoint paths (Warsong Gulch, Arathi Basin, Alterac Valley, Eye of the Storm,
    // Isle of Conquest), along the waypoints first; every step guarded (Step). A target nothing leads to is left standing
    // (logged once) after a short random step, never walked to through a wall.
    float const RouteMinDistance = 45.0f;

    void MoveTo(Player* bot, BotBattleground::State& state, Position const& pos)
    {
        MotionMaster* motion = bot->GetMotionMaster();
        bool const newTarget = !state.hasTarget || state.target.GetExactDist(&pos) >= 3.0f;
        if (newTarget)
        {
            state.target = pos;
            state.hasTarget = true;
            state.stuckTime = 0;
            state.lastPos = bot->GetPosition();
            state.route.clear();
            state.routeIndex = 0;
            state.routeTried = false;
            state.failLogged = false;
        }
        else if (motion->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE && bot->GetExactDist(&state.hop) > 3.0f)
            return;                                         // still walking the current step

        if (bot->GetExactDist(&pos) > RouteMinDistance)
        {
            if (!state.routeTried)
            {
                state.routeTried = true;
                state.route = BotBattleground::Route(bot->GetMapId(),
                    { bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ() },
                    { pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ() });
                state.routeIndex = 0;
            }
            while (state.routeIndex < state.route.size())
            {
                BotBattleground::PathPoint const& point = state.route[state.routeIndex];
                if (bot->GetExactDist(point.x, point.y, point.z) > 5.0f)
                    break;
                ++state.routeIndex;
            }
            if (state.routeIndex < state.route.size())
            {
                BotBattleground::PathPoint const& point = state.route[state.routeIndex];
                if (Step(bot, state, Position(point.x, point.y, point.z)))
                    return;
                ++state.routeIndex;                         // an unreachable waypoint: the next one, then the target
            }
        }

        if (Step(bot, state, pos))
            return;

        if (!state.failLogged)
        {
            state.failLogged = true;
            TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' finds no path to %.1f %.1f %.1f (map %u): it does not walk through walls.",
                bot->GetName(), pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), bot->GetMapId());
        }
        // perhaps it stands off the movement map: a short step nearby, then the next check tries again
        Step(bot, state, NearPoint(bot, bot->GetPosition(), 10.0f));
    }

    Position NearPoint(Player* bot, Position const& center, float radius)
    {
        Position pos;
        bot->GetRandomPoint(center, radius, pos);
        if (!std::isfinite(pos.GetPositionZ()) || std::fabs(pos.GetPositionZ() - center.GetPositionZ()) > 8.0f)
            return center;
        return pos;
    }

    // ---- players --------------------------------------------------------------------------------------------------

    TeamId TeamOf(BattlegroundPlayer const& info)
    {
        return info.Team == ALLIANCE ? TEAM_ALLIANCE : TEAM_HORDE;
    }

    // the flag aura a player of team `team` wears when he carries the enemy flag
    uint32 CarriedFlagAura(TeamId team)
    {
        return team == TEAM_ALLIANCE ? FlagAuraHorde : FlagAuraAlliance;
    }

    // a player of team `team` carrying the other team's flag (or the Netherstorm flag), on the bot's map
    Player* FindCarrier(Player* bot, Battleground* bg, TeamId team)
    {
        uint32 const aura = CarriedFlagAura(team);
        for (auto const& pair : bg->GetPlayers())
        {
            if (TeamOf(pair.second) != team)
                continue;
            Player* player = ObjectAccessor::GetPlayer(*bot, pair.first);
            if (player && player->IsAlive() && (player->HasAura(aura) || player->HasAura(FlagAuraNetherstorm)))
                return player;
        }
        return nullptr;
    }

    // ---- flag battlegrounds (Warsong Gulch, Twin Peaks) -------------------------------------------------------

    bool UseFlag(Player* bot, BotBattleground::State& state, GameObject* flag)
    {
        if (bot->GetDistance(flag) > FlagUseRange)
        {
            MoveTo(bot, state, flag->GetPosition());
            return true;
        }
        StandStill(bot);
        flag->Use(bot);                                     // GameObject::Use -> Battleground::EventPlayerClickedOnFlag
        state.selectTimer = 0;
        return true;
    }

    void FlagTactics(Player* bot, Battleground* bg, BotBattleground::State& state)
    {
        uint16 const type = bg->GetTypeID(true);
        TeamId const team = bot->GetBGTeamId();
        TeamId const enemyTeam = BG::GetOtherTeamID(team);

        // carrying the enemy flag: home to the capture point; the server captures once there (_CheckPositions)
        if (bot->HasAura(CarriedFlagAura(team)))
        {
            if (AreaTriggerEntry const* trigger = sAreaTriggerStore.LookupEntry(CaptureTrigger(type, team)))
                MoveTo(bot, state, Position(trigger->Pos.X, trigger->Pos.Y, trigger->Pos.Z));
            return;
        }

        // a flag on the ground nearby: the enemy's is picked up, ours is returned
        for (uint32 entry : { AllianceFlagGroundEntry, HordeFlagGroundEntry })
            if (GameObject* dropped = bot->FindNearestGameObject(entry, DroppedFlagRange))
                if (dropped->isSpawned())
                {
                    UseFlag(bot, state, dropped);
                    return;
                }

        // the enemy carries our flag: kill him (defenders always, attackers most of the time)
        if (Player* enemyCarrier = FindCarrier(bot, bg, enemyTeam))
            if (bot->GetDistance(enemyCarrier) < CarrierChaseRange && (state.defender || urand(0, 99) < 70))
            {
                MoveTo(bot, state, enemyCarrier->GetPosition());
                return;
            }

        uint32 const ownFlag = team == TEAM_ALLIANCE ? AllianceFlagEntry : HordeFlagEntry;
        uint32 const enemyFlag = team == TEAM_ALLIANCE ? HordeFlagEntry : AllianceFlagEntry;

        if (state.defender)
        {
            if (GameObject* home = FindBgObject(bg, ownFlag))
                if (!state.hasTarget || state.target.GetExactDist(home) > 15.0f || bot->GetDistance(state.target) < 2.0f)
                    MoveTo(bot, state, NearPoint(bot, home->GetPosition(), 10.0f));
            return;
        }

        // our carrier is out: escort him
        if (Player* ourCarrier = FindCarrier(bot, bg, team))
            if (ourCarrier != bot && bot->GetDistance(ourCarrier) < EscortRange)
            {
                MoveTo(bot, state, ourCarrier->GetPosition());
                return;
            }

        // attackers go for the enemy flag and take it when it stands there
        if (GameObject* flag = FindBgObject(bg, enemyFlag))
        {
            if (flag->isSpawned() && bot->GetDistance(flag) < 30.0f)
            {
                UseFlag(bot, state, flag);
                return;
            }
            MoveTo(bot, state, flag->GetPosition());
        }
    }

    // ---- capture-point battlegrounds (Arathi Basin, Battle for Gilneas, Deepwind Gorge...) --------------------

    bool OurState(PointState point, TeamId team)
    {
        return team == TEAM_ALLIANCE ? (point == PointState::Alliance || point == PointState::AllianceAssault)
            : (point == PointState::Horde || point == PointState::HordeAssault);
    }

    bool CapturePointTactics(Player* bot, Battleground* bg, BotBattleground::State& state)
    {
        std::vector<GameObject*> const points = CapturePoints(bg);
        if (points.empty())
            return false;

        TeamId const team = bot->GetBGTeamId();
        Map* map = bg->GetBgMap();

        // keep the chosen banner until it is ours, unless it has gone
        GameObject* chosen = !state.objective.IsEmpty() && map ? map->GetGameObject(state.objective) : nullptr;
        if (chosen && OurState(StateOf(chosen), team) && !state.defender)
            chosen = nullptr;
        if (!chosen || state.selectTimer == 0)
        {
            // attackers: the nearest banner not ours (neutral or enemy, or one the enemy assaults); defenders: the
            // nearest banner we hold or assault, to guard
            GameObject* best = nullptr;
            float bestDist = 0.0f;
            for (GameObject* go : points)
            {
                bool const ours = OurState(StateOf(go), team);
                if (state.defender != ours)
                    continue;
                // a little randomness, so a team does not move as one block
                float const dist = bot->GetDistance(go) + frand(0.0f, 60.0f);
                if (!best || dist < bestDist)
                {
                    best = go;
                    bestDist = dist;
                }
            }
            if (!best && state.defender)                    // nothing to guard yet: attack
                for (GameObject* go : points)
                    if (!OurState(StateOf(go), team) && (!best || bot->GetDistance(go) < bot->GetDistance(best)))
                        best = go;
            chosen = best;
            state.objective = chosen ? chosen->GetGUID() : ObjectGuid::Empty;
            state.selectTimer = 20000;
        }
        if (!chosen)
            return false;

        PointState const point = StateOf(chosen);
        if (OurState(point, team))
        {
            // guarding: stay around it
            if (!state.hasTarget || state.target.GetExactDist(chosen) > 15.0f || bot->GetDistance(state.target) < 2.0f)
                MoveTo(bot, state, NearPoint(bot, chosen->GetPosition(), 10.0f));
            return true;
        }

        if (bot->GetDistance(chosen) > CaptureRange)
        {
            MoveTo(bot, state, chosen->GetPosition());
            return true;
        }

        // at the banner: open it as a client would (its lock's spell, a channel the enemy can interrupt)
        if (bot->IsNonMeleeSpellCast(false))
            return true;
        StandStill(bot);
        if (uint32 const spellId = OpenSpellFor(chosen))
            if (SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId))
            {
                SpellCastTargets targets;
                targets.SetGOTarget(chosen);
                SpellCastResult const result = bot->CastSpell(targets, info, nullptr, TRIGGERED_NONE);
                if (result == SPELL_CAST_OK)
                    return true;
            }
        // no usable lock spell: use it directly (GameObject::Use -> EventPlayerClickedOnFlag)
        chosen->Use(bot);
        return true;
    }

    // ---- other battlegrounds: where the fight is --------------------------------------------------------------

    void GenericTactics(Player* bot, Battleground* bg, BotBattleground::State& state)
    {
        // the nearest enemy player anywhere on the map, else the nearest teammate who is moving away
        TeamId const team = bot->GetBGTeamId();
        Player* best = nullptr;
        float bestDist = 0.0f;
        for (auto const& pair : bg->GetPlayers())
        {
            if (TeamOf(pair.second) == team)
                continue;
            Player* player = ObjectAccessor::GetPlayer(*bot, pair.first);
            if (!player || !player->IsAlive())
                continue;
            float const dist = bot->GetDistance(player);
            if (!best || dist < bestDist)
            {
                best = player;
                bestDist = dist;
            }
        }
        if (best)
        {
            if (!state.hasTarget || state.target.GetExactDist(best) > 15.0f)
                MoveTo(bot, state, best->GetPosition());
            return;
        }
        for (auto const& pair : bg->GetPlayers())
        {
            if (TeamOf(pair.second) != team || pair.first == bot->GetGUID())
                continue;
            Player* player = ObjectAccessor::GetPlayer(*bot, pair.first);
            if (player && player->IsAlive() && bot->GetDistance(player) > 20.0f && (!best || bot->GetDistance(player) < bestDist))
            {
                best = player;
                bestDist = bot->GetDistance(player);
            }
        }
        if (best && (!state.hasTarget || state.target.GetExactDist(best) > 15.0f))
            MoveTo(bot, state, NearPoint(bot, best->GetPosition(), 5.0f));
    }

    // dead: release the spirit (CMSG_REPOP_REQUEST), then wait at the spirit healer's resurrection
    void HandleDeath(Player* bot, Battleground* bg, BotBattleground::State& state, uint32 diff)
    {
        state.hasTarget = false;
        state.objective.Clear();
        if (!bot->HasFlag(PLAYER_FIELD_PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
        {
            state.deadTime += diff;
            if (state.deadTime < 2000 || bot->HasAuraType(SPELL_AURA_PREVENT_RESURRECTION))
                return;
            state.deadTime = 0;
            if (bot->getDeathState() == JUST_DIED)
                bot->KillPlayer();
            bot->RemovePet(nullptr, PET_SAVE_NOT_IN_SLOT, true);
            bot->BuildPlayerRepop();
            bot->RepopAtGraveyard();
            state.rezTimer = 0;
            return;
        }

        state.rezTimer += diff;
        if (state.rezTimer < 3000)
            return;
        state.rezTimer = 0;
        Map* map = bg->GetBgMap();
        if (!map)
            return;
        for (ObjectGuid const& guid : bg->BgCreatures)
        {
            if (guid.IsEmpty())
                continue;
            Creature* healer = map->GetCreature(guid);
            if (healer && healer->isSpiritService() && bot->IsWithinDistInMap(healer, 60.0f))
            {
                // what HandleAreaSpiritHealerQueue does: the next wave resurrects it
                bg->AddPlayerToResurrectQueue(healer->GetGUID(), bot->GetGUID());
                return;
            }
        }
    }
}

namespace BotBattleground
{
    bool IsFillableQueue(uint8 queueTypeId)
    {
        return queueTypeId >= BG::BattlegroundQueueTypeId::BattlegroundAlteracValley &&
            queueTypeId <= BG::BattlegroundQueueTypeId::BattlegroundRandom;
    }

    bool IsQueued(Player* bot)
    {
        for (uint32 i = 0; i < PLAYER_MAX_BATTLEGROUND_QUEUES; ++i)
            if (bot->GetBattlegroundQueueTypeId(i) != BG::BattlegroundQueueTypeId::None)
                return true;
        return false;
    }

    bool IsInvited(Player* bot)
    {
        for (uint32 i = 0; i < PLAYER_MAX_BATTLEGROUND_QUEUES; ++i)
        {
            uint8 const queue = bot->GetBattlegroundQueueTypeId(i);
            if (queue != BG::BattlegroundQueueTypeId::None && bot->IsInvitedForBattlegroundQueueType(queue))
                return true;
        }
        return false;
    }

    bool AcceptInvites(Player* bot)
    {
        if (bot->InBattleground() || bot->IsBeingTeleported() || !bot->IsInWorld())
            return false;
        for (uint32 i = 0; i < PLAYER_MAX_BATTLEGROUND_QUEUES; ++i)
        {
            uint8 const queue = bot->GetBattlegroundQueueTypeId(i);
            if (queue == BG::BattlegroundQueueTypeId::None || !bot->IsInvitedForBattlegroundQueueType(queue))
                continue;
            // the client's "Enter Battle": Port::Read gives the ticket (its id is the queue slot) and the answer
            WorldPackets::Battleground::Port port(WorldPacket(CMSG_BATTLEFIELD_PORT, 0));
            port.Ticket.Id = i;
            port.Ticket.RequesterGuid = bot->GetGUID();
            port.AcceptedInvite = true;
            TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' enters its battleground (queue %u).", bot->GetName(), queue);
            bot->GetSession()->HandleBattleFieldPort(port);
            return true;
        }
        return false;
    }

    void LeaveQueues(Player* bot)
    {
        for (uint32 i = 0; i < PLAYER_MAX_BATTLEGROUND_QUEUES; ++i)
        {
            uint8 const queue = bot->GetBattlegroundQueueTypeId(i);
            if (queue == BG::BattlegroundQueueTypeId::None || bot->IsInvitedForBattlegroundQueueType(queue))
                continue;
            WorldPackets::Battleground::Port port(WorldPacket(CMSG_BATTLEFIELD_PORT, 0));
            port.Ticket.Id = i;
            port.Ticket.RequesterGuid = bot->GetGUID();
            port.AcceptedInvite = false;
            bot->GetSession()->HandleBattleFieldPort(port);
        }
    }

    bool JoinQueue(Player* bot, uint16 bgTypeId, uint8 roles, std::string& why)
    {
        if (bot->InBattleground())
        {
            why = "already in a battleground";
            return false;
        }
        if (bot->GetGroup())
        {
            why = "in a group";
            return false;
        }
        uint8 const queue = BG::GetBgQueueTypeIdByBgTypeID(bgTypeId);
        WorldPackets::Battleground::IgnorMapInfo blacklist;
        bot->GetSession()->JoinBattlegroundQueue(bgTypeId, roles, false, blacklist);
        for (uint32 i = 0; i < PLAYER_MAX_BATTLEGROUND_QUEUES; ++i)
            if (bot->GetBattlegroundQueueTypeId(i) == queue)
                return true;
        why = "the queue refused it (deserter, dungeon finder, level, or no free queue slot)";
        return false;
    }

    bool CarriesFlag(Player* bot)
    {
        return bot->HasAura(FlagAuraHorde) || bot->HasAura(FlagAuraAlliance) || bot->HasAura(FlagAuraNetherstorm);
    }

    Unit* SelectEnemy(Player* bot, float range)
    {
        Battleground* bg = bot->GetBattleground();
        if (!bg || bg->GetStatus() != STATUS_IN_PROGRESS)
            return nullptr;

        TeamId const team = bot->GetBGTeamId();
        Player* best = nullptr;
        float bestDist = 0.0f;
        for (auto const& pair : bg->GetPlayers())
        {
            if (TeamOf(pair.second) == team)
                continue;
            Player* enemy = ObjectAccessor::GetPlayer(*bot, pair.first);
            if (!enemy || !enemy->IsInWorld() || !enemy->IsAlive() || !bot->IsValidAttackTarget(enemy))
                continue;
            float dist = bot->GetDistance(enemy);
            if (dist > range || !bot->IsWithinLOSInMap(enemy))
                continue;
            // the carrier of our flag counts as much nearer; a wounded enemy a little
            if (CarriesFlag(enemy))
                dist -= 30.0f;
            dist -= (100.0f - enemy->GetHealthPct()) * 0.1f;
            if (!best || dist < bestDist)
            {
                best = enemy;
                bestDist = dist;
            }
        }
        return best;
    }

    bool Update(Player* bot, State& state, bool engaged, uint32 diff)
    {
        Battleground* bg = bot->GetBattleground();
        if (!bg || !bot->InBattleground())
            return false;

        if (state.instanceId != bg->GetInstanceID())
        {
            state = State();
            state.instanceId = bg->GetInstanceID();
            // AzerothCore's roles: about a quarter of the team defends (by guid, so it stays the same)
            state.defender = bot->GetGUID().GetCounter() % 4 == 0;
            TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' is in battleground %u (%s).", bot->GetName(),
                uint32(bg->GetTypeID(true)), state.defender ? "defender" : "attacker");
        }

        if (state.selectTimer > diff)
            state.selectTimer -= diff;
        else
            state.selectTimer = 0;

        // over: leave a few seconds later, as players do
        if (bg->GetStatus() == STATUS_WAIT_LEAVE)
        {
            state.leaveTimer += diff;
            if (state.leaveTimer >= 5000)
            {
                TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' leaves its battleground: it is over.", bot->GetName());
                bot->LeaveBattleground();
            }
            return true;
        }

        if (!bot->IsAlive())
        {
            HandleDeath(bot, bg, state, diff);
            return true;
        }
        state.deadTime = 0;

        // the arena and the waiting time before the doors open: no tactics (combat still fights what comes)
        if (bg->IsArena() || bg->GetStatus() != STATUS_IN_PROGRESS || engaged || bot->IsNonMeleeSpellCast(false))
            return true;

        // stuck on the way (an obstacle, a bad path): a new objective and a short detour
        if (state.hasTarget && bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
        {
            if (bot->GetExactDist(&state.lastPos) < 1.0f)
            {
                state.stuckTime += diff;
                if (state.stuckTime >= 8000)
                {
                    state.stuckTime = 0;
                    state.selectTimer = 0;
                    state.objective.Clear();
                    Position detour = NearPoint(bot, bot->GetPosition(), 15.0f);
                    state.hasTarget = false;
                    MoveTo(bot, state, detour);
                    return true;
                }
            }
            else
            {
                state.stuckTime = 0;
                state.lastPos = bot->GetPosition();
            }
        }

        uint16 const type = bg->GetTypeID(true);
        if (IsFlagBattleground(type))
            FlagTactics(bot, bg, state);
        else if (!CapturePointTactics(bot, bg, state))
            GenericTactics(bot, bg, state);
        return true;
    }
}

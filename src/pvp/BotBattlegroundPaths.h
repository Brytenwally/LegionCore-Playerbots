/*
 * mod-playerbots: battleground waypoint paths (AzerothCore's, from vmangos) and the graph bots route on.
 */

#ifndef MOD_PLAYERBOTS_BOTBATTLEGROUNDPATHS_H
#define MOD_PLAYERBOTS_BOTBATTLEGROUNDPATHS_H

#include "Define.h"
#include <vector>

namespace BotBattleground
{
    struct PathPoint
    {
        float x, y, z;
    };

    struct BattlegroundPath
    {
        bool oneWay;                                        // a jump down: forwards only
        std::vector<PathPoint> points;
    };

    // the paths of a battleground map (Warsong Gulch 489, Arathi Basin 529, Alterac Valley 30, Eye of the Storm 566,
    // Isle of Conquest 628); empty for the others (BotBattlegroundPaths.cpp, generated)
    std::vector<BattlegroundPath> const& PathsOfMap(uint32 mapId);

    // the waypoints to walk from `from` to `to` on that map, the last one near `to`; empty when the map has no paths or
    // no waypoint is near enough to either end (then the bot walks by pathfinding alone)
    std::vector<PathPoint> Route(uint32 mapId, PathPoint const& from, PathPoint const& to);
}

#endif

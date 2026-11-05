/*
 * mod-playerbots: follow formations, from SkyFire-playerbots (engine/BotFormation: role slots fanned per
 * role, same-role bots spread by guid order) and mod-playerbots (Formations.cpp: near, line, queue, circle,
 * arrow, shield, chaos, far).
 *
 * A slot is a distance and an angle off the anchor's facing (0 = in front, PI = behind, positive = his left),
 * which is what MotionMaster::MoveFollow takes, so the core's follow movement keeps the slot while he walks.
 * Runs on the bot's map thread; members = the group members on that map (BotCombat::GroupMembersOnMap).
 */

#ifndef MOD_PLAYERBOTS_BOTFORMATION_H
#define MOD_PLAYERBOTS_BOTFORMATION_H

#include "Define.h"
#include "ObjectGuid.h"
#include <string>
#include <vector>

class Player;

namespace BotFormation
{
    enum class Shape : uint8
    {
        Role,       // SkyFire's: tank ahead at his side, melee rear left, ranged rear right, healer behind
        Near,       // everyone close behind, fanned
        Line,       // abreast, the leader in the middle
        Queue,      // single file behind the leader
        Circle,     // evenly around the leader
        Arrow,      // rows: tanks in front, then melee, ranged, healers
        Shield,     // tanks in a line in front, everyone else in a line behind
        Chaos,      // near, with a random offset changed every few seconds
        Far         // well behind, fanned
    };

    bool ShapeFromName(std::string const& name, Shape& shape);
    char const* ShapeName(Shape shape);
    char const* ShapeList();

    struct Slot
    {
        float distance = 2.0f;
        float angle = 0.0f;
        ObjectGuid anchor;          // whom to follow: the leader, or the one ahead in a queue
    };

    // jitterForward / jitterLeft: the chaos offset (yards), ignored by the other shapes
    Slot SlotFor(Player* bot, Player* leader, std::vector<Player*> const& members, Shape shape,
        float jitterForward = 0.0f, float jitterLeft = 0.0f);
}

#endif

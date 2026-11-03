/*
 * mod-playerbots: movement building blocks. See BotMovement.h.
 */

#include "BotMovement.h"
#include "MotionMaster.h"
#include "Player.h"
#include <cmath>

namespace BotMovement
{
    void FormationOffset(BotCombat::BotRole role, uint8 roleIndex, float& distance, float& angle)
    {
        switch (role)
        {
            case BotCombat::BotRole::Healer:
                distance = 9.0f;
                angle = roleIndex ? 0.7f : 0.0f;
                break;
            case BotCombat::BotRole::Dps:
                distance = 4.0f;
                angle = (roleIndex % 2) ? -0.8f : 0.8f;
                break;
            default:
                distance = 2.5f;
                angle = roleIndex ? 0.8f : 0.0f;
                break;
        }
    }

    Position FormationPosition(Player* leader, BotCombat::BotRole role, uint8 roleIndex)
    {
        float distance = 2.5f;
        float angle = 0.0f;
        FormationOffset(role, roleIndex, distance, angle);

        float const direction = leader->GetOrientation() + float(M_PI) + angle;
        float const x = leader->GetPositionX() + distance * std::cos(direction);
        float const y = leader->GetPositionY() + distance * std::sin(direction);
        float z = leader->GetPositionZ();
        leader->UpdateGroundPositionZ(x, y, z);
        return Position(x, y, z, leader->GetOrientation());
    }

    bool MoveToFormation(Player* bot, Player* leader, BotCombat::BotRole role, uint8 roleIndex, float tolerance)
    {
        Position const spot = FormationPosition(leader, role, roleIndex);
        if (bot->GetDistance(spot.GetPositionX(), spot.GetPositionY(), spot.GetPositionZ()) <= tolerance)
            return false;

        MotionMaster* motion = bot->GetMotionMaster();
        if (motion->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
            return false;

        motion->MovePoint(0, spot.GetPositionX(), spot.GetPositionY(), spot.GetPositionZ(), false);
        return true;
    }

    bool CatchUp(Player* bot, Player* leader, BotCombat::BotRole role, uint8 roleIndex, float maxDistance, float maxZ)
    {
        if (bot->GetMap() != leader->GetMap() || bot->IsBeingTeleported() || leader->IsBeingTeleported())
            return false;

        if (bot->GetDistance(leader) <= maxDistance && std::fabs(bot->GetPositionZ() - leader->GetPositionZ()) <= maxZ)
            return false;

        Position const spot = FormationPosition(leader, role, roleIndex);
        bot->NearTeleportTo(spot.GetPositionX(), spot.GetPositionY(), spot.GetPositionZ(), leader->GetOrientation());
        return true;
    }

    void Engage(Player* bot, Unit* target, bool melee, float range)
    {
        if (bot->getVictim() != target)
            bot->Attack(target, melee);

        MotionMaster* motion = bot->GetMotionMaster();
        if (motion->GetCurrentMovementGeneratorType() != CHASE_MOTION_TYPE)
            motion->MoveChase(target, melee ? 0.0f : range);
    }

    void Disengage(Player* bot)
    {
        bot->AttackStop();
        bot->GetMotionMaster()->Clear();
        bot->GetMotionMaster()->MoveIdle();
    }
}

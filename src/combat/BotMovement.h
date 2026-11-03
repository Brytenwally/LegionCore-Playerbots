/*
 * mod-playerbots: movement building blocks, copied from LegionBotAI (LegionCore-7.3.5): role
 * formation behind the leader, catch-up teleport, engaging a target. Not used yet: the bots follow
 * with PlayerbotAI's MoveFollow; formation and combat movement come with plan step 4.
 *
 * Not copied: LegionBotAI's map transfer by hand (RemovePlayerFromMap / AddPlayerToMap). Bots
 * change maps through the core's teleports (summon order), which the bot sessions answer.
 */

#ifndef MOD_PLAYERBOTS_BOTMOVEMENT_H
#define MOD_PLAYERBOTS_BOTMOVEMENT_H

#include "BotCombat.h"
#include "Position.h"

class Player;
class Unit;

namespace BotMovement
{
    // LegionBotAI's formation: distance behind the leader and an angle off his back, by role.
    // roleIndex = the bot's index among the group's bots of the same role (spreads them apart).
    // Tank 2.5 yd, dps 4 yd left/right, healer 9 yd.
    void FormationOffset(BotCombat::BotRole role, uint8 roleIndex, float& distance, float& angle);
    // the formation spot behind leader, on the ground
    Position FormationPosition(Player* leader, BotCombat::BotRole role, uint8 roleIndex);

    // Walks to the formation spot when farther than tolerance from it and not already walking to a
    // point. True if a move was started.
    bool MoveToFormation(Player* bot, Player* leader, BotCombat::BotRole role, uint8 roleIndex, float tolerance = 2.5f);

    // LegionBotAI's catch-up: same map, but farther than maxDistance or more than maxZ above/below
    // (another floor) -> near teleport to the formation spot. True if teleported.
    bool CatchUp(Player* bot, Player* leader, BotCombat::BotRole role, uint8 roleIndex,
        float maxDistance = 80.0f, float maxZ = 3.0f);

    // Starts attacking target (melee swings when melee) and chases it: into melee range, or up to
    // range yards for casters. Re-issues the chase if another movement took over.
    void Engage(Player* bot, Unit* target, bool melee, float range = 0.0f);
    // LegionBotAI's target drop: stop attacking and stand still
    void Disengage(Player* bot);
}

#endif

/*
 * Avoid AoE: step out of harmful ground effects (mod-playerbots' "avoid aoe" strategy, AvoidAoeAction).
 *
 * A hazard is one of (checked in this order, as mod-playerbots, plus Legion's area triggers):
 *  1. a negative aura on the bot cast by a dynamic object (Rain of Fire, Consecration-like puddles);
 *  2. a hostile area trigger the bot stands in whose spell is negative (most Legion ground effects);
 *  3. a hostile trap object casting a damaging spell (lava bombs, fire traps);
 *  4. a hostile unselectable trigger creature whose periodic aura triggers a damaging spell around it.
 * Hazards wider than MaxRadius are ignored (whole-room effects cannot be left), and spells in the whitelist
 * are never avoided (soak / stand-in mechanics).
 *
 * The flee position follows mod-playerbots (MovementAction::BestPositionFor{Melee,Ranged}ToFlee): melee try
 * left / right of their target first and stay in reach when they can, ranged keep their target within
 * spell range; a direction opposite to one taken in the last 5 seconds is refused, so a bot does not walk
 * back and forth between two hazards. Positions that end outside the hazard win, then the farthest one.
 */

#ifndef MOD_PLAYERBOTS_BOT_AVOID_AOE_H
#define MOD_PLAYERBOTS_BOT_AVOID_AOE_H

#include "Define.h"
#include "Position.h"
#include <deque>
#include <set>
#include <string>

class Player;
class Unit;

namespace BotAvoidAoe
{
    struct Options
    {
        float maxRadius = 15.0f;                // hazards wider than this are not avoided
        float maxStep = 8.0f;                   // longest single flee move
        std::set<uint32> whitelist;             // spell ids never avoided
    };

    struct Hazard
    {
        Position center;
        float radius = 0.0f;
        uint32 spellId = 0;
        char const* kind = "";                  // aura, area trigger, trap, trigger unit
    };

    // the last flee directions (absolute angles), kept 5 seconds
    struct FleeMemory
    {
        struct Entry { float angle; uint32 time; };
        std::deque<Entry> recent;
    };

    // the hazard the bot stands in, if any
    bool FindHazard(Player* bot, Options const& options, Hazard& hazard);

    // a reachable position out of (or farther from) the hazard; target = the bot's enemy, may be null
    bool FindFleePosition(Player* bot, Unit* target, bool melee, Hazard const& hazard, Options const& options,
        FleeMemory& memory, Position& out);

    // the hazard spell's English name, for logs and whispers
    std::string SpellName(uint32 spellId);
}

#endif

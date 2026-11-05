/*
 * mod-playerbots: per-spec rotations (plan step 4.2).
 *
 * The table is AshamaneCore-Legion735's g_BotSpecRotations (src/server/scripts/Custom/Bots/BotMgr.cpp),
 * a short priority list for each of the 36 specs with its role, interrupt and dispel. Its author
 * researched the 7.3.5 names and order (Icy Veins / Wowhead guides) and marked Demonology as low
 * confidence; the lists are simple, not optimal.
 *
 * Spells are named, not numbered: the names are looked up once in the server's own Spell.db2. A step
 * casts the first spell of that name the bot knows (HasSpell), so old ranks and other classes'
 * spells of the same name never matter. Aura conditions accept any spell of the aura's name.
 */

#ifndef MOD_PLAYERBOTS_BOTROTATIONS_H
#define MOD_PLAYERBOTS_BOTROTATIONS_H

#include "Define.h"
#include "ObjectGuid.h"
#include "SharedDefines.h"
#include <string>
#include <vector>

class Player;
class Unit;

namespace BotRotations
{
    enum class CombatRole : uint8
    {
        Tank,
        Healer,
        Melee,
        Ranged
    };
    char const* RoleName(CombatRole role);

    enum class Cond : uint8
    {
        Always,
        TargetHealthPctBelow,       // the step's target at or below value %
        SelfHealthPctBelow,         // the bot at or below value %
        ResourceAtLeast,            // the bot's power `power` at least value, in displayed units
        AuraMissingOnSelf,          // the bot lacks auxSpell
        AuraPresentOnSelf,          // the bot has auxSpell (a proc)
        AuraMissingOnTarget         // the target lacks auxSpell from the bot (DoT, HoT)
    };

    enum class StepTarget : uint8
    {
        RoleDefault,                // healers: the heal target; others: the enemy
        Enemy,
        HealTarget
    };

    struct RotationStep
    {
        char const* spell;
        Cond cond = Cond::Always;
        float value = 0.0f;
        uint32 power = 0;           // Powers, for ResourceAtLeast
        char const* auxSpell = nullptr;
        StepTarget target = StepTarget::RoleDefault;
    };

    struct SpecRotation
    {
        uint32 specId = 0;
        uint32 family = 0;          // SpellFamilyNames: the class's spells are preferred among same names
        CombatRole role = CombatRole::Melee;
        std::vector<RotationStep> steps;
        char const* interrupt = nullptr;
        char const* dispel = nullptr;
    };

    // Looks every name of the table up in Spell.db2, once; call after the spells are loaded (startup).
    // Logs the names no spell carries.
    void Initialize();

    SpecRotation const* GetRotation(uint32 specId);
    // the rotation's role, or one from the spec (tank/healer) and class when the spec has no rotation
    CombatRole RoleOf(Player const* bot);

    // the id of a spell of that name the bot knows (highest id first, its class's spells first), 0 if none
    uint32 KnownSpell(Player* bot, char const* name, uint32 family);
    // unit has an aura of that name (from caster when given)
    bool HasAuraNamed(Unit const* unit, char const* name, ObjectGuid const& caster = ObjectGuid::Empty);
    // power as the client shows it (rage, runic power, soul shards are stored x10)
    float DisplayedPower(Player const* bot, Powers power);
    // step's condition for this bot and target; target may be null for Self* conditions
    bool CheckCondition(Player* bot, Unit* target, RotationStep const& step);
    std::string DescribeCondition(RotationStep const& step);
}

#endif

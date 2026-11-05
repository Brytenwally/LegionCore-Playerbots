/*
 * mod-playerbots: combat building blocks, copied from LegionBotAI (LegionCore-7.3.5,
 * src/server/scripts/Custom/LegionBotAI.cpp) and rewritten for this module's threading.
 *
 * Not used yet: plan step 4 (core CLAUDE.md, "Combat plan") builds the combat AI on these.
 * No rotations, spell lists or other per-spec content here: callers pass the spell ids.
 *
 * Threading: everything runs on the bot's own map thread (PlayerScript::OnUpdate or a
 * PostToPlayer action). Teammates are read with ObjectAccessor::GetPlayer(*bot, guid), which only
 * finds players on the bot's map; LegionBotAI's FindPlayer is not safe from a map thread.
 */

#ifndef MOD_PLAYERBOTS_BOTCOMBAT_H
#define MOD_PLAYERBOTS_BOTCOMBAT_H

#include "Define.h"
#include "ObjectGuid.h"
#include "SharedDefines.h"
#include <unordered_map>
#include <vector>

class Player;
class Spell;
class SpellInfo;
class Unit;

namespace BotCombat
{
    // ---- Role ----

    enum class BotRole : uint8
    {
        Tank,
        Healer,
        Dps
    };

    // From the current specialization (ChrSpecialization.Role); no spec = dps.
    BotRole RoleOf(Player const* bot);
    char const* RoleName(BotRole role);

    // ---- Casting (LegionBotAI's BotCast) ----

    // Why BotCast did not cast, for the cast logs of step 4.2.
    enum class CastRefusal : uint8
    {
        None,               // cast
        NoTarget,
        TargetDead,
        UnknownSpell,
        Level,              // spell level above the bot's
        NotLearned,
        Cooldown,
        NoCharges,          // a charge spell with every charge spent
        GlobalCooldown,
        OutOfRange,
        NoLineOfSight,
        Casting,            // already casting or channelling
        Throttled,          // the AI's own per-spell delay (SpellThrottle)
        CastFailed          // the core refused it: see CastOutcome::result
    };
    char const* CastRefusalName(CastRefusal refusal);

    struct CastOutcome
    {
        CastRefusal refusal = CastRefusal::None;
        SpellCastResult result = SPELL_CAST_OK;
        explicit operator bool() const { return refusal == CastRefusal::None; }
    };

    // LegionBotAI's per-spell AI delay: after a cast, the spell is not tried again for the longest of
    // minCooldown, its recovery time and its category recovery time. Keeps the AI from re-casting a
    // spell whose cooldown the core has not started yet (cast time, travel). One per bot.
    class SpellThrottle
    {
    public:
        bool IsThrottled(uint32 spellId, uint32 now) const;
        void Mark(SpellInfo const* spellInfo, uint32 minCooldown, uint32 now);
        void Clear() { _until.clear(); }

    private:
        std::unordered_map<uint32, uint32> _until;          // spell id -> getMSTime() it may be cast again
    };

    // What casting a spell means for a bot: the spell exists, its level is reached, the bot knows it, no
    // cooldown, no global cooldown, not already casting, not throttled; then face the target and cast
    // (not triggered, so costs, range, line of sight and the rest are the core's normal checks).
    CastOutcome BotCast(Player* bot, Unit* target, uint32 spellId, SpellThrottle& throttle, uint32 minCooldown = 1500);

    // ---- Group and targets ----

    // The bot's group members on its own map, alive or not, the bot included (alone: just the bot).
    std::vector<Player*> GroupMembersOnMap(Player* bot);

    // enemy is fighting ally (ally is its victim, or it is in ally's attacker list)
    bool IsAttackingAlly(Unit* enemy, Player* ally);
    // LegionBotAI's pull rule: only a unit attacking one of these allies may be engaged
    bool IsAggressorOf(Unit* enemy, std::vector<Player*> const& allies);
    // the first unit attacking one of the allies (in their order) that the bot may attack, within
    // maxDistance of the bot (0 = any distance)
    Unit* FindAttackerOf(Player* bot, std::vector<Player*> const& allies, float maxDistance = 0.0f);

    // allies alive below healthPct within radius of center
    uint32 CountInjured(std::vector<Player*> const& allies, Unit* center, float radius, float healthPct = 85.0f);
    // LegionBotAI's heal target: the lowest ally below healthPct; a tank below tankPct comes first;
    // the bot itself only below selfPct when nobody else needs it. nullptr = nobody.
    Player* FindLowestHpAlly(Player* bot, std::vector<Player*> const& allies, float healthPct = 85.0f,
        float tankPct = 75.0f, float selfPct = 60.0f);

    // LegionBotAI's engage delay: true once target has been the chosen target for delayMs, so the
    // player swings first. One per bot.
    class EngageTimer
    {
    public:
        bool Ready(ObjectGuid const& target, uint32 now, uint32 delayMs);
        void Reset() { _target.Clear(); }

    private:
        ObjectGuid _target;
        uint32 _since = 0;
    };

    // ---- Interrupts, dispels, buffs, items ----

    // target's interruptible cast (generic with a cast time, else channelled), nullptr when none
    Spell* GetInterruptibleCast(Unit* target);
    // casts spellId (the spec's interrupt or stun) on target when it is casting something interruptible
    bool TryInterrupt(Player* bot, Unit* target, uint32 spellId, SpellThrottle& throttle);

    // LegionBotAI's TryDispel: the dispel types are read from the dispel spell's own effects; the first
    // ally within range with a harmful aura of those types gets the spell. False if nothing to dispel.
    bool TryDispel(Player* bot, std::vector<Player*> const& allies, uint32 spellId, SpellThrottle& throttle,
        float range = 40.0f);

    // casts spellId on target unless target has auraId (0 = spellId) from the bot
    bool KeepAura(Player* bot, Unit* target, uint32 spellId, SpellThrottle& throttle, uint32 auraId = 0);

    // Uses an item from the bot's bags (potion, healthstone): its on-use spell is cast with the item,
    // so the core takes the charge and starts the item's cooldown. Only items the bot has: none are
    // created (owner's alt rule).
    bool UseBagItem(Player* bot, uint32 itemId, Unit* target = nullptr);
}

#endif

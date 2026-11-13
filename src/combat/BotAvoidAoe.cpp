#include "BotAvoidAoe.h"
#include "AreaTrigger.h"
#include "CellImpl.h"
#include "Creature.h"
#include "DynamicObject.h"
#include "GameObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Map.h"
#include "Player.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"
#include <cmath>
#include <list>
#include <vector>

namespace BotAvoidAoe
{
    namespace
    {
        // mod-playerbots' search ranges ("nearest trap with damage", "possible triggers": 15 yards); area triggers a
        // little wider, since the bot only counts when it stands inside one
        float const ObjectSearchRange = 15.0f;
        float const AreaTriggerSearchRange = 30.0f;
        uint32 const FleeMemoryMs = 5000;
        // a target ranged bots keep within reach while stepping aside (mod-playerbots' spellDistance)
        float const RangedKeepDistance = 30.0f;
        float const TooCloseDistance = 5.0f;

        bool Whitelisted(Options const& options, uint32 spellId)
        {
            return options.whitelist.count(spellId) != 0;
        }

        bool Hostile(Player* bot, Unit* owner)
        {
            return owner && owner != bot && !owner->IsFriendlyTo(bot);
        }

        bool DealsDamage(SpellInfo const* spellInfo)
        {
            if (!spellInfo)
                return false;
            for (SpellEffectInfo const* effect : spellInfo->Effects)
            {
                if (!effect)
                    continue;
                if (effect->Effect == SPELL_EFFECT_SCHOOL_DAMAGE || effect->Effect == SPELL_EFFECT_HEALTH_LEECH ||
                    effect->Effect == SPELL_EFFECT_ENVIRONMENTAL_DAMAGE)
                    return true;
                if (effect->Effect == SPELL_EFFECT_APPLY_AURA &&
                    (effect->ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE || effect->ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE_PERCENT ||
                     effect->ApplyAuraName == SPELL_AURA_PERIODIC_LEECH))
                    return true;
            }
            return false;
        }

        // 1. mod-playerbots' "area debuff": a negative aura applied by a dynamic object the bot stands in
        bool FromDynamicObjectAura(Player* bot, Options const& options, Hazard& hazard)
        {
            for (AuraType type : { SPELL_AURA_PERIODIC_DAMAGE, SPELL_AURA_PERIODIC_DAMAGE_PERCENT, SPELL_AURA_PERIODIC_TRIGGER_SPELL,
                                   SPELL_AURA_PERIODIC_TRIGGER_SPELL_WITH_VALUE, SPELL_AURA_DUMMY })
            {
                for (AuraEffect* aurEff : bot->GetAuraEffectsByType(type))
                {
                    Aura* aura = aurEff ? aurEff->GetBase() : nullptr;
                    if (!aura || aura->IsRemoved() || aura->IsExpired() || aura->GetType() != DYNOBJ_AURA_TYPE)
                        continue;
                    SpellInfo const* spellInfo = aura->GetSpellInfo();
                    if (!spellInfo || spellInfo->IsPositive() || Whitelisted(options, spellInfo->Id))
                        continue;
                    DynamicObject* dynObj = aura->GetDynobjOwner();
                    if (!dynObj || !dynObj->IsInWorld())
                        continue;
                    if (Unit* caster = dynObj->GetCaster())
                        if (caster == bot || caster->IsFriendlyTo(bot))
                            continue;
                    float const radius = dynObj->GetRadius();
                    if (radius <= 0.0f || radius > options.maxRadius || bot->GetExactDist2d(dynObj) > radius)
                        continue;
                    hazard.center = dynObj->GetPosition();
                    hazard.radius = radius;
                    hazard.spellId = spellInfo->Id;
                    hazard.kind = "aura";
                    return true;
                }
            }
            return false;
        }

        struct AreaTriggerInRange
        {
            WorldObject const* me;
            float range;
            bool operator()(AreaTrigger* at) const { return me->IsWithinDistInMap(at, range); }
        };

        // 2. Legion: a hostile area trigger of a negative spell the bot stands in (round shapes only: a box or
        // polygon has no radius to step out of)
        bool FromAreaTrigger(Player* bot, Options const& options, Hazard& hazard)
        {
            std::list<AreaTrigger*> triggers;
            AreaTriggerInRange check{ bot, AreaTriggerSearchRange };
            Trinity::AreaTriggerListSearcher<AreaTriggerInRange> searcher(bot, triggers, check);
            bot->VisitNearbyObject(AreaTriggerSearchRange, searcher);

            for (AreaTrigger* at : triggers)
            {
                if (!at || !at->IsInWorld() || !Hostile(bot, at->GetCaster()))
                    continue;
                AreaTriggerInfo const& info = at->GetAreaTriggerInfo();
                if (info.hasAreaTriggerBox || info.polygon)
                    continue;
                uint32 const spellId = at->GetSpellId();
                SpellInfo const* spellInfo = spellId ? sSpellMgr->GetSpellInfo(spellId) : nullptr;
                if (!spellInfo || spellInfo->IsPositive() || Whitelisted(options, spellId))
                    continue;
                float const radius = at->GetRadius();
                if (radius <= 0.0f || radius > options.maxRadius || !at->IsInArea(bot))
                    continue;
                hazard.center = at->GetPosition();
                hazard.radius = radius;
                hazard.spellId = spellId;
                hazard.kind = "area trigger";
                return true;
            }
            return false;
        }

        struct TrapInRange
        {
            WorldObject const* me;
            float range;
            bool operator()(GameObject* go) const
            {
                return go->GetGoType() == GAMEOBJECT_TYPE_TRAP && me->IsWithinDistInMap(go, range);
            }
        };

        // 3. mod-playerbots' "nearest trap with damage"
        bool FromTrap(Player* bot, Options const& options, Hazard& hazard)
        {
            std::list<GameObject*> traps;
            TrapInRange check{ bot, ObjectSearchRange };
            Trinity::GameObjectListSearcher<TrapInRange> searcher(bot, traps, check);
            bot->VisitNearbyObject(ObjectSearchRange, searcher);

            for (GameObject* go : traps)
            {
                if (!go || !go->IsInWorld() || !go->isSpawned())
                    continue;
                if (Unit* owner = go->GetOwner())
                    if (owner == bot || owner->IsFriendlyTo(bot))
                        continue;
                GameObjectTemplate const* goInfo = go->GetGOInfo();
                uint32 const spellId = goInfo ? goInfo->trap.spell : 0;
                if (!spellId || Whitelisted(options, spellId))
                    continue;
                SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
                if (!spellInfo || spellInfo->IsPositive() || !DealsDamage(spellInfo))
                    continue;
                float const radius = float(goInfo->trap.radius) + go->GetCombatReach();
                if (radius <= 0.0f || radius > options.maxRadius || bot->GetExactDist2d(go) > radius)
                    continue;
                hazard.center = go->GetPosition();
                hazard.radius = radius;
                hazard.spellId = spellId;
                hazard.kind = "trap";
                return true;
            }
            return false;
        }

        // 4. mod-playerbots' "possible triggers": an unselectable hostile creature pulsing a damaging spell around it
        bool FromTriggerUnit(Player* bot, Options const& options, Hazard& hazard)
        {
            std::list<Creature*> creatures;
            bot->GetCreatureListInGrid(creatures, ObjectSearchRange);
            for (Creature* unit : creatures)
            {
                if (!unit || !unit->IsInWorld() || !unit->IsAlive() || unit->IsFriendlyTo(bot) ||
                    !unit->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE))
                    continue;
                for (AuraType type : { SPELL_AURA_PERIODIC_TRIGGER_SPELL, SPELL_AURA_PERIODIC_TRIGGER_SPELL_WITH_VALUE })
                {
                    for (AuraEffect* aurEff : unit->GetAuraEffectsByType(type))
                    {
                        SpellEffectInfo const* effect = aurEff ? aurEff->GetSpellEffectInfo() : nullptr;
                        SpellInfo const* triggered = effect && effect->TriggerSpell ? sSpellMgr->GetSpellInfo(effect->TriggerSpell) : nullptr;
                        if (!triggered || Whitelisted(options, triggered->Id))
                            continue;
                        for (SpellEffectInfo const* triggeredEffect : triggered->Effects)
                        {
                            if (!triggeredEffect || triggeredEffect->Effect != SPELL_EFFECT_SCHOOL_DAMAGE)
                                continue;
                            float const radius = triggeredEffect->CalcRadius();
                            if (radius <= 0.0f || radius > options.maxRadius || bot->GetExactDist2d(unit) > radius)
                                continue;
                            hazard.center = unit->GetPosition();
                            hazard.radius = radius;
                            hazard.spellId = triggered->Id;
                            hazard.kind = "trigger unit";
                            return true;
                        }
                    }
                }
            }
            return false;
        }

        // a direction opposite to one taken in the last seconds is refused (mod-playerbots' CheckLastFlee)
        bool OppositeToRecent(float angle, FleeMemory& memory)
        {
            uint32 const now = getMSTime();
            while (!memory.recent.empty() && (memory.recent.size() > 10 || getMSTimeDiff(memory.recent.front().time, now) > FleeMemoryMs))
                memory.recent.pop_front();
            angle = Position::NormalizeOrientation(angle);
            for (FleeMemory::Entry const& entry : memory.recent)
            {
                float const reverse = Position::NormalizeOrientation(entry.angle + float(M_PI));
                float diff = std::fabs(reverse - angle);
                if (diff > float(M_PI))
                    diff = 2.0f * float(M_PI) - diff;
                if (diff < float(M_PI) / 4.0f)
                    return true;
            }
            return false;
        }

        struct Candidate
        {
            float angle;
            bool strict;                        // must keep the target in reach
        };
    }

    bool FindHazard(Player* bot, Options const& options, Hazard& hazard)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive())
            return false;
        return FromDynamicObjectAura(bot, options, hazard) || FromAreaTrigger(bot, options, hazard) ||
            FromTrap(bot, options, hazard) || FromTriggerUnit(bot, options, hazard);
    }

    bool FindFleePosition(Player* bot, Unit* target, bool melee, Hazard const& hazard, Options const& options,
        FleeMemory& memory, Position& out)
    {
        float const awayFromCenter = Position::NormalizeOrientation(hazard.center.GetAngle(bot->GetPositionX(), bot->GetPositionY()));
        std::vector<Candidate> candidates;
        if (target && target->IsInWorld())
        {
            float const toTarget = bot->GetAngle(target);
            // left or right of the target is usually best: the bot keeps facing and reaching it
            candidates.push_back({ toTarget + float(M_PI) / 2.0f, false });
            candidates.push_back({ toTarget - float(M_PI) / 2.0f, false });
            if (melee)
            {
                candidates.push_back({ toTarget, true });
                if (target->getVictim() == bot)     // tanking: backing off drags the enemy out of it
                {
                    candidates.push_back({ toTarget + float(M_PI), false });
                    candidates.push_back({ awayFromCenter, false });
                }
            }
            else
            {
                candidates.push_back({ toTarget + float(M_PI), true });
                candidates.push_back({ toTarget, true });
                candidates.push_back({ awayFromCenter, true });
            }
        }
        candidates.push_back({ awayFromCenter, false });

        float const fromCenter = bot->GetExactDist2d(&hazard.center);
        float const step = std::max(2.0f, std::min(options.maxStep, hazard.radius - fromCenter + 1.5f));
        float bestScore = -1.0f;
        for (Candidate const& candidate : candidates)
        {
            float const angle = Position::NormalizeOrientation(candidate.angle);
            if (OppositeToRecent(angle, memory))
                continue;

            // a lateral step must be longer to leave a circle than a straight one
            float const dist = std::min(options.maxStep, std::max(step, hazard.radius + 1.0f - (angle == awayFromCenter ? fromCenter : 0.0f)));
            Position pos = bot->GetFirstCollisionPosition(dist, angle - bot->GetOrientation());
            float const moved = bot->GetExactDist2d(&pos);
            if (moved < 1.5f || !bot->IsWithinLOS(pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ() + 1.5f))
                continue;

            if (candidate.strict && target)
            {
                float const reach = pos.GetExactDist2d(target) - target->GetCombatReach();
                if (melee && reach > TooCloseDistance)
                    continue;
                if (!melee && (reach > RangedKeepDistance || reach < TooCloseDistance))
                    continue;
            }

            float const fromHazard = pos.GetExactDist2d(&hazard.center);
            // out of the hazard first, then the farthest from its centre
            float const score = fromHazard + (fromHazard > hazard.radius ? 1000.0f : 0.0f);
            if (score > bestScore)
            {
                bestScore = score;
                out = pos;
            }
        }

        if (bestScore < 0.0f || bot->GetExactDist2d(&out) < 1.5f)
            return false;

        if (memory.recent.size() >= 10)
            memory.recent.pop_front();
        memory.recent.push_back({ Position::NormalizeOrientation(bot->GetAngle(&out)), getMSTime() });
        return bestScore >= 0.0f;
    }

    std::string SpellName(uint32 spellId)
    {
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        return spellInfo && spellInfo->SpellName ? std::string(spellInfo->SpellName) : std::to_string(spellId);
    }
}

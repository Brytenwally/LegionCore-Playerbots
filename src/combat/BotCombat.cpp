/*
 * mod-playerbots: combat building blocks. See BotCombat.h.
 */

#include "BotCombat.h"
#include "DB2Stores.h"
#include "Group.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"
#include <algorithm>

namespace BotCombat
{
    BotRole RoleOf(Player const* bot)
    {
        switch (bot->GetSpecializationRole())
        {
            case ROLES_TANK:   return BotRole::Tank;
            case ROLES_HEALER: return BotRole::Healer;
            default:           return BotRole::Dps;
        }
    }

    char const* RoleName(BotRole role)
    {
        switch (role)
        {
            case BotRole::Tank:   return "tank";
            case BotRole::Healer: return "healer";
            default:              return "dps";
        }
    }

    char const* CastRefusalName(CastRefusal refusal)
    {
        switch (refusal)
        {
            case CastRefusal::None:           return "cast";
            case CastRefusal::NoTarget:       return "no target";
            case CastRefusal::TargetDead:     return "target dead";
            case CastRefusal::UnknownSpell:   return "unknown spell";
            case CastRefusal::Level:          return "level too low";
            case CastRefusal::NotLearned:     return "not learned";
            case CastRefusal::Cooldown:       return "on cooldown";
            case CastRefusal::GlobalCooldown: return "global cooldown";
            case CastRefusal::Casting:        return "already casting";
            case CastRefusal::Throttled:      return "AI delay";
            case CastRefusal::CastFailed:     return "cast failed";
        }
        return "?";
    }

    bool SpellThrottle::IsThrottled(uint32 spellId, uint32 now) const
    {
        auto itr = _until.find(spellId);
        return itr != _until.end() && int32(itr->second - now) > 0;
    }

    void SpellThrottle::Mark(SpellInfo const* spellInfo, uint32 minCooldown, uint32 now)
    {
        uint32 delay = minCooldown;
        delay = std::max(delay, spellInfo->Cooldowns.RecoveryTime);
        delay = std::max(delay, spellInfo->Cooldowns.CategoryRecoveryTime);
        _until[spellInfo->Id] = now + delay;
    }

    CastOutcome BotCast(Player* bot, Unit* target, uint32 spellId, SpellThrottle& throttle, uint32 minCooldown)
    {
        CastOutcome outcome;
        auto refuse = [&outcome](CastRefusal refusal) -> CastOutcome
        {
            outcome.refusal = refusal;
            return outcome;
        };

        if (!target)
            return refuse(CastRefusal::NoTarget);
        if (!target->IsAlive())
            return refuse(CastRefusal::TargetDead);

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!spellInfo)
            return refuse(CastRefusal::UnknownSpell);
        if (spellInfo->SpellLevel > bot->getLevel())
            return refuse(CastRefusal::Level);
        if (!bot->HasSpell(spellId))
            return refuse(CastRefusal::NotLearned);
        if (bot->HasSpellCooldown(spellId))
            return refuse(CastRefusal::Cooldown);
        if (bot->GetGlobalCooldownMgr().HasGlobalCooldown(spellInfo))
            return refuse(CastRefusal::GlobalCooldown);
        if (bot->IsNonMeleeSpellCast(false))
            return refuse(CastRefusal::Casting);

        uint32 const now = getMSTime();
        if (throttle.IsThrottled(spellId, now))
            return refuse(CastRefusal::Throttled);

        if (target != bot)
            bot->SetFacingToObject(target);

        outcome.result = bot->CastSpell(target, spellInfo, false);
        if (outcome.result != SPELL_CAST_OK)
            return refuse(CastRefusal::CastFailed);

        throttle.Mark(spellInfo, minCooldown, now);
        return outcome;
    }

    std::vector<Player*> GroupMembersOnMap(Player* bot)
    {
        std::vector<Player*> members;
        Group* group = bot->GetGroup();
        if (!group)
        {
            members.push_back(bot);
            return members;
        }

        for (Group::MemberSlot const& slot : group->GetMemberSlots())
        {
            if (slot.Guid == bot->GetGUID())
            {
                members.push_back(bot);
                continue;
            }
            if (Player* member = ObjectAccessor::GetPlayer(*bot, slot.Guid))
                if (member->IsInWorld() && !member->IsBeingTeleported())
                    members.push_back(member);
        }
        return members;
    }

    bool IsAttackingAlly(Unit* enemy, Player* ally)
    {
        return enemy && ally && enemy->IsAlive() && enemy->IsInWorld() && ally->IsAlive() && ally->IsInWorld() &&
            enemy->GetMap() == ally->GetMap() &&
            (enemy->getVictim() == ally || ally->getAttackers()->count(enemy) != 0);
    }

    bool IsAggressorOf(Unit* enemy, std::vector<Player*> const& allies)
    {
        for (Player* ally : allies)
            if (IsAttackingAlly(enemy, ally))
                return true;
        return false;
    }

    Unit* FindAttackerOf(Player* bot, std::vector<Player*> const& allies, float maxDistance)
    {
        for (Player* ally : allies)
        {
            if (!ally->IsAlive() || ally->GetMap() != bot->GetMap())
                continue;
            for (Unit* attacker : *ally->getAttackers())
                if (IsAttackingAlly(attacker, ally) && bot->IsValidAttackTarget(attacker) &&
                    (maxDistance <= 0.0f || bot->GetDistance(attacker) <= maxDistance))
                    return attacker;
        }
        return nullptr;
    }

    uint32 CountInjured(std::vector<Player*> const& allies, Unit* center, float radius, float healthPct)
    {
        uint32 count = 0;
        for (Player* ally : allies)
            if (ally->IsAlive() && ally->GetMap() == center->GetMap() && ally->GetHealthPct() < healthPct &&
                center->GetDistance(ally) <= radius)
                ++count;
        return count;
    }

    Player* FindLowestHpAlly(Player* bot, std::vector<Player*> const& allies, float healthPct, float tankPct, float selfPct)
    {
        Player* lowest = nullptr;
        float lowestPct = healthPct;
        Player* tank = nullptr;
        float tankHp = 101.0f;

        for (Player* ally : allies)
        {
            if (ally == bot || !ally->IsAlive())
                continue;

            float const pct = ally->GetHealthPct();
            if (RoleOf(ally) == BotRole::Tank && pct < tankHp)
            {
                tank = ally;
                tankHp = pct;
            }
            if (pct < lowestPct)
            {
                lowest = ally;
                lowestPct = pct;
            }
        }

        // keep the tank up first once it takes real damage
        if (tank && tankHp < tankPct)
            return tank;
        if (!lowest && bot->IsAlive() && bot->GetHealthPct() < selfPct)
            return bot;
        return lowest;
    }

    bool EngageTimer::Ready(ObjectGuid const& target, uint32 now, uint32 delayMs)
    {
        if (_target != target)
        {
            _target = target;
            _since = now;
        }
        return now - _since >= delayMs;
    }

    Spell* GetInterruptibleCast(Unit* target)
    {
        Spell* cast = target->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (!cast || cast->getState() == SPELL_STATE_FINISHED || cast->GetCastTime() <= 0)
            cast = target->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
        if (!cast || cast->getState() == SPELL_STATE_FINISHED || !cast->IsInterruptable())
            return nullptr;
        return cast;
    }

    bool TryInterrupt(Player* bot, Unit* target, uint32 spellId, SpellThrottle& throttle)
    {
        if (!spellId || !target || !GetInterruptibleCast(target))
            return false;
        return bool(BotCast(bot, target, spellId, throttle));
    }

    bool TryDispel(Player* bot, std::vector<Player*> const& allies, uint32 spellId, SpellThrottle& throttle, float range)
    {
        SpellInfo const* spellInfo = spellId ? sSpellMgr->GetSpellInfo(spellId) : nullptr;
        if (!spellInfo || !bot->HasSpell(spellId))
            return false;

        uint32 const dispelMask = spellInfo->GetSimilarEffectsMiscValueMask(SPELL_EFFECT_DISPEL, bot);
        if (!dispelMask)
            return false;

        for (Player* ally : allies)
        {
            if (!ally->IsAlive() || ally->GetMap() != bot->GetMap() || bot->GetDistance(ally) > range)
                continue;

            DispelChargesList auras;
            ally->GetDispellableAuraList(bot, dispelMask, auras);
            for (auto const& entry : auras)
            {
                AuraApplication* application = entry.first->GetApplicationOfTarget(ally->GetGUID());
                if (application && !application->IsPositive())
                    return bool(BotCast(bot, ally, spellId, throttle));
            }
        }
        return false;
    }

    bool KeepAura(Player* bot, Unit* target, uint32 spellId, SpellThrottle& throttle, uint32 auraId)
    {
        if (!target || !target->IsAlive())
            return false;
        if (target->HasAura(auraId ? auraId : spellId, bot->GetGUID()))
            return false;
        return bool(BotCast(bot, target, spellId, throttle));
    }

    bool UseBagItem(Player* bot, uint32 itemId, Unit* target)
    {
        Item* item = bot->GetItemByEntry(itemId);
        if (!item || bot->IsNonMeleeSpellCast(false))
            return false;

        ItemTemplate const* proto = item->GetTemplate();
        if (!proto || bot->CanUseItem(item) != EQUIP_ERR_OK)
            return false;

        for (ItemEffectEntry const* effect : proto->Effects)
        {
            if (!effect || effect->SpellID <= 0 || effect->TriggerType != ITEM_SPELLTRIGGER_ON_USE)
                continue;

            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(uint32(effect->SpellID));
            if (!spellInfo || bot->HasSpellCooldown(spellInfo->Id))
                return false;

            return bot->CastSpell(target ? target : bot, spellInfo, false, item) == SPELL_CAST_OK;
        }
        return false;
    }
}

/*
 * mod-playerbots: potions and food/drink. See BotConsumables.h.
 */

#include "BotConsumables.h"
#include "BotCombat.h"
#include "Bag.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

namespace BotConsumables
{
    namespace
    {
        // the on-use spell of an item heals / restores mana
        bool IsPotionFor(ItemTemplate const* proto, bool mana)
        {
            for (ItemEffectEntry const* effect : proto->Effects)
            {
                if (!effect || effect->SpellID <= 0 || effect->TriggerType != ITEM_SPELLTRIGGER_ON_USE)
                    continue;
                SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(uint32(effect->SpellID));
                if (!spellInfo)
                    continue;
                for (SpellEffectInfo const* spellEffect : spellInfo->Effects)
                {
                    if (!spellEffect)
                        continue;
                    if (!mana && spellEffect->Effect == SPELL_EFFECT_HEAL)
                        return true;
                    if (mana && spellEffect->Effect == SPELL_EFFECT_ENERGIZE && spellEffect->MiscValue == POWER_MANA)
                        return true;
                }
            }
            return false;
        }

        // SkyFire's level tiers: the food / drink spell of an item of that level (the aura only)
        struct RestTier
        {
            uint8 minLevel;
            uint32 spellId;
        };
        RestTier const FoodTiers[] =
        {
            { 85, 104934 }, { 80, 80169 }, { 70, 43180 }, { 65, 35270 }, { 55, 27094 }, { 45, 1131 },
            { 35, 1129 }, { 25, 1127 }, { 15, 435 }, { 5, 434 }, { 1, 433 },
        };
        RestTier const DrinkTiers[] =
        {
            { 85, 104262 }, { 80, 80167 }, { 70, 43182 }, { 60, 27089 }, { 45, 1137 }, { 35, 1135 },
            { 25, 1133 }, { 15, 432 }, { 5, 431 }, { 1, 430 },
        };

        template <size_t N>
        uint32 TierSpell(RestTier const (&tiers)[N], uint8 level)
        {
            for (RestTier const& tier : tiers)
                if (level >= tier.minLevel)
                    return tier.spellId;
            return tiers[N - 1].spellId;
        }

        template <size_t N>
        bool HasTier(Player* bot, RestTier const (&tiers)[N])
        {
            for (RestTier const& tier : tiers)
                if (bot->HasAura(tier.spellId))
                    return true;
            return false;
        }

        template <size_t N>
        void RemoveTiers(Player* bot, RestTier const (&tiers)[N])
        {
            for (RestTier const& tier : tiers)
                bot->RemoveAurasDueToSpell(tier.spellId);
        }
    }

    bool UsePotion(Player* bot, bool mana)
    {
        Item* best = nullptr;
        auto consider = [&](Item* item)
        {
            if (!item)
                return;
            ItemTemplate const* proto = item->GetTemplate();
            if (!proto || bot->CanUseItem(item) != EQUIP_ERR_OK || !IsPotionFor(proto, mana))
                return;
            if (!best || proto->GetBaseItemLevel() > best->GetTemplate()->GetBaseItemLevel())
                best = item;
        };

        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            consider(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
        for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
            if (Bag* bag = bot->GetBagByPos(bagSlot))
                for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                    consider(bag->GetItemByPos(uint8(slot)));

        return best && BotCombat::UseBagItem(bot, best->GetEntry());
    }

    void StartRest(Player* bot, bool needHealth, bool needMana)
    {
        bot->StopMoving();
        if (bot->getStandState() != UNIT_STAND_STATE_SIT)
            bot->SetStandState(UNIT_STAND_STATE_SIT);

        uint8 const level = bot->getLevel();
        if (needHealth)
            if (uint32 spell = TierSpell(FoodTiers, level))
                if (sSpellMgr->GetSpellInfo(spell) && !bot->HasAura(spell))
                    bot->AddAura(spell, bot);
        if (needMana)
            if (uint32 spell = TierSpell(DrinkTiers, level))
                if (sSpellMgr->GetSpellInfo(spell) && !bot->HasAura(spell))
                    bot->AddAura(spell, bot);
    }

    void StopRest(Player* bot)
    {
        RemoveTiers(bot, FoodTiers);
        RemoveTiers(bot, DrinkTiers);
        if (bot->getStandState() == UNIT_STAND_STATE_SIT)
            bot->SetStandState(UNIT_STAND_STATE_STAND);
    }

    bool HasRestAura(Player* bot)
    {
        return HasTier(bot, FoodTiers) || HasTier(bot, DrinkTiers);
    }
}

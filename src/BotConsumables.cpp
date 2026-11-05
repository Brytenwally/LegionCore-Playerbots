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
#include "Log.h"
#include "ObjectMgr.h"
#include <algorithm>
#include <mutex>
#include <vector>

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

        // Food and drink above SkyFire's tiers (owner, 2026-11-05: scale the list to 110): read once from the
        // server's own items: Food & Drink consumables whose on-use spell is a seated aura
        // (AURA_INTERRUPT_FLAG_NOT_SEATED) that restores health (OBS_MOD_HEALTH / MOD_REGEN) or mana
        // (OBS_MOD_POWER / MOD_POWER_REGEN of mana); for a level the item with the highest required level at
        // or below it, then the highest item level. SkyFire's tiers stay the fallback.
        struct RestItem
        {
            uint32 requiredLevel;
            uint32 itemLevel;
            uint32 spellId;
        };
        std::once_flag restOnce;
        std::vector<RestItem> foodItems, drinkItems;

        void BuildRestIndex()
        {
            for (auto const& pair : *sObjectMgr->GetItemTemplateStore())
            {
                ItemTemplate const* proto = &pair.second;
                if (proto->GetClass() != ITEM_CLASS_CONSUMABLE || proto->GetSubClass() != ITEM_SUBCLASS_FOOD_DRINK)
                    continue;
                for (ItemEffectEntry const* effect : proto->Effects)
                {
                    if (!effect || effect->SpellID <= 0 || effect->TriggerType != ITEM_SPELLTRIGGER_ON_USE)
                        continue;
                    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(uint32(effect->SpellID));
                    if (!spellInfo || !spellInfo->HasAuraInterruptFlag(AURA_INTERRUPT_FLAG_NOT_SEATED))
                        continue;
                    bool food = false, drink = false;
                    for (SpellEffectInfo const* spellEffect : spellInfo->Effects)
                    {
                        if (!spellEffect)
                            continue;
                        uint32 const aura = spellEffect->ApplyAuraName;
                        if (aura == SPELL_AURA_OBS_MOD_HEALTH || aura == SPELL_AURA_MOD_REGEN)
                            food = true;
                        if ((aura == SPELL_AURA_OBS_MOD_POWER || aura == SPELL_AURA_MOD_POWER_REGEN) && spellEffect->MiscValue == POWER_MANA)
                            drink = true;
                    }
                    RestItem const item{ uint32(std::max(1, proto->GetBaseRequiredLevel())), proto->GetBaseItemLevel(), spellInfo->Id };
                    if (food)
                        foodItems.push_back(item);
                    if (drink)
                        drinkItems.push_back(item);
                }
            }
            TC_LOG_INFO("server.loading", "[mod-playerbots] Rest: %u food and %u drink spells from items.",
                uint32(foodItems.size()), uint32(drinkItems.size()));
        }

        uint32 ItemRestSpell(std::vector<RestItem> const& items, uint8 level)
        {
            RestItem const* best = nullptr;
            for (RestItem const& item : items)
                if (item.requiredLevel <= level &&
                    (!best || item.requiredLevel > best->requiredLevel ||
                     (item.requiredLevel == best->requiredLevel && item.itemLevel > best->itemLevel)))
                    best = &item;
            return best ? best->spellId : 0;
        }

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

        // the food / drink aura for a level: from the items when they reach further than SkyFire's tiers
        // (above 85), else SkyFire's tier
        uint32 RestSpell(bool drink, uint8 level)
        {
            std::call_once(restOnce, BuildRestIndex);
            uint32 const tier = drink ? TierSpell(DrinkTiers, level) : TierSpell(FoodTiers, level);
            if (level <= 85)
                return tier;
            uint32 const fromItems = ItemRestSpell(drink ? drinkItems : foodItems, level);
            return fromItems ? fromItems : tier;
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
            if (uint32 spell = RestSpell(false, level))
                if (sSpellMgr->GetSpellInfo(spell) && !bot->HasAura(spell))
                    bot->AddAura(spell, bot);
        if (needMana)
            if (uint32 spell = RestSpell(true, level))
                if (sSpellMgr->GetSpellInfo(spell) && !bot->HasAura(spell))
                    bot->AddAura(spell, bot);
    }

    void StopRest(Player* bot)
    {
        RemoveTiers(bot, FoodTiers);
        RemoveTiers(bot, DrinkTiers);
        uint8 const level = bot->getLevel();
        bot->RemoveAurasDueToSpell(RestSpell(false, level));
        bot->RemoveAurasDueToSpell(RestSpell(true, level));
        if (bot->getStandState() == UNIT_STAND_STATE_SIT)
            bot->SetStandState(UNIT_STAND_STATE_STAND);
    }

    bool HasRestAura(Player* bot)
    {
        uint8 const level = bot->getLevel();
        return HasTier(bot, FoodTiers) || HasTier(bot, DrinkTiers) ||
            bot->HasAura(RestSpell(false, level)) || bot->HasAura(RestSpell(true, level));
    }
}

/*
 * mod-playerbots: gear by stat weights. See BotGear.h.
 */

#include "BotGear.h"
#include "DB2Stores.h"
#include "Bag.h"
#include "Item.h"
#include "ItemEnchantmentMgr.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotMgr.h"
#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace BotGear
{
    namespace
    {
        struct Weights
        {
            float strength, agility, intellect, stamina, crit, haste, mastery, versatility, leech, avoidance,
                armor, minDamage, maxDamage;
        };

        // Ask Mr. Robot scales (Pawn, AskMrRobot.lua, Pawn 2.0218), by ChrSpecialization id:
        // Strength, Agility, Intellect, Stamina, Crit, Haste, Mastery, Versatility, Leech, Avoidance,
        // Armor, MinDamage, MaxDamage
        std::unordered_map<uint32, Weights> const WeightTable =
        {
            { 250, { 12.89f, 0.00f, 0.00f, 0.95f, 12.46f, 11.24f, 9.64f, 10.39f, 0.95f, 0.01f, 0.95f, 0.00f, 0.00f } }, // Death Knight Blood
            { 251, { 22.89f, 0.00f, 0.00f, 0.00f, 25.30f, 23.39f, 20.94f, 18.91f, 0.01f, 0.02f, 0.00f, 2.67f, 2.67f } }, // Death Knight Frost
            { 252, { 21.22f, 0.00f, 0.00f, 0.00f, 19.17f, 22.85f, 24.22f, 17.50f, 0.01f, 0.02f, 0.00f, 1.73f, 1.73f } }, // Death Knight Unholy
            { 577, { 0.00f, 19.25f, 0.00f, 0.00f, 27.74f, 16.47f, 18.98f, 19.11f, 0.01f, 0.02f, 0.00f, 8.15f, 8.15f } }, // Demon Hunter Havoc
            { 581, { 0.00f, 16.64f, 0.00f, 0.00f, 14.23f, 11.99f, 13.66f, 13.52f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f } }, // Demon Hunter Vengeance
            { 102, { 0.00f, 0.00f, 18.14f, 0.00f, 21.01f, 24.03f, 23.45f, 18.84f, 0.01f, 0.03f, 0.00f, 0.00f, 0.00f } }, // Druid Balance
            { 103, { 0.00f, 23.44f, 0.00f, 0.00f, 19.92f, 10.33f, 15.34f, 18.43f, 0.01f, 0.02f, 0.00f, 6.64f, 6.64f } }, // Druid Feral
            { 104, { 0.00f, 13.01f, 0.00f, 0.00f, 11.78f, 14.19f, 11.63f, 11.37f, 0.00f, 0.01f, 0.00f, 0.00f, 0.00f } }, // Druid Guardian
            { 105, { 0.00f, 0.00f, 10.86f, 0.00f, 13.36f, 7.70f, 12.98f, 12.09f, 4.93f, 0.02f, 0.00f, 0.00f, 0.00f } }, // Druid Restoration
            { 253, { 0.00f, 24.86f, 0.00f, 0.00f, 23.30f, 26.67f, 29.62f, 20.00f, 0.01f, 0.03f, 0.00f, 2.33f, 2.33f } }, // Hunter Beast Mastery
            { 254, { 0.00f, 19.88f, 0.00f, 0.00f, 27.88f, 25.12f, 33.35f, 20.51f, 0.01f, 0.03f, 0.00f, 9.83f, 9.83f } }, // Hunter Marksmanship
            { 255, { 0.00f, 21.89f, 0.00f, 0.00f, 20.76f, 21.92f, 4.85f, 19.78f, 0.01f, 0.02f, 0.00f, 3.53f, 3.53f } }, // Hunter Survival
            {  62, { 0.00f, 0.00f, 17.27f, 0.00f, 18.59f, 18.82f, 16.16f, 17.94f, 0.01f, 0.03f, 0.00f, 0.00f, 0.00f } }, // Mage Arcane
            {  63, { 0.00f, 0.00f, 17.35f, 0.00f, 20.27f, 19.85f, 16.59f, 18.20f, 0.01f, 0.03f, 0.00f, 0.00f, 0.00f } }, // Mage Fire
            {  64, { 0.00f, 0.00f, 18.52f, 0.00f, 21.16f, 23.32f, 13.09f, 19.28f, 0.01f, 0.03f, 0.00f, 0.00f, 0.00f } }, // Mage Frost
            { 268, { 0.00f, 28.83f, 0.00f, 16.45f, 26.35f, 21.04f, 29.39f, 35.59f, 35.14f, 0.01f, 27.31f, 0.00f, 0.00f } }, // Monk Brewmaster
            { 270, { 0.00f, 0.00f, 11.09f, 0.00f, 13.86f, 13.01f, 7.53f, 13.35f, 0.93f, 0.02f, 0.00f, 0.00f, 0.00f } }, // Monk Mistweaver
            { 269, { 0.00f, 24.05f, 0.00f, 0.00f, 19.45f, 13.03f, 20.18f, 18.57f, 0.01f, 0.02f, 0.00f, 0.93f, 0.93f } }, // Monk Windwalker
            {  65, { 0.00f, 0.00f, 9.93f, 0.00f, 17.52f, 16.82f, 13.58f, 12.31f, 10.61f, 0.02f, 0.00f, 0.00f, 0.00f } }, // Paladin Holy
            {  66, { 26.68f, 0.00f, 0.00f, 10.84f, 27.40f, 32.38f, 28.02f, 29.70f, 13.17f, 0.01f, 10.80f, 0.00f, 0.00f } }, // Paladin Protection
            {  70, { 17.78f, 0.00f, 0.00f, 0.00f, 22.60f, 20.12f, 18.64f, 18.39f, 0.01f, 0.02f, 0.00f, 6.49f, 6.49f } }, // Paladin Retribution
            { 256, { 0.00f, 0.00f, 8.91f, 0.00f, 9.97f, 22.10f, 7.71f, 9.38f, 0.56f, 0.02f, 0.00f, 0.00f, 0.00f } }, // Priest Discipline
            { 257, { 0.00f, 0.00f, 15.08f, 0.00f, 19.01f, 8.04f, 15.07f, 16.64f, 5.94f, 0.02f, 0.00f, 0.00f, 0.00f } }, // Priest Holy
            { 258, { 0.00f, 0.00f, 17.40f, 0.00f, 20.57f, 30.16f, 19.91f, 17.12f, 0.01f, 0.03f, 0.00f, 0.00f, 0.00f } }, // Priest Shadow
            { 259, { 0.00f, 21.41f, 0.00f, 0.00f, 19.88f, 9.86f, 23.53f, 17.04f, 0.01f, 0.02f, 0.00f, 2.96f, 2.96f } }, // Rogue Assassination
            { 260, { 0.00f, 19.96f, 0.00f, 0.00f, 13.00f, 12.85f, 9.74f, 16.57f, 0.01f, 0.02f, 0.00f, 3.14f, 3.14f } }, // Rogue Outlaw
            { 261, { 0.00f, 21.50f, 0.00f, 0.00f, 22.03f, 7.88f, 18.87f, 18.19f, 0.01f, 0.02f, 0.00f, 8.04f, 8.04f } }, // Rogue Subtlety
            { 262, { 0.00f, 0.00f, 19.32f, 0.00f, 22.88f, 22.71f, 17.48f, 19.79f, 0.01f, 0.03f, 0.00f, 0.00f, 0.00f } }, // Shaman Elemental
            { 263, { 0.00f, 21.72f, 0.00f, 0.00f, 18.65f, 24.63f, 21.24f, 18.82f, 0.01f, 0.02f, 0.00f, 4.78f, 4.78f } }, // Shaman Enhancement
            { 264, { 0.00f, 0.00f, 13.00f, 0.00f, 18.29f, 11.67f, 14.70f, 14.36f, 4.47f, 0.02f, 0.00f, 0.00f, 0.00f } }, // Shaman Restoration
            { 265, { 0.00f, 0.00f, 17.62f, 0.00f, 22.83f, 24.85f, 25.22f, 18.20f, 0.01f, 0.03f, 0.00f, 0.00f, 0.00f } }, // Warlock Affliction
            { 266, { 0.00f, 0.00f, 17.78f, 0.00f, 19.43f, 34.99f, 21.29f, 18.66f, 0.01f, 0.03f, 0.00f, 0.00f, 0.00f } }, // Warlock Demonology
            { 267, { 0.00f, 0.00f, 19.21f, 0.00f, 22.49f, 26.85f, 22.62f, 19.76f, 0.01f, 0.03f, 0.00f, 0.00f, 0.00f } }, // Warlock Destruction
            {  71, { 16.83f, 0.00f, 0.00f, 0.00f, 14.55f, 17.62f, 27.99f, 17.58f, 0.01f, 0.02f, 0.00f, 6.38f, 6.38f } }, // Warrior Arms
            {  72, { 13.84f, 0.00f, 0.00f, 0.00f, 18.82f, 22.69f, 16.12f, 15.58f, 0.01f, 0.02f, 0.00f, 4.28f, 4.28f } }, // Warrior Fury
            {  73, { 18.07f, 0.00f, 0.00f, 0.00f, 14.06f, 17.22f, 15.63f, 13.62f, 0.01f, 0.01f, 0.00f, 0.00f, 0.00f } }, // Warrior Protection
        };

        // how a spec fills its main and off hand
        enum class Hands : uint8
        {
            TwoHand,            // a two-hander
            OneHandShield,      // one-hander + shield
            DualWield,          // two one-handers
            Caster,             // a two-hander (staff), or one-hander + held off-hand
            CasterShield,       // a two-hander, or one-hander + shield
            DualOrTwoHand,      // two one-handers or a two-hander
            TitanGrip,          // two two-handers when the bot can (Fury), else two one-handers
            Ranged              // a bow, gun or crossbow
        };

        Hands HandsFor(uint32 spec)
        {
            switch (spec)
            {
                case 72:                                return Hands::TitanGrip;
                case 73: case 65: case 66:              return Hands::OneHandShield;
                case 253: case 254:                     return Hands::Ranged;
                case 259: case 260: case 261:
                case 251: case 263: case 577: case 581: return Hands::DualWield;
                case 269:                               return Hands::DualOrTwoHand;
                case 256: case 257: case 258: case 62: case 63: case 64:
                case 265: case 266: case 267: case 270: case 102: case 105:
                                                        return Hands::Caster;
                case 262: case 264:                     return Hands::CasterShield;
                default:                                return Hands::TwoHand; // 71 70 250 252 255 268 103 104
            }
        }

        bool IsTwoHandType(InventoryType type)
        {
            return type == INVTYPE_2HWEAPON || type == INVTYPE_RANGED || type == INVTYPE_RANGEDRIGHT;
        }

        // Does what the bot holds fit its spec's layout? Owner's report (build #24): a prot warrior, prot paladin or
        // enhancement shaman sometimes kept a two-hander because it out-scores a one-hander + shield / two
        // one-handers by raw stats; those specs need their off hand. A layout that does not fit counts as nothing
        // held, so the right weapons always replace it.
        bool WornHandsFit(Player* bot, Hands hands)
        {
            Item* main = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
            if (!main)
                return true;
            InventoryType const type = main->GetTemplate()->GetInventoryType();
            bool const twoHand = IsTwoHandType(type);
            bool const ranged = type == INVTYPE_RANGED || type == INVTYPE_RANGEDRIGHT;
            switch (hands)
            {
                case Hands::OneHandShield:
                case Hands::DualWield:
                    return !twoHand;
                case Hands::TwoHand:
                    return twoHand && !ranged;
                case Hands::Ranged:
                    return ranged;
                case Hands::TitanGrip:
                case Hands::DualOrTwoHand:
                case Hands::Caster:
                case Hands::CasterShield:
                    return !ranged;
            }
            return true;
        }

        uint32 ArmorTypeFor(uint8 cls)
        {
            switch (cls)
            {
                case CLASS_WARRIOR: case CLASS_PALADIN: case CLASS_DEATH_KNIGHT: return ITEM_SUBCLASS_ARMOR_PLATE;
                case CLASS_HUNTER: case CLASS_SHAMAN:                            return ITEM_SUBCLASS_ARMOR_MAIL;
                case CLASS_ROGUE: case CLASS_DRUID: case CLASS_MONK: case CLASS_DEMON_HUNTER:
                                                                                 return ITEM_SUBCLASS_ARMOR_LEATHER;
                default:                                                         return ITEM_SUBCLASS_ARMOR_CLOTH;
            }
        }

        // the armor type the bot wears: its class's, or the next lighter one while it lacks the skill
        uint32 WornArmorType(Player* bot)
        {
            uint32 type = ArmorTypeFor(bot->getClass());
            static uint32 const skills[] = { 0, SKILL_CLOTH, SKILL_LEATHER, SKILL_MAIL, SKILL_PLATE_MAIL };
            while (type > ITEM_SUBCLASS_ARMOR_CLOTH && !bot->HasSkill(skills[type]))
                --type;
            return type;
        }

        bool IsBodyArmor(InventoryType type)
        {
            switch (type)
            {
                case INVTYPE_HEAD: case INVTYPE_SHOULDERS: case INVTYPE_CHEST: case INVTYPE_ROBE:
                case INVTYPE_WAIST: case INVTYPE_LEGS: case INVTYPE_FEET: case INVTYPE_WRISTS: case INVTYPE_HANDS:
                    return true;
                default:
                    return false;
            }
        }

        // test, placeholder and NPC items of the store
        bool LooksUnobtainable(ItemTemplate const* proto)
        {
            char const* name = proto->GetName() ? proto->GetName()->Str[LOCALE_enUS] : nullptr;
            if (!name || !*name)
                return true;
            static char const* const marks[] = { "Monster -", "[PH]", "Test ", "TEST", "QA ", "Deprecated", "DEPRECATED", "OLD ", "(old)", "zzOLD" };
            for (char const* mark : marks)
                if (strstr(name, mark))
                    return true;
            return false;
        }

        // the stat amount of an item at an item level (as Item::GetItemStatValue)
        int32 StatAmount(ItemTemplate const* proto, uint32 index, uint32 itemLevel)
        {
            if (float points = float(GetRandomPropertyPoints(itemLevel, proto->GetQuality(), proto->GetInventoryType(), proto->GetSubClass())))
                return int32(proto->GetItemStatAllocation(index) * points * 0.0001f + 0.5f);
            return proto->GetItemStatValue(index);
        }

        uint32 ItemLevelOf(ItemTemplate const* proto)
        {
            return proto->GetBaseItemLevel();
        }

        // Highest item level for a bot level. Needed because many quest rewards have no required level: the
        // owner saw a level 15 bot take item level 840 quest rewards (build #21), and LegionBotAI's caps
        // were still too generous (build #22); the brackets and their values are the owner's.
        uint32 ItemLevelCapFor(uint8 level, Options const& options)
        {
            uint32 cap = 0;
            if (level < 60)
                cap = uint32(std::max(1.0f, level + options.belowSixtyAdd));
            else if (level < 110)
                cap = options.brackets[std::min<uint32>((level - 60) / 5, 9)];
            // Playerbots.AutoGear.MaxItemLevel caps every level
            if (options.maxItemLevel && (!cap || cap > options.maxItemLevel))
                cap = options.maxItemLevel;
            return cap;
        }
    }

    uint32 ItemLevelCap(uint8 level, Options const& options)
    {
        return ItemLevelCapFor(level, options);
    }

    float StatWeight(Player const* bot, int32 itemMod)
    {
        auto itr = WeightTable.find(bot->GetSpecializationId());
        if (itr == WeightTable.end())
            return 0.0f;
        Weights const& w = itr->second;
        switch (itemMod)
        {
            case ITEM_MOD_STRENGTH:        return w.strength;
            case ITEM_MOD_AGILITY:         return w.agility;
            case ITEM_MOD_INTELLECT:       return w.intellect;
            case ITEM_MOD_AGI_STR_INT:     return std::max({ w.agility, w.strength, w.intellect });
            case ITEM_MOD_AGI_STR:         return std::max(w.agility, w.strength);
            case ITEM_MOD_AGI_INT:         return std::max(w.agility, w.intellect);
            case ITEM_MOD_STR_INT:         return std::max(w.strength, w.intellect);
            case ITEM_MOD_STAMINA:         return w.stamina;
            case ITEM_MOD_CRIT_RATING:     return w.crit;
            case ITEM_MOD_HASTE_RATING:    return w.haste;
            case ITEM_MOD_MASTERY_RATING:  return w.mastery;
            case ITEM_MOD_VERSATILITY:     return w.versatility;
            case ITEM_MOD_CR_LIFESTEAL:    return w.leech;
            case ITEM_MOD_CR_AVOIDANCE:    return w.avoidance;
            default:                       return 0.0f;
        }
    }

    float ItemScore(Player const* bot, ItemTemplate const* proto, uint32 itemLevel)
    {
        auto itr = WeightTable.find(bot->GetSpecializationId());
        if (itr == WeightTable.end())
            return 0.0f;
        Weights const& w = itr->second;

        float score = 0.0f;
        bool hasPrimary = false;
        float primaryValue = 0.0f;
        for (uint32 i = 0; i < MAX_ITEM_PROTO_STATS; ++i)
        {
            int32 const type = proto->GetItemStatType(i);
            if (type <= 0)
                continue;
            float const amount = float(StatAmount(proto, i, itemLevel));
            if (amount <= 0.0f)
                continue;

            float weight = 0.0f;
            bool primary = false;
            switch (type)
            {
                case ITEM_MOD_STRENGTH:        weight = w.strength; primary = true; break;
                case ITEM_MOD_AGILITY:         weight = w.agility; primary = true; break;
                case ITEM_MOD_INTELLECT:       weight = w.intellect; primary = true; break;
                // hybrid primaries give the stat the spec uses (Pawn's PawnCombineStats)
                case ITEM_MOD_AGI_STR_INT:     weight = std::max({ w.agility, w.strength, w.intellect }); primary = true; break;
                case ITEM_MOD_AGI_STR:         weight = std::max(w.agility, w.strength); primary = true; break;
                case ITEM_MOD_AGI_INT:         weight = std::max(w.agility, w.intellect); primary = true; break;
                case ITEM_MOD_STR_INT:         weight = std::max(w.strength, w.intellect); primary = true; break;
                case ITEM_MOD_STAMINA:         weight = w.stamina; break;
                case ITEM_MOD_CRIT_RATING:     weight = w.crit; break;
                case ITEM_MOD_HASTE_RATING:    weight = w.haste; break;
                case ITEM_MOD_MASTERY_RATING:  weight = w.mastery; break;
                case ITEM_MOD_VERSATILITY:     weight = w.versatility; break;
                case ITEM_MOD_CR_LIFESTEAL:    weight = w.leech; break;
                case ITEM_MOD_CR_AVOIDANCE:    weight = w.avoidance; break;
                default: break;
            }
            if (primary)
            {
                hasPrimary = true;
                primaryValue += weight * amount;
            }
            score += weight * amount;
        }

        // another spec's piece: primary stats this spec does not use at all
        if (hasPrimary && primaryValue <= 0.0f)
            return 0.0f;

        if (proto->GetClass() == ITEM_CLASS_ARMOR)
            score += w.armor * float(proto->GetArmor(itemLevel));
        else if (proto->GetClass() == ITEM_CLASS_WEAPON)
        {
            float minDamage = 0.0f, maxDamage = 0.0f;
            proto->GetDamage(itemLevel, minDamage, maxDamage);
            score += w.minDamage * minDamage + w.maxDamage * maxDamage;
        }
        return score;
    }

    Result AutoGear(Player* bot, Options const& options)
    {
        Result result;
        uint32 const spec = bot->GetSpecializationId();
        if (WeightTable.find(spec) == WeightTable.end())
            return result;

        uint8 const level = bot->getLevel();
        uint32 const levelCap = ItemLevelCapFor(level, options);
        uint32 const armorType = WornArmorType(bot);
        Hands const hands = HandsFor(spec);
        result.itemLevelCap = levelCap;

        result.bagsAdded = AddBags(bot, options.bagEntry);

        struct Candidate
        {
            ItemTemplate const* proto = nullptr;
            float score = 0.0f;
        };
        // the best `keep` of each kind: the random choice among the top `choices`, plus one more so rings,
        // trinkets and dual wield still find a second distinct item
        size_t const choices = std::max<uint32>(1, options.choices);
        size_t const keep = choices + 1;
        std::unordered_map<uint32, std::vector<Candidate>> best;
        auto offer = [&best, keep](uint32 kind, ItemTemplate const* proto, float score)
        {
            std::vector<Candidate>& list = best[kind];
            list.push_back({ proto, score });
            std::sort(list.begin(), list.end(), [](Candidate const& a, Candidate const& b) { return a.score > b.score; });
            if (list.size() > keep)
                list.resize(keep);
        };

        // kinds: equipment slot + 1 for armor (the head slot is 0, and 0 meant "no kind": no helmet was ever
        // a candidate, the owner's "autogear skips the head" report); weapons by how they are held
        enum : uint32 { KIND_NONE = 0, KIND_2H = 100, KIND_1H_MAIN, KIND_1H_OFF, KIND_SHIELD, KIND_HELD, KIND_RANGED };
        auto slotKind = [](uint8 slot) -> uint32 { return uint32(slot) + 1; };

        for (auto const& pair : *sObjectMgr->GetItemTemplateStore())
        {
            ItemTemplate const* proto = &pair.second;
            uint32 const cls = proto->GetClass();
            if (cls != ITEM_CLASS_ARMOR && cls != ITEM_CLASS_WEAPON)
                continue;

            uint32 const quality = proto->GetQuality();
            if (quality < options.minQuality || quality > options.maxQuality || quality >= ITEM_QUALITY_ARTIFACT)
                continue;
            uint32 const itemLevel = ItemLevelOf(proto);
            if (options.maxItemLevel && itemLevel > options.maxItemLevel)
                continue;
            if (levelCap && itemLevel > levelCap)
                continue;
            if (proto->GetBaseRequiredLevel() > int32(level) || proto->GetScalingStatDistribution())
                continue;

            InventoryType const type = proto->GetInventoryType();
            uint32 kind = KIND_NONE;
            switch (type)
            {
                case INVTYPE_HEAD:      kind = slotKind(EQUIPMENT_SLOT_HEAD); break;
                case INVTYPE_NECK:      kind = slotKind(EQUIPMENT_SLOT_NECK); break;
                case INVTYPE_SHOULDERS: kind = slotKind(EQUIPMENT_SLOT_SHOULDERS); break;
                case INVTYPE_CHEST:
                case INVTYPE_ROBE:      kind = slotKind(EQUIPMENT_SLOT_CHEST); break;
                case INVTYPE_WAIST:     kind = slotKind(EQUIPMENT_SLOT_WAIST); break;
                case INVTYPE_LEGS:      kind = slotKind(EQUIPMENT_SLOT_LEGS); break;
                case INVTYPE_FEET:      kind = slotKind(EQUIPMENT_SLOT_FEET); break;
                case INVTYPE_WRISTS:    kind = slotKind(EQUIPMENT_SLOT_WRISTS); break;
                case INVTYPE_HANDS:     kind = slotKind(EQUIPMENT_SLOT_HANDS); break;
                case INVTYPE_FINGER:    kind = slotKind(EQUIPMENT_SLOT_FINGER1); break;
                case INVTYPE_TRINKET:   kind = slotKind(EQUIPMENT_SLOT_TRINKET1); break;
                case INVTYPE_CLOAK:     kind = slotKind(EQUIPMENT_SLOT_BACK); break;
                case INVTYPE_2HWEAPON:  kind = KIND_2H; break;
                case INVTYPE_WEAPON:    kind = KIND_1H_MAIN; break;    // also offered as an off-hand below
                case INVTYPE_WEAPONMAINHAND: kind = KIND_1H_MAIN; break;
                case INVTYPE_WEAPONOFFHAND:  kind = KIND_1H_OFF; break;
                case INVTYPE_SHIELD:    kind = KIND_SHIELD; break;
                case INVTYPE_HOLDABLE:  kind = KIND_HELD; break;
                case INVTYPE_RANGED:
                case INVTYPE_RANGEDRIGHT:
                    if (cls == ITEM_CLASS_WEAPON && proto->GetSubClass() != ITEM_SUBCLASS_WEAPON_WAND)
                        kind = KIND_RANGED;
                    break;
                default: break;
            }
            if (kind == KIND_NONE)
                continue;
            if (IsBodyArmor(type) && proto->GetSubClass() != armorType)
                continue;

            if (!proto->IsUsableBySpecialization(spec, level, false) || bot->CanUseItem(proto) != EQUIP_ERR_OK)
                continue;
            if (LooksUnobtainable(proto))
                continue;

            float const score = ItemScore(bot, proto, itemLevel);
            if (score <= 0.0f)
                continue;

            ++result.candidates;
            offer(kind, proto, score);
            if (type == INVTYPE_WEAPON)
                offer(KIND_1H_OFF, proto, score);
        }

        // what the bot wears in a slot, scored the same way
        // a worn piece above the cap counts as nothing (owner, build #26: about 1 bot in 10 kept a near end-game
        // piece at a low level, from a quest reward or loot): any allowed piece replaces it
        auto wornScore = [bot, levelCap](uint8 slot) -> float
        {
            Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (!item)
                return 0.0f;
            uint32 const itemLevel = item->GetItemLevel(bot->getLevel());
            if (levelCap && itemLevel > levelCap)
                return 0.0f;
            return ItemScore(bot, item->GetTemplate(), itemLevel);
        };

        auto equip = [&](uint8 slot, ItemTemplate const* proto) -> bool
        {
            Item* old = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (old && old->GetEntry() == proto->GetId())
                return false;

            uint16 dest = 0;
            if (bot->CanEquipNewItem(slot, dest, proto->GetId(), true) != EQUIP_ERR_OK)
                return false;

            if (old)
            {
                if (options.destroyReplaced)
                    bot->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);
                else
                {
                    ItemPosCountVec bagDest;
                    if (bot->CanStoreItem(NULL_BAG, NULL_SLOT, bagDest, old, false) != EQUIP_ERR_OK)
                    {
                        ++result.noBagRoom;
                        return false;
                    }
                    bot->RemoveItem(INVENTORY_SLOT_BAG_0, slot, true);
                    bot->StoreItem(bagDest, old, true);
                }
            }

            if (!bot->EquipNewItem(dest, proto->GetId(), true))
                return false;

            ++result.equipped;
            if (proto->GetName())
                result.pieces.push_back(proto->GetName()->Str[LOCALE_enUS]);
            return true;
        };

        auto top = [&best](uint32 kind, size_t index = 0) -> Candidate
        {
            auto itr = best.find(kind);
            return (itr != best.end() && itr->second.size() > index) ? itr->second[index] : Candidate();
        };

        // a random one of the kind's best `choices` candidates scoring above minScore and not `exclude` (an
        // item already chosen for the other ring / trinket / hand slot); none when no candidate beats it
        auto pick = [&best, choices](uint32 kind, float minScore, ItemTemplate const* exclude) -> Candidate
        {
            std::vector<Candidate> pool;
            auto itr = best.find(kind);
            if (itr != best.end())
                for (Candidate const& c : itr->second)
                    if (c.proto != exclude && c.score > minScore && pool.size() < choices)
                        pool.push_back(c);
            return pool.empty() ? Candidate() : pool[urand(0, pool.size() - 1)];
        };
        auto wornProto = [bot](uint8 slot) -> ItemTemplate const*
        {
            Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            return item ? item->GetTemplate() : nullptr;
        };

        // armor and jewellery, slot by slot: only upgrades
        static uint8 const singleSlots[] = { EQUIPMENT_SLOT_HEAD, EQUIPMENT_SLOT_NECK, EQUIPMENT_SLOT_SHOULDERS,
            EQUIPMENT_SLOT_CHEST, EQUIPMENT_SLOT_WAIST, EQUIPMENT_SLOT_LEGS, EQUIPMENT_SLOT_FEET,
            EQUIPMENT_SLOT_WRISTS, EQUIPMENT_SLOT_HANDS, EQUIPMENT_SLOT_BACK };
        for (uint8 slot : singleSlots)
        {
            Candidate c = pick(slotKind(slot), wornScore(slot), nullptr);
            if (c.proto)
                equip(slot, c.proto);
        }
        // two distinct rings / trinkets (both are often unique-equipped)
        for (uint8 base : { uint8(EQUIPMENT_SLOT_FINGER1), uint8(EQUIPMENT_SLOT_TRINKET1) })
            for (uint8 n = 0; n < 2; ++n)
            {
                uint8 const slot = base + n;
                Candidate c = pick(slotKind(base), wornScore(slot), wornProto(n ? base : base + 1));
                if (c.proto)
                    equip(slot, c.proto);
            }

        // weapons: the best way to fill both hands for the spec (by the best candidates), against what the bot
        // holds now; then a random pick among the top candidates of that layout
        uint32 mainKind = KIND_NONE, offKind = KIND_NONE;
        float layoutScore = 0.0f;
        auto consider = [&](uint32 main, uint32 off)
        {
            Candidate const m = top(main);
            if (!m.proto)
                return;
            Candidate o;
            if (off != KIND_NONE)
            {
                o = top(off);
                if (o.proto == m.proto)
                    o = top(off, 1);                        // a second copy may be unique-equipped
            }
            if (m.score + o.score > layoutScore)
            {
                layoutScore = m.score + o.score;
                mainKind = main;
                offKind = o.proto ? off : KIND_NONE;
            }
        };
        switch (hands)
        {
            case Hands::TwoHand:       consider(KIND_2H, KIND_NONE); break;
            case Hands::Ranged:        consider(KIND_RANGED, KIND_NONE); break;
            case Hands::OneHandShield: consider(KIND_1H_MAIN, KIND_SHIELD); break;
            case Hands::DualWield:     consider(KIND_1H_MAIN, KIND_1H_OFF); break;
            case Hands::Caster:        consider(KIND_2H, KIND_NONE); consider(KIND_1H_MAIN, KIND_HELD); break;
            case Hands::CasterShield:  consider(KIND_2H, KIND_NONE); consider(KIND_1H_MAIN, KIND_SHIELD); break;
            case Hands::DualOrTwoHand: consider(KIND_2H, KIND_NONE); consider(KIND_1H_MAIN, KIND_1H_OFF); break;
            case Hands::TitanGrip:
                if (bot->CanTitanGrip())
                    consider(KIND_2H, KIND_2H);
                consider(KIND_1H_MAIN, KIND_1H_OFF);
                break;
        }

        Candidate newMain, newOff;
        float const wornHands = WornHandsFit(bot, hands) ?
            wornScore(EQUIPMENT_SLOT_MAINHAND) + wornScore(EQUIPMENT_SLOT_OFFHAND) : 0.0f;
        if (mainKind != KIND_NONE)
        {
            newMain = pick(mainKind, 0.0f, nullptr);
            if (offKind != KIND_NONE)
                newOff = pick(offKind, 0.0f, newMain.proto);
            // the random pair must still beat what the bot holds; else the best pair, if it does
            if (newMain.score + newOff.score <= wornHands)
            {
                newMain = top(mainKind);
                newOff = Candidate();
                if (offKind != KIND_NONE)
                {
                    newOff = top(offKind);
                    if (newOff.proto == newMain.proto)
                        newOff = top(offKind, 1);
                }
            }
        }

        if (newMain.proto && newMain.score + newOff.score > wornHands)
        {
            bool const mainTwoHanded = newMain.proto->GetInventoryType() == INVTYPE_2HWEAPON ||
                newMain.proto->GetInventoryType() == INVTYPE_RANGED || newMain.proto->GetInventoryType() == INVTYPE_RANGEDRIGHT;

            // a two-hander needs a free off hand (unless Titan's Grip pairs it)
            if (mainTwoHanded && !newOff.proto)
                if (Item* off = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND))
                {
                    if (options.destroyReplaced)
                        bot->DestroyItem(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND, true);
                    else
                    {
                        ItemPosCountVec bagDest;
                        if (bot->CanStoreItem(NULL_BAG, NULL_SLOT, bagDest, off, false) == EQUIP_ERR_OK)
                        {
                            bot->RemoveItem(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND, true);
                            bot->StoreItem(bagDest, off, true);
                        }
                        else
                            ++result.noBagRoom;
                    }
                }

            if (equip(EQUIPMENT_SLOT_MAINHAND, newMain.proto) || bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND))
                if (newOff.proto)
                    equip(EQUIPMENT_SLOT_OFFHAND, newOff.proto);
        }

        return result;
    }

    namespace
    {
        // large bags tried in turn when Playerbots.AutoGear.BagEntry is 0 or not a bag on this server:
        // Hexweave Bag (30), Royal Satchel (24), Embersilk Bag (22), Frostweave Bag (20)
        uint32 const BagFallbacks[] = { 114821, 82446, 54443, 41599 };

        uint32 BagToUse(uint32 configured)
        {
            auto isBag = [](uint32 entry)
            {
                ItemTemplate const* proto = entry ? sObjectMgr->GetItemTemplate(entry) : nullptr;
                return proto && proto->GetClass() == ITEM_CLASS_CONTAINER && proto->GetInventoryType() == INVTYPE_BAG;
            };
            if (isBag(configured))
                return configured;
            for (uint32 entry : BagFallbacks)
                if (isBag(entry))
                    return entry;
            return 0;
        }
    }

    uint32 AddBags(Player* bot, uint32 bagEntry)
    {
        uint32 const entry = BagToUse(bagEntry);
        if (!entry)
        {
            TC_LOG_ERROR("server.loading", "[mod-playerbots] Autogear: no bag item found (Playerbots.AutoGear.BagEntry = %u).", bagEntry);
            return 0;
        }

        uint32 added = 0;
        for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
        {
            if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                continue;
            uint16 dest = 0;
            if (bot->CanEquipNewItem(slot, dest, entry, false) != EQUIP_ERR_OK)
                continue;
            if (bot->EquipNewItem(dest, entry, true))
                ++added;
        }
        return added;
    }

    uint32 ClearInventory(Player* bot)
    {
        uint32 destroyed = 0;
        // what the bags hold
        for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
            if (Bag* container = bot->GetBagByPos(bag))
                for (uint32 slot = 0; slot < container->GetBagSize(); ++slot)
                    if (container->GetItemByPos(uint8(slot)))
                    {
                        bot->DestroyItem(bag, uint8(slot), true);
                        ++destroyed;
                    }
        // worn gear and the backpack (the bag slots, between them, are kept)
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        {
            if (slot >= INVENTORY_SLOT_BAG_START && slot < INVENTORY_SLOT_BAG_END)
                continue;
            if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            {
                bot->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);
                ++destroyed;
            }
        }
        return destroyed;
    }

    uint32 ItemLevelWithBonuses(ItemTemplate const* proto, std::vector<uint32> const& bonusListIds)
    {
        BonusData bonus;
        bonus.Initialize(proto);
        for (uint32 id : bonusListIds)
            if (DB2Manager::ItemBonusList const* list = sDB2Manager.GetItemBonusList(id))
                for (ItemBonusEntry const* entry : *list)
                    bonus.AddBonus(entry->Type, entry->Value);
        return uint32(std::max<int32>(1, int32(ItemLevelOf(proto)) + bonus.ItemLevelBonus));
    }

    Comparison CompareToWorn(Player* bot, ItemTemplate const* proto, uint32 itemLevel)
    {
        Comparison result;
        uint32 const spec = bot->GetSpecializationId();
        if (!proto || WeightTable.find(spec) == WeightTable.end())
            return result;
        uint32 const cls = proto->GetClass();
        if (cls != ITEM_CLASS_ARMOR && cls != ITEM_CLASS_WEAPON)
            return result;

        InventoryType const type = proto->GetInventoryType();
        if (IsBodyArmor(type) && proto->GetSubClass() != WornArmorType(bot))
            return result;
        if (!proto->IsUsableBySpecialization(spec, bot->getLevel(), false) || bot->CanUseItem(proto) != EQUIP_ERR_OK)
            return result;

        auto worn = [bot](uint8 slot) -> Item* { return bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot); };
        // the same cap as autogear: nothing above it is put on, and a worn piece above it counts as nothing
        uint32 const levelCap = ItemLevelCap(bot->getLevel(), sPlayerbotMgr->GetGearOptions());
        if (levelCap && itemLevel > levelCap)
            return result;
        auto wornScore = [bot, &worn, levelCap](uint8 slot) -> float
        {
            Item* item = worn(slot);
            if (!item)
                return 0.0f;
            uint32 const wornLevel = item->GetItemLevel(bot->getLevel());
            if (levelCap && wornLevel > levelCap)
                return 0.0f;
            return ItemScore(bot, item->GetTemplate(), wornLevel);
        };
        auto isTwoHander = [](ItemTemplate const* p)
        {
            InventoryType const t = p->GetInventoryType();
            return t == INVTYPE_2HWEAPON || t == INVTYPE_RANGED || t == INVTYPE_RANGEDRIGHT;
        };
        Item* mainHand = worn(EQUIPMENT_SLOT_MAINHAND);
        bool const titanGrip = HandsFor(spec) == Hands::TitanGrip && bot->CanTitanGrip();
        bool const mainTwoHanded = mainHand && isTwoHander(mainHand->GetTemplate()) && !titanGrip;
        Hands const hands = HandsFor(spec);
        bool const dual = hands == Hands::DualWield || hands == Hands::DualOrTwoHand || hands == Hands::TitanGrip;
        bool const handsFit = WornHandsFit(bot, hands);

        // the weaker of two slots (an empty one first)
        auto weaker = [&](uint8 a, uint8 b) -> uint8 { return wornScore(b) < wornScore(a) ? b : a; };

        uint8 slot = 0;
        float old = 0.0f;
        switch (type)
        {
            case INVTYPE_HEAD:      slot = EQUIPMENT_SLOT_HEAD; break;
            case INVTYPE_NECK:      slot = EQUIPMENT_SLOT_NECK; break;
            case INVTYPE_SHOULDERS: slot = EQUIPMENT_SLOT_SHOULDERS; break;
            case INVTYPE_CHEST:
            case INVTYPE_ROBE:      slot = EQUIPMENT_SLOT_CHEST; break;
            case INVTYPE_WAIST:     slot = EQUIPMENT_SLOT_WAIST; break;
            case INVTYPE_LEGS:      slot = EQUIPMENT_SLOT_LEGS; break;
            case INVTYPE_FEET:      slot = EQUIPMENT_SLOT_FEET; break;
            case INVTYPE_WRISTS:    slot = EQUIPMENT_SLOT_WRISTS; break;
            case INVTYPE_HANDS:     slot = EQUIPMENT_SLOT_HANDS; break;
            case INVTYPE_CLOAK:     slot = EQUIPMENT_SLOT_BACK; break;
            case INVTYPE_FINGER:    slot = weaker(EQUIPMENT_SLOT_FINGER1, EQUIPMENT_SLOT_FINGER2); break;
            case INVTYPE_TRINKET:   slot = weaker(EQUIPMENT_SLOT_TRINKET1, EQUIPMENT_SLOT_TRINKET2); break;
            case INVTYPE_2HWEAPON:
                if (hands == Hands::OneHandShield || hands == Hands::DualWield || hands == Hands::Ranged)
                    return result;
                if (titanGrip)
                    slot = weaker(EQUIPMENT_SLOT_MAINHAND, EQUIPMENT_SLOT_OFFHAND);
                else
                {
                    slot = EQUIPMENT_SLOT_MAINHAND;
                    old = wornScore(EQUIPMENT_SLOT_MAINHAND) + wornScore(EQUIPMENT_SLOT_OFFHAND);
                    result.twoHanded = true;
                }
                break;
            case INVTYPE_RANGED:
            case INVTYPE_RANGEDRIGHT:
                if (hands != Hands::Ranged || cls != ITEM_CLASS_WEAPON || proto->GetSubClass() == ITEM_SUBCLASS_WEAPON_WAND)
                    return result;
                slot = EQUIPMENT_SLOT_MAINHAND;
                old = wornScore(EQUIPMENT_SLOT_MAINHAND) + wornScore(EQUIPMENT_SLOT_OFFHAND);
                result.twoHanded = true;
                break;
            case INVTYPE_WEAPON:
            case INVTYPE_WEAPONMAINHAND:
                if (hands == Hands::TwoHand || hands == Hands::Ranged)
                    return result;
                // a one-hander instead of a two-hander must beat the whole two-hander alone
                if (dual && type == INVTYPE_WEAPON && !mainTwoHanded)
                    slot = weaker(EQUIPMENT_SLOT_MAINHAND, EQUIPMENT_SLOT_OFFHAND);
                else
                    slot = EQUIPMENT_SLOT_MAINHAND;
                break;
            case INVTYPE_WEAPONOFFHAND:
                if (!dual || mainTwoHanded)
                    return result;
                slot = EQUIPMENT_SLOT_OFFHAND;
                break;
            case INVTYPE_SHIELD:
                if ((hands != Hands::OneHandShield && hands != Hands::CasterShield) || mainTwoHanded)
                    return result;
                slot = EQUIPMENT_SLOT_OFFHAND;
                break;
            case INVTYPE_HOLDABLE:
                if (hands != Hands::Caster || mainTwoHanded)
                    return result;
                slot = EQUIPMENT_SLOT_OFFHAND;
                break;
            default:
                return result;
        }

        if (!result.twoHanded)
            old = wornScore(slot);
        // a weapon layout the spec does not use (a prot warrior's two-hander) counts as nothing held
        if (!handsFit && (slot == EQUIPMENT_SLOT_MAINHAND || slot == EQUIPMENT_SLOT_OFFHAND))
            old = 0.0f;
        result.newScore = ItemScore(bot, proto, itemLevel);
        if (result.newScore <= 0.0f)
            return result;

        result.usable = true;
        result.slot = slot;
        result.oldScore = old;
        result.gain = old > 0.0f ? (result.newScore - old) / old : 1.0f;
        return result;
    }
}

/*
 * mod-playerbots: the "maintenance" order and riding. See BotMaintenance.h.
 */

#include "BotMaintenance.h"
#include "BotFactory.h"
#include "BotGear.h"
#include "CollectionMgr.h"
#include "DB2Stores.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotMgr.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <vector>

namespace
{
    // ---- riding ----------------------------------------------------------------------------------------------

    struct RidingSpell
    {
        uint32 spellId;
        uint8 level;
    };
    // the riding skills, and the old flying licences where this server still has them (a missing spell is skipped)
    RidingSpell const RidingSpells[] =
    {
        { 33388, 20 },      // Apprentice Riding
        { 33391, 40 },      // Journeyman Riding
        { 34090, 60 },      // Expert Riding (flying)
        { 90267, 60 },      // Flight Master's License
        { 54197, 68 },      // Cold Weather Flying
        { 34091, 70 },      // Artisan Riding
        { 90265, 80 },      // Master Riding
        { 115913, 90 },     // Wisdom of the Four Winds
    };

    // racial mounts: a normal and a swift list per race (spells; checked against Mount.db2 at use)
    struct RaceMounts
    {
        uint8 race;
        std::vector<uint32> normal;
        std::vector<uint32> swift;
    };
    std::vector<RaceMounts> const RacialMounts =
    {
        { RACE_HUMAN,              { 458, 470, 472, 6648 },             { 23227, 23228, 23229 } },
        { RACE_ORC,                { 580, 6653, 6654 },                 { 23250, 23251, 23252 } },
        { RACE_DWARF,              { 6777, 6898, 6899 },                { 23238, 23239, 23240 } },
        { RACE_NIGHTELF,           { 8394, 10789, 10793 },              { 23219, 23221, 23338 } },
        { RACE_UNDEAD_PLAYER,      { 17462, 17463, 17464 },             { 17465, 23246 } },
        { RACE_TAUREN,             { 18989, 18990, 64657 },             { 23247, 23248, 23249 } },
        { RACE_GNOME,              { 10873, 10969, 17453, 17454 },      { 23222, 23223, 23225 } },
        { RACE_TROLL,              { 8395, 10796, 10799 },              { 23241, 23242, 23243 } },
        { RACE_GOBLIN,             { 87090 },                           { 87091 } },
        { RACE_BLOODELF,           { 34795, 35018, 35020, 35022 },      { 33660, 35025, 35027 } },
        { RACE_DRAENEI,            { 34406, 35710, 35711 },             { 35712, 35713, 35714 } },
        { RACE_WORGEN,             { 103195 },                          { 103196 } },
        { RACE_PANDAREN_NEUTRAL,   { 120395, 127286, 127287, 127288, 127289, 127290 }, { 120822, 127293, 127295, 127302, 127308, 127310 } },
        { RACE_PANDAREN_ALLIANCE,  { 120395, 127286, 127287, 127288, 127289, 127290 }, { 120822, 127293, 127295, 127302, 127308, 127310 } },
        { RACE_PANDAREN_HORDE,     { 120395, 127286, 127287, 127288, 127289, 127290 }, { 120822, 127293, 127295, 127302, 127308, 127310 } },
        { RACE_NIGHTBORNE,         { },                                 { 258845 } },
        { RACE_HIGHMOUNTAIN_TAUREN,{ },                                 { 258060 } },
        { RACE_VOID_ELF,           { },                                 { 259202 } },
        { RACE_LIGHTFORGED_DRAENEI,{ },                                 { 258022 } },
    };
    std::vector<uint32> const AllianceFlying = { 32235, 32239, 32240 };
    std::vector<uint32> const AllianceSwiftFlying = { 32242, 32289, 32290, 32292 };
    std::vector<uint32> const HordeFlying = { 32243, 32244, 32245 };
    std::vector<uint32> const HordeSwiftFlying = { 32246, 32295, 32296, 32297 };

    bool IsMount(uint32 spellId)
    {
        return sDB2Manager.GetMount(spellId) && sSpellMgr->GetSpellInfo(spellId);
    }

    RaceMounts const* MountsOfRace(uint8 race, uint32 team)
    {
        for (RaceMounts const& entry : RacialMounts)
            if (entry.race == race)
                return &entry;
        // another race: the faction's classic mounts
        uint8 const fallback = team == ALLIANCE ? RACE_HUMAN : RACE_ORC;
        for (RaceMounts const& entry : RacialMounts)
            if (entry.race == fallback)
                return &entry;
        return nullptr;
    }

    bool OwnsAny(Player* bot, std::vector<uint32> const& list)
    {
        for (uint32 spell : list)
            if (bot->HasSpell(spell))
                return true;
        return false;
    }

    // a random valid mount of the list into the bot's collection (it learns the spell)
    bool AddRandomMount(Player* bot, std::vector<uint32> const& list)
    {
        std::vector<uint32> valid;
        for (uint32 spell : list)
            if (IsMount(spell))
                valid.push_back(spell);
        if (valid.empty())
            return false;
        uint32 const spell = valid[urand(0, valid.size() - 1)];
        if (!bot->GetCollectionMgr() || !bot->GetCollectionMgr()->AddMount(spell))
            bot->learnSpell(spell, false);
        return true;
    }

    bool CanFlyByRiding(Player* bot)
    {
        return bot->HasSpell(34090) || bot->HasSpell(34091) || bot->HasSpell(90265);
    }

    // a mount that flies: our flying lists, or a Mount.db2 flying type
    bool IsFlyingMount(uint32 spellId)
    {
        for (std::vector<uint32> const* list : { &AllianceFlying, &AllianceSwiftFlying, &HordeFlying, &HordeSwiftFlying })
            if (std::find(list->begin(), list->end(), spellId) != list->end())
                return true;
        if (MountEntry const* mount = sDB2Manager.GetMount(spellId))
            return mount->MountTypeID == 247 || mount->MountTypeID == 248;
        return false;
    }

    // ---- enchants and gems -----------------------------------------------------------------------------------

    float const BudgetShare = 0.4f;                         // an enchant / gem may be worth this share of the item's budget

    struct EnchantCandidate
    {
        uint32 spellId;                                     // the recipe / enhancement spell (its item restrictions)
        uint32 enchantId;
        uint8 requiredLevel;                                // item enhancements' required level (recipes: 0)
    };

    struct GemCandidate
    {
        uint32 itemId;
        uint32 enchantId;
        int32 type;                                         // GemProperties::Type (socket colours it fits)
        uint8 requiredLevel;
        uint32 itemLevel;
        uint32 minItemLevel;                                // of the item it goes into
    };

    std::once_flag candidatesOnce;
    std::vector<EnchantCandidate> enchantCandidates;
    std::vector<GemCandidate> gemCandidates;

    // the enchantment of a spell that permanently enchants an item, 0 if none
    uint32 PermanentEnchantOf(SpellInfo const* spell)
    {
        for (SpellEffectInfo const* effect : spell->Effects)
            if (effect && effect->Effect == SPELL_EFFECT_ENCHANT_ITEM && effect->MiscValue > 0)
                return uint32(effect->MiscValue);
        return 0;
    }

    // a usable enchantment: stats only from fixed amounts, no profession or scaling requirement
    bool UsableEnchantment(uint32 enchantId)
    {
        SpellItemEnchantmentEntry const* enchant = sSpellItemEnchantmentStore.LookupEntry(enchantId);
        if (!enchant || enchant->RequiredSkillID || enchant->ScalingClass)
            return false;
        for (uint8 i = 0; i < 3; ++i)
            if (enchant->Effect[i] == ITEM_ENCHANTMENT_TYPE_STAT && enchant->EffectPointsMin[i] > 0)
                return true;
        return false;
    }

    void BuildCandidates()
    {
        std::set<uint32> seen;
        // Enchanting recipes
        for (SkillLineAbilityEntry const* ability : sSkillLineAbilityStore)
        {
            if (!ability || ability->SkillLine != SKILL_ENCHANTING || seen.count(ability->Spell))
                continue;
            SpellInfo const* spell = sSpellMgr->GetSpellInfo(ability->Spell);
            if (!spell)
                continue;
            uint32 const enchantId = PermanentEnchantOf(spell);
            if (!enchantId || !UsableEnchantment(enchantId))
                continue;
            seen.insert(ability->Spell);
            enchantCandidates.push_back({ ability->Spell, enchantId, 0 });
        }
        // item enhancements (scrolls, armor kits, shoulder inscriptions...) and gems
        for (auto const& pair : *sObjectMgr->GetItemTemplateStore())
        {
            ItemTemplate const& proto = pair.second;
            if (proto.GetRequiredSkill())
                continue;                                   // a profession's own
            if (proto.GetClass() == ITEM_CLASS_GEM)
            {
                GemPropertiesEntry const* gem = sGemPropertiesStore.LookupEntry(proto.GetGemProperties());
                if (!gem || !gem->EnchantID || !UsableEnchantment(gem->EnchantID) || (proto.GetFlags() & ITEM_FLAG_UNIQUE_EQUIPPABLE) ||
                    proto.GetArtifactID() || !(gem->Type & SOCKET_COLOR_PRISMATIC))
                    continue;                               // relics and odd gems out: only gems for normal sockets
                gemCandidates.push_back({ proto.GetId(), gem->EnchantID, gem->Type,
                    uint8(std::max(0, proto.GetBaseRequiredLevel())), proto.GetBaseItemLevel(), gem->MinItemLevel });
                continue;
            }
            if (proto.GetBaseRequiredLevel() <= 0)
                continue;                                   // no level to judge it by
            for (ItemEffectEntry const* effect : proto.Effects)
            {
                if (!effect || effect->SpellID <= 0)
                    continue;
                SpellInfo const* spell = sSpellMgr->GetSpellInfo(effect->SpellID);
                if (!spell || seen.count(spell->Id))
                    continue;
                uint32 const enchantId = PermanentEnchantOf(spell);
                if (!enchantId || !UsableEnchantment(enchantId))
                    continue;
                seen.insert(spell->Id);
                enchantCandidates.push_back({ spell->Id, enchantId, uint8(proto.GetBaseRequiredLevel()) });
            }
        }
        TC_LOG_INFO("server.loading", "[mod-playerbots] Maintenance: %u enchant(s) and %u gem(s) available to bots.",
            uint32(enchantCandidates.size()), uint32(gemCandidates.size()));
    }

    // an enchantment's stats: weighted for the bot's spec (score) and in raw points (power, against the budget)
    void ValueEnchantment(Player* bot, uint32 enchantId, float& score, float& power)
    {
        score = 0.0f;
        power = 0.0f;
        SpellItemEnchantmentEntry const* enchant = sSpellItemEnchantmentStore.LookupEntry(enchantId);
        if (!enchant)
            return;
        for (uint8 i = 0; i < 3; ++i)
        {
            if (enchant->Effect[i] != ITEM_ENCHANTMENT_TYPE_STAT)
                continue;
            float const amount = float(enchant->EffectPointsMin[i]);
            score += BotGear::StatWeight(bot, int32(enchant->EffectArg[i])) * amount;
            power += amount;
        }
    }

    // the stat budget of the item: an epic piece's random property points at its item level
    float Budget(Player* bot, Item* item)
    {
        RandPropPointsEntry const* points = sRandPropPointsStore.LookupEntry(item->GetItemLevel(bot->getLevel()));
        return points ? float(points->Epic[0]) : 0.0f;
    }

    void Enchant(Player* bot, Item* item, uint32 enchantId)
    {
        // as Spell::EffectEnchantItemPerm
        bot->ApplyEnchantment(item, PERM_ENCHANTMENT_SLOT, false);
        item->SetEnchantment(PERM_ENCHANTMENT_SLOT, enchantId, 0, 0, bot->GetGUID());
        bot->ApplyEnchantment(item, PERM_ENCHANTMENT_SLOT, true);
        item->SetState(ITEM_CHANGED, bot);
    }
}

namespace BotMaintenance
{
    void LearnRiding(Player* bot, Result& result)
    {
        uint8 const level = bot->getLevel();
        for (RidingSpell const& riding : RidingSpells)
            if (level >= riding.level && !bot->HasSpell(riding.spellId) && sSpellMgr->GetSpellInfo(riding.spellId))
            {
                bot->learnSpell(riding.spellId, false);
                ++result.ridingSpells;
            }

        RaceMounts const* racial = MountsOfRace(bot->getRace(), bot->GetTeam());
        bool const alliance = bot->GetTeam() == ALLIANCE;
        if (racial)
        {
            if (level >= 40 && !OwnsAny(bot, racial->swift) && AddRandomMount(bot, racial->swift))
                ++result.mounts;
            else if (level >= 20 && level < 40 && !OwnsAny(bot, racial->normal) && !OwnsAny(bot, racial->swift) &&
                AddRandomMount(bot, racial->normal.empty() ? racial->swift : racial->normal))
                ++result.mounts;
        }
        std::vector<uint32> const& flying = alliance ? AllianceFlying : HordeFlying;
        std::vector<uint32> const& swiftFlying = alliance ? AllianceSwiftFlying : HordeSwiftFlying;
        if (level >= 70 && !OwnsAny(bot, swiftFlying) && AddRandomMount(bot, swiftFlying))
            ++result.mounts;
        else if (level >= 60 && level < 70 && !OwnsAny(bot, flying) && !OwnsAny(bot, swiftFlying) && AddRandomMount(bot, flying))
            ++result.mounts;
    }

    void ForgetRiding(Player* bot)
    {
        for (RidingSpell const& riding : RidingSpells)
            if (bot->getLevel() < riding.level && bot->HasSpell(riding.spellId))
                bot->removeSpell(riding.spellId, false, false);
    }

    bool CanFlyByRiding(Player* bot)
    {
        return ::CanFlyByRiding(bot);
    }

    bool IsFlyingMountSpell(uint32 spellId)
    {
        return IsFlyingMount(spellId);
    }

    uint32 MountSpellFor(Player* bot, bool flying)
    {
        if (bot->getLevel() < 20 || !bot->HasSpell(33388))
            return 0;
        bool const canFly = flying && CanFlyByRiding(bot);
        std::vector<uint32> ground;
        std::vector<uint32> fliers;
        for (auto const& pair : bot->GetSpellMap())
        {
            if (pair.second.state == PLAYERSPELL_REMOVED || pair.second.disabled || !sDB2Manager.GetMount(pair.first))
                continue;
            (IsFlyingMount(pair.first) ? fliers : ground).push_back(pair.first);
        }
        if (canFly && !fliers.empty())
            return fliers[urand(0, fliers.size() - 1)];
        if (!ground.empty())
            return ground[urand(0, ground.size() - 1)];
        // a flier also runs on the ground (Legion mounts follow the riding skill)
        return fliers.empty() ? 0 : fliers[urand(0, fliers.size() - 1)];
    }

    void ApplyEnchantsAndGems(Player* bot, bool replaceEnchants, Result& result)
    {
        std::call_once(candidatesOnce, BuildCandidates);
        uint8 const level = bot->getLevel();
        uint32 const gemLevelCap = BotGear::ItemLevelCap(level, sPlayerbotMgr->GetGearOptions());

        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (!item || item->GetTemplate()->GetArtifactID())
                continue;
            float const budget = Budget(bot, item) * BudgetShare;

            // enchant: the best weighted one that fits the item, within the budget and the bot's level
            uint32 const current = item->GetEnchantmentId(PERM_ENCHANTMENT_SLOT);
            if (!current || replaceEnchants)
            {
                float currentScore = 0.0f, currentPower = 0.0f;
                if (current)
                    ValueEnchantment(bot, current, currentScore, currentPower);
                uint32 best = 0;
                float bestScore = currentScore;
                for (EnchantCandidate const& candidate : enchantCandidates)
                {
                    if (candidate.requiredLevel > level || candidate.enchantId == current)
                        continue;
                    SpellItemEnchantmentEntry const* enchant = sSpellItemEnchantmentStore.LookupEntry(candidate.enchantId);
                    if (!enchant || enchant->MinLevel > level)
                        continue;
                    SpellInfo const* spell = sSpellMgr->GetSpellInfo(candidate.spellId);
                    if (!spell || !item->IsFitToSpellRequirements(spell))
                        continue;
                    float score = 0.0f, power = 0.0f;
                    ValueEnchantment(bot, candidate.enchantId, score, power);
                    if (power > budget || score <= bestScore)
                        continue;
                    best = candidate.enchantId;
                    bestScore = score;
                }
                if (best)
                {
                    Enchant(bot, item, best);
                    ++result.enchants;
                }
            }

            // gems: the best weighted gem for each empty socket (as WorldSession::HandleSocketGems, without gem items)
            ItemTemplate const* proto = item->GetTemplate();
            uint32 const itemLevel = item->GetItemLevel(level);
            std::vector<std::pair<uint8, GemCandidate const*>> picks;
            for (uint8 socket = 0; socket < MAX_GEM_SOCKETS; ++socket)
            {
                uint8 const color = item->GetSocketColor(socket);
                if (!color || item->GetGem(socket))
                    continue;
                GemCandidate const* best = nullptr;
                float bestScore = 0.0f;
                for (GemCandidate const& gem : gemCandidates)
                {
                    if (gem.requiredLevel > level || (gemLevelCap && gem.itemLevel > gemLevelCap) || gem.minItemLevel > itemLevel)
                        continue;
                    if (!(SocketColorToGemTypeMask[color] & gem.type))
                        continue;
                    float score = 0.0f, power = 0.0f;
                    ValueEnchantment(bot, gem.enchantId, score, power);
                    if (power > budget || score <= bestScore)
                        continue;
                    best = &gem;
                    bestScore = score;
                }
                if (best)
                    picks.emplace_back(socket, best);
            }
            if (picks.empty())
                continue;
            bool const bonusBefore = item->GemsFitSockets();
            bot->_ApplyItemMods(item, slot, false);
            for (auto const& pick : picks)
            {
                ItemDynamicFieldGems gem;
                memset(&gem, 0, sizeof(gem));
                gem.ItemId = pick.second->itemId;
                item->SetGem(pick.first, &gem, level);
                item->SetEnchantment(EnchantmentSlot(SOCK_ENCHANTMENT_SLOT + pick.first), pick.second->enchantId, 0, 0, bot->GetGUID());
                ++result.gems;
            }
            bot->_ApplyItemMods(item, slot, true);
            bool const bonusAfter = item->GemsFitSockets();
            if (bonusBefore != bonusAfter)
            {
                bot->ApplyEnchantment(item, BONUS_ENCHANTMENT_SLOT, false);
                item->SetEnchantment(BONUS_ENCHANTMENT_SLOT, bonusAfter ? proto->GetSocketBonus() : 0, 0, 0, bot->GetGUID());
                bot->ApplyEnchantment(item, BONUS_ENCHANTMENT_SLOT, true);
            }
            item->SetState(ITEM_CHANGED, bot);
        }
    }

    std::string Maintain(Player* bot, bool randomBot, uint32 bagEntry)
    {
        Result result;
        result.talents = BotFactory::FillTalents(bot);
        LearnRiding(bot, result);
        ApplyEnchantsAndGems(bot, randomBot, result);
        result.bags = BotGear::AddBags(bot, bagEntry);
        bot->DurabilityRepairAll(false, 1.0f, false);
        bot->SaveToDB();

        std::string summary = "Maintenance done: " + std::to_string(result.talents) + " talent(s), " +
            std::to_string(result.ridingSpells) + " riding skill(s), " + std::to_string(result.mounts) + " mount(s), " +
            std::to_string(result.enchants) + " enchant(s), " + std::to_string(result.gems) + " gem(s), " +
            std::to_string(result.bags) + " bag(s), repaired.";
        TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' (level %u): %s", bot->GetName(), uint32(bot->getLevel()), summary.c_str());
        return summary;
    }
}

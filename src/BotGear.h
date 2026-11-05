/*
 * mod-playerbots: gear by stat weights (".playerbots create" initialization and the "autogear" order).
 *
 * Weights: Ask Mr. Robot's scales from the Pawn addon (the owner's file, 2026-11-05), per spec: value of
 * an item = sum of stat x weight, like Pawn (weapon min/max damage and armor included). Candidates come
 * from the item template store (as SkyFire-playerbots does, no SQL): armor and weapons the bot can use
 * (CanUseItem), meant for its spec (the item's ItemSpec mask), of its class's armor type, within the
 * level the bot has and the quality / item level limits of playerbots.conf.
 */

#ifndef MOD_PLAYERBOTS_BOTGEAR_H
#define MOD_PLAYERBOTS_BOTGEAR_H

#include "Define.h"
#include <string>
#include <vector>

class Player;
struct ItemTemplate;

namespace BotGear
{
    struct Options
    {
        uint32 minQuality = 0;
        uint32 maxQuality = 4;          // epic
        uint32 maxItemLevel = 0;        // 0 = no limit (the bot's level still limits by required level)
        bool destroyReplaced = false;   // a new bot's starting gear is destroyed, otherwise it goes to the bags
        // owner's rule (2026-11-05): each slot takes a random one of its `choices` best upgrades (fewer when
        // fewer exist), so bots of a spec do not all look alike; Playerbots.AutoGear.Choices
        uint32 choices = 5;
        // put in the empty bag slots before gearing (owner's rule); 0 = the first of BagFallbacks that exists
        uint32 bagEntry = 0;

        // Item level cap by bot level (Playerbots.AutoGear.IlvlCap.*): below 60 level + belowSixtyAdd; from 60
        // brackets of 5 levels with a fixed item level each (owner, build #26: the level x factor formula gave
        // odd caps, e.g. 158 at 79 but 224 at 80), indexed (level - 60) / 5: 60-64 ... 105-109; 0 = no cap;
        // 110 has only maxItemLevel
        float belowSixtyAdd = 6.0f;
        uint32 brackets[10] = { 100, 115, 165, 200, 285, 400, 560, 630, 735, 820 };
    };

    struct Result
    {
        uint32 equipped = 0;
        uint32 noBagRoom = 0;           // upgrades skipped because the old piece did not fit in the bags
        uint32 bagsAdded = 0;
        uint32 itemLevelCap = 0;        // the cap used (0 = none)
        uint32 candidates = 0;          // items allowed for the bot (before comparing with what it wears)
        std::vector<std::string> pieces;// names of the items put on
    };

    // the highest item level the bot may wear by its level and Options (0 = none)
    uint32 ItemLevelCap(uint8 level, Options const& options);

    // value of the item for the bot's current spec at itemLevel (0 for an unknown spec)
    float ItemScore(Player const* bot, ItemTemplate const* proto, uint32 itemLevel);

    // item level of a template with Legion bonus lists (a loot roll's item)
    uint32 ItemLevelWithBonuses(ItemTemplate const* proto, std::vector<uint32> const& bonusListIds);

    // Loot rolls and equipping from the bags: is the item for this bot (armor / weapon it can use, meant for
    // its spec, its class's armor type, a hand layout its spec uses), into which slot it would go and by how
    // much it beats what is worn there (the weaker ring / trinket / dual-wield hand; a two-hander against
    // both hands). gain = (new - old) / old, 1 when the slot is empty.
    struct Comparison
    {
        bool usable = false;
        uint8 slot = 0;
        float newScore = 0.0f;
        float oldScore = 0.0f;
        float gain = 0.0f;
        bool twoHanded = false;         // takes both hands: the off hand must be emptied first
    };
    Comparison CompareToWorn(Player* bot, ItemTemplate const* proto, uint32 itemLevel);
    // equips, slot by slot, a random one of the best candidates that beat what the bot wears; first fills
    // the empty bag slots with large bags
    Result AutoGear(Player* bot, Options const& options);
    // large bags into the empty bag slots; returns how many were added
    uint32 AddBags(Player* bot, uint32 bagEntry);
    // Owner's rule for a dismissed random bot: every item it carries or wears is destroyed except the bags in
    // its bag slots (and their slots stay). Returns the number of items destroyed.
    uint32 ClearInventory(Player* bot);
}

#endif

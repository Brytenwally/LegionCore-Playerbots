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

        // item level cap by bot level (owner's values, 2026-11-05; Playerbots.AutoGear.IlvlCap.*): below 60
        // level + belowSixtyAdd, then level x the bracket's factor; 110 has only maxItemLevel
        float belowSixtyAdd = 6.0f;
        float factor60 = 1.3f;          // 60-69
        float factor70 = 2.0f;          // 70-79
        float factor80 = 2.8f;          // 80-84
        float factor85 = 4.6f;          // 85-89
        float factor90 = 5.8f;          // 90-99
        float factor100 = 7.0f;         // 100-109
    };

    struct Result
    {
        uint32 equipped = 0;
        uint32 noBagRoom = 0;           // upgrades skipped because the old piece did not fit in the bags
        uint32 bagsAdded = 0;
        std::vector<std::string> pieces;// names of the items put on
    };

    // value of the item for the bot's current spec at itemLevel (0 for an unknown spec)
    float ItemScore(Player const* bot, ItemTemplate const* proto, uint32 itemLevel);
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

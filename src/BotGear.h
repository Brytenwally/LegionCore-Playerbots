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
    };

    struct Result
    {
        uint32 equipped = 0;
        uint32 noBagRoom = 0;           // upgrades skipped because the old piece did not fit in the bags
        std::vector<std::string> pieces;// names of the items put on
    };

    // value of the item for the bot's current spec at itemLevel (0 for an unknown spec)
    float ItemScore(Player const* bot, ItemTemplate const* proto, uint32 itemLevel);
    // equips, slot by slot, the best candidate that beats what the bot wears
    Result AutoGear(Player* bot, Options const& options);
}

#endif

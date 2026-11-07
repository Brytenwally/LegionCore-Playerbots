/*
 * mod-playerbots: the "maintenance" order and riding (owner's request, 2026-11-07).
 *
 * As SkyFire-playerbots' MaintainBot (AzerothCore's maintenance): talents by the spec's blueprint, riding skills and a
 * mount per riding tier, enchants and gems by the spec's stat weights, bags, repair. Rewritten for Legion: mounts go
 * through the account's mount collection (CollectionMgr::AddMount), mount speed follows the riding skill, enchants come
 * from Enchanting recipes and item enhancements (SPELL_EFFECT_ENCHANT_ITEM), gems from the gem items' GemProperties.
 * Everything runs on the bot's own thread.
 */

#ifndef MOD_PLAYERBOTS_BOTMAINTENANCE_H
#define MOD_PLAYERBOTS_BOTMAINTENANCE_H

#include "Define.h"
#include <string>

class Player;

namespace BotMaintenance
{
    struct Result
    {
        uint32 talents = 0;             // rows filled
        uint32 ridingSpells = 0;        // riding skills / licences learned
        uint32 mounts = 0;              // mounts added
        uint32 enchants = 0;
        uint32 gems = 0;
        uint32 bags = 0;
    };

    // Riding skills up to the bot's level (Apprentice 20, Journeyman 40, Expert 60, Artisan 70, Master 80, and the
    // old flying licences that exist on this server), and for each tier reached without a mount of it, a random mount
    // of the bot's race (ground) or faction (flying). Random bots get this when they are set up.
    void LearnRiding(Player* bot, Result& result);
    // a dismissed random bot back at its starting level: its riding skills go (its mounts stay in the collection)
    void ForgetRiding(Player* bot);

    // the mount the bot rides: a flying one when wanted and allowed by its riding, else a ground one; 0 if none
    uint32 MountSpellFor(Player* bot, bool flying);

    // Enchants and gems by the spec's stat weights (BotGear::StatWeight), level adequate: enchanting recipes whose skill
    // rank's content is not above the bot's level, item enhancements of a required level it has, gems of a required
    // level it has and an item level within its gear cap. Profession-only enchants and gems, unique gems and artifact
    // relics are left out. replaceEnchants: a better enchant replaces the current one (random bots); otherwise only
    // bare items get one (alts: their player's choice stays). Gems only go into empty sockets.
    void ApplyEnchantsAndGems(Player* bot, bool replaceEnchants, Result& result);

    // The "maintenance" order: talents (empty rows, by the spec's blueprint), riding and mounts, enchants and gems,
    // large bags in empty bag slots, repair, save. Returns a summary for the owner.
    std::string Maintain(Player* bot, bool randomBot, uint32 bagEntry);
}

#endif

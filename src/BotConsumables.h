/*
 * mod-playerbots: potions from the bags and food/drink out of combat (plan step 4.5).
 *
 * Potions: only what the bot carries (LegionBotAI's BotUsePotion idea, any potion rather than two ids):
 * an item whose on-use spell heals (SPELL_EFFECT_HEAL: potions, healthstones) or restores mana
 * (SPELL_EFFECT_ENERGIZE, POWER_MANA), the highest item level the bot can use first, cast with the item
 * so the core takes the charge and starts the shared potion cooldown.
 *
 * Food and drink: as SkyFire-playerbots (CastRefreshmentSpell): the bot sits and gets the food / drink aura
 * of an item of its level (no item needed or used); it stands up when topped up, when a fight starts, or
 * when its leader walks off.
 */

#ifndef MOD_PLAYERBOTS_BOTCONSUMABLES_H
#define MOD_PLAYERBOTS_BOTCONSUMABLES_H

#include "Define.h"

class Player;

namespace BotConsumables
{
    // uses the best healing (mana = false) or mana potion in the bags; false if none or not ready
    bool UsePotion(Player* bot, bool mana);

    // sits and applies the food (needHealth) and/or drink (needMana) aura for the bot's level
    void StartRest(Player* bot, bool needHealth, bool needMana);
    // removes the food/drink auras and stands up
    void StopRest(Player* bot);
    bool HasRestAura(Player* bot);
}

#endif

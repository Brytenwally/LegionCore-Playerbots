/*
 * mod-playerbots: talent allocation, copied from LegionBotAI's LearnBotTalents (LegionCore-7.3.5)
 * and made generic. Not used yet.
 *
 * Fills only empty, unlocked talent rows and never replaces a talent already chosen, so a
 * player's own choices on an alt stay. Owner's decision (2026-11-03): talents are learned
 * automatically on every bot, alts included, into empty rows only.
 */

#ifndef MOD_PLAYERBOTS_BOTTALENTS_H
#define MOD_PLAYERBOTS_BOTTALENTS_H

#include "Define.h"
#include "DBCEnums.h"
#include <array>

class Player;

namespace BotTalents
{
    // One choice per row, 0 = leave the row alone.
    using TalentPicks = std::array<uint32, MAX_TALENT_TIERS>;   // talent ids (Talent.db2)
    using ColumnPicks = std::array<int8, MAX_TALENT_TIERS>;     // column 0-2, -1 = leave the row

    // number of rows the bot may fill at its level (PLAYER_FIELD_MAX_TALENT_TIERS)
    uint32 UnlockedRows(Player const* bot);
    // the talent the bot has in row tier of its active spec, 0 if the row is empty
    uint32 TalentInRow(Player const* bot, uint8 tier);
    // the talent at (tier, column) for the bot's class and current spec (spec-specific before
    // class-wide, as Player::LearnTalent picks it), 0 if none
    uint32 TalentAt(Player const* bot, uint8 tier, uint8 column);

    // Learns each pick whose row is unlocked and empty and that fits the bot's class and spec.
    // Not in combat or dead (Player::LearnTalent refuses). Returns the number learned.
    uint32 LearnTalents(Player* bot, TalentPicks const& picks);
    uint32 LearnTalentColumns(Player* bot, ColumnPicks const& columns);
}

#endif

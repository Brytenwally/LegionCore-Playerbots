/*
 * mod-playerbots: talent allocation. See BotTalents.h.
 */

#include "BotTalents.h"
#include "DB2Stores.h"
#include "Player.h"
#include <algorithm>

namespace BotTalents
{
    uint32 UnlockedRows(Player const* bot)
    {
        return std::min<uint32>(bot->GetUInt32Value(PLAYER_FIELD_MAX_TALENT_TIERS), MAX_TALENT_TIERS);
    }

    uint32 TalentInRow(Player const* bot, uint8 tier)
    {
        uint8 const group = bot->GetActiveTalentGroup();
        for (auto const& known : *bot->GetTalentMap(group))
            if (TalentEntry const* talent = sTalentStore.LookupEntry(known.first))
                if (talent->TierID == tier && bot->HasTalent(known.first, group))
                    return known.first;
        return 0;
    }

    uint32 TalentAt(Player const* bot, uint8 tier, uint8 column)
    {
        uint32 const spec = bot->GetSpecializationId();
        uint32 found = 0;
        for (TalentEntry const* talent : sTalentStore)
        {
            if (talent->ClassID != bot->getClass() || talent->TierID != tier || talent->ColumnIndex != column)
                continue;
            if (talent->SpecID == spec)
                return talent->ID;
            if (!talent->SpecID)
                found = talent->ID;
        }
        return found;
    }

    uint32 LearnTalents(Player* bot, TalentPicks const& picks)
    {
        if (bot->isInCombat() || bot->isDead())
            return 0;

        uint32 const rows = UnlockedRows(bot);
        uint32 learned = 0;
        for (uint32 talentId : picks)
        {
            if (!talentId)
                continue;

            TalentEntry const* talent = sTalentStore.LookupEntry(talentId);
            if (!talent || talent->ClassID != bot->getClass() ||
                (talent->SpecID && talent->SpecID != bot->GetSpecializationId()) || talent->TierID >= rows)
                continue;

            // the row's talent stays, whoever chose it
            if (TalentInRow(bot, talent->TierID))
                continue;

            if (bot->LearnTalent(talentId))
                ++learned;
        }
        return learned;
    }

    uint32 LearnTalentColumns(Player* bot, ColumnPicks const& columns)
    {
        TalentPicks picks{};
        for (uint8 tier = 0; tier < MAX_TALENT_TIERS; ++tier)
            if (columns[tier] >= 0 && columns[tier] < MAX_TALENT_COLUMNS)
                picks[tier] = TalentAt(bot, tier, uint8(columns[tier]));
        return LearnTalents(bot, picks);
    }
}

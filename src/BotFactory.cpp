/*
 * mod-playerbots: creating bots. See BotFactory.h.
 */

#include "BotFactory.h"
#include "AccountMgr.h"
#include "BotGear.h"
#include "BotTalents.h"
#include "CharacterData.h"
#include "CharacterPackets.h"
#include "DB2Stores.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotMgr.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "World.h"
#include "WorldSession.h"
#include <algorithm>
#include <cctype>

namespace BotFactory
{
    namespace
    {
        std::string Lower(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            return text;
        }

        std::string Squash(std::string text)            // "Demon Hunter" -> "demonhunter"
        {
            text = Lower(text);
            text.erase(std::remove_if(text.begin(), text.end(), [](unsigned char c) { return !std::isalnum(c); }), text.end());
            return text;
        }

        // playable races of a faction (allied races excluded: they need unlocking); pandaren by faction
        std::vector<uint8> RacesOf(uint32 team)
        {
            if (team == ALLIANCE)
                return { RACE_HUMAN, RACE_DWARF, RACE_NIGHTELF, RACE_GNOME, RACE_DRAENEI, RACE_WORGEN, RACE_PANDAREN_ALLIANCE };
            return { RACE_ORC, RACE_UNDEAD_PLAYER, RACE_TAUREN, RACE_TROLL, RACE_GOBLIN, RACE_BLOODELF, RACE_PANDAREN_HORDE };
        }

        // a pronounceable name the realm does not have yet
        std::string GenerateName()
        {
            static char const* const first[] = { "Ar", "Bel", "Cor", "Dar", "El", "Fen", "Gal", "Har", "Is", "Jor", "Kal", "Lor",
                "Mor", "Nor", "Or", "Per", "Quel", "Ral", "Sar", "Tal", "Ul", "Val", "Wen", "Xan", "Yor", "Zan", "Bri", "Dra", "Gre", "Thal" };
            static char const* const middle[] = { "a", "e", "i", "o", "u", "ae", "ia", "or", "an", "el", "in", "ar" };
            static char const* const last[] = { "dor", "wyn", "mir", "las", "thas", "ric", "vin", "dan", "gar", "nor", "ros", "tan",
                "wen", "ra", "lia", "na", "ius", "eth", "ion", "ok", "ash", "us", "en", "ys" };

            for (int attempt = 0; attempt < 100; ++attempt)
            {
                std::string name = std::string(first[urand(0, std::size(first) - 1)]) + middle[urand(0, std::size(middle) - 1)] +
                    last[urand(0, std::size(last) - 1)];
                if (name.size() < 4 || name.size() > 12)
                    continue;
                if (ObjectMgr::GetPlayerGUIDByName(name).IsEmpty() && sWorld->CheckCharacterName(name) &&
                    CharacterDataStoreMgr::CheckPlayerName(name, LOCALE_enUS, true) == CHAR_NAME_SUCCESS)
                    return name;
            }
            return std::string();
        }

        // an account <prefix><n> with room for one more character, created when needed
        uint32 FindBotAccount(CreateConfig const& config, std::string& accountName, std::string& error)
        {
            for (uint32 n = 1; n < 10000; ++n)
            {
                std::string name = config.accountPrefix + std::to_string(n);
                uint32 id = AccountMgr::GetId(name);
                if (!id)
                {
                    if (AccountMgr::CreateAccount(name, config.accountPassword, false) != AccountOpResult::AOR_OK)
                    {
                        error = "Could not create bot account " + name + ".";
                        return 0;
                    }
                    id = AccountMgr::GetId(name);
                    if (!id)
                    {
                        error = "Bot account " + name + " was not found after creating it.";
                        return 0;
                    }
                    TC_LOG_INFO("server.loading", "[mod-playerbots] Created bot account %s (%u).", name.c_str(), id);
                }

                if (AccountMgr::GetCharactersCount(id) < config.charactersPerAccount)
                {
                    accountName = name;
                    return id;
                }
            }
            error = "No bot account with room left.";
            return 0;
        }
    }

    uint8 ClassFromName(std::string name)
    {
        name = Squash(name);
        static std::pair<char const*, uint8> const names[] = {
            { "warrior", CLASS_WARRIOR }, { "paladin", CLASS_PALADIN }, { "hunter", CLASS_HUNTER }, { "rogue", CLASS_ROGUE },
            { "priest", CLASS_PRIEST }, { "deathknight", CLASS_DEATH_KNIGHT }, { "dk", CLASS_DEATH_KNIGHT },
            { "shaman", CLASS_SHAMAN }, { "mage", CLASS_MAGE }, { "warlock", CLASS_WARLOCK }, { "lock", CLASS_WARLOCK },
            { "monk", CLASS_MONK }, { "druid", CLASS_DRUID }, { "demonhunter", CLASS_DEMON_HUNTER }, { "dh", CLASS_DEMON_HUNTER } };
        for (auto const& pair : names)
            if (name == pair.first)
                return pair.second;
        return 0;
    }

    char const* ClassName(uint8 cls)
    {
        if (ChrClassesEntry const* entry = sChrClassesStore.LookupEntry(cls))
            if (entry->Name && entry->Name->Str[LOCALE_enUS])
                return entry->Name->Str[LOCALE_enUS];
        return "?";
    }

    std::vector<uint32> SpecsOfClass(uint8 cls)
    {
        std::vector<std::pair<uint8, uint32>> ordered;
        for (ChrSpecializationEntry const* spec : sChrSpecializationStore)
            if (spec->ClassID == cls && !(spec->Flags & CHR_SPECIALIZATION_FLAG_PET_OVERRIDE_SPEC) && spec->OrderIndex < MAX_SPECIALIZATIONS)
                ordered.emplace_back(spec->OrderIndex, uint32(spec->ID));
        std::sort(ordered.begin(), ordered.end());
        std::vector<uint32> specs;
        for (auto const& pair : ordered)
            specs.push_back(pair.second);
        return specs;
    }

    std::string SpecName(uint32 specId)
    {
        if (ChrSpecializationEntry const* spec = sChrSpecializationStore.LookupEntry(specId))
            if (spec->Name && spec->Name->Str[LOCALE_enUS])
                return spec->Name->Str[LOCALE_enUS];
        return "none";
    }

    uint32 SpecFromName(uint8 cls, std::string const& name)
    {
        std::vector<uint32> const specs = SpecsOfClass(cls);
        std::string const wanted = Squash(name);
        if (wanted.size() == 1 && wanted[0] >= '1' && wanted[0] <= '4')
        {
            size_t index = size_t(wanted[0] - '1');
            return index < specs.size() ? specs[index] : 0;
        }
        for (uint32 spec : specs)
            if (Squash(SpecName(spec)) == wanted)
                return spec;
        for (uint32 spec : specs)                           // "beast" for Beast Mastery
            if (!wanted.empty() && Squash(SpecName(spec)).rfind(wanted, 0) == 0)
                return spec;
        return 0;
    }

    std::string CreateCharacter(CreateRequest const& request, CreateConfig const& config, std::string& error)
    {
        std::vector<uint8> races;
        for (uint8 race : RacesOf(request.team))
            if (sObjectMgr->GetPlayerInfo(race, request.cls))
                races.push_back(race);
        if (races.empty())
        {
            error = std::string("No race of your faction can be a ") + ClassName(request.cls) + ".";
            return std::string();
        }

        std::string accountName;
        uint32 const accountId = FindBotAccount(config, accountName, error);
        if (!accountId)
            return std::string();

        std::string const name = GenerateName();
        if (name.empty())
        {
            error = "Could not find a free name.";
            return std::string();
        }

        WorldPackets::Character::CharacterCreateInfo info;
        info.Race = races[urand(0, races.size() - 1)];
        info.Class = request.cls;
        info.Sex = uint8(urand(GENDER_MALE, GENDER_FEMALE));
        info.CustomDisplay.fill(0);
        info.Name = name;
        info.CharCount = uint8(AccountMgr::GetCharactersCount(accountId));

        // as WorldSession::HandleCharCreateOpcode, on a bot session of the bot account
        WorldSessionPtr session = WorldSession::CreateBotSession(accountId, std::string(accountName), SEC_PLAYER,
            uint8(sWorld->getIntConfig(CONFIG_EXPANSION)), LOCALE_enUS);
        {
            Player newChar(session.get());
            newChar.GetMotionMaster()->Initialize();
            if (!newChar.Create(sObjectMgr->GetGenerator<HighGuid::Player>()->Generate(), &info))
            {
                newChar.CleanupsBeforeDelete();
                error = "The core refused to create the character.";
                return std::string();
            }

            newChar.setCinematic(1);                        // no intro
            newChar.SetAtLoginFlag(AT_LOGIN_FIRST);
            newChar.SaveToDB(true);
            info.CharCount += 1;

            LoginDatabaseTransaction trans = LoginDatabase.BeginTransaction();
            LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_DEL_REALM_CHARACTERS_BY_REALM);
            stmt->setUInt32(0, accountId);
            stmt->setUInt32(1, realm.Id.Realm);
            trans->Append(stmt);
            stmt = LoginDatabase.GetPreparedStatement(LOGIN_INS_REALM_CHARACTERS);
            stmt->setUInt32(0, info.CharCount);
            stmt->setUInt32(1, accountId);
            stmt->setUInt32(2, realm.Id.Realm);
            trans->Append(stmt);
            LoginDatabase.CommitTransaction(trans);

            sScriptMgr->OnPlayerCreate(&newChar);
            sWorld->AddCharacterInfo(newChar.GetGUID(), accountId, std::string(newChar.GetName()), newChar.getGender(),
                newChar.getRace(), newChar.getClass(), newChar.getLevel());
            sWorld->UpdateCharacterAccount(newChar.GetGUID(), accountId);

            TC_LOG_INFO("server.loading", "[mod-playerbots] Created bot character '%s' (%s, race %u, account %s).",
                name.c_str(), ClassName(request.cls), uint32(info.Race), accountName.c_str());

            newChar.GetAchievementMgr()->ClearMap();
            newChar.CleanupsBeforeDelete();
        }
        return name;
    }

    std::string InitializeNewBot(Player* bot, CreateRequest const& request)
    {
        if (request.level > bot->getLevel())
        {
            bot->GiveLevel(request.level);
            bot->SetUInt32Value(PLAYER_FIELD_XP, 0);
        }

        std::vector<uint32> const specs = SpecsOfClass(bot->getClass());
        uint32 spec = request.specId;
        if (!spec && !specs.empty())
            spec = specs[urand(0, specs.size() - 1)];
        if (spec)
            bot->ForceChangeTalentGroup(spec);

        uint32 const talents = FillTalents(bot);

        BotGear::Options options = sPlayerbotMgr->GetGearOptions();
        options.destroyReplaced = true;                     // a new character's starting gear
        BotGear::Result const gear = BotGear::AutoGear(bot, options);

        bot->SetFullHealth();
        for (uint8 i = 0; i < MAX_POWERS; ++i)
            if (bot->GetMaxPower(Powers(i)) > 0 && (i == POWER_MANA || i == POWER_ENERGY || i == POWER_FOCUS))
                bot->SetPower(Powers(i), bot->GetMaxPower(Powers(i)));

        bot->SaveToDB();
        bot->TeleportTo(request.location);

        char buffer[256];
        snprintf(buffer, sizeof(buffer), "Level %u %s %s, %u talent(s), %u item(s) equipped.", uint32(bot->getLevel()),
            SpecName(bot->GetSpecializationId()).c_str(), ClassName(bot->getClass()), talents, gear.equipped);
        TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' initialized: %s", bot->GetName(), buffer);
        return buffer;
    }

    uint32 FillTalents(Player* bot)
    {
        if (bot->isInCombat() || bot->isDead())
            return 0;

        uint32 const rows = BotTalents::UnlockedRows(bot);
        BotTalents::TalentPicks picks{};
        for (uint8 tier = 0; tier < rows; ++tier)
        {
            if (BotTalents::TalentInRow(bot, tier))
                continue;

            // no talent builds yet (to come from the owner): a random column of the row
            std::vector<uint32> choices;
            for (uint8 column = 0; column < MAX_TALENT_COLUMNS; ++column)
                if (uint32 talent = BotTalents::TalentAt(bot, tier, column))
                    choices.push_back(talent);
            if (!choices.empty())
                picks[tier] = choices[urand(0, choices.size() - 1)];
        }
        return BotTalents::LearnTalents(bot, picks);
    }

    std::string DescribeTalents(Player* bot)
    {
        std::string text = "Spec: " + SpecName(bot->GetSpecializationId()) + ". Talents:";
        uint32 const rows = BotTalents::UnlockedRows(bot);
        if (!rows)
            text += " none yet";
        for (uint8 tier = 0; tier < rows; ++tier)
        {
            text += " " + std::to_string(tier + 1) + ": ";
            uint32 talentId = BotTalents::TalentInRow(bot, tier);
            TalentEntry const* talent = talentId ? sTalentStore.LookupEntry(talentId) : nullptr;
            SpellInfo const* spell = talent ? sSpellMgr->GetSpellInfo(talent->SpellID) : nullptr;
            text += spell && spell->SpellName ? spell->SpellName : "-";
            if (tier + 1 < rows)
                text += ",";
        }
        text += ". Specs:";
        for (uint32 spec : SpecsOfClass(bot->getClass()))
            text += " " + SpecName(spec);
        return text + ".";
    }
}

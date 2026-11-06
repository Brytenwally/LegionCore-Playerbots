/*
 * mod-playerbots: creating bots. See BotFactory.h.
 */

#include "BotFactory.h"
#include "AccountMgr.h"
#include "BotApl.h"
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
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"
#include "QuestData.h"
#include "QuestDef.h"
#include "World.h"
#include "WorldSession.h"
#include <algorithm>
#include <cstring>
#include <cctype>
#include <mutex>
#include <map>
#include <set>

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

        uint8 alliedRaceLevel = 10;

        // playable races of a faction, allied races included (owner, 2026-11-06); pandaren by faction
        std::vector<uint8> RacesOf(uint32 team)
        {
            if (team == ALLIANCE)
                return { RACE_HUMAN, RACE_DWARF, RACE_NIGHTELF, RACE_GNOME, RACE_DRAENEI, RACE_WORGEN, RACE_PANDAREN_ALLIANCE,
                    RACE_VOID_ELF, RACE_LIGHTFORGED_DRAENEI };
            return { RACE_ORC, RACE_UNDEAD_PLAYER, RACE_TAUREN, RACE_TROLL, RACE_GOBLIN, RACE_BLOODELF, RACE_PANDAREN_HORDE,
                RACE_NIGHTBORNE, RACE_HIGHMOUNTAIN_TAUREN };
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

        // Talent builds (owner's file Talents.txt, 2026-11-05: leveling guides, one talent per row from
        // the 15/30/45/60/75/90/100 rows; DK rows 56/57/58/60/75/90/100). Names are matched against the
        // spell of each talent of the row, so the column does not matter. Monk rows in the file only
        // describe changes: the picks there are taken from that text (marked). Warrior and Demon Hunter
        // builds were not in the file: random until they come.
        struct TalentBuild
        {
            uint32 specId;
            char const* names[MAX_TALENT_TIERS];
        };
        // The owner's builds come from 7.0 guides; 7.3.5 moved or replaced some talents. Corrected from the startup
        // check's log (owner's server, build #26): a talent that moved to another row is kept there; otherwise the
        // row's passive talent (the rotations do not cast talent abilities yet), never one that replaces a spell
        // the rotation casts (Blessed Hammer replaces Prot's Hammer of the Righteous).
        TalentBuild const TalentBuilds[] =
        {
            { 250, { "Bloodworms", "Rapid Decomposition", "Ossuary", "Red Thirst", "Tremble Before Me", "Foul Bulwark", "Bonestorm" } },
            { 251, { "Shattering Strikes", "Freezing Fog", "Icecap", "Abomination's Might", "Permafrost", "Frostscythe", "Obliteration" } },
            { 252, { "All Will Serve", "Pestilent Pustules", "Unholy Frenzy", "Sludge Belcher", "Corpse Shield", "Shadow Infusion", "Defile" } },
            { 102, { "Force of Nature", "Renewal", "Restoration Affinity", "Typhoon", "Soul of the Forest", "Astral Communion", "Fury of Elune" } },
            { 103, { "Predator", "Wild Charge", "Restoration Affinity", "Mighty Bash", "Jagged Wounds", "Sabertooth", "Moment of Clarity" } },
            { 104, { "Brambles", "Wild Charge", "Restoration Affinity", "Mighty Bash", "Incarnation: Guardian of Ursoc", "Guardian of Elune", "Rend and Tear" } },
            { 105, { "Cenarion Ward", "Renewal", "Guardian Affinity", "Typhoon", "Cultivation", "Germination", "Flourish" } },
            { 253, { "Way of the Cobra", "Stomp", "Posthaste", "Bestial Fury", "Binding Shot", "A Murder of Crows", "Aspect of the Beast" } },
            { 254, { "Lone Wolf", "Black Arrow", "Posthaste", "Patient Sniper", "Camouflage", "Barrage", "Sidewinders" } },
            { 255, { "Throwing Axes", "Snake Hunter", "Posthaste", "Guerrilla Tactics", "Camouflage", "Dragonsfire Grenade", "Expert Trapper" } },
            { 62,  { "Amplification", "Shimmer", "Rune of Power", "Supernova", "Chrono Shift", "Nether Tempest", "Arcane Orb" } },
            { 63,  { "Conflagration", "Shimmer", "Incanter's Flow", "Controlled Burn", "Frenetic Speed", "Unstable Magic", "Meteor" } },
            { 64,  { "Ray of Frost", "Glacial Insulation", "Incanter's Flow", "Ice Nova", "Frigid Winds", "Frost Bomb", "Comet Storm" } },
            // Monk: chosen from the file's descriptions of each row
            { 268, { "Eye of the Tiger", "Chi Torpedo", "Light Brewing", "Leg Sweep", "Healing Elixir", "Invoke Niuzao, the Black Ox", "Elusive Dance" } },
            { 269, { "Eye of the Tiger", "Chi Torpedo", "Ascension", "Leg Sweep", "Healing Elixir", "Hit Combo", "Whirling Dragon Punch" } },
            { 270, { "Chi Burst", "Chi Torpedo", "Mist Wrap", "Leg Sweep", "Healing Elixir", "Refreshing Jade Wind", "Mana Tea" } },
            // Holy Paladin: the file's dungeon picks where it gives them (Bestow Faith, Holy Avenger; bots heal
            // groups), Beacon of the Lightbringer (Beacon of Virtue would replace the rotation's Beacon of Light)
            { 65,  { "Bestow Faith", "Unbreakable Spirit", "Fist of Justice", "Devotion Aura", "Holy Avenger", "Sanctified Wrath", "Beacon of the Lightbringer" } },
            { 66,  { "Holy Shield", "Crusader's Judgment", "Blinding Light", "Blessing of Spellwarding", "Knight Templar", "Aegis of Light", "Seraphim" } },
            { 70,  { "Final Verdict", "The Fires of Justice", "Fist of Justice", "Virtue's Blade", "Justicar's Vengeance", "Divine Intervention", "Divine Purpose" } },
            // Discipline: the file's suggestions (Castigation, Halo, Grace among its options)
            { 256, { "Castigation", "Angelic Feather", "Shining Force", "Mindbender", "Sanctuary", "Halo", "Grace" } },
            // Holy Priest: no row 30 in the file (Angelic Feather); row 100 "pick your favorite" (Circle of Healing, for 5-mans)
            { 257, { "Trail of Light", "Angelic Feather", "Censure", "Light of the Naaru", "Surge of Light", "Divinity", "Circle of Healing" } },
            { 258, { "Shadow Word: Void", "Body and Soul", "Mind Bomb", "Lingering Insanity", "San'layn", "Misery", "Legacy of the Void" } },
            { 262, { "Totem Mastery", "Ancestral Guidance", "Voodoo Totem", "Aftershock", "Elemental Fusion", "Storm Elemental", "Lightning Rod" } },
            { 263, { "Hot Hand", "Rainfall", "Voodoo Totem", "Lightning Shield", "Tempest", "Crashing Storm", "Boulderfist" } },
            { 264, { "Unleash Life", "Graceful Spirit", "Voodoo Totem", "Deluge", "Earthen Shield Totem", "Echo of the Elements", "High Tide" } },
            { 259, { "Master Poisoner", "Nightstalker", "Anticipation", "Leeching Poison", "Prey on the Weak", "Alacrity", "Marked for Death" } },
            { 260, { "Swordmaster", "Grappling Hook", "Vigor", "Iron Stomach", "Parley", "Killing Spree", "Marked for Death" } },
            { 261, { "Gloomblade", "Subterfuge", "Vigor", "Soothing Darkness", "Strike from the Shadows", "Alacrity", "Marked for Death" } },
            // Warlock row 90: "Supremacy" as the file describes it; Demonology's Grimoire of Service
            { 265, { "Haunt", "Absolute Corruption", "Demonic Circle", "Sow the Seeds", "Burning Rush", "Grimoire of Supremacy", "Soul Conduit" } },
            { 266, { "Shadowflame", "Improved Dreadstalkers", "Demonic Circle", "Power Trip", "Burning Rush", "Grimoire of Service", "Summon Darkglare" } },
            { 267, { "Roaring Blaze", "Reverse Entropy", "Demonic Circle", "Fire and Brimstone", "Burning Rush", "Grimoire of Supremacy", "Soul Conduit" } },
        };

        char const* BuildTalentName(uint32 spec, uint8 tier)
        {
            for (TalentBuild const& build : TalentBuilds)
                if (build.specId == spec)
                    return build.names[tier];
            return nullptr;
        }

        // the talent of the row whose spell carries that name, for the bot's class and spec
        uint32 TalentNamed(Player* bot, uint8 tier, char const* name)
        {
            std::string const wanted = Lower(name);
            for (uint8 column = 0; column < MAX_TALENT_COLUMNS; ++column)
                if (uint32 talentId = BotTalents::TalentAt(bot, tier, column))
                    if (TalentEntry const* talent = sTalentStore.LookupEntry(talentId))
                        if (SpellInfo const* spell = sSpellMgr->GetSpellInfo(talent->SpellID))
                            if (spell->SpellName && Lower(spell->SpellName) == wanted)
                                return talentId;
            return 0;
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

    std::vector<uint32> BotAccountIds(CreateConfig const& config)
    {
        std::vector<uint32> ids;
        std::string prefix;
        for (char c : config.accountPrefix)                 // the prefix goes into a LIKE pattern
            if (std::isalnum(static_cast<unsigned char>(c)))
                prefix += char(std::toupper(static_cast<unsigned char>(c)));
        if (prefix.size() < 3)
            return ids;

        if (QueryResult result = LoginDatabase.PQuery("SELECT id FROM account WHERE username LIKE '%s%%'", prefix.c_str()))
            do
                ids.push_back((*result)[0].GetUInt32());
            while (result->NextRow());
        return ids;
    }

    namespace
    {
        std::string IdList(std::vector<uint32> const& ids)
        {
            std::string list;
            for (uint32 id : ids)
                list += (list.empty() ? "" : ",") + std::to_string(id);
            return list;
        }
    }

    std::vector<StoredBot> OfflineBots(std::vector<uint32> const& accounts, uint8 cls)
    {
        std::vector<StoredBot> bots;
        if (accounts.empty())
            return bots;

        std::string query = "SELECT guid, name, race, class, level, account FROM characters WHERE online = 0 AND account IN (" +
            IdList(accounts) + ")";
        if (cls)
            query += " AND class = " + std::to_string(cls);
        if (QueryResult result = CharacterDatabase.Query(query.c_str()))
            do
            {
                Field* fields = result->Fetch();
                StoredBot bot;
                bot.guid = ObjectGuid::Create<HighGuid::Player>(fields[0].GetUInt64());
                bot.name = fields[1].GetString();
                bot.race = fields[2].GetUInt8();
                bot.cls = fields[3].GetUInt8();
                bot.level = fields[4].GetUInt8();
                bot.account = fields[5].GetUInt32();
                bots.push_back(bot);
            }
            while (result->NextRow());
        return bots;
    }

    uint32 CountCharacters(std::vector<uint32> const& accounts)
    {
        if (accounts.empty())
            return 0;
        std::string const query = "SELECT COUNT(*) FROM characters WHERE account IN (" + IdList(accounts) + ")";
        if (QueryResult result = CharacterDatabase.Query(query.c_str()))
            return uint32((*result)[0].GetUInt64());
        return 0;
    }

    bool EnsureAccount(CreateConfig const& config, uint32 n, uint32& accountId, std::string& accountName)
    {
        accountName = config.accountPrefix + std::to_string(n);
        accountId = AccountMgr::GetId(accountName);
        if (accountId)
            return false;
        if (AccountMgr::CreateAccount(accountName, config.accountPassword, false) != AccountOpResult::AOR_OK)
            return false;
        accountId = AccountMgr::GetId(accountName);
        if (accountId)
            TC_LOG_INFO("server.loading", "[mod-playerbots] Created bot account %s.", accountName.c_str());
        return accountId != 0;
    }

    std::map<uint32, uint32> CharacterCounts(std::vector<uint32> const& accounts)
    {
        std::map<uint32, uint32> counts;
        for (uint32 id : accounts)
            counts[id] = 0;
        if (accounts.empty())
            return counts;
        std::string const query = "SELECT account, COUNT(*) FROM characters WHERE account IN (" + IdList(accounts) + ") GROUP BY account";
        if (QueryResult result = CharacterDatabase.Query(query.c_str()))
            do
                counts[(*result)[0].GetUInt32()] = uint32((*result)[1].GetUInt64());
            while (result->NextRow());
        return counts;
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

    bool IsAlliedRace(uint8 race)
    {
        return race == RACE_VOID_ELF || race == RACE_LIGHTFORGED_DRAENEI || race == RACE_NIGHTBORNE || race == RACE_HIGHMOUNTAIN_TAUREN;
    }

    void SetAlliedRaceLevel(uint8 level)
    {
        alliedRaceLevel = std::max<uint8>(1, level);
    }

    uint8 StartLevelFor(uint8 race, uint8 cls)
    {
        if (cls == CLASS_DEATH_KNIGHT)
            return 55;
        if (cls == CLASS_DEMON_HUNTER)
            return 98;
        return IsAlliedRace(race) ? alliedRaceLevel : 1;
    }

    void FlagBotAccounts(CreateConfig const& config)
    {
        std::string prefix;
        for (char c : config.accountPrefix)                 // the prefix goes into a LIKE pattern
            if (std::isalnum(static_cast<unsigned char>(c)))
                prefix += char(std::toupper(static_cast<unsigned char>(c)));
        if (prefix.empty())
            return;
        LoginDatabase.PExecute("UPDATE account SET expansion = %u WHERE username LIKE '%s%%' AND expansion < %u",
            uint32(EXPANSION_LEGION), prefix.c_str(), uint32(EXPANSION_LEGION));
    }

    std::string CreateCharacter(CreateRequest const& request, CreateConfig const& config, std::string& error,
        uint32 onAccountId, std::string const& onAccountName)
    {
        // allied races only from their starting level (pool characters are created at it)
        std::vector<uint8> races;
        for (uint8 race : RacesOf(request.team))
            if (sObjectMgr->GetPlayerInfo(race, request.cls) &&
                (!IsAlliedRace(race) || request.pool || request.level >= alliedRaceLevel))
                races.push_back(race);
        if (races.empty())
        {
            error = std::string("No race of your faction can be a ") + ClassName(request.cls) + ".";
            return std::string();
        }

        std::string accountName = onAccountName;
        uint32 const accountId = onAccountId ? onAccountId : FindBotAccount(config, accountName, error);
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

            // owner's rule: allied races start at Playerbots.Create.AlliedRaceLevel (the stats follow at login)
            if (IsAlliedRace(newChar.getRace()) && newChar.getLevel() != alliedRaceLevel)
                newChar.SetLevel(alliedRaceLevel);

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

    namespace
    {
        // Starting zones a random bot must have done (owner's rule, 2026-11-05): races with their own
        // starting zone (worgen, goblin, pandaren) and the hero classes (death knight, demon hunter).
        // Zones by AreaTable name, resolved once (QuestSortID > 0 is the quest's zone id).
        std::vector<char const*> StartingZoneNames(uint8 race, uint8 cls)
        {
            std::vector<char const*> names;
            switch (race)
            {
                case RACE_WORGEN:            names = { "Gilneas", "Gilneas City" }; break;
                case RACE_GOBLIN:            names = { "Kezan", "The Lost Isles" }; break;
                case RACE_PANDAREN_NEUTRAL:
                case RACE_PANDAREN_ALLIANCE:
                case RACE_PANDAREN_HORDE:    names = { "The Wandering Isle" }; break;
                default: break;
            }
            if (cls == CLASS_DEATH_KNIGHT)
                names.push_back("Plaguelands: The Scarlet Enclave");
            else if (cls == CLASS_DEMON_HUNTER)
            {
                names.push_back("Mardum, the Shattered Abyss");
                names.push_back("Vault of the Wardens");
            }
            return names;
        }

        std::set<uint32> AreaIdsNamed(std::vector<char const*> const& names)
        {
            std::set<uint32> ids;
            for (AreaTableEntry const* area : sAreaTableStore)
            {
                char const* areaName = area->AreaName ? area->AreaName->Str[LOCALE_enUS] : nullptr;
                if (!areaName)
                    continue;
                for (char const* name : names)
                    if (Lower(areaName) == Lower(name))
                        ids.insert(uint32(area->ID));
            }
            return ids;
        }

        // the reward spell's learned spells (worgen forms and the like); other effects are left out
        void LearnRewardSpell(Player* bot, uint32 spellId)
        {
            SpellInfo const* spellInfo = spellId ? sSpellMgr->GetSpellInfo(spellId) : nullptr;
            if (!spellInfo)
                return;
            for (SpellEffectInfo const* effect : spellInfo->Effects)
                if (effect && effect->Effect == SPELL_EFFECT_LEARN_SPELL && effect->TriggerSpell && !bot->HasSpell(effect->TriggerSpell))
                    bot->learnSpell(effect->TriggerSpell, false);
        }
    }

    uint32 CompleteStartingZones(Player* bot)
    {
        std::vector<char const*> const names = StartingZoneNames(bot->getRace(), bot->getClass());
        if (names.empty())
            return 0;

        std::set<uint32> const zones = AreaIdsNamed(names);
        if (zones.empty())
        {
            TC_LOG_ERROR("server.loading", "[mod-playerbots] Bot '%s': no AreaTable entry found for its starting zone.", bot->GetName());
            return 0;
        }

        uint32 done = 0;
        for (auto const& pair : sQuestDataStore->GetQuestTemplates())
        {
            Quest const* quest = pair.second;
            if (!quest || quest->QuestSortID <= 0 || !zones.count(uint32(quest->QuestSortID)))
                continue;
            if (quest->IsRepeatable() || quest->IsDailyOrWeekly() || quest->IsSeasonal())
                continue;
            if (!bot->SatisfyQuestClass(quest, false) || !bot->SatisfyQuestRace(quest, false))
                continue;
            uint32 const questId = quest->GetQuestId();
            if (bot->IsQuestRewarded(questId))
                continue;

            if (bot->FindQuestSlot(questId) < MAX_QUEST_LOG_SIZE)
                bot->RemoveActiveQuest(questId);
            bot->SetRewardedQuest(questId);
            LearnRewardSpell(bot, uint32(std::max(0, quest->RewardSpell)));
            for (uint32 displaySpell : quest->RewardDisplaySpell)
                LearnRewardSpell(bot, displaySpell);
            ++done;
        }

        if (done)
        {
            // phases follow quest state
            bot->UpdateAreaDependentAuras(bot->GetAreaId());
            TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s': starting zone done (%u quests marked complete).", bot->GetName(), done);
        }
        return done;
    }

    uint32 CheckTalentBuilds()
    {
        // the SimC profiles' talents, row by row, as this core's Talent.db2 names them
        for (ChrSpecializationEntry const* spec : sChrSpecializationStore)
        {
            char const* simc = BotApl::TalentString(spec->ID);
            if (!simc)
                continue;
            std::string names;
            for (uint8 tier = 0; tier < MAX_TALENT_TIERS && simc[tier]; ++tier)
            {
                std::string name = "-";
                if (simc[tier] >= '1' && simc[tier] <= '3')
                {
                    name = "?";
                    uint8 const column = uint8(simc[tier] - '1');
                    for (TalentEntry const* talent : sTalentStore)
                        if (talent->ClassID == spec->ClassID && talent->TierID == tier && talent->ColumnIndex == column &&
                            (!talent->SpecID || talent->SpecID == spec->ID))
                            if (SpellInfo const* info = sSpellMgr->GetSpellInfo(talent->SpellID))
                            {
                                name = info->SpellName ? info->SpellName : "?";
                                if (talent->SpecID == spec->ID)
                                    break;
                            }
                }
                names += (tier ? ", " : "") + name;
            }
            TC_LOG_INFO("server.loading", "[mod-playerbots] SimC talents of %s (spec %u): %s: %s.", SpecName(spec->ID).c_str(),
                spec->ID, simc, names.c_str());
        }

        uint32 missing = 0;
        for (TalentBuild const& build : TalentBuilds)
        {
            ChrSpecializationEntry const* spec = sChrSpecializationStore.LookupEntry(build.specId);
            if (!spec)
                continue;
            for (uint8 tier = 0; tier < MAX_TALENT_TIERS; ++tier)
            {
                char const* wanted = build.names[tier];
                if (!wanted)
                    continue;
                std::string present;
                bool found = false;
                for (TalentEntry const* talent : sTalentStore)
                {
                    if (talent->ClassID != spec->ClassID || talent->TierID != tier || (talent->SpecID && talent->SpecID != build.specId))
                        continue;
                    SpellInfo const* spell = sSpellMgr->GetSpellInfo(talent->SpellID);
                    char const* name = spell && spell->SpellName ? spell->SpellName : "?";
                    if (Lower(name) == Lower(wanted))
                        found = true;
                    present += std::string(present.empty() ? "" : ", ") + name;
                }
                if (found)
                    continue;
                ++missing;
                TC_LOG_ERROR("server.loading", "[mod-playerbots] Talent build of %s (spec %u), row %u: no talent '%s' on this core; the row has: %s.",
                    SpecName(build.specId).c_str(), build.specId, uint32(tier + 1), wanted, present.empty() ? "nothing" : present.c_str());
            }
        }
        TC_LOG_INFO("server.loading", "[mod-playerbots] Talent builds: %u specs, %u name(s) not found on this core (those rows get a random talent).",
            uint32(std::size(TalentBuilds)), missing);
        return missing;
    }

    std::string InitializeNewBot(Player* bot, CreateRequest const& request)
    {
        // each step's time is logged: the owner saw 1-2 s freezes when some bots were set up (build #24)
        uint32 const start = getMSTime();
        CompleteStartingZones(bot);
        uint32 const zonesDone = getMSTime();

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

        uint32 const levelDone = getMSTime();
        uint32 const talents = FillTalents(bot);
        uint32 const talentsDone = getMSTime();

        BotGear::Options options = sPlayerbotMgr->GetGearOptions();
        options.destroyReplaced = true;                     // a new character's starting gear
        BotGear::Result const gear = BotGear::AutoGear(bot, options);
        uint32 const gearDone = getMSTime();

        bot->SetFullHealth();
        for (uint8 i = 0; i < MAX_POWERS; ++i)
            if (bot->GetMaxPower(Powers(i)) > 0 && (i == POWER_MANA || i == POWER_ENERGY || i == POWER_FOCUS))
                bot->SetPower(Powers(i), bot->GetMaxPower(Powers(i)));

        bot->SaveToDB();
        if (request.teleport)
            bot->TeleportTo(request.location);
        uint32 const end = getMSTime();
        TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' setup took %u ms: starting zones %u, level and spec %u, talents %u, gear %u, save %u.",
            bot->GetName(), getMSTimeDiff(start, end), getMSTimeDiff(start, zonesDone), getMSTimeDiff(zonesDone, levelDone),
            getMSTimeDiff(levelDone, talentsDone), getMSTimeDiff(talentsDone, gearDone), getMSTimeDiff(gearDone, end));

        char buffer[256];
        snprintf(buffer, sizeof(buffer), "Level %u %s %s, %u talent(s), %u item(s) equipped.", uint32(bot->getLevel()),
            SpecName(bot->GetSpecializationId()).c_str(), ClassName(bot->getClass()), talents, gear.equipped);
        TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' initialized: %s", bot->GetName(), buffer);
        return buffer;
    }

    uint32 ResetBot(Player* bot, bool toGMIsland)
    {
        if (bot->isDead())
        {
            bot->ResurrectPlayer(1.0f);
            bot->SpawnCorpseBones();
        }
        bot->CombatStop();
        bot->GetMotionMaster()->Clear();
        bot->GetMotionMaster()->MoveIdle();

        uint32 const destroyed = BotGear::ClearInventory(bot);

        // hero classes cannot go below their starting level (owner's rule for .playerbots create)
        uint8 const startLevel = StartLevelFor(bot->getRace(), bot->getClass());
        if (bot->getLevel() != startLevel)
            bot->GiveLevel(startLevel);
        bot->SetUInt32Value(PLAYER_FIELD_XP, 0);
        bot->SetFullHealth();

        bot->SaveToDB();
        if (toGMIsland)
            bot->TeleportTo(1, 16222.1f, 16252.1f, 12.5872f, 1.35f);
        TC_LOG_INFO("server.loading", "[mod-playerbots] Bot '%s' reset: level %u, %u item(s) destroyed (bags kept).",
            bot->GetName(), uint32(startLevel), destroyed);
        return destroyed;
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

            // the spec's build (owner's Talents.txt); a name missing from the row on this core is logged once
            // and the row gets a random column, as for specs with no build (warriors, demon hunters)
            uint32 const spec = bot->GetSpecializationId();
            // the spec's SimC profile first (owner, 2026-11-05): its talents= digit is the row's column, 0 = none
            if (char const* simc = BotApl::TalentString(spec))
                if (std::strlen(simc) > tier && simc[tier] >= '1' && simc[tier] <= '3')
                    if (uint32 talent = BotTalents::TalentAt(bot, tier, uint8(simc[tier] - '1')))
                    {
                        picks[tier] = talent;
                        continue;
                    }
            if (char const* name = BuildTalentName(spec, tier))
            {
                if (uint32 talent = TalentNamed(bot, tier, name))
                {
                    picks[tier] = talent;
                    continue;
                }
                static std::mutex logLock;
                static std::set<uint32> logged;
                std::lock_guard<std::mutex> lock(logLock);
                if (logged.insert(spec * 10 + tier).second)
                {
                    // the names the row has on this core, to correct the build
                    std::string present;
                    for (uint8 column = 0; column < MAX_TALENT_COLUMNS; ++column)
                        if (uint32 talentId = BotTalents::TalentAt(bot, tier, column))
                            if (TalentEntry const* talent = sTalentStore.LookupEntry(talentId))
                                if (SpellInfo const* spell = sSpellMgr->GetSpellInfo(talent->SpellID))
                                    present += std::string(present.empty() ? "" : ", ") + (spell->SpellName ? spell->SpellName : "?") +
                                        " (" + std::to_string(talentId) + ")";
                    TC_LOG_ERROR("server.loading", "[mod-playerbots] Talent build of spec %u: no talent '%s' in row %u (it has: %s), a random one is taken.",
                        spec, name, uint32(tier + 1), present.empty() ? "nothing" : present.c_str());
                }
            }

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

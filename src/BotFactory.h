/*
 * mod-playerbots: creating bots (".playerbots create <class> [spec]") and setting them up.
 *
 * Like SkyFire-playerbots (CreateBotPopulation / InitializeBot) and AzerothCore's
 * RandomPlayerbotFactory: bot characters live on bot accounts named <AccountPrefix><n> (created when
 * needed, at most CharactersPerAccount characters each), so they are never anyone's alts. A character
 * is created as the core's own CMSG_CHARACTER_CREATE handler does (Player::Create + SaveToDB), with a
 * random race of the creator's faction that can be the class, a random gender and a generated name.
 * Once it has logged in, InitializeNewBot (on the bot's own thread) levels it to the creator's level,
 * gives it a spec (random unless asked), fills its talent rows, gears it (BotGear) and brings it to
 * the creator.
 */

#ifndef MOD_PLAYERBOTS_BOTFACTORY_H
#define MOD_PLAYERBOTS_BOTFACTORY_H

#include "Define.h"
#include "ObjectGuid.h"
#include "Position.h"
#include <map>
#include <string>
#include <vector>

class Player;

namespace BotFactory
{
    struct CreateRequest
    {
        uint8 cls = 0;
        uint32 specId = 0;              // 0 = random
        uint8 level = 1;                // the creator's level
        uint32 team = 0;                // ALLIANCE / HORDE: the creator's faction
        WorldLocation location;         // where the creator stood
        ObjectGuid creator;
        uint32 creatorAccount = 0;      // for his party invite to the bot
        bool teleport = true;           // to location once set up (not for a dungeon finder fill)
    };

    // a bot character in the characters database
    struct StoredBot
    {
        ObjectGuid guid;
        std::string name;
        uint8 race = 0;
        uint8 cls = 0;
        uint8 level = 0;
        uint32 account = 0;
    };

    struct CreateConfig
    {
        std::string accountPrefix = "PLAYERBOT";
        std::string accountPassword = "playerbot";
        uint32 charactersPerAccount = 10;
        uint32 poolAccounts = 2;        // bot accounts made at startup
        uint32 poolCharacters = 20;     // bot characters made at startup (all bot accounts together)
        uint32 poolLogins = 5;          // of them logged in at startup, standing on GM Island
    };

    // World thread, synchronous queries. Ids of the bot accounts (<prefix>...).
    std::vector<uint32> BotAccountIds(CreateConfig const& config);
    // offline (online = 0) bot characters, of a class or any (cls 0)
    std::vector<StoredBot> OfflineBots(std::vector<uint32> const& accounts, uint8 cls);
    uint32 CountCharacters(std::vector<uint32> const& accounts);
    // account <prefix><n>: made if missing (true when it was made now: a slow password hash); id and name out
    bool EnsureAccount(CreateConfig const& config, uint32 n, uint32& accountId, std::string& accountName);
    // characters per bot account (0 included)
    std::map<uint32, uint32> CharacterCounts(std::vector<uint32> const& accounts);

    // class from a name ("warrior", "dk", "demon hunter"...), 0 if unknown
    uint8 ClassFromName(std::string name);
    char const* ClassName(uint8 cls);
    // specs of a class a bot can take (not pet specs), in client order
    std::vector<uint32> SpecsOfClass(uint8 cls);
    // spec of the class by name ("frost", "beast mastery") or number (1-4), 0 if unknown
    uint32 SpecFromName(uint8 cls, std::string const& name);
    std::string SpecName(uint32 specId);

    // World thread. Creates the account if needed and the character; returns its name, or empty and
    // sets error.
    // onAccountId (and its name): the account to use; 0 = the first bot account with room (made if needed)
    std::string CreateCharacter(CreateRequest const& request, CreateConfig const& config, std::string& error,
        uint32 onAccountId = 0, std::string const& onAccountName = std::string());

    // Owner's rule: a random bot has done its race's own starting zone (worgen, goblin, pandaren) and its
    // hero class's (death knight, demon hunter): every quest of those zones its race/class can take is
    // marked rewarded (removed from the log if there), the learn-spell effects of their reward spells are
    // learned. Returns the number of quests marked. Bot's thread.
    uint32 CompleteStartingZones(Player* bot);

    // Bot's thread, once in the world: level, spec, talents, gear, then off to the creator.
    // Returns what was done, for the creator.
    std::string InitializeNewBot(Player* bot, CreateRequest const& request);
    // Owner's rule (2026-11-05) for a dismissed random bot: revived, inventory cleared except its bags, back
    // to its class's starting level (1; death knight 55, demon hunter 98), saved, and to GM Island when
    // toGMIsland. Returns the number of items destroyed.
    uint32 ResetBot(Player* bot, bool toGMIsland);

    // Fills each empty unlocked talent row (a row with a talent keeps it: owner's rule, alts included).
    // Returns the number of talents learned. Not in combat (Player::LearnTalent refuses).
    uint32 FillTalents(Player* bot);
    // At startup: every talent build name checked against this core's Talent.db2 (the builds are the owner's
    // 7.0 guides; 7.3.5 renamed or moved some talents). Each missing name is logged with the talents its row
    // really has. Returns the number of missing names.
    uint32 CheckTalentBuilds();
    // "Spec: Frost. Talents: 15 Frozen Touch, 30 ..." for the talents order
    std::string DescribeTalents(Player* bot);
}

#endif

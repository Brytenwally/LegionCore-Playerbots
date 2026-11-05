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
    };

    struct CreateConfig
    {
        std::string accountPrefix = "PLAYERBOT";
        std::string accountPassword = "playerbot";
        uint32 charactersPerAccount = 10;
    };

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
    std::string CreateCharacter(CreateRequest const& request, CreateConfig const& config, std::string& error);

    // Bot's thread, once in the world: level, spec, talents, gear, then off to the creator.
    // Returns what was done, for the creator.
    std::string InitializeNewBot(Player* bot, CreateRequest const& request);

    // Fills each empty unlocked talent row (a row with a talent keeps it: owner's rule, alts included).
    // Returns the number of talents learned. Not in combat (Player::LearnTalent refuses).
    uint32 FillTalents(Player* bot);
    // "Spec: Frost. Talents: 15 Frozen Touch, 30 ..." for the talents order
    std::string DescribeTalents(Player* bot);
}

#endif

/*
 * mod-playerbots: script registration and GM commands.
 *
 * Plan step 2/3: bots log in, stay in the world and log out on the core's bot sessions. They have
 * no AI yet: a bot stands where its character was saved.
 */

#include "ScriptMgr.h"
#include "Chat.h"
#include "Config.h"
#include "Log.h"
#include "Player.h"
#include "PlayerbotMgr.h"
#include "WorldSession.h"

class playerbots_world : public WorldScript
{
public:
    playerbots_world() : WorldScript("playerbots_world") { }

    void OnConfigLoad(bool /*reload*/) override
    {
        sPlayerbotMgr->LoadConfig();
    }

    void OnStartup() override
    {
        TC_LOG_INFO("server.loading", "[mod-playerbots] Module loaded (%s). GM commands: .playerbots add/remove/list.",
            sPlayerbotMgr->IsEnabled() ? "enabled" : "disabled");
    }

    void OnUpdate(uint32 diff) override
    {
        sPlayerbotMgr->Update(diff);
    }

    void OnShutdown() override
    {
        sPlayerbotMgr->Shutdown();
    }
};

class playerbots_player : public PlayerScript
{
public:
    playerbots_player() : PlayerScript("playerbots_player") { }

    void OnLogout(Player* player) override
    {
        if (WorldSession* session = player->GetSession())
            if (!session->IsBot())
                sPlayerbotMgr->OnPlayerLogout(session->GetAccountId());
    }
};

class playerbots_commandscript : public CommandScript
{
public:
    playerbots_commandscript() : CommandScript("playerbots_commandscript") { }

    std::vector<ChatCommand> GetCommands() const override
    {
        static std::vector<ChatCommand> playerbotsCommandTable =
        {
            { "add",       SEC_GAMEMASTER, true,  &HandleAddCommand,       "" },
            { "remove",    SEC_GAMEMASTER, true,  &HandleRemoveCommand,    "" },
            { "removeall", SEC_GAMEMASTER, true,  &HandleRemoveAllCommand, "" },
            { "list",      SEC_GAMEMASTER, true,  &HandleListCommand,      "" }
        };

        static std::vector<ChatCommand> commandTable =
        {
            { "playerbots", SEC_GAMEMASTER, true, nullptr, "", playerbotsCommandTable }
        };
        return commandTable;
    }

    // .playerbots add <name>: logs the character in as a bot (its account's own character, any account)
    static bool HandleAddCommand(ChatHandler* handler, char const* args)
    {
        if (!args || !*args)
        {
            handler->SendSysMessage("Usage: .playerbots add <character name>");
            return true;
        }

        ObjectGuid notifyGuid;
        if (WorldSession* session = handler->GetSession())
            if (Player* player = session->GetPlayer())
                notifyGuid = player->GetGUID();

        std::string error;
        if (!sPlayerbotMgr->AddBot(args, notifyGuid, error))
        {
            handler->PSendSysMessage("%s", error.c_str());
            return true;
        }

        handler->PSendSysMessage("Adding bot '%s'...", args);
        return true;
    }

    // .playerbots remove <name>: logs the bot out (saved)
    static bool HandleRemoveCommand(ChatHandler* handler, char const* args)
    {
        if (!args || !*args)
        {
            handler->SendSysMessage("Usage: .playerbots remove <character name>");
            return true;
        }

        std::string error;
        if (!sPlayerbotMgr->RemoveBot(args, error))
        {
            handler->PSendSysMessage("%s", error.c_str());
            return true;
        }

        handler->PSendSysMessage("Logging out bot '%s'.", args);
        return true;
    }

    static bool HandleRemoveAllCommand(ChatHandler* handler, char const* /*args*/)
    {
        sPlayerbotMgr->RemoveAllBots();
        handler->SendSysMessage("Logging out every bot.");
        return true;
    }

    static bool HandleListCommand(ChatHandler* handler, char const* /*args*/)
    {
        std::vector<PlayerbotMgr::BotInfo> const bots = sPlayerbotMgr->ListBots();
        if (bots.empty())
        {
            handler->SendSysMessage("No bots.");
            return true;
        }

        handler->PSendSysMessage("%u bot(s):", uint32(bots.size()));
        for (PlayerbotMgr::BotInfo const& bot : bots)
            handler->PSendSysMessage("  %s - %s", bot.name.c_str(), bot.state.c_str());
        return true;
    }
};

void AddSC_playerbot_scripts()
{
    std::string error;
    if (!sConfigMgr->LoadModuleConfig("playerbots.conf", error))
        TC_LOG_ERROR("server.loading", "[mod-playerbots] playerbots.conf not loaded, using defaults: %s", error.c_str());

    new playerbots_world();
    new playerbots_player();
    new playerbots_commandscript();
}

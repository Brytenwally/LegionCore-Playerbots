/*
 * mod-playerbots: script registration and GM commands.
 *
 * Bots log in and out on the core's bot sessions (PlayerbotMgr); each has a PlayerbotAI that joins
 * its master's group, follows, and takes whispered orders.
 */

#include "ScriptMgr.h"
#include "Chat.h"
#include "Config.h"
#include "Log.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "QuestDef.h"
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

    // the bot's AI tick, on its map thread
    void OnUpdate(Player* player, uint32 diff) override
    {
        WorldSession* session = player->GetSession();
        if (!session || !session->IsBot())
            return;

        if (std::shared_ptr<PlayerbotAI> ai = sPlayerbotMgr->GetAI(player->GetGUID()))
            ai->Update(player, diff);
    }

    // whispered orders: the hook runs in the sender's thread, the order is carried out in the bot's
    void OnWhisper(Player* sender, ObjectGuid const& receiver, uint32 /*lang*/, std::string const& msg) override
    {
        WorldSession* session = sender->GetSession();
        if (!session || session->IsBot())
            return;

        std::shared_ptr<PlayerbotAI> ai = sPlayerbotMgr->GetAI(receiver);
        if (!ai)
            return;

        ObjectGuid const senderGuid = sender->GetGUID();
        uint32 const senderAccount = session->GetAccountId();
        ObjectAccessor::PostToPlayer(receiver, [ai, senderGuid, senderAccount, msg](Player* bot) -> void
        {
            ai->HandleWhisper(bot, senderGuid, senderAccount, msg);
        });
    }

    // party, raid and instance chat: orders reach the sender's own bots only, each checks in its own thread
    // that it is still in that group
    void OnChat(Player* player, uint32 type, uint32 /*lang*/, std::string& msg, Group* group) override
    {
        switch (type)
        {
            case CHAT_MSG_PARTY:
            case CHAT_MSG_PARTY_LEADER:
            case CHAT_MSG_RAID:
            case CHAT_MSG_RAID_LEADER:
            case CHAT_MSG_INSTANCE_CHAT:
            case CHAT_MSG_INSTANCE_CHAT_LEADER:
                break;
            default:
                return;
        }

        WorldSession* session = player->GetSession();
        if (!group || !session || session->IsBot())
            return;

        ObjectGuid const senderGuid = player->GetGUID();
        uint32 const senderAccount = session->GetAccountId();
        ObjectGuid const groupGuid = group->GetGUID();
        std::string const text = msg;
        for (auto const& owned : sPlayerbotMgr->GetBotsOwnedBy(senderGuid, senderAccount))
        {
            std::shared_ptr<PlayerbotAI> ai = owned.second;
            ObjectAccessor::PostToPlayer(owned.first, [ai, senderGuid, senderAccount, groupGuid, text](Player* bot) -> void
            {
                ai->HandlePartyChat(bot, senderGuid, senderAccount, groupGuid, text);
            });
        }
    }

    // quests: the owner's accepts and turn-ins are mirrored by his bots in his group
    void OnQuestAccept(Player* player, Quest const* quest) override
    {
        ForOwnedBotsInGroup(player, quest, [](PlayerbotAI* ai, Player* bot, ObjectGuid const& owner, ObjectGuid const& group, uint32 questId)
        {
            ai->MirrorQuestAccept(bot, owner, group, questId);
        });
    }

    void OnQuestReward(Player* player, Quest const* quest) override
    {
        ForOwnedBotsInGroup(player, quest, [](PlayerbotAI* ai, Player* bot, ObjectGuid const& owner, ObjectGuid const& group, uint32 questId)
        {
            ai->MirrorQuestReward(bot, owner, group, questId);
        });
    }

    void OnLogout(Player* player) override
    {
        if (WorldSession* session = player->GetSession())
            if (!session->IsBot())
                sPlayerbotMgr->OnPlayerLogout(session->GetAccountId());
    }

private:
    // runs action in the thread of each bot the player owns; each checks there that it is in his group.
    // A bot's own quest accepts and turn-ins call the same hooks: they are not mirrored.
    template<class Action>
    static void ForOwnedBotsInGroup(Player* player, Quest const* quest, Action action)
    {
        WorldSession* session = player->GetSession();
        Group* group = player->GetGroup();
        if (!quest || !group || !session || session->IsBot())
            return;

        ObjectGuid const owner = player->GetGUID();
        ObjectGuid const groupGuid = group->GetGUID();
        uint32 const questId = quest->GetQuestId();
        for (auto const& owned : sPlayerbotMgr->GetBotsOwnedBy(owner, session->GetAccountId()))
        {
            std::shared_ptr<PlayerbotAI> ai = owned.second;
            ObjectAccessor::PostToPlayer(owned.first, [ai, action, owner, groupGuid, questId](Player* bot) -> void
            {
                action(ai.get(), bot, owner, groupGuid, questId);
            });
        }
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

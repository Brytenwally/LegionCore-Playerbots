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
#include "BotApl.h"
#include "BotRotations.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "World.h"
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
        BotRotations::Initialize();
        BotApl::Initialize(sPlayerbotMgr->GetAplDirectory());
        BotFactory::CheckTalentBuilds();
        // the core's single-player dungeon finder makes a lone player's group at once: no time for the fill
        if (sPlayerbotMgr->IsEnabled() && sWorld->getBoolConfig(CONFIG_LFG_DEBUG_JOIN))
            TC_LOG_WARN("server.loading", "[mod-playerbots] DungeonFinder.DebugJoin = 1 in worldserver.conf: a player alone is sent "
                "to a dungeon at once, so bots cannot fill the queue. Set it to 0 for the dungeon finder fill.");
        TC_LOG_INFO("server.loading", "[mod-playerbots] Module loaded (%s). GM commands: .playerbots add/create/remove/removeall/list/self.",
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
        if (!session)
            return;
        if (!session->IsBot())
        {
            // a client: his self-bot AI, his dungeon finder queues for the fill
            sPlayerbotMgr->OnClientUpdate(player, diff);
            return;
        }

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

        OrderSender const order = MakeOrderSender(sender);
        ObjectAccessor::PostToPlayer(receiver, [ai, order, msg](Player* bot) -> void
        {
            ai->HandleWhisper(bot, order, msg);
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
            case CHAT_MSG_RAID_WARNING:
            case CHAT_MSG_INSTANCE_CHAT:                    // also battleground chat on Legion
            case CHAT_MSG_INSTANCE_CHAT_LEADER:
                break;
            default:
                return;
        }

        WorldSession* session = player->GetSession();
        if (!group || !session || session->IsBot())
            return;

        OrderSender order = MakeOrderSender(player);
        order.partyChat = true;
        ObjectGuid const groupGuid = group->GetGUID();
        std::string const text = msg;
        for (auto const& owned : sPlayerbotMgr->GetBotsOwnedBy(order.guid, order.accountId))
        {
            std::shared_ptr<PlayerbotAI> ai = owned.second;
            ObjectAccessor::PostToPlayer(owned.first, [ai, order, groupGuid, text](Player* bot) -> void
            {
                ai->HandlePartyChat(bot, order, groupGuid, text);
            });
        }
    }

    // quests: the owner's accepts and turn-ins are mirrored by his bots in his group
    void OnQuestAccept(Player* player, Quest const* quest) override
    {
        ForOwnedBotsInGroup(player, quest, "accepted", [](PlayerbotAI* ai, Player* bot, ObjectGuid const& owner, ObjectGuid const& group, uint32 questId)
        {
            ai->MirrorQuestAccept(bot, owner, group, questId);
        });
    }

    void OnQuestReward(Player* player, Quest const* quest) override
    {
        ForOwnedBotsInGroup(player, quest, "turned in", [](PlayerbotAI* ai, Player* bot, ObjectGuid const& owner, ObjectGuid const& group, uint32 questId)
        {
            ai->MirrorQuestReward(bot, owner, group, questId);
        });
    }

    void OnLogout(Player* player) override
    {
        if (WorldSession* session = player->GetSession())
            if (!session->IsBot())
                sPlayerbotMgr->OnPlayerLogout(session->GetAccountId(), player->GetGUID());
    }

private:
    // read in the sender's own thread, carried with the order to the bot's
    static OrderSender MakeOrderSender(Player* sender)
    {
        OrderSender order;
        order.guid = sender->GetGUID();
        order.accountId = sender->GetSession()->GetAccountId();
        order.location = WorldLocation(sender->GetMapId(), sender->GetPositionX(), sender->GetPositionY(),
            sender->GetPositionZ(), sender->GetOrientation());
        order.inCombat = sender->isInCombat();
        order.selection = sender->GetSelection();
        return order;
    }

    // runs action in the thread of each bot the player owns; each checks there that it is in his group.
    // A bot's own quest accepts and turn-ins call the same hooks: they are not mirrored.
    template<class Action>
    static void ForOwnedBotsInGroup(Player* player, Quest const* quest, char const* what, Action action)
    {
        WorldSession* session = player->GetSession();
        if (!quest || !session || session->IsBot())
            return;

        Group* group = player->GetGroup();
        if (!group)
        {
            TC_LOG_INFO("server.loading", "[mod-playerbots] %s %s quest %u: not in a group, no bot follows.",
                player->GetName(), what, quest->GetQuestId());
            return;
        }

        ObjectGuid const owner = player->GetGUID();
        ObjectGuid const groupGuid = group->GetGUID();
        uint32 const questId = quest->GetQuestId();
        auto const bots = sPlayerbotMgr->GetBotsOwnedBy(owner, session->GetAccountId());
        TC_LOG_INFO("server.loading", "[mod-playerbots] %s %s quest %u: passed to %u bot(s).",
            player->GetName(), what, questId, uint32(bots.size()));
        for (auto const& owned : bots)
        {
            if (owned.first == owner)                       // his own character as a self-bot
                continue;
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
            { "create",    SEC_GAMEMASTER, false, &HandleCreateCommand,    "" },
            { "remove",    SEC_GAMEMASTER, true,  &HandleRemoveCommand,    "" },
            { "removeall", SEC_GAMEMASTER, true,  &HandleRemoveAllCommand, "" },
            { "list",      SEC_GAMEMASTER, true,  &HandleListCommand,      "" },
            // open to players when Playerbots.Self.GMOnly = 0 (checked in the handler)
            { "self",      SEC_PLAYER,     false, &HandleSelfCommand,      "" }
        };

        static std::vector<ChatCommand> commandTable =
        {
            { "playerbots", SEC_PLAYER, true, nullptr, "", playerbotsCommandTable }
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

    // .playerbots create <class> [spec]: a new bot character on a bot account, at the GM's level, of his faction,
    // with a random (or the given) spec, talents and gear, brought to him
    static bool HandleCreateCommand(ChatHandler* handler, char const* args)
    {
        Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!player)
        {
            handler->SendSysMessage("Use it in game: the bot takes your level, faction and position.");
            return true;
        }
        if (!sPlayerbotMgr->IsEnabled())
        {
            handler->SendSysMessage("Playerbots are disabled (set Playerbots.Enable = 1 in playerbots.conf).");
            return true;
        }

        std::string text = args ? args : "";
        std::string className = text, specName;
        if (size_t space = text.find(' '); space != std::string::npos)
        {
            // "demon hunter havoc": try the longest class name first
            std::string const twoWords = text.substr(0, text.find(' ', space + 1));
            if (BotFactory::ClassFromName(twoWords))
            {
                className = twoWords;
                specName = twoWords.size() < text.size() ? text.substr(twoWords.size() + 1) : std::string();
            }
            else
            {
                className = text.substr(0, space);
                specName = text.substr(space + 1);
            }
        }

        uint8 const cls = BotFactory::ClassFromName(className);
        if (!cls)
        {
            handler->SendSysMessage("Usage: .playerbots create <class> [spec] - classes: warrior paladin hunter rogue priest "
                "deathknight shaman mage warlock monk druid demonhunter");
            return true;
        }

        // owner's rule: a hero class is created only from its starting level (Death Knight 55, Demon Hunter 98)
        uint8 const heroLevel = cls == CLASS_DEATH_KNIGHT ? 55 : cls == CLASS_DEMON_HUNTER ? 98 : 0;
        if (player->getLevel() < heroLevel)
        {
            handler->PSendSysMessage("You need level %u to create a %s bot.", uint32(heroLevel), BotFactory::ClassName(cls));
            return true;
        }

        BotFactory::CreateRequest request;
        request.cls = cls;
        if (!specName.empty())
        {
            request.specId = BotFactory::SpecFromName(cls, specName);
            if (!request.specId)
            {
                handler->PSendSysMessage("%s has no spec called '%s'.", BotFactory::ClassName(cls), specName.c_str());
                return true;
            }
        }
        request.level = player->getLevel();
        request.team = player->GetTeam();
        request.location = WorldLocation(player->GetMapId(), player->GetPositionX(), player->GetPositionY(),
            player->GetPositionZ(), player->GetOrientation());
        request.creator = player->GetGUID();
        request.creatorAccount = handler->GetSession()->GetAccountId();

        sPlayerbotMgr->RequestCreate(request);
        handler->PSendSysMessage("Creating a level %u %s bot...", uint32(request.level), BotFactory::ClassName(cls));
        return true;
    }

    // .playerbots remove <name>: logs the bot out (saved)
    // .playerbots self: the player's own character becomes a bot (rotation, orders, moves to its target), again = off
    static bool HandleSelfCommand(ChatHandler* handler, char const* /*args*/)
    {
        WorldSession* session = handler->GetSession();
        Player* player = session ? session->GetPlayer() : nullptr;
        if (!player)
            return false;
        if (!sPlayerbotMgr->IsEnabled())
        {
            handler->SendSysMessage("Playerbots are disabled (Playerbots.Enable = 0).");
            handler->SetSentErrorMessage(true);
            return false;
        }
        if (sPlayerbotMgr->IsSelfGMOnly() && session->GetSecurity() < SEC_GAMEMASTER)
        {
            handler->SendSysMessage("Only game masters may use .playerbots self (Playerbots.Self.GMOnly).");
            handler->SetSentErrorMessage(true);
            return false;
        }

        std::string reply;
        bool const done = sPlayerbotMgr->ToggleSelfBot(player, reply);
        handler->SendSysMessage(reply.c_str());
        if (!done)
            handler->SetSentErrorMessage(true);
        return done;
    }

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

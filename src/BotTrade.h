/*
 * mod-playerbots: bag inspection and trading (owner's request, 2026-11-13).
 *
 * AzerothCore playerbots' TellItemCountAction ("c" / "items" / "inventory" / "inv" [filter]: the bot whispers what its
 * bags hold) and TradeAction / TradeStatusAction ("t" / "trade" [items] [money]: the bot opens a trade with its master
 * and puts items and money in it; a trade the master or a group member opens is begun, and accepted once the other
 * side accepts). Every trade step goes through the core's own handlers (WorldSession::HandleBeginTrade,
 * HandleInitiateTrade, HandleSetTradeItem, HandleSetTradeGold, HandleAcceptTrade, HandleCancelTrade), as the client's
 * packets would, so the trade rules (distance, binding, bag room) are the core's. Everything runs on the bot's thread.
 */

#ifndef MOD_PLAYERBOTS_BOTTRADE_H
#define MOD_PLAYERBOTS_BOTTRADE_H

#include "Define.h"
#include <string>
#include <vector>

class Item;
class ItemTemplate;
class Player;

namespace BotTrade
{
    // bag items matching AzerothCore's item filters: empty / "all", item links, a quality (poor ... legendary),
    // food, drink, potion, healing potion, mana potion, flask, elixir, bandage, quest, weapon, armor, gem,
    // materials / trade goods, recipe, consumable, bag, else a part of the item's name
    std::vector<Item*> FindItems(Player* bot, std::string const& filter);

    // a chat link to the item ("|cff1eff00|Hitem:123|h[Name]|h|r")
    std::string ItemLink(ItemTemplate const* proto);

    // "=== Inventory ===", one line per item kind under a header per item class, then the money
    std::vector<std::string> InventoryLines(Player* bot, std::string const& filter);

    // "1g 20s 5c" parts of the text (copper), the text left without them
    uint64 ParseMoney(std::string const& text, std::string& rest);

    // the core's trade handlers, as the client would call them
    bool Initiate(Player* bot, Player* target);
    void Begin(Player* bot);
    void Accept(Player* bot);
    void Cancel(Player* bot);
    void SetMoney(Player* bot, uint64 copper);
    // puts up to max of the items into free trade slots; returns how many went in
    uint32 PutItems(Player* bot, std::vector<Item*> const& items, uint32 max);
}

#endif

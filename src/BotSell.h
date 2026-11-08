/*
 * mod-playerbots: selling to vendors (owner's request, 2026-11-08).
 *
 * AzerothCore playerbots' SellAction ("sell" / "s" order): gray / * (gray items), vendor (what the bot has no use for),
 * <quality> [all] (up to that quality: equipment only unless "all"; profession tools never), or item links / names.
 * Every sale goes through the core's own handler (WorldSession::HandleSellItemOpcode, as the client's CMSG_SELL_ITEM),
 * so the vendor must be within interaction range and the buyback slots, money and quest item checks are the core's.
 * Also the automatic sale of gray items when the bot stands at a vendor (Playerbots.Sell.AutoGray).
 * Everything runs on the bot's own thread.
 */

#ifndef MOD_PLAYERBOTS_BOTSELL_H
#define MOD_PLAYERBOTS_BOTSELL_H

#include "Define.h"
#include <string>

class Creature;
class Player;

namespace BotSell
{
    struct Result
    {
        uint32 items = 0;                                   // stacks sold
        uint64 money = 0;                                   // copper earned
        std::string names;                                  // "[A], [B]..." (the first few)
    };

    // a vendor the bot can trade with right now (within interaction range), else null
    Creature* FindVendor(Player* bot);

    // the "sell" order's argument: gray / grey / *, vendor, <quality> [all], item links or a name; false when the
    // argument means nothing (the caller then shows the usage)
    bool Sell(Player* bot, Creature* vendor, std::string const& args, Result& result);

    // the bot carries a gray item it could sell
    bool HasGrayItems(Player* bot);

    // "1g 20s 5c"
    std::string FormatMoney(uint64 copper);
}

#endif

/*
 * mod-playerbots: selling to vendors. See BotSell.h.
 */

#include "BotSell.h"
#include "BotGear.h"
#include "Bag.h"
#include "Creature.h"
#include "Item.h"
#include "ItemPackets.h"
#include "ItemTemplate.h"
#include "Opcodes.h"
#include "Player.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <algorithm>
#include <cctype>
#include <functional>
#include <list>
#include <regex>
#include <set>
#include <vector>

namespace
{
    float const VendorSearchRange = 10.0f;                  // the core checks the interaction distance itself
    uint32 const NamesShown = 6;

    std::string Lower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return text;
    }

    std::string NameOf(ItemTemplate const* proto)
    {
        LocalizedString const* name = proto->GetName();
        return name ? std::string(name->Str[LOCALE_enUS]) : std::string();
    }

    // the items in the backpack and the bags (never what the bot wears)
    std::vector<Item*> BagItems(Player* bot)
    {
        std::vector<Item*> items;
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                items.push_back(item);
        for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
            if (Bag* container = bot->GetBagByPos(bag))
                for (uint32 slot = 0; slot < container->GetBagSize(); ++slot)
                    if (Item* item = container->GetItemByPos(uint8(slot)))
                        items.push_back(item);
        return items;
    }

    // what a vendor would take: a sell price, not refundable (the handler would refund it instead), not a bag with
    // something in it, not an artifact
    bool Sellable(Item* item)
    {
        ItemTemplate const* proto = item->GetTemplate();
        if (!proto || !item->GetSellPrice() || item->IsNotEmptyBag())
            return false;
        if (item->HasFlag(ITEM_FIELD_DYNAMIC_FLAGS, ITEM_FLAG_REFUNDABLE))
            return false;
        return proto->GetQuality() != ITEM_QUALITY_ARTIFACT;
    }

    bool IsEquipment(ItemTemplate const* proto)
    {
        return proto->GetClass() == ITEM_CLASS_ARMOR || proto->GetClass() == ITEM_CLASS_WEAPON;
    }

    // AzerothCore's IsProfessionTool: fishing poles, miscellaneous weapons and anything with a totem category
    bool IsProfessionTool(ItemTemplate const* proto)
    {
        if (proto->GetClass() != ITEM_CLASS_WEAPON)
            return false;
        if (proto->GetSubClass() == ITEM_SUBCLASS_WEAPON_MISCELLANEOUS || proto->GetSubClass() == ITEM_SUBCLASS_WEAPON_FISHING_POLE)
            return true;
        return proto->GetTotemCategoryID() != 0;
    }

    // AzerothCore's ChatHelper::parseItemQuality; MAX_ITEM_QUALITY when the word is no quality
    uint32 ParseQuality(std::string const& word)
    {
        static std::pair<char const*, uint32> const names[] =
        {
            { "poor", ITEM_QUALITY_POOR }, { "gray", ITEM_QUALITY_POOR }, { "grey", ITEM_QUALITY_POOR },
            { "normal", ITEM_QUALITY_NORMAL }, { "white", ITEM_QUALITY_NORMAL }, { "common", ITEM_QUALITY_NORMAL },
            { "uncommon", ITEM_QUALITY_UNCOMMON }, { "green", ITEM_QUALITY_UNCOMMON },
            { "rare", ITEM_QUALITY_RARE }, { "blue", ITEM_QUALITY_RARE },
            { "epic", ITEM_QUALITY_EPIC }, { "violet", ITEM_QUALITY_EPIC }, { "purple", ITEM_QUALITY_EPIC },
            { "legendary", ITEM_QUALITY_LEGENDARY }, { "yellow", ITEM_QUALITY_LEGENDARY }, { "orange", ITEM_QUALITY_LEGENDARY },
        };
        for (auto const& name : names)
            if (word == name.first)
                return name.second;
        return MAX_ITEM_QUALITY;
    }

    // as the client's CMSG_SELL_ITEM, through the core's handler (vendor range, buyback, money, quest items)
    bool SellOne(Player* bot, Creature* vendor, Item* item, BotSell::Result& result)
    {
        ItemTemplate const* proto = item->GetTemplate();
        std::string const name = NameOf(proto);
        uint64 const before = bot->GetMoney();

        WorldPackets::Item::SellItem packet{ WorldPacket(CMSG_SELL_ITEM) };
        packet.VendorGUID = vendor->GetGUID();
        packet.ItemGUID = item->GetGUID();
        packet.Amount = 0;                                  // the whole stack
        bot->GetSession()->HandleSellItemOpcode(packet);

        if (bot->GetMoney() <= before)
            return false;                                   // refused (the item may be gone or kept)
        ++result.items;
        result.money += bot->GetMoney() - before;
        if (result.items <= NamesShown)
            result.names += (result.names.empty() ? "[" : ", [") + name + "]";
        else if (result.items == NamesShown + 1)
            result.names += "...";
        return true;
    }

    uint32 SellMatching(Player* bot, Creature* vendor, BotSell::Result& result, std::function<bool(Item*)> const& wanted)
    {
        uint32 sold = 0;
        for (Item* item : BagItems(bot))                    // collected first: selling moves items around
            if (Sellable(item) && wanted(item) && SellOne(bot, vendor, item, result))
                ++sold;
        return sold;
    }
}

namespace BotSell
{
    Creature* FindVendor(Player* bot)
    {
        std::list<Creature*> creatures;
        bot->GetCreatureListInGrid(creatures, VendorSearchRange);
        Creature* best = nullptr;
        float bestDist = VendorSearchRange + 1.0f;
        for (Creature* creature : creatures)
        {
            if (!creature->IsAlive() || !creature->HasFlag(UNIT_FIELD_NPC_FLAGS, UNIT_NPC_FLAG_VENDOR))
                continue;
            if (!bot->GetNPCIfCanInteractWith(creature->GetGUID(), UNIT_NPC_FLAG_VENDOR))
                continue;                                   // out of reach, hostile, busy...
            float const dist = bot->GetDistance(creature);
            if (dist < bestDist)
            {
                best = creature;
                bestDist = dist;
            }
        }
        return best;
    }

    bool HasGrayItems(Player* bot)
    {
        for (Item* item : BagItems(bot))
            if (item->GetTemplate()->GetQuality() == ITEM_QUALITY_POOR && Sellable(item))
                return true;
        return false;
    }

    bool Sell(Player* bot, Creature* vendor, std::string const& args, Result& result)
    {
        std::string const text = Lower(args);

        // gray items (AzerothCore: "gray" or "*")
        if (text == "gray" || text == "grey" || text == "*")
        {
            SellMatching(bot, vendor, result, [](Item* item)
            {
                ItemTemplate const* proto = item->GetTemplate();
                return proto->GetQuality() == ITEM_QUALITY_POOR && !IsProfessionTool(proto);
            });
            return true;
        }

        // what the bot has no use for (AzerothCore: item usage vendor / AH): gray items, and armor and weapons it
        // cannot wear or that would not replace what it wears (the same comparison as autogear and need rolls)
        if (text == "vendor")
        {
            SellMatching(bot, vendor, result, [bot](Item* item)
            {
                ItemTemplate const* proto = item->GetTemplate();
                if (IsProfessionTool(proto))
                    return false;
                if (proto->GetQuality() == ITEM_QUALITY_POOR)
                    return true;
                if (!IsEquipment(proto) || proto->GetQuality() > ITEM_QUALITY_EPIC)
                    return false;
                BotGear::Comparison const cmp = BotGear::CompareToWorn(bot, proto, item->GetItemLevel(bot->getLevel()));
                return !cmp.usable || cmp.gain <= 0.0f;
            });
            return true;
        }

        // "<quality> [all]": up to that quality; equipment only unless "all" (AzerothCore's SellQualityItemsVisitor)
        std::string quality = text;
        bool allClasses = false;
        size_t const split = quality.rfind(' ');
        if (split != std::string::npos && quality.substr(split + 1) == "all")
        {
            quality.erase(split);
            allClasses = true;
        }
        uint32 const maxQuality = ParseQuality(quality);
        if (maxQuality != MAX_ITEM_QUALITY)
        {
            SellMatching(bot, vendor, result, [maxQuality, allClasses](Item* item)
            {
                ItemTemplate const* proto = item->GetTemplate();
                if (proto->GetQuality() > maxQuality || IsProfessionTool(proto))
                    return false;
                return allClasses || proto->GetQuality() == ITEM_QUALITY_POOR || IsEquipment(proto);
            });
            return true;
        }

        if (text.empty())
            return false;

        // item links (every |Hitem:<id> in the text), else a name: every bag item whose name contains it
        std::set<uint32> entries;
        static std::regex const link("Hitem:(\\d+)");
        for (std::sregex_iterator it(args.begin(), args.end(), link), end; it != end; ++it)
            entries.insert(uint32(std::stoul((*it)[1].str())));
        if (!entries.empty())
            SellMatching(bot, vendor, result, [&entries](Item* item) { return entries.count(item->GetEntry()) != 0; });
        else
            SellMatching(bot, vendor, result, [&text](Item* item)
            {
                return Lower(NameOf(item->GetTemplate())).find(text) != std::string::npos;
            });
        return true;
    }

    std::string FormatMoney(uint64 copper)
    {
        std::string out;
        if (copper >= 10000)
            out += std::to_string(copper / 10000) + "g ";
        if (copper >= 100)
            out += std::to_string((copper / 100) % 100) + "s ";
        out += std::to_string(copper % 100) + "c";
        return out;
    }
}

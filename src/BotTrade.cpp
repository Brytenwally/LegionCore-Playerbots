/*
 * mod-playerbots: bag inspection and trading. See BotTrade.h.
 */

#include "BotTrade.h"
#include "Bag.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Opcodes.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "TradeData.h"
#include "TradePackets.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <functional>
#include <regex>
#include <set>

namespace
{
    uint32 const MaxListedKinds = 80;                       // whispers per "c" answer, at most

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

    // the backpack and the bags, never what the bot wears
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

    // an on-use spell of the item with that effect (AzerothCore's FindPotionVisitor)
    bool UseHasEffect(ItemTemplate const* proto, SpellEffects wanted, int32 powerType = -1)
    {
        for (ItemEffectEntry const* effect : proto->Effects)
        {
            if (!effect || effect->SpellID <= 0 || effect->TriggerType != ITEM_SPELLTRIGGER_ON_USE)
                continue;
            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(uint32(effect->SpellID));
            if (!spellInfo)
                continue;
            for (SpellEffectInfo const* spellEffect : spellInfo->Effects)
                if (spellEffect && spellEffect->Effect == uint32(wanted) &&
                    (powerType < 0 || spellEffect->MiscValue == powerType))
                    return true;
        }
        return false;
    }

    bool IsConsumable(ItemTemplate const* proto, uint32 subClass)
    {
        return proto->GetClass() == ITEM_CLASS_CONSUMABLE && proto->GetSubClass() == subClass;
    }

    // AzerothCore's named qualifiers and item classes; false when the word is none of them
    bool NamedFilter(std::string const& text, std::function<bool(ItemTemplate const*)>& match)
    {
        if (text == "food" || text == "drink" || text == "water")
            match = [](ItemTemplate const* p) { return IsConsumable(p, ITEM_SUBCLASS_FOOD_DRINK); };
        else if (text == "potion" || text == "potions")
            match = [](ItemTemplate const* p) { return IsConsumable(p, ITEM_SUBCLASS_POTION); };
        else if (text == "healing potion")
            match = [](ItemTemplate const* p) { return IsConsumable(p, ITEM_SUBCLASS_POTION) && UseHasEffect(p, SPELL_EFFECT_HEAL); };
        else if (text == "mana potion")
            match = [](ItemTemplate const* p) { return IsConsumable(p, ITEM_SUBCLASS_POTION) && UseHasEffect(p, SPELL_EFFECT_ENERGIZE, POWER_MANA); };
        else if (text == "flask" || text == "flasks")
            match = [](ItemTemplate const* p) { return IsConsumable(p, ITEM_SUBCLASS_FLASK); };
        else if (text == "elixir" || text == "elixirs")
            match = [](ItemTemplate const* p) { return IsConsumable(p, ITEM_SUBCLASS_ELIXIR); };
        else if (text == "bandage" || text == "bandages")
            match = [](ItemTemplate const* p) { return IsConsumable(p, ITEM_SUBCLASS_BANDAGE); };
        else if (text == "quest")
            match = [](ItemTemplate const* p) { return p->GetClass() == ITEM_CLASS_QUEST; };
        else if (text == "weapon" || text == "weapons")
            match = [](ItemTemplate const* p) { return p->GetClass() == ITEM_CLASS_WEAPON; };
        else if (text == "armor" || text == "armour")
            match = [](ItemTemplate const* p) { return p->GetClass() == ITEM_CLASS_ARMOR; };
        else if (text == "gear" || text == "equipment")
            match = [](ItemTemplate const* p) { return p->GetClass() == ITEM_CLASS_ARMOR || p->GetClass() == ITEM_CLASS_WEAPON; };
        else if (text == "gem" || text == "gems")
            match = [](ItemTemplate const* p) { return p->GetClass() == ITEM_CLASS_GEM; };
        else if (text == "materials" || text == "trade goods" || text == "mats")
            match = [](ItemTemplate const* p) { return p->GetClass() == ITEM_CLASS_TRADE_GOODS; };
        else if (text == "recipe" || text == "recipes")
            match = [](ItemTemplate const* p) { return p->GetClass() == ITEM_CLASS_RECIPE; };
        else if (text == "consumable" || text == "consumables")
            match = [](ItemTemplate const* p) { return p->GetClass() == ITEM_CLASS_CONSUMABLE; };
        else if (text == "bag" || text == "bags" || text == "container")
            match = [](ItemTemplate const* p) { return p->GetClass() == ITEM_CLASS_CONTAINER; };
        else
            return false;
        return true;
    }

    char const* ClassHeader(uint32 itemClass)
    {
        switch (itemClass)
        {
            case ITEM_CLASS_CONSUMABLE:    return "--- consumable ---";
            case ITEM_CLASS_CONTAINER:     return "--- container ---";
            case ITEM_CLASS_WEAPON:        return "--- weapon ---";
            case ITEM_CLASS_GEM:           return "--- gem ---";
            case ITEM_CLASS_ARMOR:         return "--- armor ---";
            case ITEM_CLASS_REAGENT:       return "--- reagent ---";
            case ITEM_CLASS_TRADE_GOODS:   return "--- trade goods ---";
            case ITEM_CLASS_RECIPE:        return "--- recipe ---";
            case ITEM_CLASS_QUEST:         return "--- quest items ---";
            case ITEM_CLASS_KEY:           return "--- keys ---";
            default:                       return "--- other ---";
        }
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

    WorldPacket Empty(OpcodeClient opcode)
    {
        return WorldPacket(opcode);
    }
}

namespace BotTrade
{
    std::vector<Item*> FindItems(Player* bot, std::string const& filter)
    {
        std::string const text = Lower(filter);
        std::vector<Item*> const all = BagItems(bot);
        if (text.empty() || text == "all" || text == "*")
            return all;

        std::function<bool(ItemTemplate const*)> match;
        if (!NamedFilter(text, match))
        {
            uint32 const quality = ParseQuality(text);
            std::set<uint32> entries;
            static std::regex const link("Hitem:(\\d+)");
            for (std::sregex_iterator it(filter.begin(), filter.end(), link), end; it != end; ++it)
                entries.insert(uint32(std::stoul((*it)[1].str())));

            if (!entries.empty())
                match = [entries](ItemTemplate const* p) { return entries.count(p->GetId()) != 0; };
            else if (quality != MAX_ITEM_QUALITY)
                match = [quality](ItemTemplate const* p) { return p->GetQuality() == quality; };
            else
                match = [text](ItemTemplate const* p) { return Lower(NameOf(p)).find(text) != std::string::npos; };
        }

        std::vector<Item*> found;
        for (Item* item : all)
            if (ItemTemplate const* proto = item->GetTemplate())
                if (match(proto))
                    found.push_back(item);
        return found;
    }

    std::string ItemLink(ItemTemplate const* proto)
    {
        uint32 const quality = std::min<uint32>(proto->GetQuality(), MAX_ITEM_QUALITY - 1);
        char color[16];
        std::snprintf(color, sizeof(color), "%08x", ItemQualityColors[quality]);
        return std::string("|c") + color + "|Hitem:" + std::to_string(proto->GetId()) + "|h[" + NameOf(proto) + "]|h|r";
    }

    std::vector<std::string> InventoryLines(Player* bot, std::string const& filter)
    {
        // one line per item kind: its count over every stack, soulbound or not (AzerothCore's TellItems)
        struct Kind { ItemTemplate const* proto; uint32 count; bool soulbound; };
        std::map<uint32, Kind> kinds;
        for (Item* item : FindItems(bot, filter))
        {
            ItemTemplate const* proto = item->GetTemplate();
            Kind& kind = kinds.emplace(proto->GetId(), Kind{ proto, 0, false }).first->second;
            kind.count += item->GetCount();
            kind.soulbound = kind.soulbound || item->IsSoulBound();
        }

        std::vector<Kind> sorted;
        for (auto const& pair : kinds)
            sorted.push_back(pair.second);
        // AzerothCore's compare_items: class, subclass, quality, then the highest item level first
        std::sort(sorted.begin(), sorted.end(), [](Kind const& a, Kind const& b)
        {
            if (a.proto->GetClass() != b.proto->GetClass())
                return a.proto->GetClass() > b.proto->GetClass();
            if (a.proto->GetSubClass() != b.proto->GetSubClass())
                return a.proto->GetSubClass() < b.proto->GetSubClass();
            if (a.proto->GetQuality() != b.proto->GetQuality())
                return a.proto->GetQuality() < b.proto->GetQuality();
            return a.proto->GetBaseItemLevel() > b.proto->GetBaseItemLevel();
        });

        std::vector<std::string> lines;
        lines.push_back("=== Inventory ===");
        uint32 lastClass = MAX_ITEM_CLASS;
        uint32 listed = 0;
        for (Kind const& kind : sorted)
        {
            if (++listed > MaxListedKinds)
            {
                lines.push_back("... and " + std::to_string(sorted.size() - MaxListedKinds) + " more (give a filter: c <name>)");
                break;
            }
            if (kind.proto->GetClass() != lastClass)
            {
                lastClass = kind.proto->GetClass();
                lines.push_back(ClassHeader(lastClass));
            }
            std::string line = ItemLink(kind.proto);
            if (kind.count > 1)
                line += " x" + std::to_string(kind.count);
            if (kind.soulbound)
                line += " (soulbound)";
            lines.push_back(line);
        }
        if (sorted.empty())
            lines.push_back(filter.empty() ? "My bags are empty." : "Nothing like that in my bags.");
        lines.push_back("Money: " + FormatMoney(bot->GetMoney()));
        return lines;
    }

    uint64 ParseMoney(std::string const& text, std::string& rest)
    {
        // AzerothCore's ChatHelper::parseMoney: "5g", "20s", "15c" (or "5g20s"), anywhere in the text
        static std::regex const part("(^|\\s)(\\d+)([gsc])(?=\\s|$|\\d)", std::regex::icase);
        uint64 copper = 0;
        std::string left;
        std::string::const_iterator from = text.begin();
        for (std::sregex_iterator it(text.begin(), text.end(), part), end; it != end; ++it)
        {
            uint64 const amount = std::stoull((*it)[2].str());
            char const unit = char(std::tolower((*it)[3].str()[0]));
            copper += unit == 'g' ? amount * 10000 : unit == 's' ? amount * 100 : amount;
            left.append(from, (*it)[0].first);
            from = (*it)[0].second;
        }
        left.append(from, text.end());
        size_t const first = left.find_first_not_of(' ');
        size_t const last = left.find_last_not_of(' ');
        rest = first == std::string::npos ? std::string() : left.substr(first, last - first + 1);
        return copper;
    }

    bool Initiate(Player* bot, Player* target)
    {
        WorldPacket data(CMSG_INITIATE_TRADE);
        WorldPackets::Trade::InitiateTrade packet(std::move(data));
        packet.Guid = target->GetGUID();
        bot->GetSession()->HandleInitiateTrade(packet);
        return bot->GetTradeData() != nullptr;
    }

    void Begin(Player* bot)
    {
        WorldPackets::Trade::NullCmsg packet{ Empty(CMSG_BEGIN_TRADE) };
        bot->GetSession()->HandleBeginTrade(packet);
    }

    void Accept(Player* bot)
    {
        WorldPacket data(CMSG_ACCEPT_TRADE);
        WorldPackets::Trade::AcceptTrade packet(std::move(data));
        if (TradeData* trade = bot->GetTradeData())
            if (TradeData* his = trade->GetTraderData())
                packet.StateIndex = his->GetServerStateIndex();
        bot->GetSession()->HandleAcceptTrade(packet);
    }

    void Cancel(Player* bot)
    {
        WorldPackets::Trade::NullCmsg packet{ Empty(CMSG_CANCEL_TRADE) };
        bot->GetSession()->HandleCancelTrade(packet);
    }

    void SetMoney(Player* bot, uint64 copper)
    {
        WorldPacket data(CMSG_SET_TRADE_GOLD);
        WorldPackets::Trade::SetTradeGold packet(std::move(data));
        packet.Coinage = std::min<uint64>(copper, bot->GetMoney());
        bot->GetSession()->HandleSetTradeGold(packet);
    }

    uint32 PutItems(Player* bot, std::vector<Item*> const& items, uint32 max)
    {
        TradeData* trade = bot->GetTradeData();
        if (!trade)
            return 0;

        uint32 put = 0;
        for (Item* item : items)
        {
            if (put >= max)
                break;
            if (trade->HasItem(item->GetGUID()) || !item->CanBeTraded(false, true) || item->IsBindedNotWith(trade->GetTrader()))
                continue;

            int8 freeSlot = -1;
            for (uint8 slot = 0; slot < TRADE_SLOT_TRADED_COUNT && freeSlot < 0; ++slot)
                if (!trade->GetItem(TradeSlots(slot)))
                    freeSlot = int8(slot);
            if (freeSlot < 0)
                break;

            WorldPacket data(CMSG_SET_TRADE_ITEM);
            WorldPackets::Trade::SetTradeItem packet(std::move(data));
            packet.TradeSlot = uint8(freeSlot);
            packet.PackSlot = item->GetBagSlot();
            packet.ItemSlotInPack = item->GetSlot();
            bot->GetSession()->HandleSetTradeItem(packet);
            if (trade->GetItem(TradeSlots(freeSlot)) == item)
                ++put;
        }
        return put;
    }
}

# mod-playerbots (LegionCore-Reforged 7.3.5)

Playerbots for **LegionCore-Reforged** (World of Warcraft Legion 7.3.5, build 26972): a port of the
[AzerothCore playerbots](https://github.com/mod-playerbots/mod-playerbots) (3.3.5), by way of
[SkyFire-playerbots](https://github.com/Brytenwally/SkyFire-playerbots) (5.4.8). The goal is the AzerothCore
module's feature set, as close to 1:1 as Legion allows, rewritten for this core (128-bit guids, map threads,
specializations, Legion spells and content). See [Comparison and roadmap](#comparison-with-azerothcore-playerbots).

Needs LegionCore-Reforged with its module system and bot hooks: bot sessions
(`WorldSession::CreateBotSession`), the whisper hook, and the dungeon finder / loot roll / kick vote accessors.

## Status

See the comparison below for what is verified, built and missing.

## GM commands

Set `Playerbots.Enable = 1` in `playerbots.conf` first.

| Command | What it does |
| --- | --- |
| `.playerbots add <name>` | Logs the character in as a bot (it must not be online). You get `Bot 'X' logged in.` once it is in the world. |
| `.playerbots remove <name>` | Logs the bot out (saved). |
| `.playerbots removeall` | Logs every bot out. |
| `.playerbots list` | Lists the bots and their state. |
| `.playerbots create <class> [spec]` | A bot of your faction at your level, into your party (see below). |
| `.playerbots self` | Your own character becomes a bot (again = off). GM only unless `Playerbots.Self.GMOnly = 0`. |

When you log out, the bots of your account (your alts) log out with you.

## Playing with a bot

- **Invite** the bot to your group: it accepts invites from the player who added it, or from a
  character of its own account, and declines others.
- In a group it **follows** you (or the group leader), on the same map.
- **Orders**: `follow`, `stay`, `summon` (teleports the bot to you, any map), `leave` (leaves the
  group), `quests` (quest log summary; `quests all`, `quests in` for quests in progress with
  their objectives, `quests co` for completed ones). Whisper them to a bot (wherever it is), or say them in party /
  raid chat to all your bots in the group at once. Bots only obey their owner: the player who
  added them, or a character of their own account. They confirm by whisper.
- **Quests**: bots in your group take the quests you take, and turn a quest in when you do, even
  if they did not finish its objectives themselves. Quests that exist once per class (most
  starting zones) are taken in the bot's own class version. They pick the best reward they can use. If
  they can't take or turn in a quest (log full, level, previous quest, bags full, daily limit),
  they whisper you why.
- **Quests by hand**, for class quests that are not copies of yours (Northshire's "Encrypted
  Letter"): stand by the quest giver and whisper `quests available` (what the bot can take or
  turn in there), `quest take <name>`, `quest complete <name>` (near the NPC that takes it back;
  it is turned in whatever the bot's own objectives). You can paste a quest link as the name.
- **Combat**: bots never pull. They fight what attacks the group (or, by mode, only you and
  themselves), within 30 yards, after a short delay so you swing first; tanks and melee chase the
  enemy, ranged stay at about 25 yards, healers stay with you and heal (the lowest ally below 90%,
  the tank first, a dying ally before anything else; they walk closer to someone out of reach and
  keep beacons / Prayer of Mending on the tank). Tank bots taunt and take whatever hits someone
  else in the group (`player tank` if you hold aggro yourself, `bot tank` to switch back). Each spec casts a simple
  priority list (all 36 specs, spell names looked up in the server's spell data). Orders:
  `attack` (your target, until it dies or you say `passive`), `passive` (stop fighting and fight
  nothing, not even what attacks the bot, until `attack` or `assist full|defend`), `assist
  full|defend|passive`; `follow` does not end a fight. A dead
  bot is revived by `summon` in party chat (`Playerbots.Summon.Revive`).
- **Strategies**: `co ?` / `nc ?` list a bot's combat / non-combat strategies; `co +aggressive`,
  `co -tank`, `nc +loot,-rest` change them (whisper or any group chat). Combat: `aggressive` (starts
  fights with nearby enemies up to 6 levels above it, only out of combat; in combat it helps with
  what already attacks the group), `passive`, `defend`, `tank`, `defensives`, `potions`. Non-combat:
  `follow`, `loot` (loots our kills after the fight; `Playerbots.Loot.MinQuality`), `rest`, `roll`,
  `autoequip`, `talents`. The older orders (`follow`, `stay`, `passive`, `assist ...`, `player tank`,
  `bot tank`) switch the same strategies. Defaults in `Playerbots.Strategies.*`.
- **Dead groups**: once a minute a bot checks its group; with no player online in it for two checks in
  a row it leaves (a random bot is reset and returns to GM Island).
- **Rotations**: 28 specs fight with SimulationCraft's T21 action lists and take their talents (built
  in; `.simc` files in `Playerbots.Rotation.Directory` replace them). The startup log tells, per spec,
  what could not be read. Most healers, Protection Warrior and Vengeance keep the module's own lists.
- **Formations**: `formation` shows the current one, `formation <name>` sets it: `role` (default:
  tank ahead at your side, melee rear left, ranged rear right, healer behind), `near`, `line`,
  `queue`, `circle`, `arrow`, `shield`, `chaos`, `far` (`Playerbots.Formation.Default`).
- **Dungeon finder**: bots in your party answer the role check with their spec's role and accept
  the dungeon. Queue alone or with a short party: 30 seconds later (`Playerbots.LFG.FillDelay`, so
  other players can queue first) idle bots from GM Island take the missing roles one by one, set up
  to your level in a spec of that role, and queue for the same dungeon (random or the ones you
  picked). Leave the queue and they go back to GM Island. When your dungeon finder group loses a member
  (a kick, someone leaving), a bot of the missing role joins it after 30 seconds: you invite it automatically
  and it teleports to you in the dungeon. Kick votes against a bot get every other bot's yes, and a bot
  leading a dungeon finder group hands the lead to you.
- **Battlegrounds**: bots in your party enter the battleground you queue the group for. Queue alone or
  with a party: 30 seconds later (`Playerbots.BG.FillDelay`) idle bots of both factions, set up to your
  level, fill both teams to the battleground's maximum (`Playerbots.BG.BotsPerTeam`) with 10% tanks, 20%
  healers and the rest dps. A player who queues later for a battleground the bots are filling takes a
  bot's place (a bot of his role leaves); a team left short (a refused invitation) gets a bot of the
  missing role. Inside, bots fight enemy players (the flag carrier first), carry and return flags in
  Warsong Gulch and Twin Peaks, take and guard bases in Arathi Basin, Battle for Gilneas and Deepwind
  Gorge, release and resurrect at the spirit healer, and leave when it is over. They walk along
  waypoint paths in Warsong Gulch, Arathi Basin, Alterac Valley, Eye of the Storm and Isle of Conquest
  and never take a path through walls.
- **Mounts**: a bot in your party mounts when you mount (a flier if you fly and its riding allows) and dismounts when
  you do or a fight starts, outdoors, if it has riding and a mount. Random bots always get the riding of their level and
  a racial mount when they are set up.
- **Self-bot**: `.playerbots self` lets the AI play your own character: it attacks your target (a
  hostile one, or one already fighting) and whatever attacks the group, moving to it and casting
  your spec's rotation, follows your group's leader and takes the same orders (whisper yourself or
  use party chat). Say it again to take control back.

- **Creating bots** (GM): `.playerbots create <class> [spec]` makes a new bot of your faction at
  your level, with a random (or the given) spec, talents and gear, brings it to you and into
  your party. It first uses a free bot of that class from the database (the startup pool,
  `Playerbots.Pool.*`: bot accounts and characters made at startup, some logged in on GM Island),
  and only creates a new character when there is none. Bots can be allied races (Void Elf, Lightforged
  Draenei, Nightborne, Highmountain Tauren) from level 10 (`Playerbots.Create.AlliedRaceLevel`).
  `Playerbots.DeleteBotAccounts = 1` deletes every bot account and its characters at the next startup
  (no pool that time); set it back to 0 and restart to build a new pool. Orders `talents` (show; `talents
  <spec>` switches), `maintenance` (talents by the spec's blueprint, riding skills and a racial mount for
  its level, level-adequate enchants and gems by the spec's stat weights, bags, repair; an alt's own enchants are kept)
  and `autogear` (upgrades for the spec by stat weights, each a random one of
  the 5 best for its slot, within `Playerbots.AutoGear.*` limits; empty bag slots get large bags
  first, replaced pieces go to the bags).
- **Loot**: in group loot bots roll need on gear that is at least a 5% upgrade for their spec
  (`Playerbots.Loot.NeedUpgradePct`), greed on the rest; `Playerbots.Loot.RandomBotsRoll = 0`
  leaves rolling to alt bots only (random bots pass). Any item that reaches a bot's bags and is at
  least a 5% upgrade is put on at once, or when the fight ends (`Playerbots.AutoEquip.*`; alt bots
  only with `Playerbots.AutoEquip.Alts = 1`). The bot whispers you what it equipped.
- **Dismissing a random bot** (it leaves or is removed from your group, or you log out for a
  minute): it leaves the group, loses everything it carries except its bags, goes back to level 1
  (death knights 55, demon hunters 98) and returns to GM Island for the next player. `.playerbots
  remove` resets it the same way before logging it out. Alt bots still just log out.

## Comparison with AzerothCore playerbots

Status as of 2026-11-07, build #32 plus `maintenance` and mounts (built after it). **Verified** = tested on the owner's server and working. **Built** = written
and compiling, waiting for its in-game test. **Missing** = not started; the roadmap below orders them. AzerothCore's
code is a 3.3.5 module with its own strategy / trigger / action / value engine; here the same behaviours are rewritten
directly for Legion, so the comparison is by feature, not by file.

### Verified

| Area | Feature | AzerothCore equivalent |
| --- | --- | --- |
| Bots | Log in / out any character as a bot (`.playerbots add / remove / removeall / list`); alts log out with their player | `.playerbots bot add/remove`, alt bots |
| Bots | Startup pool of bot accounts and characters (built a little per world update), idle on GM Island; `Playerbots.DeleteBotAccounts` wipe | RandomPlayerbotMgr pool, `DeleteRandomBotAccounts` |
| Bots | `.playerbots create <class> [spec]` (reuses pool bots, random among the eligible), allied races from level 10, starting zones marked done | `.playerbots bot addclass`, RandomPlayerbotFactory |
| Bots | Random bots: any player may invite a free one (set up to his level and summoned); dismissed bots are reset and freed; dead bot-only groups are left | random bot invites, rndbot reset |
| Group | Invite, follow, stay, summon across maps (revive by `summon` in party chat), leave | follow / stay / summon / leave |
| Group | Formations: role, near, line, queue, circle, arrow, shield, chaos, far | `formation` |
| Orders | Whisper and every group chat; owner only; `co` / `nc` strategies (`co ?`, `co +x,-y`) | chat commands, `co` / `nc` |
| Quests | Mirror the owner's accepts and turn-ins (class/race copies), `quests` lists, `quests available`, `quest take / complete` | quest sharing, `quests`, talk to quest giver |
| Combat | Never-pull targeting, assist modes, `attack`, `passive`, engage delay; `aggressive` pulls | attack / assist / passive, grind |
| Combat | Rotations for all 36 specs; 28 from SimulationCraft T21 action lists (with their talents) | class strategies |
| Combat | Healers (triage, beacons, dispels), tanks (taunts, `player tank`), interrupts, defensives, potions, eat and drink | heal / tank strategies, potions, food |
| Talents | Talent rows filled by spec builds (empty rows only, alts included), `talents [spec]` | `talents`, auto talents |
| Gear | `autogear` with stat weights (Ask Mr. Robot), item level brackets, hand layouts, bags | `autogear` |
| Loot | Loots kills (`nc loot`, minimum quality) | loot strategy |
| Dungeon finder | Party bots answer role checks and proposals; pool bots fill missing roles; lead passed to the player; bots vote yes to kick a bot | LFG |
| Self-bot | `.playerbots self`: the AI plays your character | self bot |

### Built, waiting for the in-game test

| Area | Feature | AzerothCore equivalent |
| --- | --- | --- |
| Dungeon finder | A group short of a member is filled by invite (the leader invites a bot of the missing role, it teleports in) | LFG replacement |
| Bots | Pool bots picked at random (allied races no longer always win) | random bot selection |
| Loot | Need / greed / pass rolls by upgrade; equip upgrades from the bags | `LootRoll`, auto-equip upgrades |
| Battlegrounds | Party bots enter with their leader; queue fill of both teams to capacity with a 10/20/70 role mix; bots give a player their place; top-ups | `BattleGroundJoin`, random bots in BG queues |
| Battlegrounds | PvP targeting, release and spirit healer, leave at the end | BG strategies, `ReleaseSpirit` |
| Battlegrounds | Tactics: Warsong Gulch / Twin Peaks flags; Arathi Basin / Battle for Gilneas / Deepwind Gorge bases; others go where the enemy is | `BGTactics` (WS, AB; AV / EotS / IoC partly) |
| Battlegrounds | Guarded pathfinding and AzerothCore's waypoint paths (WSG, AB, AV, EotS, IoC) | vmangos `BattleBotPath`s |
| Upkeep | `maintenance`: talents by blueprint, riding and a racial mount per tier, level-adequate enchants and gems by stat weights, bags, repair | `maintenance` (SkyFire `MaintainBot`) |
| Movement | Party bots mount and dismount with their leader (fliers when he flies); random bots set up with riding and a mount | `CheckMountState` |

### Missing (roadmap)

| Area | Feature | AzerothCore equivalent | Notes |
| --- | --- | --- | --- |
| Battlegrounds | Own tactics for Eye of the Storm (towers + flag), Alterac Valley (towers, captains, bosses), Isle of Conquest (vehicles, gates), Strand (demolishers) | `BGTactics` AV / EY / IC | |
| Battlegrounds | Legion battlegrounds' tactics: Silvershard Mines (carts), Temple of Kotmogu (orbs), Seething Shore (azerite) | none (Legion only) | |
| Battlegrounds | Waypoints for Twin Peaks, Battle for Gilneas, Silvershard, Kotmogu, Deepwind Gorge, Seething Shore | none | record in game only if pathfinding fails |
| PvP | Arenas: skirmish queue fill, arena tactics; rated arenas and teams; rated battlegrounds | `ArenaTactics`, arena teams | |
| PvP | Duels (accept), world PvP (attack enemy players outside battlegrounds) | `AcceptDuel`, attack enemy players | |
| Dungeons / raids | Boss strategies | ~25 dungeon and ~20 raid strategies (3.3.5 content) | Legion dungeons and raids need their own |
| Dungeons / raids | LFR / raid finder, scenarios, Mythic+ | LFG raids | |
| Combat | Ground-effect dodge and fleeing, crowd control, raid target icons (`rti`), focus heal targets, healers dealing damage when idle | avoid AoE, CC, `rti`, focus heal | |
| Combat | Hand-made lists for the 8 specs without a SimC profile (most healers, Protection Warrior, Vengeance) | class strategies | |
| Pets | Hunter taming and pet management, warlock pets beyond the precombat summon, pet talents | `Tame`, pet strategies | |
| Movement | Flight paths, following across maps on its own, flying follow tested | `Taxi` | |
| Items | Vendors (sell, buy, repair), bank, mail, trade with players, use / open / destroy items by order | `Sell`, `Buy`, `RepairAll`, `Bank`, `Mail`, `Trade` | |
| Items | Consumables (flasks, food buffs), bonus IDs and `Game.Patch` item level tiers, scaling enchants | maintenance consumables | |
| Legion | Artifact weapons and traits, relics, PvP talents, class halls | none | Legion only |
| Professions | Crafting, gathering, fishing | `SetCraft`, `Fishing`, gathering | |
| Social | Guilds (create, invite, guild bank), chat replies and emotes, `who`, `stats`, `reputation` and other tell commands, ready checks, resurrection acceptance | guild actions, `Say`, `Emote`, tell actions, `ReadyCheck`, `AcceptResurrect` | |
| Random bots | Open-world life: questing on their own, travel to level zones, grinding, RPG wandering, level spread and periodic refresh | RandomPlayerbotMgr, travel / RPG managers, RandomBotLevelMgr | **last priority** (owner's rule) |
| Tools | Performance monitor, command server | PerfMonitor, PlayerbotCommandServer | addon helpers are not planned (owner's rule: no server changes just for addons) |

### Roadmap

1. **Now**: test build #32 (battleground fill, tactics and movement, dungeon finder invites, random picks); fix what
   the logs show.
2. **PvP**: own tactics for Eye of the Storm, Alterac Valley and Isle of Conquest (from AzerothCore), then Silvershard,
   Kotmogu, Seething Shore and Strand; arenas (skirmish fill, then tactics), duels.
3. **Combat depth**: ground-effect dodge, crowd control, raid target icons, hand-made lists for the specs without a
   SimC profile, healer damage, pets and hunter taming.
4. **Dungeons and raids**: boss strategies for Legion dungeons, then raids; LFR and scenarios.
5. **Character upkeep**: flight paths, vendors / repair / bank / mail / trade, consumables, bonus IDs and `Game.Patch`
   tiers, artifact weapons and traits.
6. **Social and professions**: guilds, chat and emotes, tell commands, ready checks, professions.
7. **Last (owner's priority)**: random bots' own life in the open world (questing, travel, grinding, level spread).

## Install

Clone into the core's `modules/` folder under this exact name (the folder name decides the
loader function, `Addmod_playerbotsScripts`):

```sh
git clone https://github.com/Brytenwally/LegionCore-Playerbots.git modules/mod-playerbots
```

Re-run CMake and build. `playerbots.conf.dist` lands next to the worldserver; copy it to
`playerbots.conf` to change settings. Windows builds:
[LegionCore-Build](https://github.com/Brytenwally/LegionCore-Build) (`with_playerbots`).

# CLAUDE.md

Notes for Claude sessions working on LegionCore-Playerbots. **Keep this file up to date** with
anything future sessions need, in the same commit as the change.

## What this is

The playerbots module for **LegionCore-Reforged 7.3.5** (`Brytenwally/LegionCore-Reforged`),
built into the core from `modules/mod-playerbots`. The project-wide notes, rules and the step
plan live in the core's `CLAUDE.md`; read it first.

Ported from `Brytenwally/SkyFire-playerbots` (5.4.8; its `CLAUDE.md` documents every system),
which is a **read-only reference**, like `Brytenwally/mod-playerbots` (AzerothCore 3.3.5). Code
is read there and rewritten for Legion here.

## Git rules

- Commit and push straight to **`main`**. No feature branches or PRs unless the owner asks.
- Imperative subject, body explaining what and why. No model names in commits.

## Keep this file current (owner's rule, 2026-11-03)

Every time a problem is resolved or something new is learned about how the core, the database or
this module behaves, write it here **in the same commit as the fix**, without being asked: what
went wrong, the evidence that found it (log lines, data), and the rule that follows for future
code. Record each runtime test result the owner reports under "Status".

## Owner's priority

Random bots' open-world activity (questing on their own, level-appropriate zones, grinding,
wandering) is the **absolute last priority**: plan step 7 in the core's `CLAUDE.md`. When
porting from SkyFire-playerbots, leave its random-bot open-world systems (quest/grind
strategies, zone travel, random bot pool behaviour) for last; bots the player adds and leads
come first.

## How the module plugs into the core

- The core builds `modules/<folder>/src` as a script module; the loader is
  `Addmod_playerbotsScripts()` (`src/mod_playerbots_loader.cpp`). Every `src/` subfolder is an
  include directory.
- Config: `conf/playerbots.conf.dist`, loaded with `sConfigMgr->LoadModuleConfig(
  "playerbots.conf", error)` in `AddSC_playerbot_scripts()`; values read in
  `WorldScript::OnConfigLoad`. Keys use the `Playerbots.*` prefix (same names as SkyFire's
  where the feature matches). The file needs the `[worldserver]` header.
- Logging: `TC_LOG_INFO("server.loading", "[mod-playerbots] ...")` style (TrinityCore printf
  macros on this core, not SkyFire's `SF_LOG_*`).
- SQL: the core's updater does not run module SQL; create tables from code at startup.

## Bot lifecycle (plan step 2, done 2026-11-02)

- Core API (LegionCore-Reforged `WorldSession`): `CreateBotSession(accountId, accountName,
  SEC_PLAYER, expansion, locale)` -> `WorldSessionPtr` (socketless, `IsBot()`, not in
  `World::m_sessions`); `LoginBot(guid)` starts the login; `LogoutBot()` logs out (saved).
  The core answers logins/teleports for bots inside `WorldSession::Update`.
- `PlayerbotMgr` (`src/PlayerbotMgr.*`) holds the sessions (`std::map<ObjectGuid, Bot>`, world
  thread only) and, from `WorldScript::OnUpdate`, calls `session->Update(diff)` while
  `GetMap()` is null (as `World::UpdateSessions`); maps update them otherwise. `Update` returning
  false = session ended (logged out, or the login failed): the entry is dropped.
- "logged in" = player in world and `!PlayerLoading()`; then `Bot 'X' logged in.` is sent to
  the GM who added it (SkyFire wording, SquidBots Lite parses it).
- Owner's rule kept from SkyFire: a client's character logging out (`PlayerScript::OnLogout`,
  map thread -> mutex-protected account list) logs out every bot of the same account, so an
  alt is never left running while the player logs into it.
- GM commands: `.playerbots add <name>`, `remove <name>`, `removeall`, `list`. Need
  `Playerbots.Enable = 1`. Any character of any account; must not be online.

## Bot AI (plan step 3, in progress)

- `PlayerbotAI` (`src/PlayerbotAI.*`), one per bot, created by `PlayerbotMgr::Update` when the bot
  is in the world (`_ais`, mutex: written on the world thread, read from map threads via
  `GetAI`). Driven by `PlayerScript::OnUpdate` (bot's map thread); orders arrive from the
  sender's thread and are posted to the bot's (`ObjectAccessor::PostToPlayer`), so the AI only
  ever touches its bot on the bot's thread and knows the sender by guid + account.
- Master = the player who added the bot (`.playerbots add`); "master" checks also accept any
  player of the bot's own account (`IsMaster`).
- Group invite: answered with a queued `CMSG_PARTY_INVITE_RESPONSE` (int8 PartyIndex, bit
  Accept, bit HasRolesDesired) so the core's `AcceptPartyInvite` does the joining. Accepted
  from the master, declined from anyone else. Once per invite (`_answeredInvite`, cleared when
  no invite is pending), remembered by the `Group*` itself: an invite that creates a group gives
  a pending group with an **empty guid** until someone accepts (`HandlePartyInvite` ->
  `AddLeaderInvite`), so remembering by guid ignored every such invite (owner hit it 2026-11-02).
- Follow: every 500 ms, out of `stay`, in a group, alive: `MoveFollow(target, 2 yd, behind)`
  where target = master if in the group, else the group leader, same map only. Not yet: combat,
  mounts, following across maps (summon), formations.
- Orders, from the **owner only** (owner's rule, 2026-11-02; owner = `IsMaster`: the master or
  a player of the bot's account): `follow`, `stay`, `summon` (teleport to where the owner stood
  when he gave the order, any map; not on a flight; clears `stay`; a dead bot is revived first,
  see below), `leave` (queued
  `CMSG_LEAVE_GROUP`, int8 PartyIndex), `quests [all|in|incompleted|co|completed|summary]` (bare
  = summary; SkyFire's ListQuestsAction: titles in brackets, objectives "3/8 label" from
  `GetQuestObjectiveData(quest, StorageIndex)` / item counts, label = objective Description or
  creature/gameobject name, lines cut at ~230 chars). The sender is an `OrderSender` (guid,
  account, `WorldLocation`) read in his own thread by the hook, so the bot never touches
  another map's player.
  - Summon revive (owner's rule, 2026-11-03): a dead bot is revived (`ResurrectPlayer(1.0f)`,
    `SpawnCorpseBones`) only by `summon` **in party/raid chat** (`OrderSender::partyChat`), and
    while the owner is out of combat (`OrderSender::inCombat`, read in his thread) unless
    `Playerbots.Summon.Revive = 2` (0 = never). A whispered summon only tells how. Bots are never
    revived on their own.
  - Whisper: core hook `PlayerScript::OnWhisper` (receiver guid, any map). Unknown text gets
    the list of orders.
  - Party / raid / instance chat (`OnChat(..., Group*)`): sent to the sender's own bots
    (`PlayerbotMgr::GetBotsOwnedBy`); each bot checks on its thread that it is in that group.
    Unknown text is ignored (it is normal party talk). Replies go by whisper to the owner.
- Quests (owner's rule, as on SkyFire / AzerothCore): `PlayerScript::OnQuestAccept` /
  `OnQuestReward` of the owner -> posted to his bots; each acts only if it is in the owner's
  group (any map). Accept: `AddQuestAndCheckCompletion(quest, nullptr)` (no giver, so the
  giver's script hooks are not run twice) when `CanTakeQuest` + `CanAddQuest`, else a whispered
  reason. Turn-in: a bot that has the quest turns it in **whatever its own objectives**
  (`CompleteQuest` then `RewardQuest(quest, reward, bot)`; the bot stands in as the giver since
  the hook does not name the owner's); only bag space for the rewards and daily/weekly/seasonal
  limits stop it (whispered). Reward = best of the choice items + Legion quest package items
  (`QuestPackageItem.db2`, `CanSelectQuestPackageItem`, fallback list), SkyFire's scoring
  (usable, item level x1000, armor/weapon +5000, sell price). Bots' own accepts/turn-ins are not
  mirrored. Note: the core calls `OnQuestAccept(player, quest)` only for accepts with a quest
  giver (`AddQuestAndCheckCompletion` returns early without one).
  Every step is logged (`[mod-playerbots] <owner> accepted|turned in quest N: passed to n bot(s)`,
  then per bot `Bot 'X', quest N: accepted / turned in / ... skipped, <reason>`).
- **Class/race copies of quests** (found 2026-11-03 from those logs): this database has many
  quests once per class, e.g. Northshire's "Beating Them Back!" is quests 28757, 28762-28767,
  29078, 31139, each limited by `quest_template_addon.AllowableClasses` (128, 2, 16, 8, 256, 1, 4,
  0, 512) and leading to its own class's next quest. A bot of another class refused the owner's
  copy ("another class or race"). Now the bot takes **its own copy** (same `LogTitle` and
  `QuestSortID`, its class and race satisfied) and turns in the copy it has when the owner turns
  in his.
  - **Copies told apart only by their previous quest** (found 2026-11-03, owner's log: `quest
    28795: takes its own class/race copy, quest 28794 ... accept refused: I haven't done the quest
    before it`): Northshire's "They Sent Assassins" is 28791-28797, 29081, 31144; only 28795 has
    `AllowableClasses` (8, rogue), the others have 0 and differ by `PrevQuestID` (each follows one
    class's "Join the Battle!", 28780/28784/.../28788/...). Picking "the first copy the class
    allows" chose another class's chain. **Rule: among copies, choose the one the bot can take
    now (`CanTakeQuest`: previous quest done), class-specific before unrestricted**; same for
    `quest take` (`MatchQuest` prefers, among equal title matches, the quest the bot can take),
    since the NPC offers every copy. `quests available` was already right (lists only quests
    `CanTakeQuest` accepts).
  - Copy index (`LogTitle` + `QuestSortID` -> quest ids) built once from
    `sQuestDataStore->GetQuestTemplates()` (`std::call_once`).
- **Manual quest orders** (owner's request, 2026-11-03), for class versions that are not copies:
  `quests available` (the nearest quest giver within 30 yd: quests the bot can take there, and
  quests of its log it can turn in there), `quest take <name>` (from a quest giver within 30 yd,
  `AddQuestAndCheckCompletion(quest, giver)` so the giver's scripts run as for a client), `quest
  complete <name>` (a quest of its log, to its ender within 30 yd; same turn-in as mirroring).
  Quest givers = alive creatures / gameobjects whose entry has starter or ender relations
  (`sQuestDataStore->Get{Creature,GO}Quest[Involved]RelationBounds`), found with
  `GetCreatureListInGrid` and a `GameObjectListSearcher` cell visit, nearest first. Names: exact
  title or quest id first, then the first title containing the text; a pasted quest link works
  (text between [ ]). `TurnInQuest(bot, quest, giver, to)` is shared by mirroring and the order:
  limits, bag room, `CompleteQuest` if needed (owner's rule: objectives do not matter),
  `RewardQuest(quest, reward, giver or bot)`.

## Combat building blocks (`src/combat/`, from LegionBotAI; used by "Combat" below)

Owner's request (2026-11-03): copy LegionBotAI's generic functions as an empty structure before
writing combat, without its content (rotations, spell lists, gear sets, talent picks) and without
what conflicts with the module (its follow, map transfer by hand, settings table, level sync and
gear, looting, LFG, revive). Since 2026-11-05 `PlayerbotAI`'s combat uses `BotCast`, the targeting
helpers, `TryDispel` and `GetInterruptibleCast`; `BotTalents`, `BotMovement` and `UseBagItem` wait.
All run on the bot's map thread; teammates come from `GroupMembersOnMap` (group slots +
`ObjectAccessor::GetPlayer(*bot, guid)`, same map only) instead of LegionBotAI's `FindPlayer`.
- `BotCombat` (`BotCombat.h`): `RoleOf` (spec role), `BotCast` (what casting means: spell exists,
  level, learned, cooldown, GCD, not casting, `SpellThrottle` AI delay, face, `CastSpell` not
  triggered) returning a `CastRefusal` for the cast logs, `IsAttackingAlly` / `IsAggressorOf` /
  `FindAttackerOf` (never-pull rule), `CountInjured`, `FindLowestHpAlly` (tank first), `EngageTimer`
  (1.2 s engage delay), `GetInterruptibleCast` / `TryInterrupt`, `TryDispel` (types from the spell's
  dispel effects), `KeepAura`, `UseBagItem` (on-use spell cast with the item, so the core takes the
  charge; LegionBotAI destroyed the item by hand).
- `BotTalents`: `UnlockedRows`, `TalentInRow`, `TalentAt(tier, column)`, `LearnTalents` /
  `LearnTalentColumns`: only empty unlocked rows, never replaces a chosen talent, so it may run
  automatically on alts too (owner's decision below).
- `BotMovement`: role formation (tank 2.5 yd, dps 4 yd left/right, healer 9 yd behind the leader,
  ground Z), `MoveToFormation`, `CatchUp` (near teleport past 80 yd or 3 yd of height), `Engage`
  (attack + chase, melee or at range), `Disengage`.
- Owner's decisions for them (2026-11-03, details in the core's `CLAUDE.md`): no tank threat
  bonus; talents learned automatically on every bot, alts included, but only into empty rows.
- Compiled in the container against the core (scripts PCH), and on Windows in LegionCore-Build
  run #15 (core `77189b3` + `092a80c`), 2026-11-03.

## Combat (plan steps 4.1 + 4.2, 2026-11-05)

Built on Windows in LegionCore-Build run #17 (core `1da7859` + `8599f44`); **works on the owner's
server** (see Status, 2026-11-05).

- `PlayerbotAI::UpdateCombat` every 250 ms on the bot's thread (before follow; `UpdateFollow` does
  nothing while `_engaged` or casting, since movement would break a cast).
- Target (`SelectTarget`, LegionBotAI's rules): an `attack`-ordered target first (the owner's
  `GetSelection()`, read in his thread into `OrderSender::selection`; no delay, no range, no
  never-pull check); else, by `assist` mode (`full` default: any group member, the owner first;
  `defend`: the owner and the bot) the bot's current victim if still attacking an
  ally, the owner's victim if attacking an ally, or the first attacker of an ally
  (`FindAttackerOf`); in full/defend also self-defence (`getAttackerForHelper`). Only within
  `Playerbots.Combat.EngageRange` (30 yd) of the bot. A new enemy waits
  `Playerbots.Combat.EngageDelay` (1200 ms, `EngageTimer`).
- Engage: tank/melee `Attack(enemy, true)` + `MoveChase(enemy)`; ranged `Attack(enemy, false)` +
  `MoveChase(enemy, 25)` only while farther than 30 yd or out of sight, standing still (MoveIdle +
  `StopMoving`) once in reach; healers never engage (they follow and heal). In `stay`, no chase. No enemy
  left -> `StopCombat` (AttackStop, MoveIdle, follow again at once).
- Role (`BotRotations::RoleOf`): the rotation's role (tank/healer/melee/ranged); specs without a
  rotation fall back to the spec role and class.
- Rotation (`RunRotation`, AshamaneCore's `ProcessBotCombatAI`): interrupt (if the enemy casts
  something interruptible), dispel (`TryDispel` over the members), then the first step whose spell
  the bot knows, whose condition holds and that `BotCast` accepts; one cast per check. Healer target
  = `FindLowestHpAlly` below 90% (tank first below 75%), also out of combat.
- `src/combat/BotRotations.*`: AshamaneCore-Legion735's `g_BotSpecRotations` (36 specs) as data.
  Spell names are looked up once (`Initialize`, at `OnStartup`) in `sSpellMgr` by exact
  (lower-case) `SpellInfo::SpellName`; **a step casts the highest-id spell of that name the bot
  knows (`HasSpell`), its class family (`ClassOptions.SpellClassSet`) first**, instead of
  AshamaneCore's "highest id" guess. Aura conditions accept any spell of the aura's name (target
  auras only from the bot). Names no spell carries are logged at startup (`Rotations: no spell
  named ...`).
- Power conditions use displayed units: `GetPower / PowerType.db2 DisplayModifier`
  (`sDB2Manager.GetPowerType(enum)`): rage, runic power and soul shards are stored x10 on this core
  (`spell_warlock.cpp` divides soul shard costs by 10).
- Logs (`Playerbots.Combat.Log`): 1 = `Bot 'X' (role) engages Y.`, `Bot 'X' casts Spell (id) on Y.`,
  `Bot 'X' stops fighting: reason.`; 2 = also `Bot 'X' skips Spell: reason` (not known, condition
  not met: ..., on cooldown, global cooldown, AI delay, cast failed (result N) = `SpellCastResult`),
  logged when a step's reason changes.
- **Cast-time spells while moving** (owner's log, build #17, level 15 Affliction: `skips Unstable
  Affliction: cast failed (result 72)`, same for Drain Soul, while instants worked): 72 =
  `SPELL_FAILED_MOVING`. `Spell::prepare` refuses a cast-time / channelled spell when the player's
  movement flags say moving (`Unit::isMoving`, `m_movementInfo`; server splines set
  `MOVEMENTFLAG_FORWARD` and clear it only when the spline ends), and the ranged chase kept
  re-pathing. Fix: `BotCast` calls `StopMoving()` before a cast-time / channelled spell (movement
  generators then wait while casting, `IsMovementPreventedByCasting`), and ranged bots stop
  chasing once in reach. **Rule: a bot must stand still before any non-instant cast.**
- Owner's design rules (2026-11-05, after testing build #17): **`follow` does not interfere with
  a fight** (it no longer clears the ordered target; combat keeps the movement while engaged);
  **`passive`** (= `assist passive`) makes the bot passive towards enemies: it stops its fight at
  once, drops the ordered target and fights nothing, not even self-defence, until `attack` (an
  ordered target is still fought) or `assist full|defend`. Healing goes on.
- **Healers (plan step 4.3, 2026-11-05, untested):** heal target = `FindLowestHpAlly` below 90%
  (tank first below 75%, the bot itself below 90% when nobody else needs it), in and out of
  combat. **Triage** (LegionBotAI): a heal target below 40% gets the rotation first, before
  interrupt and dispel. A heal target beyond 35 yd or out of line of sight -> `MoveToHealTarget`
  (`MoveFollow(target, 15 yd)`, `_healMoveGuid`; `UpdateFollow` waits; cleared once in reach,
  healed, on `stay` or death). New step targets `Tank` (first alive member with the tank spec
  role, not the bot, else the owner) and `Owner`, used only while there is an enemy or a heal
  target; new conditions `InjuredNearTarget` / `InjuredNearSelf` (`value` allies below 85% within
  `radius`, new `RotationStep::radius`). Holy Priest and Holy Paladin now use LegionBotAI's lists
  (Guardian Spirit / Lay on Hands < 25%, Prayer of Mending / Beacon of Light on the tank, Beacon
  of Faith on the owner, Prayer of Healing / Light of Dawn for 3 injured in 15 yd, Serenity / Holy
  Shock < 75%, Flash Heal / Flash of Light < 55%, Renew, Bestow Faith < 85%, Heal / Holy Light,
  then Flash Heal / Flash of Light as low-level filler); Holy Priest's dispel is **Purify**
  (AshamaneCore had Dispel Magic, offensive on 7.3.5). Resto Shaman / Druid, Mistweaver and Disc
  keep AshamaneCore's lists. Not yet: healers dealing damage when nobody needs healing, mana /
  drinking (4.5), Light of Dawn's cone aims at the heal target only.
- **Tanks (plan step 4.4, 2026-11-05, untested):** a tank (spec role) not in `player tank` and not
  passive takes the first enemy hitting another ally (`FindLooseEnemy`: the owner's attackers first,
  then the members', whose victim is that ally, within the engage range; never a pull) at once, no
  engage delay, and taunts it (`TryTaunt`: `SpecRotation::taunt`, then `tauntAlt` if not ready;
  log `Bot 'X' taunts Y off Z (spell).`), then runs its rotation on it. No threat bonus (owner's
  decision). Taunts: Prot Warrior Taunt, Prot Paladin Hand of Reckoning, Blood Dark Command then
  Death Grip, Vengeance Torment, Brewmaster Provoke, Guardian Growl. Orders `player tank` (the
  owner holds aggro: tank bots neither taunt nor grab loose enemies; they fight like dps) and `bot
  tank` (default). Blood DK: LegionBotAI's defensives before AshamaneCore's list (Vampiric Blood
  < 35% and Icebound Fortitude < 30% on itself, Dancing Rune Weapon < 50%, Death Strike < 70%,
  Marrowrend below 5 Bone Shield stacks, Death Strike at 45 runic power, Heart Strike). New step
  target `Self` (only while fighting) and condition `AuraStacksBelowOnSelf` (`AuraStacksNamed`).
  Interrupts per spec were already in the table.
- Not yet: tank threat bonus (owner: no), defensives/potions (4.5), ground-effect dodge
  (AshamaneCore), pets, hunters' auto shot, talents (wired with the specs), formation.

## Created bots, talents and gear (2026-11-05, untested)

Owner's request: `.playerbots create <class> [spec]` for easier testing; a created bot is at the
creator's level, with a random spec (or the given one) and gear for it; the owner then uses the
`talents` and `autogear` orders; quality and item level limits in `playerbots.conf`.
- References: SkyFire-playerbots `CreateBotPopulation` / `CreateBotCharacter` (a core hook there;
  not needed here) and AzerothCore's RandomPlayerbotFactory: bot accounts by prefix, random race
  of the faction, generated names.
- `BotFactory` (`src/BotFactory.*`):
  - Accounts `<Playerbots.Create.AccountPrefix><n>` (default `PLAYERBOT1`...), created with
    `AccountMgr::CreateAccount(name, password, false)` (synchronous; this core's game accounts need
    no battle.net account), up to `CharactersPerAccount` (10) characters each
    (`AccountMgr::GetCharactersCount`). Such bots are not alts.
  - Character: as `WorldSession::HandleCharCreateOpcode` (`finalizeCharacterCreation`), on a bot
    session of that account: `Player newChar(session)`, `Create(generator, &info)`, `setCinematic(1)`,
    `AT_LOGIN_FIRST`, `SaveToDB(true)`, `realmcharacters` count, `OnPlayerCreate`,
    `sWorld->AddCharacterInfo` / `UpdateCharacterAccount` (so `GetPlayerGUIDByName` finds it at once),
    `CleanupsBeforeDelete`. Race: random among the creator's faction's races that can be the class
    (`GetPlayerInfo(race, class)`; allied races left out, pandaren by faction); random gender;
    generated name checked with `CheckPlayerName`, `CheckCharacterName` and the name cache.
  - `PlayerbotMgr::RequestCreate` (any thread, mutex) -> `ProcessCreates` on the world thread:
    create, then log in **3 s later** (`SaveToDB` is asynchronous; the login query must find the
    rows), with the request kept in `Bot::init`; once in the world it is posted to the bot's thread:
    `PlayerbotAI::InitializeNewBot` -> `BotFactory::InitializeNewBot`: `GiveLevel(creator's level)`
    (never lower than the class's start level), `ForceChangeTalentGroup(spec)`, `FillTalents`,
    `AutoGear` with `destroyReplaced` (starting gear), full health/mana, `SaveToDB`, `TeleportTo` the
    creator's position; the bot whispers a summary.
  - `FillTalents`: every empty unlocked row gets a talent; **no talent builds yet, so a random
    column** (`BotTalents::TalentAt`); rows with a talent are never touched. Runs at creation, on
    `talents <spec>`, and every 10 s out of combat for every bot (owner's decision: alts included,
    empty rows only).
- `BotGear` (`src/BotGear.*`): Ask Mr. Robot scales from the owner's Pawn files (`AskMrRobot.lua`,
  Pawn 2.0218), per spec id (Pawn's class/spec order mapped to ChrSpecialization ids: Monk 1/2/3 =
  268/270/269). Item value = Pawn's: sum of stat x weight with weapon min/max damage and armor;
  hybrid primaries count as the spec's best primary (`PawnCombineStats`); a piece with primary stats
  the spec weighs 0 is worth 0. Template stats at the base item level as `Item::GetItemStatValue`
  (`StatPercentEditor x GetRandomPropertyPoints x 0.0001`, else `ItemStatValue`), armor
  `GetArmor(ilvl)`, damage `GetDamage(ilvl)`.
  Candidates from `sObjectMgr->GetItemTemplateStore()` (no SQL, as SkyFire): armor/weapons,
  quality within `Playerbots.AutoGear.MinQuality..MaxQuality` (default 0..4; artifacts and
  heirlooms never), `MaxItemLevel` (0 = none), required level <= the bot's, no scaling items,
  `CanUseItem` (class, race, proficiency), **`IsUsableBySpecialization(spec, level)`** (Legion's
  ItemSpec mask on the template: the core's own "for this spec" test), body armor of the class's
  armor type (the lighter one while the skill is missing), names of test/NPC items skipped. Hands
  per spec (`HandsFor`): two-hander, one-hander + shield, dual wield, caster (staff or one-hander
  + held off-hand), caster with shield (Elemental/Resto Shaman), Titan's Grip (Fury, two
  two-handers when `CanTitanGrip`), ranged (BM/MM). Slot by slot, only upgrades over the worn
  piece's value (`Item::GetItemLevel`); rings/trinkets take the two best distinct items.
  Replaced pieces go to the bags (skipped and counted when they do not fit) or are destroyed for a
  new bot.
- Orders (owner only): `talents` (spec, talent per row, the class's specs), `talents <spec name or
  1-4>` (switch spec with `ForceChangeTalentGroup`, fill rows), `autogear` (not in combat; an order,
  so allowed on alts). GM command `.playerbots create <class> [spec]` (in game: level, faction and
  position come from the GM; class names incl. `dk`, `dh`, `demon hunter`).
- Not yet: talent builds per spec (owner offered to send them), gems/enchants, bonus IDs (item
  levels are the templates' base levels), artifact weapons (AshamaneCore's table, step 5),
  `Game.Patch` item level tiers.

## Status

- 2026-11-01: skeleton (loader, config, startup log line). Compiles and links into the
  Windows worldserver (LegionCore-Build run #3, which now includes this module by default).
  Verified on the owner's server 2026-11-02: `[mod-playerbots] Module loaded (disabled)`. 
- 2026-11-02: bot lifecycle (see above) on the core's bot sessions. **Verified on the owner's
  server** (build #5, core `b1befd3` + `c77aa64`): add, alt bot beside its player, summon, far
  teleport, list, logout with the account's player. 
- 2026-11-02: step 3 started: group invite, follow, follow/stay orders by whisper (any map) and
  party chat, owner only, quest accept/turn-in mirroring (see "Bot AI").
- 2026-11-03, owner's test of build #10 (core `d6cd726` + `68dc852`): **works**: invite (after
  the empty-guid fix), follow, stay/follow by whisper and by party chat, summon across maps,
  leave. **Failed**: quest accept and turn-in mirroring; the step logs (`d88e455`) showed
  `accept skipped, another class or race` -> class copies of quests (see "Bot AI"), fixed in
  `65dae51`. Non-owner orders/invites not tested yet (no second account).
- 2026-11-03, owner's test of build #12 (core `d6cd726` + `65dae51`): quest mirroring with class
  copies **works** (bot takes and turns in its own class's copy, follows its chain). Owner then
  found quests whose class versions are **not copies** (Northshire's "Encrypted Letter": other
  classes have different quests with different titles/objectives that unlock the same zone
  progression), which mirroring cannot match -> manual quest orders added (see "Bot AI").
- 2026-11-03, owner's test of build #13 (core `383be28` + `14c2715`): `quests available`, `quest
  take`, `quest complete` **work** (warlock bot did its "Tainted Letter" and "Join the Battle!" by
  order). "They Sent Assassins" was refused both mirrored and by `quest take` although `quests
  available` listed it: wrong copy chosen (previous-quest copies, see "Bot AI"); fixed.
- 2026-11-03, owner's test of build #14 (core `66e10fd` + `d935051`): **everything works**
  (previous-quest copies taken mirrored and by order). Step 3's group/orders/quests part is done;
  next: basic combat (owner: from LegionBotAI, see the core's `CLAUDE.md`).
- 2026-11-05, owner's test of build #17 (core `1da7859` + `8599f44`), Affliction warlock bot at
  levels 5 and 10, `attack` order and `assist full`: **combat and rotations work** (engages after
  the delay, casts Agony 980 / Corruption 172, re-applies them when they drop, stops when no enemy
  is left; never-pull respected). The lists are basic: Affliction idles between DoTs at low level
  (Unstable Affliction / Drain Soul not known yet, Shadow Bolt not in its list); refining the
  rotations comes later, the owner wants systems first. Startup: `Rotations: 36 specs, 169 spell
  names, 2 not found`: `demonic core` (Demonology's proc aura has another name on 7.3.5) and
  `wildfire bomb` (a Battle for Azeroth spell, not in 7.3.5: Survival's first step never fires).

## Checking

Linux syntax check against the core: see "Building" in the core's `CLAUDE.md` (clone this repo
into the core's `modules/mod-playerbots`, configure, `make` single objects in
`src/server/scripts`). Windows: LegionCore-Build with `with_playerbots` on. No server or
database in the cloud container: runtime behaviour is untested there.

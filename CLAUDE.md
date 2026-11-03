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
  when he gave the order, any map; not dead or on a flight; clears `stay`), `leave` (queued
  `CMSG_LEAVE_GROUP`, int8 PartyIndex), `quests [all|in|incompleted|co|completed|summary]` (bare
  = summary; SkyFire's ListQuestsAction: titles in brackets, objectives "3/8 label" from
  `GetQuestObjectiveData(quest, StorageIndex)` / item counts, label = objective Description or
  creature/gameobject name, lines cut at ~230 chars). The sender is an `OrderSender` (guid,
  account, `WorldLocation`) read in his own thread by the hook, so the bot never touches
  another map's player.
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
  `QuestSortID`, its class and race satisfied, a class-specific copy before an unrestricted one)
  and turns in the copy it has when the owner turns in his. Index built once from
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
  progression), which mirroring cannot match -> manual quest orders added (see "Bot AI"),
  compile on Linux, Windows build and test pending.

## Checking

Linux syntax check against the core: see "Building" in the core's `CLAUDE.md` (clone this repo
into the core's `modules/mod-playerbots`, configure, `make` single objects in
`src/server/scripts`). Windows: LegionCore-Build with `with_playerbots` on. No server or
database in the cloud container: runtime behaviour is untested there.

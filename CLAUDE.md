# CLAUDE.md

Notes for Claude sessions working on DemonCore-Playerbots. **Keep this file up to date** with
anything future sessions need, in the same commit as the change.

## Naming (owner, 2026-11-12)

The project is **DemonCore** (GitHub organization `DemonCore`): this module is `DemonCore/DemonCore-Playerbots` (was
`Brytenwally/LegionCore-Playerbots`), the core `DemonCore/DemonCore` (was `Brytenwally/LegionCore-Reforged`), the build
`DemonCore/DemonCore-Build` (was `Brytenwally/LegionCore-Build`). Mentions of the **LegionCore-7.3.5** fork (LegionBotAI) and of
upstream LegionCore - Reforged keep their names: they are other projects. The module folder and loader stay `mod-playerbots` /
`Addmod_playerbotsScripts`.

## What this is

The playerbots module for **DemonCore 7.3.5** (`DemonCore/DemonCore`),
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

## Keep README.md current (owner's rule, 2026-11-07)

`README.md` has a **comparison with AzerothCore playerbots** (Verified / Built, waiting for the test / Missing) and a
roadmap toward a 1:1 feature set. Update it in the same commit whenever a feature is added (Missing -> Built), the owner
reports a test result (Built -> Verified, or back with the problem), or the plan changes; bump its "Status as of" line
with the date and build.

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

- Core API (DemonCore `WorldSession`): `CreateBotSession(accountId, accountName,
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
  map thread -> mutex-protected list of (account, guid)) logs out every bot of the same account
  **at once**, so an alt is never left running while the player logs into it.
- Owner's rules (2026-11-05) for logging bots out:
  - The player logs out or disconnects: his **alt bots** (his account) log out at once (above);
    the **bot-account bots he leads** (`Bot::notifyGuid` = him) log out **after 60 s**
    (`Bot::ownerGoneTimer`, `OwnerGoneLogoutDelay`), cancelled if he is back in the world by then
    (`ObjectAccessor::FindPlayer`, existence only).
  - He **leaves the group or removes the bot** from it: the bot (alt or bot-account) logs out **3 s
    later** (`PlayerbotAI::CheckOwnerGroup`: once the bot has been in a group with its master,
    `group->IsMember(_master)` turning false starts `_groupLogoutTimer`; back in his group cancels).
    Not when the bot left by the `leave` order (`_leftByOrder`). A logged-out owner stays a group
    member, so that case is the 60 s rule, not this one. The bot calls `LogoutBot()` itself (a flag;
    the session logs out on its own thread and the manager drops the entry).
  - **Changed for random bots (owner, 2026-11-05): dismissed, not logged out.** In both cases above (and
    when a dungeon finder fill is no longer needed) a bot-account bot runs `PlayerbotAI::Dismiss`: queued
    `CMSG_LEAVE_GROUP` if grouped, `BotFactory::ResetBot` (revive, `BotGear::ClearInventory`: every item
    worn, in the backpack and in the bags destroyed, the bags in the bag slots kept; `GiveLevel` down to 1,
    death knight 55, demon hunter 98; XP 0; `SaveToDB`; teleport to GM Island), then `RequestRelease` (world
    thread: no master, new masterless AI). `_dismissed` stops the old AI until it is replaced. Alt bots keep
    the logout. `.playerbots remove` / `removeall` reset a random bot (no teleport) before logging it out
    (`PlayerbotMgr::LogoutAndReset`), so the stored character goes back to the pool clean.
  - **Dismissed bots stayed in their group** (owner, build #24: "x is already part of a group" when invited
    again): `Dismiss` queued `CMSG_LEAVE_GROUP` and teleported to GM Island in the same tick, so the packet was
    handled while the bot was between maps and lost. Now `FinishDismiss` (each update while `_dismissed`)
    re-queues the leave every 3 s (5 tries) and resets / teleports / releases only once `GetGroup()` is null.
    **Rule: a queued client packet is not done yet; never follow it in the same tick with a far teleport.**
    After two lost tries `FinishDismiss` calls `Player::RemoveFromGroup(GROUP_REMOVEMETHOD_LEAVE)` itself (what
    `HandleLeaveGroup` calls; the bot's own thread, as the handler).
  - **Bots stuck in dead groups** (owner, build #26: bots of earlier builds stayed in bot-only groups): the
    masterless leave rule sent its leave once (`_leaveQueued`) and never again. Now re-tried every 5 s, directly
    (`RemoveFromGroup`). And `CheckPlayerlessGroup` (every 60 s, cheap: member list + `PlayerbotMgr::IsBot` +
    an existence lookup per member): two checks in a row with no online non-bot player in the group (a player
    has a minute to come back) -> a random bot is dismissed, an alt leaves.
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
- Compiled in the container against the core (scripts PCH), and on Windows in DemonCore-Build
  run #15 (core `77189b3` + `092a80c`), 2026-11-03.

## Combat (plan steps 4.1 + 4.2, 2026-11-05)

Built on Windows in DemonCore-Build run #17 (core `1da7859` + `8599f44`); **works on the owner's
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
  **That fix was not enough** (owner's log, build #20: tank / healer / warlock party, still result 72
  on Unstable Affliction, Drain Soul and the healer's Flash Heal, plus 97 and 115, and 153 for the
  tank). Real cause: `BotCast` faced its target with `Unit::SetFacingToObject` -> `SetFacingTo`,
  which **launches a spline** (`MoveSplineInit::MoveTo` on the spot + `SetFacing`), and
  `MoveSplineInit::Launch` sets `MOVEMENTFLAG_FORWARD`: the bot "moves" right before every cast,
  so every cast-time spell fails 72; and `SetFacingToObject` does nothing while the bot moves
  (`!IsStopped()`), hence 153 `SPELL_FAILED_UNIT_NOT_INFRONT` for the chasing tank. Fix in
  `BotCast`: set the server orientation directly (`SetOrientation(GetAngle(target))`, what
  `Spell::CheckCast` reads), tell clients with `SetFacingTo` only when standing, and for cast-time /
  channelled spells `StopMoving()` and then `RemoveUnitMovementFlag(MOVEMENTFLAG_MASK_MOVING)` just
  before `CastSpell`. **Rule: never let a facing spline precede a cast; clear the moving flags last.**
  Also new refusals checked before casting: `NoCharges` (`Player::HasChargesForSpell`; charge spells
  like Judgment / Holy Word: Serenity have no cooldown while recharging -> result 97
  `SPELL_FAILED_NO_CHARGES_REMAIN`), `OutOfRange` (`GetMaxRange(friendly, bot)`; melee-range spells,
  `GetMisc()->RangeEntry->Flags & SPELL_RANGE_MELEE`, by `IsWithinMeleeRange`; others
  `IsWithinDistInMap`) and `NoLineOfSight`, so a chasing melee bot no longer logs 115
  (`SPELL_FAILED_OUT_OF_RANGE`) every check. Results seen so far: 72 moving, 97 no charges, 115 out
  of range, 153 not in front.
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
- **Survival (plan step 4.5, 2026-11-05, untested):**
  - **Defensives** (`BotRotations::Defensives`, `DefensivesFor(class, spec)`; names in the startup
    name index): in combat, before the rotation, the first defensive whose `belowPct` the bot's
    health has reached, whose spell it knows and whose aura it does not already have (self ones);
    log `Bot 'X' uses <spell> at N% health.` Per class: Warrior Die by the Sword / Enraged
    Regeneration / Shield Wall + Last Stand; Paladin Divine Shield (Holy, Ret), Lay on Hands (Prot,
    Ret; Holy has it in its rotation), Ardent Defender + Guardian of Ancient Kings, Divine
    Protection, Shield of Vengeance; Hunter Aspect of the Turtle, Exhilaration; Rogue Cloak of
    Shadows, Evasion / Riposte, Crimson Vial; Priest Dispersion, Desperate Prayer; DK Icebound
    Fortitude (Frost, Unholy; Blood's are in its rotation); Shaman Astral Shift; Mage Ice Block and
    each spec's barrier (< 80%); Warlock Unending Resolve, Drain Life (on the enemy); Monk
    Fortifying Brew, Touch of Karma; Druid Barkskin, Survival Instincts, Frenzied Regeneration;
    DH Blur, Metamorphosis, Fiery Brand. `Playerbots.Combat.Defensives`.
  - **Potions** (`BotConsumables::UsePotion`): from the bags only, any item whose on-use spell has
    `SPELL_EFFECT_HEAL` (potions, healthstones) or `SPELL_EFFECT_ENERGIZE` with `POWER_MANA`,
    highest item level the bot can use, cast with the item (`UseBagItem`: the core takes the charge
    and starts the potion cooldown). At `Playerbots.Combat.PotionHealthPct` (30) / `PotionManaPct`
    (20). **Alts only with `Playerbots.Combat.PotionsOnAlts = 1`** (their items; owner's alt rule).
  - **Rest** (`PlayerbotAI::UpdateRest`, SkyFire's `CastRefreshmentSpell`): out of combat (nobody in
    the group fighting, no enemy), below `Playerbots.Rest.HealthPct` (60) / `ManaPct` (50): stop,
    sit, `AddAura` of SkyFire's level-tier food and drink spells (433..104934 / 430..104262; no
    item) up to level 85; **above 85 (owner: scale it to 110)** the spell comes from the server's
    own items (`BuildRestIndex`, once: Food & Drink consumables whose on-use spell has
    `AURA_INTERRUPT_FLAG_NOT_SEATED` and an `OBS_MOD_HEALTH` / `MOD_REGEN` aura = food, an
    `OBS_MOD_POWER` / `MOD_POWER_REGEN` mana aura = drink; the highest required level at or below
    the bot's, then the highest item level; SkyFire's tier if none). Logged once: `Rest: N food
    and M drink spells from items.` Stands up (auras removed) when
    full, when a fight starts or when its leader is more than 20 yd away; follow and combat wait
    while `_resting`.
- Not yet: tank threat bonus (owner: no), ground-effect dodge
  (AshamaneCore), pets, hunters' auto shot (talents and formations: see their sections).

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
    `PlayerbotAI::InitializeNewBot` -> `BotFactory::InitializeNewBot`: `GiveLevel(creator's level)`, `ForceChangeTalentGroup(spec)`, `FillTalents`,
    `AutoGear` with `destroyReplaced` (starting gear), full health/mana, `SaveToDB`, `TeleportTo` the
    creator's position; the bot whispers a summary.
  - Owner's rule (2026-11-05): **hero classes are created only from their starting level**:
    `.playerbots create` refuses a Death Knight below level 55 and a Demon Hunter below 98.
  - `FillTalents`: every empty unlocked row gets the spec's build talent (`TalentBuilds`, from the
    owner's `Talents.txt`: Legion leveling guides, one talent per row; names matched against the
    spells of the row's talents for the class/spec, so columns do not matter). Monk rows in the file
    only describe changes, so those picks come from that text; Holy Paladin takes the file's dungeon
    picks where given and Beacon of the Lightbringer (Beacon of Virtue would replace the rotation's
    Beacon of Light). **No build yet for Warriors and Demon Hunters** (not in the file): random
    column. A build name missing from its row on this core is logged once (`Talent build of spec N:
    no talent 'X' in row R`) and the row gets a random talent; rows with a talent are never touched. Runs at creation, on
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
- **Owner's test of build #21 (2026-11-05):** `.playerbots create` and `talents` **work**; gear is
  right for the spec, but **autogear took item level 840 quest rewards on a level 15 bot**: many
  quest rewards have no required level, so the required-level filter let them through. Fix:
  an item level cap by bot level (`ItemLevelCapFor`), on top of `Playerbots.AutoGear.MaxItemLevel`.
  **Rule: never trust required level alone for gear.** LegionBotAI's caps (x2+6 / x3 / x5 / x8) were
  still too generous (owner, build #22): now the owner's brackets, configurable
  (`Playerbots.AutoGear.IlvlCap.*`, `BotGear::Options`): levels 1-59 level + 6, 60-69 x1.3, 70-79
  x2, 80-84 x2.8, 85-89 x4.6, 90-99 x5.8, 100-109 x7, 110 only `MaxItemLevel`.
- **Autogear skipped the head slot** (owner's report, 2026-11-05): candidates were bucketed by "kind" = the
  equipment slot, and `0` meant "no kind" (`if (!kind) continue;`), but `EQUIPMENT_SLOT_HEAD` is 0: no
  helmet was ever a candidate. Kinds are now slot + 1 (`KIND_NONE` = 0). **Rule: never use a slot number
  (head = 0) as a "found" flag.** Also asked then: autogear first puts a large bag in each empty bag slot
  (`BotGear::AddBags`, `Playerbots.AutoGear.BagEntry`, 0 = Hexweave Bag 114821, else Royal Satchel 82446,
  Embersilk 54443, Frostweave 41599, the first that is a bag on the server), so replaced pieces always find
  room (the backpack alone filled after two autogears); each slot takes a **random one of its best
  `Playerbots.AutoGear.Choices` (5) upgrades** over the worn piece (fewer when fewer exist): `pick` among the
  kind's top list; rings / trinkets exclude the item in the other slot; weapons: the layout (two-hander,
  main + off...) is chosen by the best candidates, then a random main and off among that layout's top, or
  the best pair if the random pair does not beat what is held. Replaced pieces go to the bags (destroyed
  only at a new bot's initialization).
- **Hand layout wins over raw score** (owner, build #24: a prot warrior / prot paladin / enhancement shaman
  sometimes kept a two-hander, which out-scores one-hander + shield / two one-handers): `WornHandsFit(bot,
  hands)`; a held layout the spec does not use (two-hander for OneHandShield / DualWield, one-hander for
  TwoHand, melee for Ranged) counts as nothing held, in `AutoGear` (`wornHands` = 0) and `CompareToWorn`
  (weapon slots' old = 0), so the spec's own weapons always replace it. "Sometimes": pool bots reused in
  another spec kept their old two-hander.
- **Item level brackets** (owner, build #26: about 1 bot in 10 kept a near end-game piece at a low level, and a
  level 80 bot kept gear 10 levels below with "nothing better found"; the level x factor formula gave odd caps,
  158 at 79 but 224 at 80). From 60, one item level per bracket of 5 levels (`Options::brackets`,
  `Playerbots.AutoGear.IlvlCap.60` ... `.105`; defaults 100, 115, 165, 200, 285, 400, 560, 630, 735, 820, an
  estimate of each range's dungeon / quest gear, the owner tunes them); below 60 level + 6; `MaxItemLevel`
  caps every level (`ItemLevelCap`). The cap now applies everywhere an item is put on: autogear, equipping from
  the bags and need rolls (`CompareToWorn` refuses items above it); a worn piece above it counts as nothing,
  so any allowed piece replaces it (the strong pieces came in through bags / quest rewards / loot, which had
  no cap). "Nothing better found" now says how many items were allowed and the cap. The level 80 case is not
  explained yet: that message's numbers will tell (0 allowed items = filters, many = the worn pieces' score).
- **Owner's requests after that test (2026-11-05):**
  - A created bot **joins its creator's party**: 3 s after its setup, `PlayerbotAI::SendCreatorInvite`
    queues `CMSG_PARTY_INVITE` on the creator's session (`sWorld->FindSession(creatorAccount)`,
    checked to still hold the creator; `PartyInviteClient::Read`: int8 PartyIndex, int32
    ProposedRoles, packed guid, 9 bits name length, 9 bits realm length, name, realm), so the
    core's `HandlePartyInvite` runs on his thread as for `/invite`; the bot accepts it like any
    master's invite. `CreateRequest::creatorAccount` is read in the GM's thread.
  - **Startup pool**: `PlayerbotMgr::UpdatePool` (`Playerbots.Enable` only): accounts
    `<prefix>1..Playerbots.Pool.Accounts` and bot characters up to `Playerbots.Pool.Characters`
    (counted over all `<prefix>%` accounts; classes in turn, factions alternating, starting level);
    5 s later `Playerbots.Pool.LoginAtStartup` offline ones log in with no master and are teleported
    to GM Island (map 1, 16222.1 16252.1 12.59). The owner's defaults (his edit of the conf.dist,
    2026-11-05): `Playerbots.Enable = 1`, 100 accounts, 600 characters, 50 logins. **Built in small
    steps per world update** (`PoolStage`: up to 20 account checks but one new account per update,
    since `AccountMgr::CreateAccount` hashes the password with Argon2; 2 characters per update
    with the per-account counts cached (`CharacterCounts`, one query) instead of 2 queries per
    account per character; 5 logins per update): built in one go, those values would hold the
    world thread for minutes (`MaxCoreStuckTime` kills a frozen server when enabled). **Rule: never
    do unbounded synchronous DB / hashing work in one world update.**
  - **`.playerbots create` reuses before creating** (`ReuseBot`): first a logged-in bot of a bot
    account with no master (the pool), of the class, the creator's faction
    (`Player::TeamForRace`) and at most his level, highest level first, read from
    `sWorld->GetCharacterInfo` (not the player, which lives on another thread); it gets a **new
    `PlayerbotAI`** whose master is the creator (`_master` is constant and read from any thread),
    and the setup is posted to its thread. Else an offline bot character (`OfflineBots`: `SELECT
    ... FROM characters WHERE online = 0 AND account IN (bot accounts) AND class = ?`), logged in
    with the setup pending. Only when neither exists is a new character made. Bot accounts =
    `SELECT id FROM account WHERE username LIKE '<PREFIX>%'` (prefix reduced to letters/digits).
- **Starting zones** (owner's rule, 2026-11-05): a random bot is initialized as having done its
  race's own starting zone (worgen: Gilneas, Gilneas City; goblin: Kezan, The Lost Isles; pandaren:
  The Wandering Isle) and its hero class's (death knight: Plaguelands: The Scarlet Enclave; demon
  hunter: Mardum, the Shattered Abyss and Vault of the Wardens). `BotFactory::CompleteStartingZones`,
  first in `InitializeNewBot` (created and reused bots, always bot accounts): zones by `AreaTable`
  `AreaName` (all ids of those names; a quest's `QuestSortID` > 0 is its zone id), then every quest
  of them that is not repeatable/daily/weekly/seasonal and whose class/race the bot satisfies
  (`SatisfyQuestClass/Race`): removed from the log if there (`RemoveActiveQuest`),
  `SetRewardedQuest` (saved with `SaveToDB`), and the `SPELL_EFFECT_LEARN_SPELL` effects of its
  `RewardSpell` / `RewardDisplaySpell` learned (no XP, items or scripts); then
  `UpdateAreaDependentAuras`. Logged: `starting zone done (N quests marked complete)`, or an error
  if no AreaTable entry has those names.
- **Deleting the bot accounts at startup** (owner, 2026-11-06; SkyFire's `Playerbots.DeleteRandomBotAccounts` =
  AzerothCore's `AiPlayerbot.DeleteRandomBotAccounts`): `Playerbots.DeleteBotAccounts = 1` ->
  `PlayerbotMgr::DeleteBotAccountsAtStartup` (`OnStartup`, before any pool work): SkyFire's prefix guards (at
  least 3 characters, not ADMIN / GM / PLAYER / ACCOUNT / TEST), then for each `<prefix>%` account its characters
  by `Player::DeleteFromDB(guid, account, false, deleteFinally = true)` and the account by
  `AccountMgr::DeleteAccount` (which alone would follow `CharDelete.Method` and might only mark the characters
  deleted). No pool this boot (`PoolStage::Done`, as both references): the deletes are queued, and recreating
  accounts by the same names at once could meet the old rows. AzerothCore's version also deletes rows of ~40
  tables by hand (its own playerbots tables, orphans); here `DeleteFromDB` already removes each character's data.
- **Allied races** (owner, 2026-11-06): bot accounts may create them and they start at level 10
  (`Playerbots.Create.AlliedRaceLevel`). This core gates allied race creation only by the account's expansion
  (`CharacterHandler`: `IsAlliedRacesCreationAllowed = GetAccountExpansion() >= EXPANSION_LEGION`; the
  achievement check in `HandleCharCreateOpcode` is commented out); `account.expansion` defaults to 6 (Legion)
  and `AccountMgr::CreateAccount` does not set it, so new bot accounts already qualify; `FlagBotAccounts` (pool
  start) raises older bot accounts to 6. `RacesOf` now includes Void Elf / Lightforged Draenei (Alliance) and
  Nightborne / Highmountain Tauren (Horde); `CreateCharacter` takes an allied race only for a pool character
  (`CreateRequest::pool`) or a creator at or above that level, and sets it to that level (`SetLevel`, stats at
  login); `ResetBot` resets it there (`StartLevelFor`: 1, allied level, DK 55, DH 98). A race/class pair without
  a `playercreateinfo` row is never picked (`GetPlayerInfo`).
- **Random bots and masters** (owner's rules, 2026-11-05). Random bot = a bot of a bot account
  (`PlayerbotMgr::IsBotAccountName`, account name starts with the prefix; `PlayerbotAI::_randomBot`).
  - `.playerbots create` never takes a bot that has a master (`ReuseBot`: `notifyGuid` empty only).
  - A random bot in a party **without its master** (a party of bots its leader left) **leaves it**
    at once (`CheckOwnerGroup`: queued `CMSG_LEAVE_GROUP`, `_leaveQueued`), so it is free again; a
    bot whose master left still logs out 3 s later (rule above).
  - **Invites**: a random bot with no master accepts **any player's** invite (`HandleGroupInvite`;
    the inviter, possibly on another map, is read with `ObjectAccessor::WithPlayer`: level, team,
    position, account; bots are refused), keeps the inviter as `_pendingMaster` (so the masterless
    leave rule does not pull it out of his party meanwhile) and calls
    `PlayerbotMgr::RequestAssign`: on the world thread the inviter becomes `notifyGuid` and the
    master of a **new `PlayerbotAI`**, whose `InitializeNewBot` levels it to him, sets spec /
    talents / gear and teleports it to him (as `.playerbots create`). A random bot with a master
    declines other players and whispers **"I already have a master, try another bot."** Alt bots
    keep accepting only their master / account.
- Not yet: Warrior and Demon Hunter talent builds, gems/enchants, bonus IDs (item
  levels are the templates' base levels), artifact weapons (AshamaneCore's table, step 5),
  `Game.Patch` item level tiers.

## Self-bot, formations, dungeon finder (2026-11-05, untested)

Owner's request: `.playerbots self`, dungeon finder (RDF) support, formations from SkyFire / mod-playerbots.
- **Self-bot** (`.playerbots self`, toggles; `Playerbots.Self.GMOnly` 1 = GM only, checked in the handler:
  the command is `SEC_PLAYER` and so is the `playerbots` parent, the other subcommands stay
  `SEC_GAMEMASTER`; this core checks each subcommand's level on its own). `PlayerbotMgr::ToggleSelfBot` (the
  player's thread) keeps a `PlayerbotAI` with master = the player and `SetSelfBot()` in `_selfAis` (by player
  guid, `_aiLock`; `_selfCount` atomic skips the lookup in every client update while 0). Run from
  `PlayerScript::OnUpdate` for clients (`OnClientUpdate`). `GetAI` finds it, so whispers to yourself and party
  chat reach it (`GetBotsOwnedBy` includes it; quest mirroring skips it). In self mode `Update` only runs
  combat and follow: no invite answers, owner/group logout rules, talent fill, rest or dungeon finder
  answers. Its target = the player's selection when hostile or already in combat, as an ordered target
  (no engage delay), then the usual assist rules; follow targets the group leader (its master is itself).
  Replies to itself are system messages. Off: `Detach` (AttackStop, MoveIdle). Removed on logout.
  SkyFire's self-bot only cast and left movement to the player; here the owner asked for movement too:
  server movement (MoveChase / MoveFollow splines) on a client-controlled character, **untested** (keys
  pressed by the player override it).
- **Formations** (`src/combat/BotFormation.*`): a slot = distance + angle off the anchor's facing, given to
  `MoveFollow`, so the core keeps it while the leader walks. Followers sorted by guid counter so every bot
  computes the same places; roles from `BotRotations::RoleOf` (players too). Shapes: `role` (SkyFire's
  `BotFormation`: tank fanned at 0.40 rad ahead-side 2 yd, healer behind 3.5 yd, ranged rear right 5 yd,
  melee rear left 2.5 yd, +0.6 yd per same-role index), and mod-playerbots' `near`, `line` (leader in the
  middle), `queue` (each follows the one ahead), `circle`, `arrow` (rows tank +2.5 / melee -2.5 / ranged -5.5
  / healer -8.5), `shield` (tanks +3, others -3), `chaos` (near + random offset every 3 s), `far` (10 yd).
  `UpdateFollow` re-issues the follow when the anchor or the slot changes (> 0.3 yd / 0.15 rad). Order
  `formation [name]`; default `Playerbots.Formation.Default`.
- **Follow is out of combat only** (owner, build #24: healers kept running after the player in fights):
  `UpdateCombat` sets `_groupInFight` (an enemy, or a group member on the map in combat); `UpdateFollow` then
  stops following and leaves the movement to combat (chase, heal range) until the fight is over.
- **Group chat** (owner, build #24: "all kinds of group chat"): party, party leader, raid, raid leader, raid
  warning, instance (= battleground on Legion) chat; the bot accepts the message's group when it is its group
  or its original group (in a battleground `/p` names the original party, `GetGroup()` is the battleground's).
- **Dungeon finder**, core commit "Add LFG accessors for modules" (DemonCore): `LFGMgr::
  GetQueueIds(guid)` (PlayerDungeons), `GetRoleCheckRoles(gguid, guid, roles)` (a running role check,
  state INITIALITING), `GetPendingProposal(guid)` (a proposal waiting for his answer). This core keeps LFG
  state per **queue id** (`PlayersStore[guid][queueId]`, queue id = the dungeon's), and those stores are
  private, hence the accessors. The bot answers through the same entry points as the packet handlers, on
  its own thread (`HandleLfgCompleteRoleCheck` -> `UpdateRoleCheck(gguid, guid, roles)`,
  `HandleLfgProposalResponse` -> `UpdateProposal(ProposalResponse)`; `ProposalResponse.Ticket.Time` is used
  for wait times, so the bot sends its own ticket from `GetTicket`).
  - `PlayerbotAI::UpdateLfg` (every 1 s, any non-self bot): role check -> its spec role (`ChrSpecialization`
    role -> `PLAYER_ROLE_TANK/HEALER/DAMAGE`) when it has none yet; a pending proposal is accepted once.
  - Fill (`PlayerbotMgr::UpdateLfgFill`, world thread every 1 s): a client's `OnClientUpdate` reports every
    2 s, from his thread, each queue he is in (alone or as leader of a non-LFG group) in state queued /
    role check / proposal: level, team, selected dungeons (the random dungeon id for a random queue), the
    roles his group brings (`GetRoles(member, queueId)`, one per member, tank > healer > dps). After
    `Playerbots.LFG.FillDelay` (30 s, owner's rule so other players queue first), while queued, for 5-man
    dungeons only (`LFGDungeonsEntry::Substruct == LFG_QUEUE_DUNGEON`; counts `CountTank/Healer/Damage`,
    1/1/3 if zero): one bot per `JoinInterval` for a missing role, tank first: an idle logged-in pool bot
    (bot account, no master, not being set up) of his faction, at most his level, whose class has a spec of
    that role, highest level first. It gets a new AI with the queuer as master, and on its thread
    `JoinLfgFor`: `BotFactory::InitializeNewBot` with `CreateRequest::teleport = false` (his level, the
    role's spec, talents, gear) then `sLFGMgr->JoinLfg(bot, role, dungeons)`.
  - Fill bots no longer in that queue after 10 s (matched with other players, join refused) leave the fill;
    when the queuer's reports stop for 5 s (left the queue, logged out, or the group was made) every fill bot
    not grouped and not in a proposal / dungeon leaves the queue (`AbandonLfg`) and is released
    (`ReleaseBot`: no master, new AI, back to GM Island).
  - A random bot in a dungeon finder group (`isLFGGroup`) does not leave it for lack of its master; when its
    master leaves that group it leaves too (queued `CMSG_LEAVE_GROUP`) and logs out 3 s later (the existing
    rule), so the group keeps no offline bot.
  - **`DungeonFinder.DebugJoin = 1`** (the core's shipped `worldserver.conf.dist`, "allows using the Dungeon
    Finder with a single player": `LFGQueue` minimum group size 1): a lone player is sent to the dungeon at
    once, so the fill never gets its chance (owner, build #24: party bots queue correctly, pool bots never
    did). Owner sets it to 0; the module logs a warning at startup while it is on.
  - **Replacement after a kick** (owner, build #26: vote kick or a bot told to `leave` -> the group queues again
    for a replacement, but pool bots did not fill it): `OnClientUpdate` now also reports dungeon finder groups
    (their leader may be a bot), from any player of the group, keyed by the group (`QueueReport::key`; a second
    player of the same group only refreshes the fill). The replacement queue does not set the selection again:
    an empty selection means the queue's own id. A released bot rests 5 min (`Bot::restUntil`, `ReleaseBot`)
    before it may fill a queue, so the kicked bot is not sent back into the same group.
  - **A member leaving gave no replacement queue** (owner, build #28: "a bot leaving the RDF party doesn't trigger
    the queue"). Core cause: `LFGGroupScript::OnRemoveMember` offers "find another player" (`SendLfgOfferContinue`)
    only to the dungeon finder leader, often a bot (nobody reads the offer), and skips it when the leader is the one
    leaving (the hook runs before the group picks a new leader); `/promote` was refused in any group with a member in
    an instance. Fixed in the core (commit "Fix dungeon finder replacement offer and lead change": offer to the next
    leader, lead change allowed in dungeon finder groups) and here: `PlayerbotAI::PassLfgLead` (every 3 s) makes a bot
    leading a dungeon finder group give the lead to an online player of it (its master first) by a queued
    `CMSG_SET_PARTY_LEADER` (int8 PartyIndex 1 = instance party, packed guid), so the offer reaches a person.
  - **The group still could not requeue on build #30**: the core refused the random dungeon cooldown (71328, given to
    every member on entering) even for a replacement request; fixed in the core (commit "Let dungeon finder groups
    requeue for a replacement", see its `CLAUDE.md`). `BotFactory::ResetBot` now removes that cooldown too, or a
    dismissed bot's next fill (within 30 min) would be refused as `LFG_JOIN_RANDOM_COOLDOWN`.
  - **Replacement by invite** (owner, build #31: after a kick the group still did not requeue on its own, only after
    he teleported out and back in; then the fill bot queued but never got into the dungeon, while a bot he invited by
    hand teleported in at once). The server log had no proposal answer from the fill bot and dropped it from the
    queue 34 s later, so the core's replacement matching is still unexplained (no `lfg` debug log to tell). The
    module no longer uses the replacement queue: `ReportDungeonGroup` (the leader's thread, every 2 s: he is a client,
    leads a dungeon finder group inside its dungeon, its state is `DUNGEON` or `QUEUED`, fewer members than the
    dungeon's role counts) -> `UpdateReplaceFill` (one fill per group; after `Playerbots.LFG.FillDelay`, one bot per
    `JoinInterval` for a missing role, roles from `Group::GetLfgRoles`) -> `StartReplaceBot` (an idle pool bot at
    random) -> `JoinDungeonGroupFor` on the bot's thread: set up to his level in a spec of that role, then his session
    invites it (`SendCreatorInvite`, 1 s), the bot accepts its master's invite, and once in the group
    `UpdateJoinTeleport` sets its LFG role and teleports it to him (read with `WithPlayer`): the path that worked by
    hand. Joining a group still queued makes the core leave the queue (`OnAddMember`). A bot not in the group after
    30 s, or when the fill ends, is dismissed. Dungeon finder groups are no longer reported to the queue fill.
  - **Random pool bots** (owner, build #31: a level 16 group always got allied races): every pick (dungeon finder fill
    and replacement, battleground fill, `.playerbots create` reuse) took the highest-level eligible bot, and pool bots
    sit at their starting level (1, allied races 10, DK 55, DH 98). Now a uniform random pick among the eligible
    (`urand(0, seen++) == 0`); the battleground fill still prefers healer classes while healers are wanted.
  - **Vote kicks** (owner, 2026-11-06): when a player's vote to kick a **bot** is running (victim is a bot, a
    non-bot member has voted yes: the kicker votes yes on his own), every other bot of the group votes yes
    (`UpdateLfg`, core accessor `LFGMgr::GetPendingBoot`, vote by `UpdateBoot`; log `votes yes to kick`). Kicks of
    players are left to the players: bots do not vote on them.
  - Not yet: raids / scenarios / LFR, bots with no group after a dungeon whose queuer was not in
    it (matched with others; they keep that master), refilling a running dungeon.
- Compiled in the container (core `LFGMgr.cpp`, module files) on 2026-11-05; runtime untested.

## Strategies, aggressive, loot (2026-11-05, untested)

Owner's request: mod-playerbots' `co` / `nc` split. Each bot has two sets of names (`PlayerbotAI::_co`, `_nc`),
filled from `Playerbots.Strategies.Combat` / `NonCombat`; `ApplyFlags` caches them into the flags the code
reads (`_assist`, `_playerTank`, `_stay`, `_aggressive`, `_lootCorpses`, `_restOn`, ...), so the older orders
(`follow`, `stay`, `passive`, `assist full|defend|passive`, `player tank`, `bot tank`, `summon`, `leave`) just
edit the sets. Orders `co ?` / `nc ?` (list, with what each does), `co +a,-b` / `co + a` / `nc -follow`
(`StrategyOrder`; tokens split by commas and spaces, a lone sign applies to the next name; unknown names are
reported), in whispers and every group chat (`HandleOrder`). Combat: `aggressive`, `passive`, `defend` (only
one of these three at a time), `tank`, `defensives`, `potions`. Non-combat: `follow`, `loot`, `rest`, `roll`
(off = pass), `autoequip`, `talents`. Config gates still apply on top (potions / auto-equip on alts).
- **aggressive** (`FindPullTarget`, in `SelectTarget` after self-defence): only when neither the bot nor a
  group member on the map is in combat; the nearest creature within `Playerbots.Aggressive.Range` (25), at
  most `MaxLevelAbove` (6) levels above the bot, hostile, valid target, in sight, not evading, no critter /
  civilian / totem / player's pet, not tapped by others; kept (`_pullTarget`) until the fight starts, fought
  as an ordered target (no engage delay). In combat the usual assist rules help with what already attacks
  the group. No pull while looting, resting or below the rest thresholds.
- **loot** (`UpdateLoot` every 500 ms; not self-bots; **alts only by order**, the default set drops `loot`
  for non-random bots: their bags are the player's): out of combat and with no group fight, the nearest
  corpse (`GetCorpseCreatureInGrid`, `Playerbots.Loot.Range` 30) tapped by the bot's group or listing the bot
  (`isTappedBy` / `HasInLootList`), not tried yet (`_lootedCorpses`); `MovePoint` to it, 15 s timeout;
  within 4 yd `LootCorpse`: as the client's loot window, on the bot's thread: `WorldSession::LootCorps`
  (CMSG_LOOT_UNIT: fills the corpse's loot and the bot's `personalLoot[corpse]`), for each slot of both loots
  `LootItemInSlot` (what the bot may take), rolled slots of group loot skipped (`isRolledSlot`, as the
  handler), quest items / currencies always, other items from `Playerbots.Loot.MinQuality` (0 = all), then
  `StoreLootItem(slot, loot)`, `GetGoldFromLoot`, `DoLootRelease`. Follow waits while `_lootTarget` is set.

## SimC rotations and talents (2026-11-05, untested)

Owner's request: SimulationCraft's T21 profiles (his `merged_simc.txt`, 35 files) as rotations and talents.
Owner's decisions: **only the default profile of each spec** (variants `_BoS`, `_RA`, `_Stomp`, `_DPS`, `_Exsg`,
`_SnD`, `_DfA` and the base `T21_Warlock.simc` left out); built into the module, a folder may override;
**`artifact.*` counts as learned** (artifacts come later; under-performing until then is fine for leveling);
gear / set bonus / trinket terms count as false; a spec that plainly fails without them is fixed by hand.
Specs without a profile (Holy / Disc Priest, Holy Paladin, Resto Druid / Shaman, Mistweaver, Protection Warrior,
Vengeance) keep the module's own lists, to be done by hand later.
- `tools/simc_to_apl.py <merged file or folder>`: keeps each default profile's `talents=` and `actions` lines
  (spec id from the file name table in the script) -> `apl/<Class>_<Spec>.simc` (with a `spec_id=` line) and the
  generated `src/combat/AplProfiles.cpp` (one string literal per line: MSVC limits single literals to ~16 KB).
  28 specs, 31-139 action lines each.
- `src/combat/BotApl.*`, the interpreter. Startup (`OnStartup` -> `BotApl::Initialize(Playerbots.Rotation.
  Directory)`): parses every profile once (lines `actions[.list](+)=a,opt=v/b...`, actions split on `/`, options on
  `,`), then files of the folder (`spec_id=` or SimC class + `spec=` lines) replace their spec's profile; one scan
  of the spell store indexes every spell name the profiles use (normalized: lower-case letters and digits, so
  `templars_verdict` = "Templar's Verdict"). Log per spec: lists, actions, talents, names no spell carries, terms
  read as 0 (`Rotation X: terms read as 0: ...`): that log is the to-do list for the next fixes.
- Expressions: SimC precedence (`|` < `^` < `&` < comparisons < `+ -` < `* % %%` < unary `! - @`, **`%` divides,
  `%%` is modulo**), `floor()`/`ceil()`. Terms (`MakeTerm`): resources (cur / max / deficit / pct / regen /
  time_to_max, displayed units), runes and `rune.time_to_N`, `cp_max_spend`; `buff.X` on the bot (passive auras
  skipped: talents share names with their procs; `buff.arcane_charge` = the Arcane Charges power); `debuff.X` /
  `dot.X` the bot's own aura on the target (refreshable = under 30% left); `active_dot.X`; `cooldown.X` /
  `action.X` (remains, ready, charges, recharge...; charge spells by `SpellCategory.ChargeRecoveryTime`);
  `talent.X.enabled` (the bot's talent map by spell name); `spell_targets` / `active_enemies` (hostile units in
  combat within 40 yd of the bot, 8 yd of the target); `target.health.pct`, `target.time_to_die` (health lost
  since the bot took that target, 300 until 2 s of data); `health.*`, `gcd` (1 s for energy users, else 1.5 x
  haste, min 0.75), `time` (since combat start), `cast_time`, `execute_time`, bare `remains` / `refreshable` /
  `charges` ... = the action's own spell; `prev_gcd.N.X`; `variable.X`; `stealthed.*`; `pet.X.active`; `race.X`;
  `incoming_damage_Ns` (own health drops, 10 s kept); Arcane's `burn_phase(_duration)`. Constants: `artifact.*`
  1, `equipped` / `set_bonus` / `trinket` 0, `raid_event.*.in` 10000, `moving` 0. Anything else is 0 (logged).
- Running (`BotApl::Run` from `RunRotation`, after triage / interrupt / dispel, when `Playerbots.Rotation.SimC`,
  a profile exists and the role is not healer): the main list in order; `call_action_list` / `run_action_list`
  (`name=`, `if=`), `variable` (set, add, sub, mul, div, min, max, reset, setif, floor, ceil; variables reset at
  each combat start), `start/stop_burn_phase`; a spell action casts the first spell of that name the bot knows
  (class family first) whose `if=` (and a plain `target_if=`) holds, `line_cd`, `sync`; positive spells and
  spells without an explicit target are cast on the bot. Skipped: auto attack / shot, items, potions, flasks,
  pooling, waits, movement spells (`charge`, `fel_rush`, `heroic_leap`...), `moving=1` lines, target selection
  (`target_if=min:`, `cycle_targets`), and the spec's interrupt (left to the interrupt code). `BotCast` with a 0.5 s
  per-spell throttle (the old lists used 1.5 s, too slow for 1 s global cooldowns). Cast log: `casts Spell (id),
  list: action text on Target`.
- Out of combat (`UpdatePrecombat`, every 2 s, not resting / looting / mounted): the `precombat` list's
  positive self spells not already up, `summon_pet` (hunter Call Pet 1, warlock `Summon <name>`) and
  `apply_poison` (`<lethal> Poison`); no `stealth` / `prowl` while following.
- Talents: `FillTalents` takes the profile's `talents=` digit for a row (column 1-3; 0 = the old name table);
  startup logs each profile's talents by name (`SimC talents of X (spec N): 1302002: ...`).
- Config: `Playerbots.Rotation.SimC` (1), `Playerbots.Rotation.Directory` ("playerbots_apl").
- Fixes from the owner's build #28 startup log (unknown names / terms per spec): `Aliases` (SimC name -> spell
  names) are now merged into the spell index, so they work in terms too, not only in actions (Feral `thrash_cat` /
  `swipe_cat`, Guardian `thrash_bear`, Affliction `unstable_affliction_1..5`, `incarnation`, `service_pet`,
  `bladestorm_mh`, `deadly_poison_dot`, Brewmaster `brews` ...). New terms: `attack_haste`, `firestarter.active`,
  `rtb_buffs`, `buff.active_uas.stack`, `stagger.heavy|moderate|light`, `death_and_decay|defile.ticking`, bare
  `time_to_die` / `max_charges` / `cooldown_react`, `pet.X.buff.Y.*`, `prev_off_gcd.X`, `dot.X.max_stacks`, cooldown
  `max_charges|usable|usable_in|adjusted_remains|cooldown`. Set bonus / legendary / trinket names (`QuietName`) and
  sim-only terms (`QuietTerm`: movement, stat.*, Demonology's imp counts...) stay 0 and are no longer reported.

## Battlegrounds (2026-11-06, untested)

Owner's request: PvP and battlegrounds, the tactics of SkyFire / AzerothCore playerbots and the same queue fill as the
dungeon finder. **SkyFire-playerbots has no battleground code** (PORTING.md: TODO); the reference is AzerothCore's
mod-playerbots (`BattleGroundJoinAction`, `BGStatusAction`, `BattleGroundTactics.cpp` 4.5k lines). Its code is not
portable as is: 3.3.5 entries, client area-trigger captures and its value/strategy engine; the ideas are rewritten on
this core's battlegrounds in `src/pvp/BotBattleground.*`.
- **What this core gives** (read 2026-11-06): `WorldSession::JoinBattlegroundQueue(bgTypeId, roles, asGroup,
  IgnorMapInfo)` (the battlemaster join, public) and `HandleBattleFieldPort(Port)` (accept / leave a queue; `Ticket.Id` =
  the queue slot): this core handles packets on the session's own thread whatever their `PROCESS_*` (the opcode
  table's `ProcessingPlace` is never read), so the bot calls them directly on its thread. `Battleground::BgObjects` /
  `BgCreatures` are public. `GetTypeID(true)` = the real map's type (also inside a random battleground), `GetTypeID()` =
  the queued one. Group queues run the dungeon finder's role check (`InitiBattlgroundCheckRoles`), which party bots
  already answer (`UpdateLfg`).
- **Queue**: party bots enter the battleground their leader queued the group for (`AcceptInvites`: every queue slot
  with an invitation, `Port` accepted); alts too (the player queued them). **Fill** (`PlayerbotMgr::ReportBgQueues` /
  `UpdateBgFill` / `StartBgBot`, as the dungeon finder fill): clients waiting in a normal queue (`IsFillableQueue`:
  Alterac Valley .. random; not arenas, rated, brawls, Ashran) report every 2 s; one fill per (queue type, bracket);
  after `Playerbots.BG.FillDelay` (30 s) one idle pool bot per `JoinInterval` (1 s) joins, to the team that lacks most,
  until each team has `BotsPerTeam` (0 = the battleground's minimum, never above its maximum; queued players and the
  leader's party bots count); about one healer in five; set up to the first queuer's level (`InitializeNewBot`,
  `teleport = false`), then `JoinBattlegroundFor` -> `JoinQueue` alone. Master of the fill bots = the first queuer.
  A fill bot no longer in the queue after 10 s (`BattlegroundQueue::GetPlayerGroupInfoData`, locked) leaves the fill;
  when nobody waits any more (the battleground started, or they left) bots still waiting leave the queue
  (`AbandonBattleground`, `Port` declined) and are dismissed; bots inside play on and are dismissed once back
  (`UpdateBattleground`: `_bgFill && _bgEntered`).
- **Full battlegrounds, role mix, places for players** (owner's rules, 2026-11-07): `BotsPerTeam = 0` now means the
  battleground's **maximum** (Warsong Gulch 10, Arathi Basin 15...), not its minimum. Each team gets a fixed mix
  (`RoleNeeds`): `perTeam / 10` tanks, `perTeam / 5` healers, the rest dps (WSG 1 / 2 / 7, AB 1 / 3 / 11); queued
  players count with their spec role (`BgReport::role`), party bots as dps, an extra tank / healer takes a dps place.
  `StartBgBot(team, role, level, bgTypeId, master)` picks an idle pool bot of a class with a spec of that role, at
  random. Every client and bot **inside** a battleground reports itself every 2 s (`BgMember`: instance, queued type ->
  queue type, bracket `GetBracketId`, status, max per team, team `GetBGTeam`, role, fill bot or not; clients from
  `ReportBgQueues`, bots from `UpdateBattleground`); the world thread keeps `_bgInstances`. **Room for a player**
  (`MakeRoomFor`): a queued player's report finds a running instance of his queue type and bracket whose team is full
  and holds a fill bot: a fill bot of his role (else a dps) leaves (`LeaveBattlegroundForPlayer` ->
  `LeaveBattleground`, dismissed once out), so the core's queue update invites him into the free place
  (`BattlegroundQueue` fills running battlegrounds from `BGFreeSlotQueue` first, `FillPlayersToBG`); once per player per
  90 s, and no new fill for him meanwhile. **Top-up**: a running instance with fill bots whose team stays short for
  `FillDelay` with nobody waiting in that queue (a refused invitation, someone gone) queues a bot of the missing role
  (one per `JoinInterval`, pending until it reports from inside, abandoned after 60 s). A fill whose players got in keeps
  its still-queued bots 60 s more (`BgFill::endedAt`), so the running battleground fills up before they give up.
- **Inside** (`BotBattleground::Update`, every 500 ms, before combat; `UpdateFollow` stays out): roles as AzerothCore's
  (about a quarter defend: guid counter % 4 == 0). Dead: after 2 s the spirit is released as `HandleRepopRequest`
  (`BuildPlayerRepop`, `RepopAtGraveyard`), then every 3 s queued at a spirit healer within 60 yd (`BgCreatures`,
  `isSpiritService`, `AddPlayerToResurrectQueue`, as `HandleAreaSpiritHealerQueue`): bots always release in a
  battleground (no order needed there). Over (`STATUS_WAIT_LEAVE`): `LeaveBattleground` after 5 s. Before the doors
  open, in arenas and while fighting: no tactics. Stuck 8 s on the way: a detour and a new objective.
- **Combat**: in a battleground or arena `SelectTarget` fights enemy players at once (no engage delay, no never-pull
  rule): the current one while within 45 yd, else `SelectEnemy` (players of the other team within 40 yd and in sight,
  the carrier of our flag counts 30 yd nearer, the wounded a little). A flag carrier fights nothing (runs home).
- **Flags** (Warsong Gulch 2, Twin Peaks 108; Legion entries 227741 / 227740 stands, 227745 / 227744 dropped; carrier
  auras 156618 / 156621): the carrier goes to its capture area trigger (3646 / 3647, Twin Peaks 5904 / 5905: the server
  captures, `_CheckPositions`); a dropped flag within 40 yd is used (picked up or returned); the enemy carrier is
  chased (defenders always, attackers 70%); defenders guard their flag stand; attackers escort their carrier within 60
  yd, else go for the enemy flag and use it (`GameObject::Use` -> `EventPlayerClickedOnFlag`, 4 yd).
- **Bases**: any battleground whose `BgObjects` hold `GAMEOBJECT_TYPE_CAPTURE_POINT` banners (Arathi Basin, Battle for
  Gilneas, Deepwind Gorge...). Owner = the banner's `GAMEOBJECT_FIELD_SPELL_VISUAL_ID` (`UpdateCapturePoint`: visual 1
  neutral, 2 / 3 Horde / Alliance assault, 4 / 5 Horde / Alliance held). Attackers take the nearest banner not ours (with
  up to 60 yd of random so a team spreads), defenders guard one of ours (an enemy assault on it is retaken). At 4 yd the
  banner is opened with its lock's spell (`OpenSpellFor`: the spell whose `SPELL_EFFECT_OPEN_LOCK` has the lock's type,
  a cast-time one first, indexed once) as a client would, else used directly.
- **Movement** (owner, build #31: bots walked through walls and climbed unclimbable cliffs in battlegrounds). Cause:
  `MovePoint` with pathfinding falls back to a straight line through anything when `PathGenerator` finds no path
  (`PATHFIND_NOPATH` / `SHORTCUT`), and a path holds at most `MAX_PATH_LENGTH` (74) polygons, so the tactics' trips across
  the map came back incomplete and ended the same way; missing or partial movement maps for a battleground map make it
  worse. Now (`BotBattleground.cpp`):
  - **Guarded steps** (`Step`): the bot computes the path itself; no path or a shortcut is refused; a long path is walked
    in steps of 50 yd along it (`MovePoint` to the point 50 yd along); without movement maps (or swimming,
    `PATHFIND_NOT_USING_PATH`) only a step of at most 40 yd in sight. A target nothing leads to is logged once (`finds no
    path to ...: it does not walk through walls`) and the bot takes a short random step (it may stand off the mesh).
  - **Waypoint routes** (`BotBattlegroundPaths.*`, `BotBattlegroundRoute.cpp`): AzerothCore's 104 vmangos paths for
    Warsong Gulch 489, Arathi Basin 529, Alterac Valley 30, Eye of the Storm 566 and Isle of Conquest 628, extracted by
    `tools/bg_paths_from_azerothcore.py` (generated file, coordinates kept: the maps are the same on 7.3.5; 4 one-way
    jump paths). One graph per map: consecutive points linked (one way for jumps), points of different paths linked
    within 15 yd, or 30 yd when one is a path's end (at 6 yd AV fell into 18 pieces, EotS 10, WSG 2; AzerothCore crosses
    the gaps by pathfinding). Beyond 45 yd the bot takes the shortest chain (Dijkstra) from the waypoint nearest to it to
    the one nearest the target (both within 60 yd), walks it waypoint by waypoint (each a guarded step; an unreachable
    waypoint is skipped), then goes straight. Checked offline: WSG flag room to flag room 31 waypoints, AB bases to nodes
    18-28, AV start to start 99, EotS start to start 25; **Isle of Conquest's two halves stay apart even at 30 yd** (its
    routes work within each half). Twin Peaks, Battle for Gilneas, Silvershard, Kotmogu, Deepwind Gorge and Seething
    Shore have no paths: guarded steps only (step 3, recording routes in game, only if they still misbehave).
- **Others** (Eye of the Storm towers and flag, Alterac Valley, Isle of Conquest, Strand, Silvershard, Kotmogu,
  Seething Shore): the nearest enemy player anywhere, else a teammate more than 20 yd away. Not yet: their own tactics
  (AzerothCore has AV / EotS / IoC ones), waypoint paths (pathfinding only: `MovePoint` with mmaps), mounts, arenas
  queues and tactics, rated play, vehicles.

## Maintenance, riding and mounts (2026-11-07, untested)

Owner's request: SkyFire's `maintenance` (talents by blueprint, riding and a racial mount per riding tier, level-adequate
enchants and gems by stat weights), random bots always set up with the riding of their level, and party bots mounting
with their leader. `src/BotMaintenance.*`; the order `maintenance` (owner only, not in combat; an order, so allowed on
alts: an alt's existing enchants are never replaced, only bare items get one).
- **Talents**: `BotFactory::FillTalents` (empty rows only, the spec's blueprint: SimC `talents=` digit, else the build
  table, else random), as everywhere else.
- **Riding** (`LearnRiding`): Apprentice 33388 (20), Journeyman 33391 (40), Expert 34090 (60), Artisan 34091 (70), Master
  90265 (80), and the old licences where the spell exists (Flight Master's License 90267, Cold Weather Flying 54197,
  Wisdom of the Four Winds 115913); for each tier reached without one, a random **racial** ground mount (normal at
  20-39, swift from 40; lists per race incl. pandaren and allied races, the faction's human / orc mounts for others) and
  a faction **flier** (gryphon / wind rider; swift from 70), each checked against `Mount.db2` (`sDB2Manager.GetMount`) and
  added with `CollectionMgr::AddMount` (which learns the spell; mounts are account-wide on Legion). Legion mount speed
  follows the riding skill, so one mount per kind is enough. Random bots get this in `BotFactory::InitializeNewBot`;
  `ResetBot` removes the riding spells above the bot's starting level (`ForgetRiding`; mounts stay in the collection).
  The allied race mount ids (258845, 258060, 259202, 258022) are from memory: an invalid one is just skipped.
- **Mounting with the leader** (`PlayerbotAI::UpdateMount`, from `UpdateFollow` every 500 ms): leader = master if in the
  group, else the group leader, same map. Leader mounted, bot not, out of combat, outdoors (`Map::IsOutdoors`), not in
  water, not resting / looting / casting: `MountSpellFor` (a flier when the leader flies, `IsFlying` / `SPELL_AURA_FLY`,
  and the bot has Expert riding or better, else a ground mount; its own known mount spells, fliers by our lists or
  `MountTypeID` 247 / 248), stand still, cast. Leader dismounted, a fight, or the bot engaging (`Engage`): dismount.
  Not in battlegrounds / arenas.
- **Flying follow** (owner, build #33: "flying following is a bit messy"). Cause: the core's `FollowMovementGenerator`
  for players plans the path on the ground mesh (`PathGenerator`; the shortcut through the air is allowed only for
  creatures that fly), and aims at `GetClosePoint`; in the air the path has no start polygon, fails (`PATHFIND_NOPATH`)
  and the bot stops and retries every 500 ms. SkyFire (`SetBotFlyingMovement`, airborne branch of its follow) and
  AzerothCore (`MovementAction::UpdateMovementState`) fly bots by hand; so now `PlayerbotAI::UpdateFlyingFollow`
  (before the ground follow in `UpdateFollow`):
  - leader **airborne** = may fly (fly flags or `SPELL_AURA_FLY`) and more than 3 yd above ground / water (SkyFire: a
    client keeps its fly flags while running on the ground with a flier, so flags alone lie);
  - bot on a **flying mount with flying riding**: `SetFlying(true)` (can fly, gravity off, `MOVEMENTFLAG_FLYING`,
    `SendMovementFlagUpdate`; a bot has no client to send them), `MovePoint` without path to its formation slot at the
    leader's height, aimed half a second ahead of a moving leader, at his flight speed (x1.3 beyond 15 yd); near
    teleport into the air beside him beyond 80 yd or 60 yd of height. `MoveSplineInit` makes the spline a flying one
    by the flags;
  - leader landed: the bot flies down to its slot beside him, clears the flags once within 1.5 yd of the ground
    (`SetFlying(false)` always clears can-fly: SkyFire's bots hovered with it kept) and walks on;
  - bot that cannot fly: `MovePoint` with path on the ground beneath his slot, teleport beyond 100 yd;
  - takeoff while on a ground mount (`UpdateMount`): it dismounts and takes its flier at the next check; it mounts a
    flier from the start when the leader's mount is one;
  - `Dismount` (leader dismounts, a fight, engaging) in the air: flags cleared and a **fall spline** launched by hand
    (`MoveSplineInit::SetFall`): `MotionMaster::MoveFall` only sets the falling flag for a player and leaves the fall
    to his client, so a bot would hang in the air. `UpdateFollow` waits while that fall runs.
  **Rule: a bot has no client; anything the core leaves to a player's client (flying flags, falls) the module does.**
- **Enchants and gems** (`ApplyEnchantsAndGems`): candidates once (`BuildCandidates`): Enchanting recipes
  (`SkillLineAbility` of `SKILL_ENCHANTING`) and item enhancements with a required level, whose spell has
  `SPELL_EFFECT_ENCHANT_ITEM`; gems (`ITEM_CLASS_GEM`, `GemProperties` fitting prismatic sockets, not unique, not relics);
  only enchantments with fixed stat amounts and no profession / scaling requirement. Score = `BotGear::StatWeight` (the
  same Ask Mr. Robot weights as autogear, new) x amount. **Level adequacy**: recipe skill ranks do not tell a level on
  7.3.5 (Legion recipes are learnable from skill 1), so an enchant's or gem's raw stat points may be at most 40% of the
  item's budget (`RandPropPoints` Epic[0] at the item's level, SkyFire's `EnchantBudgetShare` idea); plus the enchant's
  `MinLevel`, an enhancement's required level, a gem's required level, its item level within `BotGear::ItemLevelCap`
  and the item's level over the gem's `MinItemLevel`. Enchants as `Spell::EffectEnchantItemPerm` (permanent slot, fits by
  `Item::IsFitToSpellRequirements`), gems as `WorldSession::HandleSocketGems` without gem items (`_ApplyItemMods` off,
  `SetGem` + socket enchantment, on, socket bonus toggled). Artifacts are skipped. Logged once: `Maintenance: N enchant(s)
  and M gem(s) available to bots.`
- Then large bags in empty bag slots (`BotGear::AddBags`), `DurabilityRepairAll`, `SaveToDB`; the bot whispers a summary
  (`Maintenance done: talents, riding skills, mounts, enchants, gems, bags, repaired.`).

## Selling (2026-11-08, untested)

Owner's request: AzerothCore's `sell` command, and gray loot always sold once near a vendor (configurable).
`src/BotSell.*`; reference `mod-playerbots/src/Ai/Base/Actions/SellAction.cpp` (SkyFire's `SellVendorJunk` removed
items by hand instead).
- Order `sell <args>` / `s <args>` (owner only; an order, so allowed on alts; not in combat): `gray` / `grey` / `*` (gray
  items), `vendor` (AzerothCore's item usage vendor/AH: gray items, and armor / weapons up to epic that
  `BotGear::CompareToWorn` finds unusable or no upgrade), `<quality> [all]` (poor/gray, normal/white, uncommon/green,
  rare/blue, epic/purple, legendary: up to that quality, equipment only unless `all`; AzerothCore's
  `SellQualityItemsVisitor`), else item links (every `Hitem:<id>`) or a name (bag items whose English name contains
  it). Profession tools (fishing poles, miscellaneous weapons, totem category) are kept, as on AzerothCore. Only bag
  items, never worn ones. The bot whispers the count, the money and the first names.
- Each sale goes through the core: `WorldSession::HandleSellItemOpcode` with a `WorldPackets::Item::SellItem` built on
  the bot's thread (as AzerothCore), so the vendor must be within interaction range (`GetNPCIfCanInteractWith`), and
  buyback, money, achievements and quest item checks are the core's. Skipped before calling it: items without a sell
  price, bags with something in them, artifacts and **refundable items (the handler refunds those instead of selling
  them)**. A sale counts when the money went up. `FindVendor`: the nearest live vendor within 10 yd the bot can
  interact with.
- `UpdateAutoSell` (every 1 s with the loot checks): out of combat, no group fight, not casting, gray items in the bags
  and a vendor within reach -> sells the gray items, logs it and whispers its master. `Playerbots.Sell.AutoGray` (1),
  `Playerbots.Sell.AutoGrayAlts` (0: alts only by order). Self-bots never.
- C++ note: `WorldPackets::Item::SellItem packet(WorldPacket(CMSG_SELL_ITEM));` is a function declaration (most vexing
  parse); build it with braces.

## Loot rolls and equipping from the bags (2026-11-05, untested)

Owner's request; SkyFire's rules (`HandleLootRolls` / `EquipBagUpgrades`) with a 5% threshold.
- Core commit "Add pending loot rolls accessor for modules" (DemonCore): `Group::GetPendingRolls(member)`
  = rolls of live loot (`GetValidRollLoots`) whose vote for him is `NOT_EMITED_YET` (aoe slot, item id, bonus
  lists, vote mask). The bot votes with `Group::CountRollVote(bot, aoeSlot, choice)` on its own thread, as
  `WorldSession::HandleLootRoll` does (the roll list is private, there was no other way to see it).
- `BotGear::CompareToWorn(bot, proto, itemLevel)`: usable (armor/weapon, `CanUseItem`, `IsUsableBySpecialization`,
  class armor type for body armor, a hand layout of the spec's `HandsFor`), slot (weaker ring / trinket / dual
  hand; two-hander and ranged vs both hands; a one-hander replacing a two-hander must beat it alone; off-hand
  pieces refused while a two-hander is held), gain = (new - old) / old by `ItemScore`, 1 for an empty slot.
  A roll's item level = base + `BonusData::ItemLevelBonus` of its bonus lists (`ItemLevelWithBonuses`); bag
  items use `Item::GetItemLevel`.
- `PlayerbotAI::UpdateLootRolls` (every 1 s, not self-bots): need when allowed and gain >=
  `Playerbots.Loot.NeedUpgradePct` (5), else greed when allowed, else pass; random bots pass on everything when
  `Playerbots.Loot.RandomBotsRoll = 0`. Every vote is logged with its reason.
- `PlayerbotAI::UpdateBagEquip` (every 1 s): a signature of the bag contents (item guids and positions) detects
  any gain (loot, roll, quest, mail, vendor; the core calls only `OnLootItem` / `OnCreateItem`, no hook for
  quest rewards, so no hooks are used); in combat it is kept pending (`_equipPending`) until the fight ends.
  Best gain >= `Playerbots.AutoEquip.UpgradePct` (5) first, up to 6 per check, re-evaluated after each;
  `Player::SwapItem` (as `CMSG_SWAP_INV_ITEM`: the worn piece takes the bag slot), the off hand moved to the bags
  first for a two-hander. Whispers the master. Alts only with `Playerbots.AutoEquip.Alts = 1` (alt rule).

## Status

- 2026-11-01: skeleton (loader, config, startup log line). Compiles and links into the
  Windows worldserver (DemonCore-Build run #3, which now includes this module by default).
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

- 2026-11-05, owner's test of build #24 (core `23beaff` + `af7bf74`): **works**: autogear (head slot,
  bags), formations, `.playerbots self`, party bots in the dungeon finder (role check, proposal), defensives /
  potions / rest, tanks hold threat, healers heal (no damage when idle: expected), passive, startup pool (no
  lag, no crash), reuse by `.playerbots create`, invites, starting zones. **Problems**: two-handers kept by
  shield / dual-wield specs; dismissed bots stayed in their group; pool bots never filled the queue
  (`DungeonFinder.DebugJoin = 1`); healers followed during fights; orders wanted from every group chat; a
  shaman bot sometimes crashed the server on initialization (log ended with `Talent build of spec 263: no
  talent 'Boulderfist' in row 1` and `initialized: Level 16 Enhancement Shaman`; crash dump asked for).
  Loot rolls not tested (most Legion loot is personal). Fixes in the commit after.
  Owner then: the shaman "crash" is a **1-2 s freeze**, not a crash; he thinks it comes from the talent build,
  whose names are from **7.0** guides (7.3.5 renamed or moved talents, e.g. Enhancement's Boulderfist). Not
  proven: `CheckTalentBuilds` (startup) now logs every build name missing from this core with the names its
  row really has (send that log to fix the table), and `InitializeNewBot` logs each step's time (`setup took
  N ms: starting zones, level and spec, talents, gear, save`) to find what freezes.

- 2026-11-05, owner's test of build #26 (core `560dd9e` + `9f11f1c`): **works**: autogear (two-handers for
  shield / dual-wield specs fixed), follow out of combat only, all group chats, dungeon finder fill, `co`/`nc`
  strategies, old orders, `aggressive`, `loot`. No freeze seen while creating shamans. **Problems**: bots from
  older builds stuck in dead groups; no replacement filled after kicking a bot from a dungeon finder group;
  item level caps (high pieces at low levels, poor gear at 80). Fixes in the commit after.
  His log of that build: **setup takes 10-26 ms per bot** (zones 0-6, level/spec 0-7, talents 0-1, gear 9-15,
  save 0-2): no freeze in setup. Startup: `Talent builds: 31 specs, 28 name(s) not found`; the table is
  corrected from the rows the log lists (7.3.5 moved many 7.0 talents: Enhancement's Boulderfist row 1 -> 7,
  Prot Paladin's Crusader's Judgment row 1 -> 2, Frost's Ice Floes row 5 -> 2...): a moved talent is kept in
  its new row, else the row's passive one, never Blessed Hammer (replaces the Hammer of the Righteous the
  Prot rotation casts). Loot, aggressive pulls, dismissals and reuse logged as expected.

- 2026-11-06, owner's test of build #28 (allied races / account wipe / SimC rotations): **everything works except**
  a bot leaving a dungeon finder group did not queue the group for a replacement (core bug, fixed, see "Dungeon
  finder"). His log: 28 SimC profiles loaded, `Talent builds: 31 specs, 0 name(s) not found`; the unknown APL names
  and terms it listed were fixed in the commit after.
- 2026-11-06, owner's test of build #29 (allied races from level 10, `Playerbots.DeleteBotAccounts`): **works
  perfectly**.
- 2026-11-06, owner's test of build #30: **works**: the lead passes to the player (`gives the lead of its dungeon
  finder group`), bots vote yes to kick a bot, the kicked random bot is dismissed; fill, combat, SimC rotations and
  looting as before. **Problem**: the group still could not queue for a replacement, neither by the offer nor by
  hand (core: random dungeon cooldown, fixed in the commit after).
- 2026-11-07, owner's test of build #31 (core with the upstream merge, the dungeon finder cooldown fix and
  battlegrounds): **dungeon finder replacement still fails**: after a kick nothing requeues until the player teleports
  out and in; the fill bot then queues but never enters; a bot invited by hand teleports in. Pool bots were almost
  always allied races. Both changed in the commit after (replacement by invite, random picks). Battlegrounds not
  reported yet.
- 2026-11-07, owner's test of build #32 (dungeon finder replacement by invite, random pool picks, battleground fill to
  capacity with the role mix, guarded movement and waypoint routes): **works**: the dungeon finder fills a group short
  of a member as described, pool bot races are mixed, the battleground fill is correct. Movement: **better than
  build #31, still not right**: some bots still climb walls, and walking the waypoints looks robotic; the owner keeps
  it as is for now (to improve later: smoother routes, the remaining wall climbs).
  Battleground tactics (same build): **rudimentary but working**, a solid base; their logic is to be improved later.
  The place-for-a-player and top-up rules are **not testable yet**: they need a second human player (to be tested once
  the project is public).
- 2026-11-07, owner's test of build #33 (maintenance, riding, mounts): **works**: `maintenance` fully, party bots mount
  with their leader, random bots start with the riding of their level, allied races get their racial mounts.
  **Problem**: following a flying leader is messy (core follow plans on the ground mesh); rewritten in the commit after
  (see "Flying follow").

## Checking

Linux syntax check against the core: see "Building" in the core's `CLAUDE.md` (clone this repo
into the core's `modules/mod-playerbots`, configure, `make` single objects in
`src/server/scripts`). Windows: DemonCore-Build with `with_playerbots` on. No server or
database in the cloud container: runtime behaviour is untested there.

# mod-playerbots (LegionCore-Reforged 7.3.5)

Playerbots for **LegionCore-Reforged** (World of Warcraft Legion 7.3.5, build 26972), ported
from [SkyFire-playerbots](https://github.com/Brytenwally/SkyFire-playerbots) (5.4.8), itself a
port of the AzerothCore playerbots.

## Status

Bots log in and out, join your group, follow you and take whispered `follow` / `stay` orders.
Quests are shared with them. No combat yet. Needs
LegionCore-Reforged with its bot session hooks (`WorldSession::CreateBotSession`).

## GM commands

Set `Playerbots.Enable = 1` in `playerbots.conf` first.

| Command | What it does |
| --- | --- |
| `.playerbots add <name>` | Logs the character in as a bot (it must not be online). You get `Bot 'X' logged in.` once it is in the world. |
| `.playerbots remove <name>` | Logs the bot out (saved). |
| `.playerbots removeall` | Logs every bot out. |
| `.playerbots list` | Lists the bots and their state. |

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
  keep beacons / Prayer of Mending on the tank). Each spec casts a simple
  priority list (all 36 specs, spell names looked up in the server's spell data). Orders:
  `attack` (your target, until it dies or you say `passive`), `passive` (stop fighting and fight
  nothing, not even what attacks the bot, until `attack` or `assist full|defend`), `assist
  full|defend|passive`; `follow` does not end a fight. A dead
  bot is revived by `summon` in party chat (`Playerbots.Summon.Revive`).

## Install

Clone into the core's `modules/` folder under this exact name (the folder name decides the
loader function, `Addmod_playerbotsScripts`):

```sh
git clone https://github.com/Brytenwally/LegionCore-Playerbots.git modules/mod-playerbots
```

Re-run CMake and build. `playerbots.conf.dist` lands next to the worldserver; copy it to
`playerbots.conf` to change settings. Windows builds:
[LegionCore-Build](https://github.com/Brytenwally/LegionCore-Build) (`with_playerbots`).

# SquidBots Lite for DemonCore (7.3.5)

A World of Warcraft **7.3.5 (Legion)** client addon for DemonCore-Playerbots: a thin bar of bot orders, the bots' roles
drawn on your party frames, and **My alts** to log your other characters in as bots.

Port of the 5.4.8 [SquidBots Lite](https://github.com/Brytenwally/squidbots-addon) for SkyFire-playerbots (itself a port
of the 3.3.5 CoA Bots version). Every order it sends is one DemonCore-Playerbots understands; nothing on the server was
changed for it.

## Install

Copy the `SquidBotsLite` folder into `World of Warcraft/Interface/AddOns` of the 7.3.5 client.

## The bar

**Follow**, **Stay**, **Target** (attack my target), **Active / Passive**, **Dungeon**, **Regroup**, **Alts** and **Bots**.

- **Active / Passive**: `passive` (the bots fight nothing) / `assist full` (back to normal).
- **Dungeon**: no pulls of their own, normal assist, follow (`co -aggressive,+avoidaoe`, `assist full`, `follow`).
  Bots always step out of ground effects (`avoidaoe` is on for every bot).
- **Regroup**: every bot teleports next to you (`summon` in party chat).
- **Revive** shows up while a bot is dead: `summon` in party chat, which also revives the dead bots (out of combat,
  unless `Playerbots.Summon.Revive = 2`).
- **Bots**: My alts, and group orders: best gear (`autogear`), maintenance (`maintenance`: talents, riding and mounts,
  enchants and gems, bags, repair), sell grey items (`sell gray`, next to a vendor), quests nearby (`quests available`).

Right-click the bar for profiles: **Farm** (`co +aggressive`, `nc +loot,+rest,+follow`), **Strict follow**
(`co -aggressive`, `nc +follow`, `follow`), **Dungeon mode**, and **Orders by role** (tank / damage attack my target,
healers follow me / stay here, ranged follow me): the server has no role filter, so the addon whispers the order to each
member of that role.

## On your party frames

A role icon on each member's party frame (and raid-style frames; `/sbl raidframes off` to stop). The role is the party
role (set in dungeon finder groups), else the role of the specialization an **inspect** finds (`/sbl roles off` to stop).
Click the icon for that bot alone: follow, stay, attack my target, come to me, best gear, maintenance, sell, its quests,
**switch specialization** (`talents 1-4`), make it the leader, remove from group; a tank adds **I tank** (`player tank`)
and **It tanks** (`bot tank`).

## My alts

Lists your other characters (each notes itself when you play it with the addon on). **Log in** (`.playerbots add`, then
an invite), **Summon**, **Quests** (`quests all` in a small window) and **Log out** (`.playerbots remove`); who is online
comes from `.playerbots list`. These are **GM commands**: My alts needs GM rights. **Teams** save a group of characters
and log them in or out one a second.

## Commands

```
/sbl                 show or hide the bar
/sbl show | hide     show or hide it
/sbl scale <0.5-2>   bar size
/sbl lang fr | en    French or English
/sbl alts            your other characters, played as bots
/sbl quests [name]   a bot's quest log (your target when no name)
/sbl roles on | off  inspect members without a party role
/sbl raidframes on | off  role icons on the raid-style frames
/sbl gm              GM mode (list the active bots, log a bot out)
/sbl reset           put the bar back in the middle
```

Key bindings: Esc > Key Bindings > SquidBots Lite.

## Not in this version

| Feature of the SkyFire version | Why |
| --- | --- |
| Take the quests nearby (`accept`) | no such order; `quests available` lists them, `quest take <name>` takes one |
| Mark pulls with a skull (`rti`), heal only / healer damage | no such orders yet |
| Role filters (`@tank attack`) | replaced by whispers to each member of the role |
| Switch role (`.playerbots init`) | replaced by `talents <spec>` (switch specialization) |
| Quest tooltips and links in the Quests window | the bots whisper quest titles, not links |

## License

GPL-2.0, see [SquidBotsLite/LICENSE](SquidBotsLite/LICENSE).

# mod-playerbots (LegionCore-Reforged 7.3.5)

Playerbots for **LegionCore-Reforged** (World of Warcraft Legion 7.3.5, build 26972), ported
from [SkyFire-playerbots](https://github.com/Brytenwally/SkyFire-playerbots) (5.4.8), itself a
port of the AzerothCore playerbots.

## Status

Bots can log in and out; they have no AI yet (they stand where they were saved). Needs
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

## Install

Clone into the core's `modules/` folder under this exact name (the folder name decides the
loader function, `Addmod_playerbotsScripts`):

```sh
git clone https://github.com/Brytenwally/LegionCore-Playerbots.git modules/mod-playerbots
```

Re-run CMake and build. `playerbots.conf.dist` lands next to the worldserver; copy it to
`playerbots.conf` to change settings. Windows builds:
[LegionCore-Build](https://github.com/Brytenwally/LegionCore-Build) (`with_playerbots`).

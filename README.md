# mod-playerbots (LegionCore-Reforged 7.3.5)

Playerbots for **LegionCore-Reforged** (World of Warcraft Legion 7.3.5, build 26972), ported
from [SkyFire-playerbots](https://github.com/Brytenwally/SkyFire-playerbots) (5.4.8), itself a
port of the AzerothCore playerbots.

## Status

Skeleton only: the module builds, loads `playerbots.conf` and logs at startup. Bots come once
the core has its bot session hooks (see the plan in the core's `CLAUDE.md`).

## Install

Clone into the core's `modules/` folder under this exact name (the folder name decides the
loader function, `Addmod_playerbotsScripts`):

```sh
git clone https://github.com/Brytenwally/LegionCore-Playerbots.git modules/mod-playerbots
```

Re-run CMake and build. `playerbots.conf.dist` lands next to the worldserver; copy it to
`playerbots.conf` to change settings. Windows builds:
[LegionCore-Build](https://github.com/Brytenwally/LegionCore-Build) (`with_playerbots`).

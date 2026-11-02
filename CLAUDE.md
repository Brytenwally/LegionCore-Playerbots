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

## Status

- 2026-11-01: skeleton (loader, config, startup log line). Compiles and links into the
  Windows worldserver (LegionCore-Build run #3, which now includes this module by default).
  Verified on the owner's server 2026-11-02: `[mod-playerbots] Module loaded (disabled)`. Plan step 2 (bot session hooks in
  the core) comes before any bot code here.

## Checking

Linux syntax check against the core: see "Building" in the core's `CLAUDE.md` (clone this repo
into the core's `modules/mod-playerbots`, configure, `make` single objects in
`src/server/scripts`). Windows: LegionCore-Build with `with_playerbots` on. No server or
database in the cloud container: runtime behaviour is untested there.

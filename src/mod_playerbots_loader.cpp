/*
 * mod-playerbots for LegionCore-Reforged 7.3.5, ported from SkyFire-playerbots (5.4.8).
 *
 * Module loader: the core's script loader calls Addmod_playerbotsScripts() because the module
 * lives in modules/mod-playerbots (folder name with '-' as '_'). See the core's
 * modules/README.md.
 */

void AddSC_playerbot_scripts();

void Addmod_playerbotsScripts()
{
    AddSC_playerbot_scripts();
}

/*
 * mod-playerbots: script registration.
 *
 * Skeleton (plan step 3 has not started): loads playerbots.conf and reports at startup that
 * the module is present. Bots need the bot session hooks in the core first (plan step 2).
 */

#include "ScriptMgr.h"
#include "Config.h"
#include "Log.h"

namespace
{
    bool PlayerbotsEnabled = false;
}

class playerbots_world : public WorldScript
{
public:
    playerbots_world() : WorldScript("playerbots_world") { }

    void OnConfigLoad(bool /*reload*/) override
    {
        PlayerbotsEnabled = sConfigMgr->GetBoolDefault("Playerbots.Enable", false);
    }

    void OnStartup() override
    {
        TC_LOG_INFO("server.loading", "[mod-playerbots] Module loaded (%s). No bot features yet: "
            "the port starts after the core's bot session hooks.", PlayerbotsEnabled ? "enabled" : "disabled");
    }
};

void AddSC_playerbot_scripts()
{
    std::string error;
    if (!sConfigMgr->LoadModuleConfig("playerbots.conf", error))
        TC_LOG_ERROR("server.loading", "[mod-playerbots] playerbots.conf not loaded, using defaults: %s", error.c_str());

    new playerbots_world();
}

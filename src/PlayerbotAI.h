/*
 * mod-playerbots: per-bot AI.
 *
 * Plan step 3, first piece: a bot answers group invites, follows its group, and takes whispered
 * orders. Everything here runs on the bot's own map thread (PlayerScript::OnUpdate / OnChat from
 * the same map), so the bot's Player may be used freely.
 */

#ifndef MOD_PLAYERBOTS_PLAYERBOTAI_H
#define MOD_PLAYERBOTS_PLAYERBOTAI_H

#include "Define.h"
#include "ObjectGuid.h"
#include <string>

class Player;

class PlayerbotAI
{
public:
    // master: the player who added the bot (may be empty, e.g. added from the console).
    PlayerbotAI(ObjectGuid const& master, uint32 accountId) : _master(master), _accountId(accountId) { }

    void Update(Player* bot, uint32 diff);
    // a whisper from sender, who stands on the bot's map
    void HandleWhisper(Player* bot, Player* sender, std::string const& text);

private:
    // who may lead the bot: its master, or a player of the bot's own account (its alts' player)
    bool IsMaster(ObjectGuid const& guid, uint32 accountId) const;
    void HandleGroupInvite(Player* bot);
    void UpdateFollow(Player* bot);
    void StopFollowing(Player* bot);
    void Say(Player* bot, Player* to, std::string const& text);

    ObjectGuid _master;
    uint32 _accountId;
    bool _stay = false;                                     // "stay" order: hold position until "follow"
    ObjectGuid _answeredInvite;                             // the group invite already answered
    ObjectGuid _followGuid;                                 // who the current follow movement targets
    uint32 _followTimer = 0;
};

#endif

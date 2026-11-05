/*
 * mod-playerbots: follow formations. See BotFormation.h.
 */

#include "BotFormation.h"
#include "BotRotations.h"
#include "Player.h"
#include <algorithm>
#include <cmath>

namespace BotFormation
{
namespace
{
    float const Pi = float(M_PI);
    float const Spacing = 2.0f;                 // yards between neighbours in lines and rows

    struct ShapeName_
    {
        Shape shape;
        char const* name;
    };
    ShapeName_ const Names[] =
    {
        { Shape::Role, "role" }, { Shape::Near, "near" }, { Shape::Line, "line" }, { Shape::Queue, "queue" },
        { Shape::Circle, "circle" }, { Shape::Arrow, "arrow" }, { Shape::Shield, "shield" }, { Shape::Chaos, "chaos" },
        { Shape::Far, "far" }
    };

    // evenly fans count slots over [center - halfSpan, center + halfSpan] (SkyFire's FanAngle)
    float FanAngle(float center, float halfSpan, size_t index, size_t count)
    {
        if (count <= 1)
            return center;
        return center - halfSpan + float(index) / float(count - 1) * 2.0f * halfSpan;
    }

    // lateral offset (left positive) of index in a row of count, centred
    float RowOffset(size_t index, size_t count)
    {
        return (float(count - 1) / 2.0f - float(index)) * Spacing;
    }

    // a point in the leader's frame (forward, left) as MoveFollow's distance and angle
    Slot Polar(float forward, float left)
    {
        Slot slot;
        slot.distance = std::sqrt(forward * forward + left * left);
        slot.angle = slot.distance < 0.5f ? Pi : std::atan2(left, forward);
        if (slot.distance < 1.0f)
            slot.distance = 1.0f;
        return slot;
    }

    bool IsTank(BotRotations::CombatRole role) { return role == BotRotations::CombatRole::Tank; }
}

bool ShapeFromName(std::string const& name, Shape& shape)
{
    for (ShapeName_ const& entry : Names)
        if (name == entry.name)
        {
            shape = entry.shape;
            return true;
        }
    if (name == "default")
    {
        shape = Shape::Role;
        return true;
    }
    return false;
}

char const* ShapeName(Shape shape)
{
    for (ShapeName_ const& entry : Names)
        if (entry.shape == shape)
            return entry.name;
    return "role";
}

char const* ShapeList()
{
    return "role (default), near, line, queue, circle, arrow, shield, chaos, far";
}

Slot SlotFor(Player* bot, Player* leader, std::vector<Player*> const& members, Shape shape, float jitterForward, float jitterLeft)
{
    // the followers in a stable order (guid), so every bot computes the same places
    std::vector<Player*> followers;
    for (Player* member : members)
        if (member != leader && member->IsAlive())
            followers.push_back(member);
    if (std::find(followers.begin(), followers.end(), bot) == followers.end())
        followers.push_back(bot);
    std::sort(followers.begin(), followers.end(), [](Player* a, Player* b) { return a->GetGUID().GetCounter() < b->GetGUID().GetCounter(); });

    size_t const count = followers.size();
    size_t const index = size_t(std::find(followers.begin(), followers.end(), bot) - followers.begin());
    BotRotations::CombatRole const role = BotRotations::RoleOf(bot);

    // the bot's index among the followers of the same role
    size_t roleIndex = 0;
    size_t roleCount = 0;
    for (Player* follower : followers)
    {
        if (BotRotations::RoleOf(follower) != role)
            continue;
        if (follower == bot)
            roleIndex = roleCount;
        ++roleCount;
    }

    Slot slot;
    switch (shape)
    {
        case Shape::Role:
        {
            float center = Pi;
            float halfSpan = 0.45f;
            float base = 2.5f;
            switch (role)
            {
                case BotRotations::CombatRole::Tank:   center = 0.40f;       halfSpan = 0.55f; base = 2.0f; break;
                case BotRotations::CombatRole::Healer: center = Pi;          halfSpan = 0.45f; base = 3.5f; break;
                case BotRotations::CombatRole::Ranged: center = -Pi + 0.95f; halfSpan = 0.70f; base = 5.0f; break;
                default:                               center = Pi - 0.95f;  halfSpan = 0.70f; base = 2.5f; break;
            }
            slot.angle = FanAngle(center, halfSpan, roleIndex, roleCount);
            slot.distance = base + float(roleIndex) * 0.6f;
            break;
        }
        case Shape::Near:
        case Shape::Chaos:
        {
            slot.angle = FanAngle(Pi, 1.2f, index, count);
            slot.distance = 2.0f;
            if (shape == Shape::Chaos)
            {
                float const forward = std::cos(slot.angle) * slot.distance + jitterForward;
                float const left = std::sin(slot.angle) * slot.distance + jitterLeft;
                slot = Polar(forward, left);
            }
            break;
        }
        case Shape::Far:
            slot.angle = FanAngle(Pi, 0.8f, index, count);
            slot.distance = 10.0f;
            break;
        case Shape::Line:
        {
            // mod-playerbots' line: the leader in the middle of the row
            size_t const leaderPos = count / 2;
            size_t const pos = index < leaderPos ? index : index + 1;
            slot = Polar(0.0f, RowOffset(pos, count + 1) - RowOffset(leaderPos, count + 1));
            break;
        }
        case Shape::Queue:
            slot.angle = Pi;
            slot.distance = Spacing;
            if (index > 0)
                slot.anchor = followers[index - 1]->GetGUID();
            break;
        case Shape::Circle:
            slot.angle = Pi + 2.0f * Pi * float(index) / float(count);
            slot.distance = 3.0f;
            break;
        case Shape::Arrow:
        {
            // rows behind the leader's spot: tanks in front of him, then melee, ranged, healers
            float forward = -2.5f;
            switch (role)
            {
                case BotRotations::CombatRole::Tank:   forward = 2.5f;  break;
                case BotRotations::CombatRole::Melee:  forward = -2.5f; break;
                case BotRotations::CombatRole::Ranged: forward = -5.5f; break;
                default:                               forward = -8.5f; break;
            }
            slot = Polar(forward, RowOffset(roleIndex, roleCount));
            break;
        }
        case Shape::Shield:
        {
            // mod-playerbots' shield: tanks in a line in front, everyone else in a line behind
            bool const tank = IsTank(role);
            size_t rowIndex = 0;
            size_t rowCount = 0;
            for (Player* follower : followers)
            {
                if (IsTank(BotRotations::RoleOf(follower)) != tank)
                    continue;
                if (follower == bot)
                    rowIndex = rowCount;
                ++rowCount;
            }
            slot = Polar(tank ? 3.0f : -3.0f, RowOffset(rowIndex, rowCount));
            break;
        }
    }

    if (slot.anchor.IsEmpty())
        slot.anchor = leader->GetGUID();
    return slot;
}
}

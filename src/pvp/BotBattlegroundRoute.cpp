/*
 * mod-playerbots: routing on the battleground waypoint paths (BotBattlegroundPaths.h).
 *
 * AzerothCore's bots walk one path at a time and pick the next one at random where paths meet. Here the paths of a map
 * form one graph: consecutive points of a path are linked (both ways, or forwards only for a jump down), and points of
 * different paths within JunctionRange of each other are linked too, or within EndJunctionRange when one is a path's end
 * (the vmangos paths meet only roughly: at 6 yd Alterac Valley fell into 18 pieces, Eye of the Storm into 10; AzerothCore
 * crosses the gaps by pathfinding, and so does the bot, every link being a guarded step). A bot then takes the shortest
 * chain of waypoints from the one nearest to it to the one nearest to its objective.
 */

#include "BotBattlegroundPaths.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>

namespace
{
    float const JunctionRange = 15.0f;
    float const EndJunctionRange = 30.0f;
    float const EndRange = 60.0f;                           // a waypoint this close to an end may start / end the route

    struct Graph
    {
        std::vector<BotBattleground::PathPoint> nodes;
        std::vector<std::vector<std::pair<uint32, float>>> edges;   // node -> (next node, length)
    };

    float Distance(BotBattleground::PathPoint const& a, BotBattleground::PathPoint const& b)
    {
        float const dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    std::shared_ptr<Graph const> Build(uint32 mapId)
    {
        std::shared_ptr<Graph> graph = std::make_shared<Graph>();
        std::vector<uint32> pathOf;                         // node -> path index, for the junctions
        std::vector<bool> isEnd;                            // node -> first or last point of its path
        std::vector<BotBattleground::BattlegroundPath> const& paths = BotBattleground::PathsOfMap(mapId);
        for (uint32 p = 0; p < paths.size(); ++p)
        {
            uint32 previous = 0;
            for (uint32 i = 0; i < paths[p].points.size(); ++i)
            {
                uint32 const node = uint32(graph->nodes.size());
                graph->nodes.push_back(paths[p].points[i]);
                graph->edges.emplace_back();
                pathOf.push_back(p);
                isEnd.push_back(i == 0 || i + 1 == paths[p].points.size());
                if (i > 0)
                {
                    float const length = Distance(graph->nodes[previous], graph->nodes[node]);
                    graph->edges[previous].emplace_back(node, length);
                    if (!paths[p].oneWay)
                        graph->edges[node].emplace_back(previous, length);
                }
                previous = node;
            }
        }
        for (uint32 a = 0; a < graph->nodes.size(); ++a)
            for (uint32 b = a + 1; b < graph->nodes.size(); ++b)
                if (pathOf[a] != pathOf[b])
                {
                    float const length = Distance(graph->nodes[a], graph->nodes[b]);
                    if (length <= JunctionRange || ((isEnd[a] || isEnd[b]) && length <= EndJunctionRange))
                    {
                        graph->edges[a].emplace_back(b, length);
                        graph->edges[b].emplace_back(a, length);
                    }
                }
        return graph;
    }

    std::shared_ptr<Graph const> GraphOf(uint32 mapId)
    {
        static std::mutex lock;
        static std::map<uint32, std::shared_ptr<Graph const>> graphs;
        std::lock_guard<std::mutex> guard(lock);
        auto itr = graphs.find(mapId);
        if (itr == graphs.end())
            itr = graphs.emplace(mapId, Build(mapId)).first;
        return itr->second;
    }

    int32 Nearest(Graph const& graph, BotBattleground::PathPoint const& point)
    {
        int32 best = -1;
        float bestDist = EndRange;
        for (uint32 i = 0; i < graph.nodes.size(); ++i)
        {
            float const dist = Distance(graph.nodes[i], point);
            if (dist < bestDist)
            {
                best = int32(i);
                bestDist = dist;
            }
        }
        return best;
    }
}

namespace BotBattleground
{
    std::vector<PathPoint> Route(uint32 mapId, PathPoint const& from, PathPoint const& to)
    {
        std::vector<PathPoint> route;
        std::shared_ptr<Graph const> graph = GraphOf(mapId);
        if (graph->nodes.empty())
            return route;
        int32 const start = Nearest(*graph, from);
        int32 const goal = Nearest(*graph, to);
        if (start < 0 || goal < 0 || start == goal)
            return route;

        // Dijkstra
        std::vector<float> dist(graph->nodes.size(), -1.0f);
        std::vector<int32> previous(graph->nodes.size(), -1);
        typedef std::pair<float, uint32> Item;
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
        dist[start] = 0.0f;
        open.emplace(0.0f, uint32(start));
        while (!open.empty())
        {
            Item const item = open.top();
            open.pop();
            if (item.first > dist[item.second])
                continue;
            if (int32(item.second) == goal)
                break;
            for (auto const& edge : graph->edges[item.second])
            {
                float const next = item.first + edge.second;
                if (dist[edge.first] < 0.0f || next < dist[edge.first])
                {
                    dist[edge.first] = next;
                    previous[edge.first] = int32(item.second);
                    open.emplace(next, edge.first);
                }
            }
        }
        if (dist[goal] < 0.0f)
            return route;
        for (int32 node = goal; node >= 0; node = previous[node])
            route.push_back(graph->nodes[node]);
        std::reverse(route.begin(), route.end());
        return route;
    }
}

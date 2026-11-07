#include "Bazzalt/Components/Rigidbody.h"
#include "Bazzalt/Profiler.h"
#include "Bazzalt/Scene.h"
#include "Navigation/NavigationFields.h"
#include "Navigation/NavigationModuleApi.h"
#include <DetourCrowd.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <DetourTileCache.h>
#include <DetourTileCacheBuilder.h>
#include <Recast.h>
#include <algorithm>
#include <bit>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
namespace Bazzalt::Runtime {
namespace {
float *P(Vec3 &v) { return &v.X; }
const float *P(const Vec3 &v) { return &v.X; }
Vec3 V(const float *p) { return {p[0], p[1], p[2]}; }
bool Finite(Vec3 p) {
    return std::isfinite(p.X) && std::isfinite(p.Y) && std::isfinite(p.Z) &&
           std::max({std::abs(p.X), std::abs(p.Y), std::abs(p.Z)}) <= 10000000;
}
void Check(dtStatus status) {
    if (dtStatusFailed(status))
        throw std::runtime_error("Detour navigation operation failed (status " +
                                 std::to_string(static_cast<unsigned long long>(status)) + ")");
}
struct Compressor : dtTileCacheCompressor {
    int maxCompressedSize(const int size) override { return size; }
    dtStatus compress(const unsigned char *source, const int size, unsigned char *out,
                      const int capacity, int *written) override {
        if (size < 0 || size > capacity)
            return DT_FAILURE;
        std::memcpy(out, source, size);
        *written = size;
        return DT_SUCCESS;
    }
    dtStatus decompress(const unsigned char *source, const int size, unsigned char *out,
                        const int capacity, int *written) override {
        return compress(source, size, out, capacity, written);
    }
};
struct Process : dtTileCacheMeshProcess {
    void process(dtNavMeshCreateParams *params, unsigned char *areas,
                 unsigned short *flags) override {
        for (int i = 0; i < params->polyCount; ++i) {
            if (areas[i] == DT_TILECACHE_WALKABLE_AREA)
                areas[i] = 1;
            flags[i] = areas[i] >= 1 && areas[i] <= 16
                           ? static_cast<unsigned short>(1u << (areas[i] - 1))
                           : 0;
        }
    }
};
struct Actor {
    Entity Owner;
    int Index = -1;
    bool HasTarget = false;
    Vec3 Destination{};
    NavigationAgentStatus Status = NavigationAgentStatus::Idle;
};
struct Obstacle {
    Entity Owner;
    dtObstacleRef Ref = 0;
    std::array<float, 8> Signature{};
};
struct Surface {
    Entity Owner;
    NavMeshSurface Settings;
    dtTileCacheAlloc Alloc;
    Compressor Compression;
    Process MeshProcess;
    std::unique_ptr<dtNavMesh, decltype(&dtFreeNavMesh)> Mesh{dtAllocNavMesh(), dtFreeNavMesh};
    std::unique_ptr<dtNavMeshQuery, decltype(&dtFreeNavMeshQuery)> Query{dtAllocNavMeshQuery(),
                                                                         dtFreeNavMeshQuery};
    std::unique_ptr<dtTileCache, decltype(&dtFreeTileCache)> Cache{dtAllocTileCache(),
                                                                   dtFreeTileCache};
    std::unique_ptr<dtCrowd, decltype(&dtFreeCrowd)> Crowd{dtAllocCrowd(), dtFreeCrowd};
    dtQueryFilter Filter;
    std::map<unsigned, unsigned> Filters;
    std::map<UUID, Actor> Agents;
    std::map<UUID, Obstacle> Obstacles;
    bool Pending = false, ObstaclesDirty = false;
    UUID ObstacleCursor{};
    void Init(Entity owner, const dtTileCacheParams &params) {
        Owner = owner;
        Settings = owner.GetComponent<NavMeshSurface>();
        if (!Mesh || !Query || !Cache || !Crowd)
            throw std::bad_alloc();
        dtNavMeshParams nav{};
        std::copy(params.orig, params.orig + 3, nav.orig);
        nav.tileWidth = params.width * params.cs;
        nav.tileHeight = params.height * params.cs;
        nav.maxTiles = params.maxTiles;
        nav.maxPolys = 16384;
        Check(Mesh->init(&nav));
        Check(Query->init(Mesh.get(), 8192));
        Check(Cache->init(&params, &Alloc, &Compression, &MeshProcess));
        if (!Crowd->init(int(Settings.MaxAgents), std::max(params.walkableRadius, params.cs),
                         Mesh.get()))
            throw std::runtime_error("Cannot allocate navigation crowd");
    }
    unsigned AgentFilter(unsigned mask) {
        std::erase_if(Filters, [&](const auto &filter) {
            for (const auto &[id, actor] : Agents)
                if (actor.Owner && actor.Owner.HasComponent<NavigationAgent>() &&
                    actor.Owner.GetComponent<NavigationAgent>().Enabled &&
                    actor.Owner.GetComponent<NavigationAgent>().AreaMask == filter.first)
                    return false;
            return filter.first != mask;
        });
        auto found = Filters.find(mask);
        if (found != Filters.end())
            return found->second;
        if (Filters.size() >= DT_CROWD_MAX_QUERY_FILTER_TYPE)
            throw std::length_error("Surface exceeds 16 simultaneous agent area filters");
        unsigned index = 0;
        while (std::any_of(Filters.begin(), Filters.end(),
                           [&](const auto &row) { return row.second == index; }))
            ++index;
        auto filter = Crowd->getEditableFilter(index);
        *filter = Filter;
        filter->setIncludeFlags(static_cast<unsigned short>(mask));
        Filters.emplace(mask, index);
        return index;
    }
    void AddLayer(unsigned char *data, int size) {
        dtCompressedTileRef ref = 0;
        auto result = Cache->addTile(data, size, DT_COMPRESSEDTILE_FREE_DATA, &ref);
        if (dtStatusFailed(result)) {
            dtFree(data);
            Check(result);
        }
        Check(Cache->buildNavMeshTile(ref, Mesh.get()));
    }
};
using Surfaces = std::map<UUID, std::shared_ptr<Surface>>;
std::map<Scene *, Surfaces> &Worlds() {
    static std::map<Scene *, Surfaces> worlds;
    return worlds;
}
Surface &Get(Entity e) {
    if (!e || !e.HasComponent<NavMeshSurface>())
        throw std::invalid_argument("A NavMesh Surface is required");
    auto w = Worlds().find(e.GetScene());
    if (w == Worlds().end())
        throw std::logic_error("NavMesh Surface has not been baked or loaded");
    auto s = w->second.find(e.GetUUID());
    if (s == w->second.end() || s->second->Owner != e)
        throw std::logic_error("NavMesh Surface has not been baked or loaded");
    return *s->second;
}
bool Ready(Entity e) {
    if (!e || !e.HasComponent<NavMeshSurface>())
        return false;
    auto w = Worlds().find(e.GetScene());
    if (w == Worlds().end())
        return false;
    auto s = w->second.find(e.GetUUID());
    return s != w->second.end() && s->second->Owner == e &&
           s->second->Settings.Data == e.GetComponent<NavMeshSurface>().Data;
}
void Commit(Entity e, std::shared_ptr<Surface> surface) {
    auto &world = Worlds()[e.GetScene()];
    auto old = world.find(e.GetUUID());
    if (old != world.end())
        for (auto &[id, actor] : old->second->Agents) {
            actor.Index = -1;
            surface->Agents.emplace(id, actor);
        }
    world[e.GetUUID()] = std::move(surface);
}
void Bake(Entity owner, const std::vector<NavigationTriangle> &triangles) {
    ProfileScope scope("Navigation.Bake");
    auto settings = owner.GetComponent<NavMeshSurface>();
    if (!settings.IsValid())
        throw std::invalid_argument("Invalid NavMesh Surface settings");
    if (triangles.empty() || triangles.size() > 2000000)
        throw std::length_error("Navigation bake needs 1..2000000 triangles");
    auto matrix = owner.GetWorldMatrix();
    Vec3 lo{INFINITY}, hi{-INFINITY};
    for (int x : {-1, 1})
        for (int y : {-1, 1})
            for (int z : {-1, 1}) {
                auto p = matrix.TransformPoint(
                    settings.Center +
                    Vec3{settings.Size.X * x, settings.Size.Y * y, settings.Size.Z * z} * .5f);
                lo = {std::min(lo.X, p.X), std::min(lo.Y, p.Y), std::min(lo.Z, p.Z)};
                hi = {std::max(hi.X, p.X), std::max(hi.Y, p.Y), std::max(hi.Z, p.Z)};
            }
    if (!Finite(lo) || !Finite(hi))
        throw std::invalid_argument("Nonfinite surface bounds");
    rcConfig cfg{};
    cfg.cs = settings.CellSize;
    cfg.ch = settings.CellHeight;
    cfg.walkableSlopeAngle = settings.MaxSlope;
    cfg.walkableHeight = int(std::ceil(settings.AgentHeight / cfg.ch));
    cfg.walkableClimb = int(std::floor(settings.MaxClimb / cfg.ch));
    cfg.walkableRadius = int(std::ceil(settings.AgentRadius / cfg.cs));
    cfg.maxSimplificationError = settings.MaxSimplificationError;
    cfg.tileSize = int(settings.TileSize);
    cfg.borderSize = cfg.walkableRadius + 3;
    cfg.width = cfg.tileSize + cfg.borderSize * 2;
    cfg.height = cfg.width;
    if (cfg.width > 255 || cfg.walkableHeight > 255 || cfg.walkableClimb > 255)
        throw std::length_error("Navigation voxel resolution exceeds layer limits");
    const auto extent = hi - lo;
    if (std::max({std::abs(lo.X), std::abs(lo.Y), std::abs(lo.Z), std::abs(hi.X), std::abs(hi.Y),
                  std::abs(hi.Z)}) > 10000000 ||
        extent.X / cfg.cs > cfg.tileSize * 1024.f || extent.Z / cfg.cs > cfg.tileSize * 1024.f ||
        extent.Y / cfg.ch > 65535)
        throw std::length_error("Navigation world bounds exceed voxel coordinate limits");
    int width, height;
    rcCalcGridSize(P(lo), P(hi), cfg.cs, &width, &height);
    int nx = (width + cfg.tileSize - 1) / cfg.tileSize,
        ny = (height + cfg.tileSize - 1) / cfg.tileSize;
    if (nx <= 0 || ny <= 0 || double(nx) * ny > 1024)
        throw std::length_error("Navigation bounds exceed 1024 horizontal tiles");
    dtTileCacheParams params{};
    std::copy(P(lo), P(lo) + 3, params.orig);
    params.cs = cfg.cs;
    params.ch = cfg.ch;
    params.width = params.height = cfg.tileSize;
    params.walkableHeight = settings.AgentHeight;
    params.walkableRadius = settings.AgentRadius;
    params.walkableClimb = settings.MaxClimb;
    params.maxSimplificationError = cfg.maxSimplificationError;
    params.maxTiles = 4096;
    params.maxObstacles = int(settings.MaxObstacles);
    auto result = std::make_shared<Surface>();
    result->Init(owner, params);
    rcContext context;
    std::vector<float> vertices;
    std::vector<int> indices;
    std::vector<unsigned char> assigned;
    vertices.reserve(triangles.size() * 9);
    indices.reserve(triangles.size() * 3);
    for (auto &t : triangles) {
        if (!Finite(t.A) || !Finite(t.B) || !Finite(t.C) || t.Area > 16)
            throw std::invalid_argument("Invalid navigation triangle");
        for (auto p : {t.A, t.B, t.C}) {
            if (std::max({std::abs(p.X), std::abs(p.Y), std::abs(p.Z)}) > 10000000)
                throw std::invalid_argument("Navigation source exceeds world coordinate limits");
            indices.push_back(int(indices.size()));
            vertices.insert(vertices.end(), P(p), P(p) + 3);
        }
        assigned.push_back(static_cast<unsigned char>(t.Area));
    }
    std::vector<unsigned char> walkable(triangles.size(), 0);
    rcMarkWalkableTriangles(&context, cfg.walkableSlopeAngle, vertices.data(),
                            int(vertices.size() / 3), indices.data(), int(triangles.size()),
                            walkable.data());
    for (std::size_t i = 0; i < walkable.size(); ++i)
        if (walkable[i])
            walkable[i] = assigned[i];
    std::vector<std::vector<unsigned>> bins(std::size_t(nx) * ny);
    std::size_t references = 0;
    const float tileWidth = cfg.tileSize * cfg.cs, borderWidth = cfg.borderSize * cfg.cs;
    for (unsigned i = 0; i < triangles.size(); ++i) {
        auto &t = triangles[i];
        float minx = std::min({t.A.X, t.B.X, t.C.X}), maxx = std::max({t.A.X, t.B.X, t.C.X}),
              minz = std::min({t.A.Z, t.B.Z, t.C.Z}), maxz = std::max({t.A.Z, t.B.Z, t.C.Z});
        if (maxx < lo.X - borderWidth || minx > hi.X + borderWidth || maxz < lo.Z - borderWidth ||
            minz > hi.Z + borderWidth)
            continue;
        int x0 = std::clamp(int(std::floor((minx - lo.X - borderWidth) / tileWidth)), 0, nx - 1),
            x1 = std::clamp(int(std::floor((maxx - lo.X + borderWidth) / tileWidth)), 0, nx - 1),
            z0 = std::clamp(int(std::floor((minz - lo.Z - borderWidth) / tileWidth)), 0, ny - 1),
            z1 = std::clamp(int(std::floor((maxz - lo.Z + borderWidth) / tileWidth)), 0, ny - 1);
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                if (++references > 16000000)
                    throw std::length_error("Navigation triangle/tile overlap budget exceeded");
                bins[std::size_t(z) * nx + x].push_back(i);
            }
    }
    unsigned layers = 0;
    for (int y = 0; y < ny; ++y)
        for (int x = 0; x < nx; ++x) {
            const float tile = cfg.tileSize * cfg.cs, border = cfg.borderSize * cfg.cs;
            cfg.bmin[0] = lo.X + x * tile - border;
            cfg.bmin[1] = lo.Y;
            cfg.bmin[2] = lo.Z + y * tile - border;
            cfg.bmax[0] = lo.X + (x + 1) * tile + border;
            cfg.bmax[1] = hi.Y;
            cfg.bmax[2] = lo.Z + (y + 1) * tile + border;
            std::unique_ptr<rcHeightfield, decltype(&rcFreeHeightField)> solid(rcAllocHeightfield(),
                                                                               rcFreeHeightField);
            std::unique_ptr<rcCompactHeightfield, decltype(&rcFreeCompactHeightfield)> compact(
                rcAllocCompactHeightfield(), rcFreeCompactHeightfield);
            std::unique_ptr<rcHeightfieldLayerSet, decltype(&rcFreeHeightfieldLayerSet)> layerSet(
                rcAllocHeightfieldLayerSet(), rcFreeHeightfieldLayerSet);
            if (!solid || !compact || !layerSet)
                throw std::bad_alloc();
            if (!rcCreateHeightfield(&context, *solid, cfg.width, cfg.height, cfg.bmin, cfg.bmax,
                                     cfg.cs, cfg.ch))
                throw std::runtime_error("Cannot create navigation heightfield");
            std::vector<int> tileIndices;
            std::vector<unsigned char> areas;
            for (auto index : bins[std::size_t(y) * nx + x]) {
                tileIndices.push_back(int(index * 3));
                tileIndices.push_back(int(index * 3 + 1));
                tileIndices.push_back(int(index * 3 + 2));
                areas.push_back(walkable[index]);
            }
            if (areas.empty())
                continue;
            if (!rcRasterizeTriangles(&context, vertices.data(), int(vertices.size() / 3),
                                      tileIndices.data(), areas.data(), int(areas.size()), *solid,
                                      cfg.walkableClimb))
                throw std::runtime_error("Navigation rasterization failed");
            rcFilterLowHangingWalkableObstacles(&context, cfg.walkableClimb, *solid);
            rcFilterLedgeSpans(&context, cfg.walkableHeight, cfg.walkableClimb, *solid);
            rcFilterWalkableLowHeightSpans(&context, cfg.walkableHeight, *solid);
            if (!rcBuildCompactHeightfield(&context, cfg.walkableHeight, cfg.walkableClimb, *solid,
                                           *compact) ||
                !rcErodeWalkableArea(&context, cfg.walkableRadius, *compact) ||
                !rcBuildHeightfieldLayers(&context, *compact, cfg.borderSize, cfg.walkableHeight,
                                          *layerSet))
                throw std::runtime_error("Navigation layer generation failed");
            for (int i = 0; i < layerSet->nlayers; ++i) {
                if (++layers > 4096)
                    throw std::length_error("Navigation bake exceeds 4096 layers");
                auto &layer = layerSet->layers[i];
                dtTileCacheLayerHeader header{};
                header.magic = DT_TILECACHE_MAGIC;
                header.version = DT_TILECACHE_VERSION;
                header.tx = x;
                header.ty = y;
                header.tlayer = i;
                std::copy(layer.bmin, layer.bmin + 3, header.bmin);
                std::copy(layer.bmax, layer.bmax + 3, header.bmax);
                header.width = static_cast<unsigned char>(layer.width);
                header.height = static_cast<unsigned char>(layer.height);
                header.minx = static_cast<unsigned char>(layer.minx);
                header.maxx = static_cast<unsigned char>(layer.maxx);
                header.miny = static_cast<unsigned char>(layer.miny);
                header.maxy = static_cast<unsigned char>(layer.maxy);
                header.hmin = static_cast<unsigned short>(layer.hmin);
                header.hmax = static_cast<unsigned short>(layer.hmax);
                unsigned char *data = nullptr;
                int size = 0;
                Check(dtBuildTileCacheLayer(&result->Compression, &header, layer.heights,
                                            layer.areas, layer.cons, &data, &size));
                result->AddLayer(data, size);
            }
        }
    if (!layers)
        throw std::runtime_error("Navigation bake produced no walkable surface");
    Commit(owner, std::move(result));
}
NavigationHit Sample(Entity owner, Vec3 p, Vec3 ext, unsigned mask) {
    auto &s = Get(owner);
    if (!Finite(p) || !Finite(ext) || ext.X <= 0 || ext.Y <= 0 || ext.Z <= 0 || mask > 0xffff ||
        !mask)
        throw std::invalid_argument("Invalid navigation sample");
    dtQueryFilter filter = s.Filter;
    filter.setIncludeFlags(static_cast<unsigned short>(mask));
    dtPolyRef ref = 0;
    float nearest[3];
    Check(s.Query->findNearestPoly(P(p), P(ext), &filter, &ref, nearest));
    NavigationHit hit;
    if (ref) {
        hit.Found = true;
        hit.Position = V(nearest);
        hit.Distance = (p - hit.Position).Length();
    }
    return hit;
}
NavigationPath Path(Entity owner, Vec3 start, Vec3 end, unsigned mask) {
    auto &s = Get(owner);
    NavigationPath out;
    if (!Finite(start) || !Finite(end) || !mask || mask > 0xffff)
        throw std::invalid_argument("Invalid navigation path request");
    dtQueryFilter filter = s.Filter;
    filter.setIncludeFlags(static_cast<unsigned short>(mask));
    float a[3], b[3];
    float ext[3]{2, 4, 2};
    dtPolyRef ar = 0, br = 0;
    Check(s.Query->findNearestPoly(P(start), ext, &filter, &ar, a));
    Check(s.Query->findNearestPoly(P(end), ext, &filter, &br, b));
    if (!ar || !br)
        return out;
    dtPolyRef polys[2048];
    int count = 0;
    auto status = s.Query->findPath(ar, br, a, b, &filter, polys, &count, 2048);
    Check(status);
    if (!count)
        return out;
    bool complete = polys[count - 1] == br && !dtStatusDetail(status, DT_BUFFER_TOO_SMALL) &&
                    !dtStatusDetail(status, DT_OUT_OF_NODES);
    if (!complete)
        Check(s.Query->closestPointOnPoly(polys[count - 1], b, b, nullptr));
    float points[6144];
    unsigned char flags[2048];
    dtPolyRef refs[2048];
    int n = 0;
    status = s.Query->findStraightPath(a, b, polys, count, points, flags, refs, &n, 2048);
    Check(status);
    complete = complete && !dtStatusDetail(status, DT_BUFFER_TOO_SMALL);
    out.Status = complete ? NavigationPathStatus::Complete : NavigationPathStatus::Partial;
    for (int i = 0; i < n; ++i) {
        auto p = V(points + i * 3);
        if (!out.Corners.empty())
            out.Length += (p - out.Corners.back()).Length();
        out.Corners.push_back(p);
    }
    return out;
}
NavigationHit Ray(Entity owner, Vec3 a, Vec3 b, unsigned mask) {
    auto &s = Get(owner);
    if (!Finite(a) || !Finite(b) || !mask || mask > 0xffff)
        throw std::invalid_argument("Invalid navigation ray");
    dtQueryFilter filter = s.Filter;
    filter.setIncludeFlags(static_cast<unsigned short>(mask));
    float ext[3]{2, 4, 2}, start[3];
    dtPolyRef ref = 0;
    Check(s.Query->findNearestPoly(P(a), ext, &filter, &ref, start));
    NavigationHit hit;
    if (!ref)
        return hit;
    float t, normal[3];
    Check(s.Query->raycast(ref, start, P(b), &filter, &t, normal, nullptr, nullptr, 0));
    if (t <= 1) {
        hit.Found = true;
        hit.Position = V(start) + (b - V(start)) * t;
        hit.Normal = V(normal);
        hit.Distance = (hit.Position - a).Length();
    }
    return hit;
}
Surface *ForAgent(Entity e) {
    if (!e || !e.HasComponent<NavigationAgent>())
        return nullptr;
    auto w = Worlds().find(e.GetScene());
    if (w == Worlds().end())
        return nullptr;
    auto settings = e.GetComponent<NavigationAgent>();
    for (auto &[id, s] : w->second)
        if (s->Owner && s->Owner.IsComponentEnabled<NavMeshSurface>() &&
            (!settings.Surface || id == settings.Surface))
            return s.get();
    return nullptr;
}
dtCrowdAgentParams Parameters(Surface &s, const NavigationAgent &v) {
    dtCrowdAgentParams p{};
    p.radius = v.Radius;
    p.height = v.Height;
    p.maxAcceleration = v.Acceleration;
    p.maxSpeed = v.Stopped ? 0 : v.Speed;
    p.collisionQueryRange = v.Radius * 12;
    p.pathOptimizationRange = v.Radius * 30;
    p.separationWeight = v.SeparationWeight;
    p.updateFlags = DT_CROWD_ANTICIPATE_TURNS | DT_CROWD_OPTIMIZE_VIS | DT_CROWD_OPTIMIZE_TOPO |
                    DT_CROWD_SEPARATION;
    if (v.Avoidance)
        p.updateFlags |= DT_CROWD_OBSTACLE_AVOIDANCE;
    p.queryFilterType = static_cast<unsigned char>(s.AgentFilter(v.AreaMask));
    return p;
}
Actor *EnsureAgent(Surface &s, Entity e) {
    auto &v = e.GetComponent<NavigationAgent>();
    if (!v.Enabled)
        return nullptr;
    if (!v.IsValid() || v.Radius > s.Settings.AgentRadius || v.Height > s.Settings.AgentHeight)
        throw std::invalid_argument("Agent dimensions must fit the baked surface clearance");
    if (auto body = e.TryGetComponent<Rigidbody>();
        body && body->Enabled && body->MotionType != BodyMotionType::Kinematic)
        throw std::logic_error("Navigation-driven Rigidbody must be kinematic");
    auto &actor = s.Agents[e.GetUUID()];
    if (actor.Owner != e) {
        if (actor.Index >= 0)
            s.Crowd->removeAgent(actor.Index);
        actor = Actor{};
        actor.Owner = e;
    }
    auto params = Parameters(s, v);
    if (actor.Index < 0) {
        auto p = e.GetWorldTransform().Position;
        p.Y -= v.BaseOffset;
        if (!Finite(p))
            throw std::invalid_argument("Navigation agent position exceeds world bounds");
        actor.Index = s.Crowd->addAgent(P(p), &params);
        if (actor.Index < 0)
            throw std::length_error("NavMesh Surface exceeded Max Agents");
        if (s.Crowd->getAgent(actor.Index)->state == DT_CROWDAGENT_STATE_INVALID) {
            s.Crowd->removeAgent(actor.Index);
            actor.Index = -1;
            actor.Status = NavigationAgentStatus::Invalid;
            return nullptr;
        }
    } else
        s.Crowd->updateAgentParameters(actor.Index, &params);
    return &actor;
}
bool Destination(Entity e, Vec3 destination) {
    if (!Finite(destination))
        throw std::invalid_argument("Invalid navigation destination");
    auto s = ForAgent(e);
    if (!s)
        return false;
    auto a = EnsureAgent(*s, e);
    if (!a)
        return false;
    auto &v = e.GetComponent<NavigationAgent>();
    auto filter = s->Crowd->getFilter(s->Crowd->getAgent(a->Index)->params.queryFilterType);
    float ext[3]{2, 4, 2}, nearest[3];
    dtPolyRef ref = 0;
    Check(s->Query->findNearestPoly(P(destination), ext, filter, &ref, nearest));
    if (!ref)
        return false;
    if (!s->Crowd->requestMoveTarget(a->Index, ref, nearest))
        return false;
    a->Destination = V(nearest);
    a->HasTarget = true;
    a->Status = NavigationAgentStatus::Moving;
    return true;
}
void Reset(Entity e) {
    if (auto s = ForAgent(e)) {
        auto found = s->Agents.find(e.GetUUID());
        if (found != s->Agents.end()) {
            if (found->second.Index >= 0)
                s->Crowd->resetMoveTarget(found->second.Index);
            found->second.HasTarget = false;
            found->second.Status = NavigationAgentStatus::Idle;
        }
    }
}
bool Warp(Entity e, Vec3 p) {
    auto s = ForAgent(e);
    if (!s || !Finite(p))
        return false;
    auto hit = Sample(s->Owner, p, {2, 4, 2}, e.GetComponent<NavigationAgent>().AreaMask);
    if (!hit.Found)
        return false;
    auto pose = e.GetWorldTransform();
    pose.Position = hit.Position;
    pose.Position.Y += e.GetComponent<NavigationAgent>().BaseOffset;
    if (auto body = e.TryGetComponent<Rigidbody>();
        body && body->Enabled && e.GetScene()->GetPhysics().IsRunning())
        e.GetScene()->GetPhysics().Teleport(e, pose.Position, pose.Rotation);
    if (!e.SetWorldTransform(pose))
        return false;
    auto found = s->Agents.find(e.GetUUID());
    if (found != s->Agents.end()) {
        if (found->second.Index >= 0)
            s->Crowd->removeAgent(found->second.Index);
        found->second.Index = -1;
        found->second.HasTarget = false;
    }
    return EnsureAgent(*s, e) != nullptr;
}
NavigationAgentState AgentState(Entity e) {
    NavigationAgentState state;
    auto s = ForAgent(e);
    if (!s)
        return state;
    auto it = s->Agents.find(e.GetUUID());
    if (it == s->Agents.end())
        return state;
    if (it->second.Index < 0) {
        state.Status = it->second.Status;
        state.Position = e.GetWorldTransform().Position;
        state.Destination = it->second.Destination;
        return state;
    }
    auto &actor = it->second;
    auto a = s->Crowd->getAgent(actor.Index);
    state.Status = actor.Status;
    state.Position = V(a->npos);
    state.Velocity = V(a->vel);
    state.Destination = actor.Destination;
    if (actor.HasTarget) {
        auto path = Path(s->Owner, state.Position, actor.Destination,
                         e.GetComponent<NavigationAgent>().AreaMask);
        state.RemainingDistance =
            path.Status == NavigationPathStatus::Invalid ? INFINITY : path.Length;
    }
    return state;
}
bool SynchronizeOneObstacle(Surface &s, Scene &scene) {
    std::set<UUID> present;
    for (auto handle : scene.GetRegistry().view<NavigationObstacle>()) {
        auto e = scene.GetEntity(static_cast<Entity::Id>(handle));
        auto &v = e.GetComponent<NavigationObstacle>();
        if (v.Enabled && v.Carve) {
            if (!v.IsValid())
                throw std::invalid_argument("Invalid Navigation Obstacle");
            present.insert(e.GetUUID());
        }
    }
    if (present.size() > s.Settings.MaxObstacles)
        throw std::length_error("NavMesh Surface exceeded obstacle capacity");
    for (auto it = s.Obstacles.begin(); it != s.Obstacles.end(); ++it)
        if (!present.contains(it->first)) {
            Check(s.Cache->removeObstacle(it->second.Ref));
            s.ObstacleCursor = it->first;
            s.Obstacles.erase(it);
            s.Pending = true;
            return true;
        }
    std::vector<UUID> order(present.begin(), present.end());
    std::rotate(order.begin(), std::upper_bound(order.begin(), order.end(), s.ObstacleCursor),
                order.end());
    for (auto id : order) {
        auto e = scene.GetEntity(id);
        auto &v = e.GetComponent<NavigationObstacle>();
        auto matrix = e.GetWorldMatrix();
        auto center = matrix.TransformPoint(v.Center);
        Vec3 scale{matrix.TransformDirection({1, 0, 0}).Length(),
                   matrix.TransformDirection({0, 1, 0}).Length(),
                   matrix.TransformDirection({0, 0, 1}).Length()};
        auto rotation = e.GetWorldTransform().Rotation;
        auto forward = rotation.Rotate({0, 0, 1});
        float angle = std::atan2(forward.X, forward.Z);
        auto size = Vec3{v.Size.X * scale.X, v.Size.Y * scale.Y, v.Size.Z * scale.Z} * .5f;
        float radius = v.Radius * std::max(scale.X, scale.Z), height = v.Height * scale.Y;
        std::array<float, 8> signature{
            center.X, center.Y, center.Z, size.X,
            size.Y,   size.Z,   angle,    float(v.Shape == NavigationObstacleShape::Box ? 0 : 1)};
        if (v.Shape == NavigationObstacleShape::Cylinder) {
            signature[3] = radius;
            signature[4] = height;
        }
        auto it = s.Obstacles.find(e.GetUUID());
        if (it != s.Obstacles.end() && it->second.Owner == e && it->second.Signature == signature)
            continue;
        if (it != s.Obstacles.end()) {
            auto status = s.Cache->removeObstacle(it->second.Ref);
            Check(status);
            s.Obstacles.erase(it);
            s.Pending = true;
            s.ObstacleCursor = e.GetUUID();
            return true;
        }
        Vec3 bound =
            v.Shape == NavigationObstacleShape::Cylinder
                ? Vec3{radius, height * .5f, radius}
                : Vec3{std::abs(std::cos(angle)) * size.X + std::abs(std::sin(angle)) * size.Z,
                       size.Y,
                       std::abs(std::sin(angle)) * size.X + std::abs(std::cos(angle)) * size.Z};
        if (!Finite(center) || !Finite(bound) || bound.X <= 0 || bound.Y <= 0 || bound.Z <= 0)
            throw std::invalid_argument("Invalid transformed navigation obstacle");
        auto lower = center - bound, upper = center + bound;
        dtCompressedTileRef touched[DT_MAX_TOUCHED_TILES + 1];
        int touchedCount = 0;
        auto queryStatus = s.Cache->queryTiles(P(lower), P(upper), touched, &touchedCount,
                                               DT_MAX_TOUCHED_TILES + 1);
        Check(queryStatus);
        if (touchedCount > DT_MAX_TOUCHED_TILES || dtStatusDetail(queryStatus, DT_BUFFER_TOO_SMALL))
            throw std::length_error("Navigation obstacle touches more than eight layers; split it "
                                    "into smaller obstacles");
        dtObstacleRef ref = 0;
        dtStatus status;
        if (v.Shape == NavigationObstacleShape::Box)
            status = s.Cache->addBoxObstacle(P(center), P(size), angle, &ref);
        else {
            center.Y -= height * .5f;
            status = s.Cache->addObstacle(P(center), radius, height, &ref);
        }
        if (dtStatusSucceed(status)) {
            s.Obstacles.emplace(e.GetUUID(), Obstacle{e, ref, signature});
            s.Pending = true;
            s.ObstacleCursor = e.GetUUID();
            return true;
        } else
            throw std::length_error("NavMesh Surface exceeded obstacle capacity");
    }
    return false;
}
void Step(Scene &scene, float dt) {
    if (!std::isfinite(dt) || dt < 0 || dt > 1)
        throw std::invalid_argument("Navigation step must be within 0..1 seconds");
    ProfileScope scope("Navigation.Update");
    auto world = Worlds().find(&scene);
    if (world == Worlds().end())
        return;
    std::erase_if(world->second, [](auto &row) {
        return !row.second->Owner || !row.second->Owner.template HasComponent<NavMeshSurface>();
    });
    for (auto &[id, surface] : world->second) {
        auto &s = *surface;
        if (!s.Owner.IsComponentEnabled<NavMeshSurface>())
            continue;
        // Drain each request before issuing another: Detour bounds its shared tile work queue.
        // Runtime work is bounded and round-robin; preparation drains without moving agents.
        s.ObstaclesDirty = true;
        const unsigned budget = dt == 0 ? (s.Settings.MaxObstacles * 2 + 1) * 9 : 8;
        for (unsigned i = 0; i < budget; ++i) {
            if (!s.Pending && !SynchronizeOneObstacle(s, scene)) {
                s.ObstaclesDirty = false;
                break;
            }
            bool complete = false;
            Check(s.Cache->update(dt, s.Mesh.get(), &complete));
            s.Pending = !complete;
        }
        if (dt == 0)
            continue;
        std::set<UUID> active;
        for (auto handle : scene.GetRegistry().view<NavigationAgent>()) {
            auto e = scene.GetEntity(static_cast<Entity::Id>(handle));
            if (ForAgent(e) != &s || !e.IsComponentEnabled<NavigationAgent>())
                continue;
            if (auto actor = EnsureAgent(s, e)) {
                active.insert(e.GetUUID());
                if (actor->HasTarget &&
                    s.Crowd->getAgent(actor->Index)->targetState == DT_CROWDAGENT_TARGET_NONE)
                    Destination(e, actor->Destination);
            }
        }
        std::erase_if(s.Agents, [&](auto &row) {
            if (active.contains(row.first))
                return false;
            if (row.second.Index >= 0)
                s.Crowd->removeAgent(row.second.Index);
            return true;
        });
        s.Crowd->update(dt, nullptr);
        std::vector<std::pair<unsigned, UUID>> order;
        for (const auto &[uuid, actor] : s.Agents) {
            unsigned depth = 0;
            for (auto parent = actor.Owner.GetParent(); parent; parent = parent.GetParent())
                ++depth;
            order.emplace_back(depth, uuid);
        }
        std::sort(order.begin(), order.end());
        for (const auto &[depth, uuid] : order) {
            auto &actor = s.Agents.at(uuid);
            auto e = actor.Owner;
            auto &settings = e.GetComponent<NavigationAgent>();
            auto a = s.Crowd->getAgent(actor.Index);
            Vec3 position = V(a->npos), velocity = V(a->vel);
            actor.Status = !actor.HasTarget ? NavigationAgentStatus::Idle
                           : a->partial || a->targetState == DT_CROWDAGENT_TARGET_FAILED
                               ? NavigationAgentStatus::Blocked
                               : NavigationAgentStatus::Moving;
            if (actor.HasTarget &&
                (position - actor.Destination).Length() <= settings.StoppingDistance) {
                s.Crowd->resetMoveTarget(actor.Index);
                actor.HasTarget = false;
                actor.Status = NavigationAgentStatus::Arrived;
            }
            auto pose = e.GetWorldTransform();
            if (settings.UpdatePosition)
                pose.Position = position + Vec3{0, settings.BaseOffset, 0};
            if (settings.UpdateRotation &&
                velocity.X * velocity.X + velocity.Z * velocity.Z > 1e-6f) {
                auto target =
                    Quaternion::FromAxisAngle({0, 1, 0}, std::atan2(-velocity.X, -velocity.Z));
                auto dot = std::abs(pose.Rotation.X * target.X + pose.Rotation.Y * target.Y +
                                    pose.Rotation.Z * target.Z + pose.Rotation.W * target.W);
                float angle = 2 * std::acos(std::min(1.f, dot));
                pose.Rotation = Quaternion::Slerp(
                    pose.Rotation, target,
                    angle > 1e-5f ? std::min(1.f, settings.AngularSpeed * Pi / 180 * dt / angle)
                                  : 1.f);
            }
            if (auto body = e.TryGetComponent<Rigidbody>();
                body && body->Enabled && scene.GetPhysics().IsRunning()) {
                if (settings.UpdatePosition) {
                    auto current = e.GetWorldTransform();
                    auto delta = pose.Position - current.Position;
                    float distance = delta.Length();
                    if (distance > 1e-5f) {
                        PhysicsShape capsule;
                        capsule.Shape = ColliderShape::Capsule;
                        capsule.Radius = settings.Radius;
                        capsule.Height = std::max(settings.Height, settings.Radius * 2);
                        Transform cast;
                        cast.Position =
                            current.Position +
                            Vec3{0, capsule.Height * .5f - settings.BaseOffset + .03f, 0};
                        PhysicsQueryOptions options;
                        options.ExcludedEntities.push_back(e.GetUUID());
                        auto hit =
                            scene.ShapeCast(capsule, cast, delta / distance, distance, options);
                        if (hit && hit.Distance < distance) {
                            pose.Position = current.Position +
                                            delta * std::max(0.f, (hit.Distance - .02f) / distance);
                            s.Crowd->removeAgent(actor.Index);
                            actor.Index = -1;
                            actor.Status = NavigationAgentStatus::Blocked;
                        }
                    }
                }
                scene.GetPhysics().MoveKinematic(e, pose.Position, pose.Rotation, dt);
            }
            if ((settings.UpdatePosition || settings.UpdateRotation) && !e.SetWorldTransform(pose))
                throw std::runtime_error("Navigation agent has a singular parent transform");
        }
    }
}
NavigationStatistics Statistics(Scene &scene) {
    NavigationStatistics out;
    auto world = Worlds().find(&scene);
    if (world == Worlds().end())
        return out;
    out.Surfaces = unsigned(world->second.size());
    for (auto &[id, s] : world->second) {
        for (int i = 0; i < s->Cache->getTileCount(); ++i)
            if (s->Cache->getTile(i)->header)
                ++out.Tiles;
        out.Agents += unsigned(s->Agents.size());
        out.Obstacles += unsigned(s->Obstacles.size());
        out.ObstacleUpdatesPending |= s->Pending || s->ObstaclesDirty;
    }
    return out;
}
void Cost(Entity e, unsigned area, float cost) {
    if (area < 1 || area > 16 || !std::isfinite(cost) || cost < 1 || cost > 100000)
        throw std::invalid_argument("Invalid navigation area cost");
    auto &s = Get(e);
    s.Filter.setAreaCost(int(area), cost);
    for (unsigned i = 0; i < DT_CROWD_MAX_QUERY_FILTER_TYPE; ++i)
        s.Crowd->getEditableFilter(i)->setAreaCost(int(area), cost);
}
#include "Navigation/NavigationBakeData.inl"
std::vector<std::array<float, 10>> Guides(Scene &scene, UUID selected) {
    std::vector<std::array<float, 10>> out;
    auto world = Worlds().find(&scene);
    if (world == Worlds().end())
        return out;
    for (auto &[id, s] : world->second)
        if (id == selected)
            for (int t = 0; t < s->Mesh->getMaxTiles(); ++t) {
                auto tile = static_cast<const dtNavMesh *>(s->Mesh.get())->getTile(t);
                if (!tile->header)
                    continue;
                for (int i = 0; i < tile->header->polyCount; ++i) {
                    auto &p = tile->polys[i];
                    for (int j = 0; j < p.vertCount && out.size() < 32768; ++j) {
                        auto a = V(tile->verts + p.verts[j] * 3) + Vec3{0, .03f, 0},
                             b = V(tile->verts + p.verts[(j + 1) % p.vertCount] * 3) +
                                 Vec3{0, .03f, 0};
                        out.push_back({a.X, a.Y, a.Z, b.X, b.Y, b.Z, .15f, .7f, 1, .8f});
                    }
                }
            }
    return out;
}
const NavigationModuleApi Api{1,
                              sizeof(NavigationModuleApi),
                              [](Scene &s) { Worlds().erase(&s); },
                              Bake,
                              Step,
                              Ready,
                              Path,
                              Sample,
                              Ray,
                              Cost,
                              Destination,
                              Reset,
                              Warp,
                              AgentState,
                              Statistics,
                              Capture,
                              Restore,
                              Guides};
} // namespace
} // namespace Bazzalt::Runtime
#ifdef _WIN32
#define NAV_EXPORT __declspec(dllexport)
#else
#define NAV_EXPORT __attribute__((visibility("default")))
#endif
extern "C" NAV_EXPORT const Bazzalt::Runtime::NavigationModuleApi *
BazzaltGetNavigationModuleApi(unsigned version, unsigned size) {
    return version == 1 && size == sizeof(Bazzalt::Runtime::NavigationModuleApi)
               ? &Bazzalt::Runtime::Api
               : nullptr;
}

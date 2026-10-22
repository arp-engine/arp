#include "renderer/vegetation_scene_runtime.h"

#include "3d/vegetation/mesh_adapter.h"
#include "assets/glb_static_mesh_importer.h"
#include "assets/mesh/triangle_cluster_builder.h"
#include "core/hash/sha256.h"
#include "core/hash/stable_hash.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fkYAML/node.hpp>
#include <fstream>
#include <limits>

namespace arp::renderer {
namespace {
bool hashMatches(const std::filesystem::path& path, const core::pmr::String& expected)
{
    core::ByteBuffer digest;
    return core::Sha256::digestFile(path, digest)
        && core::Sha256::matchesRawHexDigest(digest, {expected.data(), expected.size()});
}

std::filesystem::path localPath(const std::filesystem::path& folder, const fkyaml::node& n)
{
    const auto& s = n.get_value_ref<const fkyaml::node::string_type&>();
    const std::filesystem::path p(s);
    if (p.empty() || p.is_absolute())
        throw std::runtime_error("VEGETATION-PATH-INVALID");
    for (const auto& part : p)
        if (part == "..")
            throw std::runtime_error("VEGETATION-PATH-INVALID");
    return folder / p;
}

core::pmr::String stringValue(const fkyaml::node& n)
{
    return core::pmr::String(n.get_value_ref<const fkyaml::node::string_type&>().c_str(),
        &core::defaultCpuMemoryResource());
}

assets::MeshSectionSpatialPartition staticScenePartition(const fkyaml::node& root)
{
    assets::MeshSectionSpatialPartition partition;
    if (!root.contains("staticSpatialPartition"))
        return partition;
    const auto& policy = root["staticSpatialPartition"];
    if (!policy.is_mapping() || !policy.contains("contractVersion")
        || !policy["contractVersion"].is_integer()
        || policy["contractVersion"].get_value<std::int64_t>() != 1)
        throw std::runtime_error("VEGETATION-STATIC-PARTITION-CONTRACT-INVALID");
    if (!policy.contains("maxTrianglesPerSection") || !policy.contains("maxSections")
        || !policy["maxTrianglesPerSection"].is_integer() || !policy["maxSections"].is_integer())
        throw std::runtime_error("VEGETATION-STATIC-PARTITION-LIMIT-INVALID");
    const auto triangles = policy["maxTrianglesPerSection"].get_value<std::int64_t>();
    const auto sections = policy["maxSections"].get_value<std::int64_t>();
    if (triangles <= 0 || triangles > UINT32_MAX || sections <= 0 || sections > UINT32_MAX)
        throw std::runtime_error("VEGETATION-STATIC-PARTITION-LIMIT-INVALID");
    partition.maxTrianglesPerSection = static_cast<std::uint32_t>(triangles);
    partition.maxSections = static_cast<std::uint32_t>(sections);
    return partition;
}

fkyaml::node validatedManifest(const std::filesystem::path& manifest)
{
    std::ifstream input(manifest);
    if (!input)
        throw std::runtime_error("VEGETATION-MANIFEST-MISSING");
    auto root = fkyaml::node::deserialize(input);
    if (root["type"].get_value<std::string>() != "VegetationScene"
        || root["contractVersion"].get_value<int>() != 1)
        throw std::runtime_error("VEGETATION-CONTRACT-INVALID");
    if (!hashMatches(localPath(manifest.parent_path(), root["staticScene"]),
            stringValue(root["staticSceneSha256"])))
        throw std::runtime_error("VEGETATION-STATIC-SCENE-HASH-MISMATCH");
    (void)staticScenePartition(root);
    return root;
}

core::UniquePtr<rhi::GpuBuffer> upload(rhi::GpuDevice& device,
    const char* label,
    const void* bytes,
    std::size_t size,
    rhi::ResourceUsage usage)
{
    if (size == 0)
        return nullptr;
    auto b = device.createBuffer({label,
        size,
        usage | rhi::ResourceUsage::CopyDst,
        rhi::MemoryUsage::CpuToGpu,
        rhi::ResourceState::ShaderRead});
    if (!b || !device.writeBuffer(*b, bytes, size).success)
        return nullptr;
    return b;
}

scene3d::Aabb transformedBounds(
    const scene3d::Aabb& local, const resource::TerrainStaticVegetationInstance& instance)
{
    const core::Quat q{
        instance.rotation[0], instance.rotation[1], instance.rotation[2], instance.rotation[3]};
    scene3d::Aabb bounds;
    for (int i = 0; i < 8; ++i) {
        core::Vec3 point{(i & 1 ? local.max.x : local.min.x) * instance.scale.x,
            (i & 2 ? local.max.y : local.min.y) * instance.scale.y,
            (i & 4 ? local.max.z : local.min.z) * instance.scale.z};
        point = core::rotate(q, point) + instance.position;
        bounds = i ? scene3d::Aabb::merge(bounds, {point, point}) : scene3d::Aabb{point, point};
    }
    return bounds;
}
} // namespace

bool resolveVegetationStaticScene(const std::filesystem::path& manifest,
    std::filesystem::path& staticScene,
    core::pmr::String& diagnostics)
{
    assets::MeshSectionSpatialPartition partition;
    return resolveVegetationStaticScene(manifest, staticScene, partition, diagnostics);
}

bool resolveVegetationStaticScene(const std::filesystem::path& manifest,
    std::filesystem::path& staticScene,
    assets::MeshSectionSpatialPartition& partition,
    core::pmr::String& diagnostics)
{
    staticScene.clear();
    partition = {};
    try {
        const auto root = validatedManifest(manifest);
        staticScene = localPath(manifest.parent_path(), root["staticScene"]);
        partition = staticScenePartition(root);
        diagnostics.clear();
        return true;
    } catch (const std::exception& error) {
        diagnostics = error.what();
        return false;
    }
}

VegetationSceneRuntime::~VegetationSceneRuntime()
{
    clear();
}

void VegetationSceneRuntime::clear()
{
    if (work_) {
        work_->shutdown(core::runtime::WorkShutdownPolicy::Drain);
        if (completions_)
            completions_->dispatch(chunks_.size() + 1);
        work_.reset();
        completions_.reset();
    }
    chunks_.clear();
    prototypes_.clear();
    retired_.reapAll();
    ready_ = false;
    telemetry_ = {};
    transitionBudget_.active = transitionBudget_.starts = 0;
    transitionBudget_.triangles = 0;
}

bool VegetationSceneRuntime::initialize(
    const std::filesystem::path& manifest, rhi::GpuDevice& device, core::pmr::String& diagnostics)
{
    if (ready_) {
        diagnostics = "VEGETATION-ALREADY-INITIALIZED";
        return false;
    }
    try {
        const auto root = validatedManifest(manifest);
        const auto& fade = root["lodTransitions"];
        if (fade["contractVersion"].get_value<int>() != 1)
            throw std::runtime_error("VEGETATION-TRANSITION-CONTRACT-INVALID");
        transitionSeconds_ = fade["durationSeconds"].get_value<float>();
        transitionBudget_.maxActive = fade["maxActive"].get_value<std::uint32_t>();
        transitionBudget_.maxStarts = fade["maxStartsPerFrame"].get_value<std::uint32_t>();
        transitionBudget_.maxTriangles = fade["maxExtraTriangles"].get_value<std::uint64_t>();
        if (!std::isfinite(transitionSeconds_) || transitionSeconds_ < 0.001f
            || transitionSeconds_ > 65.535f || !transitionBudget_.maxActive
            || !transitionBudget_.maxStarts || !transitionBudget_.maxTriangles)
            throw std::runtime_error("VEGETATION-TRANSITION-CONFIG-INVALID");
        time_ = 0;
        const auto& config = root["streaming"];
        chunkSize_ = config["chunkSize"].get_value<float>();
        loadRadius_ = config["loadRadius"].get_value<float>();
        unloadRadius_ = config["unloadRadius"].get_value<float>();
        requestsPerFrame_ = config["requestsPerFrame"].get_value<std::size_t>();
        uploadsPerFrame_ = config["uploadsPerFrame"].get_value<std::size_t>();
        maxResidentChunks_ = config["maxResidentChunks"].get_value<std::size_t>();
        const auto workers = config["workerCount"].get_value<std::size_t>();
        if (!std::isfinite(chunkSize_) || !std::isfinite(loadRadius_)
            || !std::isfinite(unloadRadius_) || chunkSize_ <= 0 || loadRadius_ <= 0
            || unloadRadius_ <= loadRadius_ || !requestsPerFrame_ || !uploadsPerFrame_
            || !maxResidentChunks_ || !workers)
            throw std::runtime_error("VEGETATION-STREAMING-CONFIG-INVALID");
        for (const auto& entry : root["prototypes"]) {
            Prototype p;
            p.id = entry["id"].get_value<std::uint32_t>();
            if (p.id == 0
                || std::any_of(prototypes_.begin(), prototypes_.end(), [&](const auto& prev) {
                       return prev.id == p.id;
                   }))
                throw std::runtime_error("VEGETATION-PROTOTYPE-ID-INVALID");
            p.castsShadow = entry["castsShadow"].get_value<bool>();
            const auto& lods = entry["lods"];
            const auto& distances = entry["lodDistances"];
            if (lods.empty() || lods.size() > 4 || distances.size() != lods.size())
                throw std::runtime_error("VEGETATION-LOD-CONTRACT-INVALID");
            if (entry["shadowLods"].size() != lods.size())
                throw std::runtime_error("VEGETATION-SHADOW-LOD-CONTRACT-INVALID");
            p.profile.tierCount = static_cast<int>(lods.size());
            p.profile.cullDistance = entry["cullDistance"].get_value<float>();
            p.screenProfile.count = p.profile.tierCount;
            p.screenProfile.hysteresis = entry["screenHysteresis"].get_value<float>();
            if (!std::isfinite(p.screenProfile.hysteresis) || p.screenProfile.hysteresis < 0
                || p.screenProfile.hysteresis >= 1
                || entry["screenDiameters"].size() != lods.size())
                throw std::runtime_error("VEGETATION-SCREEN-LOD-INVALID");
            float previousScreen = std::numeric_limits<float>::infinity();
            float previous = 0;
            for (std::size_t index = 0; index < lods.size(); ++index) {
                const float screen = entry["screenDiameters"][index].get_value<float>();
                if (!std::isfinite(screen) || screen <= 0 || screen >= previousScreen)
                    throw std::runtime_error("VEGETATION-SCREEN-LOD-INVALID");
                p.screenProfile.thresholds[index] = screen;
                previousScreen = screen;
                const float distance = distances[index].get_value<float>();
                const float hyst = entry["hysteresis"].get_value<float>();
                if (!std::isfinite(distance) || !std::isfinite(hyst) || distance <= previous
                    || hyst < 0)
                    throw std::runtime_error("VEGETATION-LOD-DISTANCE-INVALID");
                previous = distance;
                p.profile.tiers[index] = {distance, hyst};
                p.shadowLods[index] = entry["shadowLods"][index].get_value<std::uint32_t>();
                if (p.shadowLods[index] >= lods.size())
                    throw std::runtime_error("VEGETATION-SHADOW-LOD-CONTRACT-INVALID");
                const auto& lod = lods[index];
                const auto source = localPath(manifest.parent_path(), lod["source"]);
                if (!hashMatches(source, stringValue(lod["sha256"])))
                    throw std::runtime_error("VEGETATION-MODEL-HASH-MISMATCH");
                auto imported = assets::importStaticMeshFromGlbFile(source);
                if (!imported.success || imported.mesh.vertices.empty()
                    || imported.mesh.indices.empty()
                    || imported.mesh.indices.size()
                        != lod["triangles"].get_value<std::uint64_t>() * 3)
                    throw std::runtime_error("VEGETATION-MODEL-INVALID");
                Model model;
                const auto vertices =
                    vegetation::convertToInstanceVertices(imported.mesh, nullptr, false);
                for (const auto& vertex : vertices)
                    p.deformationEnvelope.include(vertex);
                model.vertices = upload(device,
                    "VegetationPrototypeVertices",
                    vertices.data(),
                    vertices.size() * sizeof(vegetation::InstanceVertex),
                    rhi::ResourceUsage::VertexBuffer);
                for (const auto& section : imported.mesh.submeshes) {
                    auto sourceSlot = section.materialSlot;
                    if (lod.contains("materialSources"))
                        for (const auto& binding : lod["materialSources"])
                            if (binding["slot"].get_value<std::uint32_t>() == section.materialSlot)
                                sourceSlot = binding["sourceSlot"].get_value<std::uint32_t>();
                    model.sections.push_back(
                        {section.firstIndex, section.indexCount, section.materialSlot, sourceSlot});
                }
                if (lod.contains("spatialClusters")) {
                    const auto& spec = lod["spatialClusters"];
                    if (spec["contractVersion"].get_value<int>() != 1)
                        throw std::runtime_error("VEGETATION-SPATIAL-CLUSTER-CONTRACT-INVALID");
                    const auto maxTriangles =
                        spec["maxTrianglesPerCluster"].get_value<std::uint32_t>();
                    const auto maxClusters =
                        spec["maxClustersPerSection"].get_value<std::uint32_t>();
                    if (!maxTriangles || !maxClusters)
                        throw std::runtime_error("VEGETATION-SPATIAL-CLUSTER-BUDGET-INVALID");
                    core::pmr::Vector<core::Vec3> positions{&core::defaultCpuMemoryResource()};
                    positions.reserve(imported.mesh.vertices.size());
                    for (const auto& vertex : imported.mesh.vertices)
                        positions.push_back(
                            {vertex.position[0], vertex.position[1], vertex.position[2]});
                    core::TriangleClusterBuilder builder;
                    for (std::uint32_t sectionIndex = 0; sectionIndex < model.sections.size();
                        ++sectionIndex) {
                        const auto& section = model.sections[sectionIndex];
                        if (section.firstIndex > imported.mesh.indices.size()
                            || section.indexCount
                                > imported.mesh.indices.size() - section.firstIndex)
                            throw std::runtime_error("VEGETATION-SPATIAL-CLUSTER-RANGE-INVALID");
                        auto partition = builder.build(positions.data(),
                            positions.size(),
                            imported.mesh.indices.data() + section.firstIndex,
                            section.indexCount,
                            maxTriangles,
                            maxClusters);
                        std::copy(partition.indices.begin(),
                            partition.indices.end(),
                            imported.mesh.indices.begin() + section.firstIndex);
                        for (const auto& cluster : partition.clusters)
                            model.sceneClusters.push_back({sectionIndex,
                                section.firstIndex + cluster.firstIndex,
                                cluster.indexCount,
                                {cluster.minimum, cluster.maximum}});
                    }
                }
                model.indices = upload(device,
                    "VegetationPrototypeIndices",
                    imported.mesh.indices.data(),
                    imported.mesh.indices.size() * sizeof(std::uint32_t),
                    rhi::ResourceUsage::IndexBuffer);
                if (!model.vertices || !model.indices)
                    throw std::runtime_error("VEGETATION-MODEL-UPLOAD-FAILED");
                bool first = true;
                for (const auto& v : imported.mesh.vertices) {
                    const core::Vec3 point{v.position[0], v.position[1], v.position[2]};
                    model.bounds = first ? scene3d::Aabb{point, point}
                                         : scene3d::Aabb::merge(model.bounds, {point, point});
                    first = false;
                }
                p.models.push_back(std::move(model));
            }
            if (!std::isfinite(p.profile.cullDistance) || p.profile.cullDistance <= 0
                || p.profile.cullDistance + p.profile.tiers[lods.size() - 1].hysteresis
                    > loadRadius_)
                throw std::runtime_error("VEGETATION-CULL-RESIDENCY-CONTRACT-INVALID");
            prototypes_.push_back(std::move(p));
        }
        if (prototypes_.empty())
            throw std::runtime_error("VEGETATION-PROTOTYPES-MISSING");
        chunks_.reserve(root["chunks"].size());
        std::size_t count = 0;
        for (const auto& entry : root["chunks"]) {
            Chunk c;
            c.owner = this;
            c.x = entry["x"].get_value<int>();
            c.z = entry["z"].get_value<int>();
            c.count = entry["count"].get_value<std::uint32_t>();
            if (!c.count || std::any_of(chunks_.begin(), chunks_.end(), [&](const auto& prev) {
                    return prev.x == c.x && prev.z == c.z;
                }))
                throw std::runtime_error("VEGETATION-CHUNK-INVALID");
            c.path = localPath(manifest.parent_path(), entry["source"]);
            c.sha256 = stringValue(entry["sha256"]);
            count += c.count;
            chunks_.push_back(std::move(c));
        }
        if (chunks_.empty() || count != root["instanceCount"].get_value<std::size_t>())
            throw std::runtime_error("VEGETATION-INSTANCE-COUNT-MISMATCH");
        completions_ = core::makeUnique<core::runtime::CompletionQueue>(
            core::runtime::CompletionQueueDesc{chunks_.size() + 1});
        work_ = core::makeUnique<core::runtime::WorkService>(
            core::runtime::WorkServiceDesc{completions_.get(), workers, maxResidentChunks_, false});
        if (!work_->valid())
            throw std::runtime_error("VEGETATION-WORK-SERVICE-UNAVAILABLE");
        ready_ = true;
        diagnostics.clear();
        return true;
    } catch (const std::exception& error) {
        diagnostics = error.what();
        clear();
        return false;
    }
}

core::runtime::WorkStatus VegetationSceneRuntime::readPage(void* context)
{
    auto& chunk = *static_cast<Chunk*>(context);
    try {
        if (!hashMatches(chunk.path, chunk.sha256))
            throw std::runtime_error("VEGETATION-CHUNK-HASH-MISMATCH");
        std::ifstream input(chunk.path, std::ios::binary | std::ios::ate);
        if (!input || input.tellg() != std::streamoff(chunk.count) * 48)
            throw std::runtime_error("VEGETATION-CHUNK-SIZE-INVALID");
        core::pmr::Vector<std::uint8_t> bytes(&core::defaultCpuMemoryResource());
        bytes.resize(std::size_t(chunk.count) * 48);
        input.seekg(0);
        if (!input.read(reinterpret_cast<char*>(bytes.data()), bytes.size()))
            throw std::runtime_error("VEGETATION-CHUNK-READ-FAILED");
        core::pmr::Vector<resource::TerrainStaticVegetationInstance> records(
            &core::defaultCpuMemoryResource());
        if (!decodeTerrainVegetationRecords(bytes.data(), bytes.size(), records, chunk.diagnostics))
            return core::runtime::WorkStatus::Failed;
        core::pmr::Vector<std::uint32_t> ids(&core::defaultCpuMemoryResource());
        chunk.placements.clear();
        chunk.placements.reserve(records.size());
        for (const auto& record : records) {
            const auto& runtime = *chunk.owner;
            if (!record.variationSeed || record.variationSeed >= (1u << 24)
                || std::find(ids.begin(), ids.end(), record.variationSeed) != ids.end()
                || std::floor(record.position.x / runtime.chunkSize_) != chunk.x
                || std::floor(record.position.z / runtime.chunkSize_) != chunk.z)
                throw std::runtime_error("VEGETATION-PLACEMENT-ID-OR-CHUNK-INVALID");
            ids.push_back(record.variationSeed);
            const auto proto = std::find_if(
                runtime.prototypes_.begin(), runtime.prototypes_.end(), [&](const auto& p) {
                    return p.id == record.prototypeId;
                });
            if (proto == runtime.prototypes_.end())
                throw std::runtime_error("VEGETATION-PROTOTYPE-UNKNOWN");
            TerrainVegetationPrototypePage page;
            page.prototypeId = record.prototypeId;
            page.instances.push_back(record);
            core::pmr::Vector<vegetation::InstanceDataGpu> lowered(
                &core::defaultCpuMemoryResource());
            if (!lowerTerrainVegetationInstancesForCull(page, lowered, chunk.diagnostics))
                return core::runtime::WorkStatus::Failed;
            Placement placement;
            placement.prototypeIndex =
                static_cast<std::uint32_t>(proto - runtime.prototypes_.begin());
            placement.gpu = lowered.front();
            placement.gpu.yawPhaseIdLod[2] = float(record.variationSeed);
            auto local = proto->models.front().bounds;
            const auto e = local.max - local.min;
            placement.screenDiameter = std::sqrt(e.x * e.x + e.y * e.y + e.z * e.z)
                * std::max({record.scale.x, record.scale.y, record.scale.z});
            for (const auto& model : proto->models)
                local = scene3d::Aabb::merge(local, model.bounds);
            placement.bounds = transformedBounds(local, record);
            chunk.placements.push_back(placement);
        }
        chunk.workerSuccess = true;
        return core::runtime::WorkStatus::Completed;
    } catch (const std::exception& error) {
        chunk.diagnostics = error.what();
        return core::runtime::WorkStatus::Failed;
    }
}

void VegetationSceneRuntime::pageCompleted(void* context, core::runtime::WorkResult result)
{
    auto& chunk = *static_cast<Chunk*>(context);
    chunk.state = result.status == core::runtime::WorkStatus::Completed && chunk.workerSuccess
        ? PageState::Loaded
        : PageState::Failed;
    if (chunk.state != PageState::Loaded)
        return;
    // Publication happens on the owner thread. Do not inspect other pages
    // from the worker while residency/LOD buffers are being replaced.
    for (const auto& other : chunk.owner->chunks_) {
        if (&other == &chunk || other.state != PageState::Loaded)
            continue;
        for (const auto& placement : chunk.placements)
            for (const auto& previous : other.placements)
                if (placement.gpu.yawPhaseIdLod[2] == previous.gpu.yawPhaseIdLod[2]) {
                    chunk.diagnostics = "VEGETATION-PLACEMENT-ID-DUPLICATE";
                    chunk.state = PageState::Failed;
                    return;
                }
    }
}

bool VegetationSceneRuntime::refreshGroups(Chunk& chunk,
    rhi::GpuDevice& device,
    core::Vec3 camera,
    const core::Frustum& frustum,
    std::uint64_t serial,
    core::pmr::String& diagnostics)
{
    core::pmr::Vector<Group> candidate(&core::defaultCpuMemoryResource());
    candidate.reserve(chunk.groups.size());
    core::pmr::Vector<std::pair<Placement*, int>> requests(&core::defaultCpuMemoryResource());
    for (auto& placement : chunk.placements) {
        const auto& prototype = prototypes_[placement.prototypeIndex];
        const core::Vec3 pos{placement.gpu.positionScale[0],
            placement.gpu.positionScale[1],
            placement.gpu.positionScale[2]};
        // Horizontal distance: the observer's eye height must not change LOD.
        const float distance = std::hypot(pos.x - camera.x, pos.z - camera.z);
        auto& transition = placement.transition;
        if (transition.finish(time_, transitionSeconds_)) {
            --transitionBudget_.active;
            transitionBudget_.triangles -= placement.transitionCost;
        }
        const bool culled = prototype.profile.shouldCull(distance, placement.distanceVisible);
        placement.distanceVisible = !culled;
        if (culled)
            continue;
        const bool visible =
            frustum.intersectsAabbMinMax(placement.bounds.min, placement.bounds.max);
        if (!visible && !prototype.castsShadow)
            continue;
        const core::Vec3 center{(placement.bounds.min.x + placement.bounds.max.x) * 0.5f,
            (placement.bounds.min.y + placement.bounds.max.y) * 0.5f,
            (placement.bounds.min.z + placement.bounds.max.z) * 0.5f};
        const auto offset = center - camera;
        const float depth =
            offset.x * view_.forward.x + offset.y * view_.forward.y + offset.z * view_.forward.z;
        const float diameter = placement.screenDiameter;
        const int desired = prototype.screenProfile.resolve(
            core::ScreenDiameterLod::diameter(diameter, view_.projectionY, depth),
            transition.current);
        if (transition.current < 0)
            transition.current = desired;
        if (!visible && !transition.active())
            transition.current = desired;
        if (visible && !transition.active() && desired != transition.current)
            requests.emplace_back(&placement, desired);
    }
    const auto complexity = [&](const Placement* p) {
        std::uint64_t triangles = 0;
        for (const auto& section :
            prototypes_[p->prototypeIndex].models[p->transition.current].sections)
            triangles += section.indexCount / 3;
        return triangles;
    };
    std::sort(requests.begin(), requests.end(), [&](const auto& a, const auto& b) {
        const auto ac = complexity(a.first), bc = complexity(b.first);
        return ac != bc ? ac > bc : a.first->gpu.yawPhaseIdLod[2] < b.first->gpu.yawPhaseIdLod[2];
    });
    for (const auto& request : requests) {
        auto& placement = *request.first;
        auto& transition = placement.transition;
        const int adjacent = transition.current + (request.second > transition.current ? 1 : -1);
        std::uint64_t cost = 0;
        for (const auto& section : prototypes_[placement.prototypeIndex]
                 .models[std::max(adjacent, transition.current)]
                 .sections)
            cost += section.indexCount / 3;
        if (transitionBudget_.admit(cost)) {
            transition.begin(request.second, time_);
            placement.transitionCost = cost;
        }
    }
    // Preserve instance ordering: prioritization changes admission, not buffer layout.
    for (auto& placement : chunk.placements) {
        const auto& prototype = prototypes_[placement.prototypeIndex];
        if (!placement.distanceVisible)
            continue;
        const bool visible =
            frustum.intersectsAabbMinMax(placement.bounds.min, placement.bounds.max);
        if (!visible && !prototype.castsShadow)
            continue;
        auto& transition = placement.transition;
        placement.lod = transition.current;
        const auto append = [&](int lod, bool incoming) {
            auto found = std::find_if(candidate.begin(), candidate.end(), [&](const auto& g) {
                return g.prototypeIndex == placement.prototypeIndex && g.lod == std::uint32_t(lod)
                    && g.sceneVisible == visible;
            });
            if (found == candidate.end()) {
                Group g;
                g.prototypeIndex = placement.prototypeIndex;
                g.lod = lod;
                g.sceneVisible = visible;
                g.bounds = placement.bounds;
                core::StableHash64 identity;
                identity.appendU64(chunk.x);
                identity.appendU64(chunk.z);
                identity.appendU64(prototype.id);
                identity.appendU64(g.lod);
                identity.appendU64(visible);
                g.stableId = identity.value();
                g.createdSerial = serial;
                const auto old =
                    std::find_if(chunk.groups.begin(), chunk.groups.end(), [&](const auto& prev) {
                        return prev.stableId == g.stableId;
                    });
                if (old != chunk.groups.end())
                    g.createdSerial = old->createdSerial;
                candidate.push_back(std::move(g));
                found = std::prev(candidate.end());
            }
            found->bounds = scene3d::Aabb::merge(found->bounds, placement.bounds);
            auto gpu = placement.gpu;
            // Both sides use the same deformation class throughout a transition.
            gpu.yawPhaseIdLod[3] =
                float(transition.active() ? std::min(transition.current, transition.target) : lod);
            vegetation::setInstanceMetadata(gpu, 0, 0, 0, 0, lod, prototype.castsShadow ? 0 : 2, 0);
            if (transition.active()) {
                gpu.speciesLodState[2] = transition.started;
                vegetation::packInstanceMetadataFloat(gpu.speciesLodState[3],
                    0x80000000u | (incoming ? 0x40000000u : 0u)
                        | ((std::uint32_t(gpu.yawPhaseIdLod[2]) & 0x3fffu) << 16)
                        | std::uint32_t(std::round(transitionSeconds_ * 1000.f)));
            }
            found->instances.push_back(gpu);
        };
        append(transition.current, false);
        if (transition.active())
            append(transition.target, true);
    }

    bool changed = candidate.size() != chunk.groups.size();
    for (std::size_t i = 0; !changed && i < candidate.size(); ++i) {
        const auto& a = candidate[i];
        const auto& b = chunk.groups[i];
        changed = a.prototypeIndex != b.prototypeIndex || a.lod != b.lod
            || a.sceneVisible != b.sceneVisible || a.instances.size() != b.instances.size()
            || std::memcmp(a.instances.data(),
                   b.instances.data(),
                   a.instances.size() * sizeof(vegetation::InstanceDataGpu))
                != 0;
    }
    if (!changed)
        return true;
    if (!chunk.groups.empty() && !retired_.canRetire()) {
        diagnostics = "VEGETATION-RETIREMENT-CAPACITY-EXHAUSTED";
        return false;
    }
    for (auto& group : candidate) {
        group.buffer = upload(device,
            "VegetationChunkInstances",
            group.instances.data(),
            group.instances.size() * sizeof(vegetation::InstanceDataGpu),
            rhi::ResourceUsage::VertexBuffer);
        if (!group.buffer) {
            diagnostics = "VEGETATION-INSTANCE-UPLOAD-FAILED";
            return false;
        }
    }
    if (!chunk.groups.empty() && !retired_.retire(serial, std::move(chunk.groups))) {
        diagnostics = "VEGETATION-RETIREMENT-CAPACITY-EXHAUSTED";
        return false;
    }
    chunk.groups = std::move(candidate);
    ++telemetry_.bufferReplacements;
    return true;
}

bool VegetationSceneRuntime::update(rhi::GpuDevice& device,
    core::Vec3 camera,
    const core::Frustum& frustum,
    std::uint64_t serial,
    core::pmr::String& diagnostics)
{
    return update(device, camera, frustum, serial, diagnostics, View{});
}

bool VegetationSceneRuntime::update(rhi::GpuDevice& device,
    core::Vec3 camera,
    const core::Frustum& frustum,
    std::uint64_t serial,
    core::pmr::String& diagnostics,
    const View& view)
{
    if (!std::isfinite(view.projectionY) || view.projectionY <= 0
        || !std::isfinite(view.deltaSeconds) || view.deltaSeconds < 0) {
        diagnostics = "VEGETATION-VIEW-INVALID";
        return false;
    }
    view_ = view;
    time_ = view.absoluteSeconds >= 0 ? view.absoluteSeconds : time_ + view.deltaSeconds;
    transitionBudget_.starts = 0;
    if (!ready_ || !serial) {
        diagnostics = "VEGETATION-RUNTIME-NOT-READY";
        return false;
    }
    completions_->dispatch(uploadsPerFrame_);
    std::size_t resident = 0, pending = 0, unloads = 0;
    for (auto& chunk : chunks_) {
        if (chunk.state == PageState::Failed) {
            diagnostics = chunk.diagnostics;
            return false;
        }
        if (chunk.state == PageState::Loading) {
            ++pending;
            continue;
        }
        if (chunk.state != PageState::Loaded)
            continue;
        const float dx =
            std::max({chunk.x * chunkSize_ - camera.x, 0.f, camera.x - (chunk.x + 1) * chunkSize_});
        const float dz =
            std::max({chunk.z * chunkSize_ - camera.z, 0.f, camera.z - (chunk.z + 1) * chunkSize_});
        if (std::hypot(dx, dz) > unloadRadius_ && unloads < uploadsPerFrame_) {
            if (!chunk.groups.empty()
                && (!retired_.canRetire() || !retired_.retire(serial, std::move(chunk.groups)))) {
                diagnostics = "VEGETATION-RETIREMENT-CAPACITY-EXHAUSTED";
                return false;
            }
            for (const auto& p : chunk.placements)
                if (p.transition.active()) {
                    --transitionBudget_.active;
                    transitionBudget_.triangles -= p.transitionCost;
                }
            chunk.groups.clear();
            chunk.placements.clear();
            chunk.placements.shrink_to_fit();
            chunk.state = PageState::Unloaded;
            chunk.workerSuccess = false;
            ++telemetry_.unloaded;
            ++unloads;
        } else
            ++resident;
    }
    std::size_t requests = 0;
    for (auto& chunk : chunks_) {
        if (requests >= requestsPerFrame_ || resident + pending >= maxResidentChunks_)
            break;
        if (chunk.state != PageState::Unloaded)
            continue;
        const float dx =
            std::max({chunk.x * chunkSize_ - camera.x, 0.f, camera.x - (chunk.x + 1) * chunkSize_});
        const float dz =
            std::max({chunk.z * chunkSize_ - camera.z, 0.f, camera.z - (chunk.z + 1) * chunkSize_});
        if (std::hypot(dx, dz) > loadRadius_)
            continue;
        const auto submitted = work_->submit({readPage, &chunk, pageCompleted, &chunk, {}});
        if (!submitted.accepted()) {
            diagnostics = "VEGETATION-PAGE-REQUEST-REJECTED";
            return false;
        }
        chunk.state = PageState::Loading;
        ++requests;
        ++pending;
        ++telemetry_.loaded;
    }
    // Round robin bounds placement upload work during turns/teleports too.
    std::size_t refreshed = 0;
    for (std::size_t n = 0; n < chunks_.size() && refreshed + unloads < uploadsPerFrame_; ++n) {
        auto& chunk = chunks_[refreshCursor_++ % chunks_.size()];
        if (chunk.state != PageState::Loaded)
            continue;
        if (!refreshGroups(chunk, device, camera, frustum, serial, diagnostics))
            return false;
        ++refreshed;
    }
    telemetry_.residentChunks = telemetry_.residentInstances = telemetry_.pendingChunks = 0;
    telemetry_.visibleInstances = telemetry_.frustumCulled = telemetry_.distanceCulled = 0;
    std::fill(std::begin(telemetry_.lodInstances), std::end(telemetry_.lodInstances), 0);
    for (const auto& chunk : chunks_) {
        if (chunk.state == PageState::Loading)
            ++telemetry_.pendingChunks;
        if (chunk.state != PageState::Loaded)
            continue;
        ++telemetry_.residentChunks;
        telemetry_.residentInstances += chunk.placements.size();
        for (const auto& p : chunk.placements) {
            if (!p.distanceVisible) {
                ++telemetry_.distanceCulled;
                continue;
            }
            if (!frustum.intersectsAabbMinMax(p.bounds.min, p.bounds.max))
                ++telemetry_.frustumCulled;
            else {
                ++telemetry_.visibleInstances;
                if (p.lod >= 0)
                    ++telemetry_.lodInstances[p.lod];
            }
        }
    }
    telemetry_.transitions = transitionBudget_.active;
    telemetry_.starts = transitionBudget_.starts;
    telemetry_.extraTriangles = transitionBudget_.triangles;
    diagnostics.clear();
    return true;
}
} // namespace arp::renderer

#include "scene_content.h"

#include "animation/linear_blend_skinning.h"
#include "assets/glb_static_mesh_importer.h"
#include "core/yaml_contract.h"
#include "renderer/mesh/gpu_skinning_contract.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace arp::build_a_queen {
namespace {
constexpr std::array<core::Vec4, 5> kStyleColors{{
    {0.92f, 0.18f, 0.42f, 1.0f},
    {0.12f, 0.62f, 0.84f, 1.0f},
    {0.44f, 0.76f, 0.18f, 1.0f},
    {0.52f, 0.30f, 0.84f, 1.0f},
    {0.96f, 0.64f, 0.12f, 1.0f},
}};
constexpr core::Vec4 kTrackColor{0.98f, 0.89f, 0.94f, 1.0f};
constexpr core::Vec4 kTrackEdgeColor{0.86f, 0.42f, 0.66f, 1.0f};
constexpr core::Vec4 kWhite{1.0f, 0.98f, 1.0f, 1.0f};

core::Vec3 sub(core::Vec3 lhs, core::Vec3 rhs)
{
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

core::Vec3 normal(core::Vec3 a, core::Vec3 b, core::Vec3 c)
{
    return core::normalize(core::cross(sub(b, a), sub(c, a)));
}

bool readTextFile(const std::filesystem::path& path, core::pmr::String& output)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return false;
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size <= 0)
        return false;
    output.resize(static_cast<std::size_t>(size));
    input.seekg(0, std::ios::beg);
    return static_cast<bool>(input.read(output.data(), size));
}

SceneContent::MaterialRole materialRole(const core::YamlContract::Node& node)
{
    const auto role = core::YamlContract::text(node, "role");
    if (role == "preserve")
        return SceneContent::MaterialRole::Preserve;
    if (role == "skin")
        return SceneContent::MaterialRole::Skin;
    if (role == "hair")
        return SceneContent::MaterialRole::Hair;
    if (role == "top")
        return SceneContent::MaterialRole::Top;
    if (role == "bottom")
        return SceneContent::MaterialRole::Bottom;
    if (role == "shoes")
        return SceneContent::MaterialRole::Shoes;
    if (role == "accessory")
        return SceneContent::MaterialRole::Accessory;
    core::YamlContract::fail();
}

} // namespace

std::size_t SceneContent::slotIndex(MaterialRole role)
{
    switch (role) {
    case MaterialRole::Hair:
        return static_cast<std::size_t>(Slot::Hair);
    case MaterialRole::Top:
        return static_cast<std::size_t>(Slot::Top);
    case MaterialRole::Bottom:
        return static_cast<std::size_t>(Slot::Bottom);
    case MaterialRole::Shoes:
        return static_cast<std::size_t>(Slot::Shoes);
    case MaterialRole::Accessory:
        return static_cast<std::size_t>(Slot::Accessory);
    default:
        return static_cast<std::size_t>(Slot::Count);
    }
}

SceneContent::CharacterModel::CharacterModel() = default;

bool SceneContent::loadCharacters(const core::pmr::String& characterDirectory)
{
    core::pmr::String source;
    const auto manifestPath =
        std::filesystem::path(characterDirectory.c_str()) / "character_manifest.yaml";
    if (!readTextFile(manifestPath, source))
        return false;
    try {
        const auto root =
            core::YamlContract::Node::deserialize(source.data(), source.data() + source.size());
        if (core::YamlContract::text(root, "type") != "arp.build-a-queen-character-manifest"
            || core::YamlContract::uint32(root, "contractVersion") != 1u)
            return false;
        for (const auto& description : core::YamlContract::sequence(root, "characters")) {
            CharacterModel model;
            model.id = core::YamlContract::text(description, "id");
            const auto file = core::YamlContract::text(description, "file");
            const auto height =
                static_cast<float>(core::YamlContract::number(description, "height"));
            const auto yaw =
                static_cast<float>(core::YamlContract::number(description, "yawDegrees"));
            if (model.id.empty() || file.empty() || height <= 0.0f || !std::isfinite(height))
                return false;
            auto imported = assets::importStaticMeshFromGlbFile(
                std::filesystem::path(characterDirectory.c_str()) / file.c_str());
            if (!imported.success || !imported.mesh.validForIndexedDraw()
                || imported.materials.empty())
                return false;

            core::pmr::Vector<MaterialRole> roles(&core::defaultCpuMemoryResource());
            core::pmr::Vector<bool> bound(&core::defaultCpuMemoryResource());
            roles.resize(imported.materials.size(), MaterialRole::Preserve);
            bound.resize(imported.materials.size(), false);
            for (const auto& binding : core::YamlContract::sequence(description, "materials")) {
                const auto name = core::YamlContract::text(binding, "name");
                std::size_t materialIndex = imported.materials.size();
                for (std::size_t index = 0; index < imported.materials.size(); ++index) {
                    if (imported.materials[index].name == name) {
                        materialIndex = index;
                        break;
                    }
                }
                if (materialIndex == imported.materials.size() || bound[materialIndex])
                    return false;
                roles[materialIndex] = materialRole(binding);
                bound[materialIndex] = true;
            }
            if (std::find(bound.begin(), bound.end(), false) != bound.end())
                return false;

            model.sourceFile = file;
            model.materialBindings.reserve(imported.materials.size());
            for (std::size_t index = 0; index < imported.materials.size(); ++index) {
                model.materialBindings.emplace_back();
                auto& binding = model.materialBindings.back();
                binding.name = imported.materials[index].name;
                binding.role = roles[index];
            }

            auto& mesh = imported.mesh;
            if (!mesh.recomputeBounds())
                return false;
            const auto& bounds = mesh.bounds;
            if (!bounds.valid || bounds.max[1] <= bounds.min[1])
                return false;
            const float centerX = (bounds.min[0] + bounds.max[0]) * 0.5f;
            const float centerZ = (bounds.min[2] + bounds.max[2]) * 0.5f;
            model.scale = height / (bounds.max[1] - bounds.min[1]);
            model.yawCos = std::cos(yaw * 0.017453292519943295f);
            model.yawSin = std::sin(yaw * 0.017453292519943295f);
            model.boundsMin = {bounds.min[0], bounds.min[1], bounds.min[2]};
            model.boundsMax = {bounds.max[0], bounds.max[1], bounds.max[2]};
            for (const auto& submesh : mesh.submeshes) {
                if (submesh.materialSlot >= imported.materials.size())
                    return false;
                const auto role = roles[submesh.materialSlot];
                const auto& material = imported.materials[submesh.materialSlot];
                const auto partIndex = slotIndex(role);
                for (std::size_t offset = 0; offset < submesh.indexCount; ++offset) {
                    const auto sourceIndex = mesh.indices[submesh.firstIndex + offset];
                    if (sourceIndex >= mesh.vertices.size())
                        return false;
                    const auto& sourceVertex = mesh.vertices[sourceIndex];
                    const auto& position = sourceVertex.position;
                    const auto& normalValue = sourceVertex.normal;
                    Vertex vertex{{(position[0] - centerX) * model.scale,
                                      (position[1] - bounds.min[1]) * model.scale,
                                      (position[2] - centerZ) * model.scale},
                        {normalValue[0], normalValue[1], normalValue[2]},
                        {material.baseColorFactor[0],
                            material.baseColorFactor[1],
                            material.baseColorFactor[2],
                            material.baseColorFactor[3]},
                        static_cast<std::uint8_t>(role)};
                    model.vertices.push_back(vertex);
                    if (partIndex < model.parts.size()) {
                        model.parts[partIndex].vertices.push_back(vertex);
                        model.parts[partIndex].valid = true;
                    }
                }
            }
            if (model.vertices.empty())
                return false;
            characters_.push_back(std::move(model));
        }
    } catch (const core::YamlContractError&) {
        return false;
    } catch (const std::exception&) {
        return false;
    }
    return characters_.size() == 2u && characters_[0].id == "player"
        && characters_[1].id == "rival";
}

bool SceneContent::loadAnimatedCharacters(const core::pmr::String& characterDirectory)
{
    constexpr std::array<const char*, 2> files{"Casual.glb", "Formal.glb"};
    animatedCharacters_.reserve(files.size());
    for (const auto* file : files) {
        const auto source =
            std::find_if(characters_.begin(), characters_.end(), [&](const auto& item) {
                return item.sourceFile == file;
            });
        if (source == characters_.end())
            return false;
        auto imported = assets::importAnimatedMeshFromGlbFile(
            std::filesystem::path(characterDirectory.c_str()) / file);
        if (!imported.success || !imported.asset.mesh.validForIndexedDraw()
            || !imported.asset.skeleton.valid()
            || imported.asset.skeleton.jointCount() > renderer::kMaxGpuSkinningJoints
            || imported.asset.skinnedVertices.size() != imported.asset.mesh.vertices.size())
            return false;
        for (const auto requiredClip : {"Run", "Idle", "Wave"}) {
            const auto found = std::find_if(imported.asset.clips.begin(),
                imported.asset.clips.end(),
                [&](const animation::AnimationClip& clip) {
                    return clip.name == requiredClip;
                });
            if (found == imported.asset.clips.end() || found->durationSeconds <= 0.0f)
                return false;
        }
        if (!imported.asset.mesh.recomputeBounds())
            return false;

        AnimatedCharacter character;
        character.asset = std::move(imported.asset);
        const auto& bounds = character.asset.mesh.bounds;
        const auto height = bounds.max[1] - bounds.min[1];
        if (!bounds.valid || !(height > 0.0f))
            return false;
        character.origin = {(bounds.min[0] + bounds.max[0]) * 0.5f,
            bounds.min[1],
            (bounds.min[2] + bounds.max[2]) * 0.5f};
        character.scale = 1.72f / height;

        const auto& mesh = character.asset.mesh;
        const auto& influences = character.asset.skinnedVertices;
        core::pmr::Vector<MaterialRole> roles(&core::defaultCpuMemoryResource());
        roles.reserve(character.asset.materials.size());
        for (const auto& material : character.asset.materials) {
            const auto binding = std::find_if(source->materialBindings.begin(),
                source->materialBindings.end(),
                [&](const MaterialBinding& item) {
                    return item.name == material.name;
                });
            if (binding == source->materialBindings.end())
                return false;
            roles.push_back(binding->role);
        }
        for (const auto& submesh : mesh.submeshes) {
            if (submesh.materialSlot >= character.asset.materials.size()
                || submesh.materialSlot >= roles.size())
                return false;
            const auto& material = character.asset.materials[submesh.materialSlot];
            for (std::size_t offset = 0; offset < submesh.indexCount; ++offset) {
                const auto vertexIndex = mesh.indices[submesh.firstIndex + offset];
                if (vertexIndex >= mesh.vertices.size())
                    return false;
                const auto& source = mesh.vertices[vertexIndex];
                const auto& skin = influences[vertexIndex];
                Vertex vertex{{source.position[0], source.position[1], source.position[2]},
                    {source.normal[0], source.normal[1], source.normal[2]},
                    {material.baseColorFactor[0],
                        material.baseColorFactor[1],
                        material.baseColorFactor[2],
                        material.baseColorFactor[3]}};
                vertex.materialRole = static_cast<std::uint8_t>(roles[submesh.materialSlot]);
                float totalWeight = 0.0f;
                for (std::size_t lane = 0; lane < 4u; ++lane) {
                    if (skin.joints[lane] >= character.asset.skeleton.jointCount()
                        || !std::isfinite(skin.weights[lane]) || skin.weights[lane] < 0.0f)
                        return false;
                    vertex.joints[lane] = static_cast<float>(skin.joints[lane]);
                    vertex.weights[lane] = skin.weights[lane];
                    totalWeight += skin.weights[lane];
                }
                if (!std::isfinite(totalWeight) || std::abs(totalWeight - 1.0f) > 0.001f)
                    return false;
                character.vertices.push_back(vertex);
            }
        }
        if (character.vertices.empty() || character.vertices.size() > UINT32_MAX)
            return false;
        character.vertexCount = static_cast<std::uint32_t>(character.vertices.size());
        animatedCharacters_.push_back(std::move(character));
    }
    return animatedCharacters_.size() == 2u;
}

bool SceneContent::appendMesh(const core::pmr::Vector<Vertex>& mesh,
    float scale,
    float yawCos,
    float yawSin,
    core::Vec3 center,
    core::Vec3 sourceOffset,
    const Snapshot* outfit)
{
    if (mesh.empty() || mesh.size() > vertexCapacity_ - vertices_.size()) {
        sceneBuildValid_ = false;
        return false;
    }
    for (auto vertex : mesh) {
        const float localX = (vertex.position[0] - sourceOffset.x) * scale;
        const float localY = (vertex.position[1] - sourceOffset.y) * scale;
        const float localZ = (vertex.position[2] - sourceOffset.z) * scale;
        vertex.position[0] = center.x + localX * yawCos + localZ * yawSin;
        vertex.position[1] = center.y + localY;
        vertex.position[2] = center.z - localX * yawSin + localZ * yawCos;
        const auto role = static_cast<MaterialRole>(vertex.materialRole);
        const auto slot = slotIndex(role);
        bool recolor = slot < static_cast<std::size_t>(Slot::Count);
        Style style = Style::Rose;
        if (recolor && outfit && outfit->equipped[slot])
            style = outfit->outfit[slot];
        else if (role == MaterialRole::Hair)
            recolor = true, style = Style::Violet;
        else if (role == MaterialRole::Top)
            recolor = true, style = Style::Rose;
        else if (role == MaterialRole::Bottom)
            recolor = true, style = Style::Violet;
        else if (role == MaterialRole::Shoes)
            recolor = true, style = Style::Gold;
        if (recolor) {
            const auto tint = kStyleColors[static_cast<std::size_t>(style)];
            vertex.color[0] = tint.x;
            vertex.color[1] = tint.y;
            vertex.color[2] = tint.z;
        }
        vertices_.push_back(vertex);
    }
    return true;
}

bool SceneContent::appendCharacter(
    const CharacterModel& model, float x, float z, const Snapshot* outfit)
{
    return appendMesh(model.vertices, 1.0f, model.yawCos, model.yawSin, {x, 0.0f, z}, {}, outfit);
}

bool SceneContent::appendPickup(
    const CharacterModel& model, Slot slot, Style style, bool obstacle, float x, float y, float z)
{
    const auto partSlot = static_cast<std::size_t>(slot);
    if (partSlot >= model.parts.size() || !model.parts[partSlot].valid)
        return false;
    const auto& part = model.parts[partSlot].vertices;
    core::Vec3 minimum{
        part.front().position[0], part.front().position[1], part.front().position[2]};
    core::Vec3 maximum = minimum;
    for (const auto& vertex : part) {
        minimum.x = std::min(minimum.x, vertex.position[0]);
        minimum.y = std::min(minimum.y, vertex.position[1]);
        minimum.z = std::min(minimum.z, vertex.position[2]);
        maximum.x = std::max(maximum.x, vertex.position[0]);
        maximum.y = std::max(maximum.y, vertex.position[1]);
        maximum.z = std::max(maximum.z, vertex.position[2]);
    }
    const core::Vec3 offset{(minimum.x + maximum.x) * 0.5f,
        (minimum.y + maximum.y) * 0.5f,
        (minimum.z + maximum.z) * 0.5f};
    const float span =
        std::max({maximum.x - minimum.x, maximum.y - minimum.y, maximum.z - minimum.z});
    if (!(span > 0.0f))
        return false;
    Snapshot tint;
    tint.equipped[partSlot] = true;
    tint.outfit[partSlot] = obstacle ? Style::Rose : style;
    return appendMesh(
        part, (obstacle ? 0.72f : 0.62f) / span, 1.0f, 0.0f, {x, y, z}, offset, &tint);
}

bool SceneContent::buildScene(const Snapshot& state,
    const Pickup* pickups,
    const bool* resolvedPickups,
    std::uint32_t pickupCount)
{
    vertices_.clear();
    sceneBuildValid_ = true;
    constexpr float playerZ = 0.0f;
    addCube({0.0f, -0.12f, 4.5f}, {2.8f, 0.12f, 6.0f}, kTrackColor);
    addCube({-2.86f, 0.18f, 4.5f}, {0.08f, 0.18f, 6.0f}, kTrackEdgeColor);
    addCube({2.86f, 0.18f, 4.5f}, {0.08f, 0.18f, 6.0f}, kTrackEdgeColor);
    for (const auto x : {-0.94f, 0.94f})
        addCube({x, 0.015f, 4.5f}, {0.018f, 0.015f, 6.0f}, kWhite);
    for (int index = 0; index < 11; ++index) {
        const float z = float(index) + 0.5f - state.distance * 0.1f;
        if (z < -1.0f || z > 10.0f)
            continue;
        addCube({-2.55f, 0.02f, z}, {0.07f, 0.02f, 0.08f}, kStyleColors[index % 5]);
        addCube({2.55f, 0.02f, z}, {0.07f, 0.02f, 0.08f}, kStyleColors[(index + 2) % 5]);
    }

    for (std::uint32_t index = 0; index < pickupCount; ++index) {
        if (resolvedPickups[index] || state.phase != Phase::Running)
            continue;
        const auto& pickup = pickups[index];
        const auto x = (float(pickup.lane) - 1.0f) * 1.88f;
        const auto z = (pickup.distance - state.distance) * 0.1f;
        if (z < -1.0f || z > 10.0f)
            continue;
        const auto& sourceCharacter =
            pickup.slot == Slot::Accessory ? characters_[1] : characters_[0];
        if (!appendPickup(sourceCharacter, pickup.slot, pickup.style, pickup.obstacle, x, 0.58f, z))
            return false;
    }

    if (state.phase == Phase::RunwayBattle) {
        addCube({0.0f, 0.32f, playerZ + 3.6f}, {2.75f, 0.18f, 0.55f}, kStyleColors[4]);
    }
    return sceneBuildValid_;
}

void SceneContent::addTriangle(core::Vec3 a, core::Vec3 b, core::Vec3 c, core::Vec4 color)
{
    if (vertices_.size() + 3 > vertexCapacity_) {
        sceneBuildValid_ = false;
        return;
    }
    const auto faceNormal = normal(a, b, c);
    const auto append = [&](core::Vec3 point) {
        vertices_.push_back({{point.x, point.y, point.z},
            {faceNormal.x, faceNormal.y, faceNormal.z},
            {color.x, color.y, color.z, color.w},
            static_cast<std::uint8_t>(MaterialRole::Preserve)});
    };
    append(a);
    append(b);
    append(c);
}

void SceneContent::addCube(core::Vec3 center, core::Vec3 halfExtent, core::Vec4 color)
{
    const core::Vec3 corners[] = {
        {center.x - halfExtent.x, center.y - halfExtent.y, center.z - halfExtent.z},
        {center.x + halfExtent.x, center.y - halfExtent.y, center.z - halfExtent.z},
        {center.x + halfExtent.x, center.y + halfExtent.y, center.z - halfExtent.z},
        {center.x - halfExtent.x, center.y + halfExtent.y, center.z - halfExtent.z},
        {center.x - halfExtent.x, center.y - halfExtent.y, center.z + halfExtent.z},
        {center.x + halfExtent.x, center.y - halfExtent.y, center.z + halfExtent.z},
        {center.x + halfExtent.x, center.y + halfExtent.y, center.z + halfExtent.z},
        {center.x - halfExtent.x, center.y + halfExtent.y, center.z + halfExtent.z},
    };
    constexpr std::uint8_t triangles[][3] = {
        {0, 2, 1},
        {0, 3, 2},
        {4, 5, 6},
        {4, 6, 7},
        {0, 1, 5},
        {0, 5, 4},
        {1, 2, 6},
        {1, 6, 5},
        {2, 3, 7},
        {2, 7, 6},
        {3, 0, 4},
        {3, 4, 7},
    };
    for (const auto& triangle : triangles)
        addTriangle(corners[triangle[0]], corners[triangle[1]], corners[triangle[2]], color);
}

renderer::RendererResult SceneContent::createContent(const core::pmr::String& characterDirectory)
{
    if (!characters_.empty() || !animatedCharacters_.empty())
        return {false, "ARP-QUEEN-CONTENT-ALREADY-CREATED"};
    if (!loadCharacters(characterDirectory) || characters_.size() != 2u
        || !loadAnimatedCharacters(characterDirectory) || animatedCharacters_.size() != 2u)
        return renderer::RendererResult{false, "ARP-QUEEN-3D-CHARACTER-ASSET-CONTRACT"};
    std::size_t maxPartVertices = 0;
    for (const auto& model : characters_) {
        for (const auto& part : model.parts)
            maxPartVertices = std::max(maxPartVertices, part.vertices.size());
    }
    constexpr std::size_t fixedSceneVertices = (5u + 22u + 1u) * 36u;
    if (maxPartVertices > (SIZE_MAX - fixedSceneVertices) / Game::kMaxPickups)
        return renderer::RendererResult{false, "ARP-QUEEN-3D-VERTEX-BUDGET-OVERFLOW"};
    const auto requiredVertices = fixedSceneVertices + maxPartVertices * Game::kMaxPickups
        + characters_[0].vertices.size() + characters_[1].vertices.size();
    if (requiredVertices > UINT32_MAX)
        return renderer::RendererResult{false, "ARP-QUEEN-3D-VERTEX-BUDGET-OVERFLOW"};
    vertexCapacity_ = static_cast<std::uint32_t>(requiredVertices);
    vertices_.reserve(vertexCapacity_);
    for (auto& character : animatedCharacters_) {
        character.player = core::makeUnique<animation::SkinnedMeshPlayer>(character.asset);
        if (!character.player->valid())
            return {false, "ARP-QUEEN-3D-SKINNED-PLAYER"};
    }
    return {true, {}};
}

core::Vec4 SceneContent::styleColor(Style style)
{
    return kStyleColors[static_cast<std::size_t>(style)];
}

bool SceneContent::appendAnimated(core::pmr::Vector<Vertex>& output,
    AnimatedCharacter& character,
    const Snapshot* appearance,
    float x,
    float z,
    float scale,
    float yaw,
    const char* clip,
    float seconds)
{
    const core::pmr::String name(clip, &core::defaultCpuMemoryResource());
    if (!character.player || !character.player->updateLooping(name, seconds)
        || character.vertices.size() > vertexCapacity_ - output.size())
        return false;
    const auto& matrices = character.player->skinMatrices();
    const float sine = std::sin(yaw), cosine = std::cos(yaw);
    for (auto vertex : character.vertices) {
        std::array<std::uint32_t, 4> joints;
        std::array<float, 4> weights;
        for (std::size_t lane = 0; lane < joints.size(); ++lane) {
            joints[lane] = static_cast<std::uint32_t>(vertex.joints[lane]);
            weights[lane] = vertex.weights[lane];
        }
        animation::LinearBlendSkinning::Result skin;
        if (!animation::LinearBlendSkinning::evaluate(matrices,
                joints,
                weights,
                {vertex.position[0], vertex.position[1], vertex.position[2]},
                {vertex.normal[0], vertex.normal[1], vertex.normal[2]},
                skin))
            return false;
        const float lx = (skin.position.x - character.origin.x) * character.scale * scale;
        const float ly = (skin.position.y - character.origin.y) * character.scale * scale;
        const float lz = (skin.position.z - character.origin.z) * character.scale * scale;
        vertex.position[0] = x + lx * cosine + lz * sine;
        vertex.position[1] = ly;
        vertex.position[2] = z - lx * sine + lz * cosine;
        vertex.normal[0] = skin.normal.x * cosine + skin.normal.z * sine;
        vertex.normal[1] = skin.normal.y;
        vertex.normal[2] = -skin.normal.x * sine + skin.normal.z * cosine;
        const auto slot = slotIndex(static_cast<MaterialRole>(vertex.materialRole));
        if (appearance && slot < appearance->equipped.size() && appearance->equipped[slot]) {
            const auto color = styleColor(appearance->outfit[slot]);
            vertex.color[0] = color.x;
            vertex.color[1] = color.y;
            vertex.color[2] = color.z;
        }
        output.push_back(vertex);
    }
    return true;
}

bool SceneContent::prepareFrame(const Snapshot& state,
    const LevelDesc& level,
    const Pickup* pickups,
    const bool* resolved,
    std::uint32_t count,
    float seconds,
    PreparedFrame& frame)
{
    if (animatedCharacters_.size() != 2 || !std::isfinite(seconds) || seconds < 0
        || count > Game::kMaxPickups || (count && (!pickups || !resolved))
        || !buildScene(state, pickups, resolved, count))
        return false;
    frame.world.reserve(vertexCapacity_);
    frame.preview.reserve(animatedCharacters_[0].vertices.size());
    frame.world.assign(vertices_.begin(), vertices_.end());
    frame.preview.clear();
    const bool result = state.phase == Phase::Result;
    constexpr float pi = 3.14159265358979323846f;
    if (!appendAnimated(frame.world,
            animatedCharacters_[0],
            &state,
            result ? -1.15f : (float(state.lane) - 1) * 1.88f,
            result ? 2 : 0,
            result ? 1.35f : 1,
            result ? pi : 0,
            state.phase == Phase::Running ? "Run" : "Idle",
            seconds))
        return false;
    if ((result || state.phase == Phase::RunwayBattle)
        && !appendAnimated(frame.world,
            animatedCharacters_[1],
            nullptr,
            1.15f,
            2,
            result ? 1.35f : 1,
            pi,
            result ? "Wave" : "Idle",
            seconds))
        return false;
    Snapshot target = state;
    if (!result) {
        target.outfit = level.target;
        target.equipped.fill(true);
    }
    return appendAnimated(
        frame.preview, animatedCharacters_[0], &target, 0, 0, 1, pi, "Idle", seconds);
}

} // namespace arp::build_a_queen

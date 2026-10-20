#pragma once
#include "animation/skinned_mesh_player.h"
#include "assets/glb_animated_mesh_importer.h"
#include "core/array_view.h"
#include "core/math.h"
#include "game.h"
#include "renderer/renderer_result.h"
#include "rhi/gpu_resources.h"

namespace arp::build_a_queen {
class SceneContent {
public:
    std::size_t previewVertexCapacity() const noexcept
    {
        return animatedCharacters_.empty() ? 0 : animatedCharacters_[0].vertices.size();
    }
    enum class MaterialRole : std::uint8_t { Preserve, Skin, Hair, Top, Bottom, Shoes, Accessory };

    struct Vertex {
        float position[3];
        float normal[3];
        float color[4];
        std::uint8_t materialRole = 0;
        float joints[4]{};
        float weights[4]{};
    };

    struct AnimatedCharacter {
        arp::assets::AnimatedMeshAsset asset;
        core::pmr::Vector<Vertex> vertices{&core::defaultCpuMemoryResource()};
        core::UniquePtr<rhi::GpuBuffer> vertexBuffer;
        core::UniquePtr<animation::SkinnedMeshPlayer> player;
        core::Vec3 origin{};
        float scale = 1.0f;
        std::uint32_t vertexCount = 0;
    };

    struct MaterialBinding {
        core::pmr::String name{&core::defaultCpuMemoryResource()};
        MaterialRole role = MaterialRole::Preserve;
    };

    struct CharacterPart {
        core::pmr::Vector<Vertex> vertices;
        bool valid = false;
    };

    struct CharacterModel {
        CharacterModel();

        core::pmr::String id{&core::defaultCpuMemoryResource()};
        core::pmr::String sourceFile{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<Vertex> vertices{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<MaterialBinding> materialBindings{&core::defaultCpuMemoryResource()};
        core::Vec3 boundsMin{};
        core::Vec3 boundsMax{};
        std::array<CharacterPart, static_cast<std::size_t>(Slot::Count)> parts;
        float scale = 1.0f;
        float yawCos = 1.0f;
        float yawSin = 0.0f;
    };

    struct PreparedFrame {
        core::pmr::Vector<Vertex> world{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<Vertex> preview{&core::defaultCpuMemoryResource()};
    };

    renderer::RendererResult createContent(const core::pmr::String& directory);
    bool prepareFrame(const Snapshot&,
        const LevelDesc&,
        const Pickup*,
        const bool*,
        std::uint32_t,
        float seconds,
        PreparedFrame&);

    std::uint32_t vertexCapacity() const { return vertexCapacity_; }

    static std::size_t slotIndex(MaterialRole);
    static core::Vec4 styleColor(Style);

protected:
    bool buildScene(const Snapshot&, const Pickup*, const bool*, std::uint32_t);
    bool loadCharacters(const core::pmr::String&);
    bool loadAnimatedCharacters(const core::pmr::String&);
    bool appendCharacter(const CharacterModel& model, float x, float z, const Snapshot* outfit);
    bool appendPickup(const CharacterModel& model,
        Slot slot,
        Style style,
        bool obstacle,
        float x,
        float y,
        float z);
    bool appendMesh(const core::pmr::Vector<Vertex>& mesh,
        float scale,
        float yawCos,
        float yawSin,
        core::Vec3 center,
        core::Vec3 sourceOffset,
        const Snapshot* outfit);
    void addCube(core::Vec3 center, core::Vec3 halfExtent, core::Vec4 color);
    void addTriangle(core::Vec3 a, core::Vec3 b, core::Vec3 c, core::Vec4 color);

    core::pmr::Vector<Vertex> vertices_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<CharacterModel> characters_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<AnimatedCharacter> animatedCharacters_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<assets::ImportedMaterialAsset> importedMaterials_{
        &core::defaultCpuMemoryResource()};
    std::uint32_t vertexCapacity_ = 0;
    float pickupSize_ = 0.0f;
    float obstacleSize_ = 0.0f;
    bool sceneBuildValid_ = true;

private:
    bool appendAnimated(core::pmr::Vector<Vertex>&,
        AnimatedCharacter&,
        const Snapshot*,
        float x,
        float z,
        float scale,
        float yaw,
        const char* clip,
        float seconds);
};
} // namespace arp::build_a_queen

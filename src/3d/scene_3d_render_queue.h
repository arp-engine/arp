#pragma once

#include "3d/components.h"
#include "core/cpu_allocator.h"
#include "reflection/reflection_macros.h"
#include "scene/camera.h"
#include "scene/ids.h"
#include "scene/transform.h"

namespace animation {

struct AnimationClip;
struct AnimatorControllerAsset;

} // namespace animation

namespace arp::scene3d {

AENUM()
enum class Scene3DDiagnosticCode {
    None,
    MissingRendererHandle,
    MissingBounds,
    InvalidBounds,
    MissingTerrainMetadata,
    MissingCameraMetadata,
};

ASTRUCT(Construct = Default)

struct Scene3DDiagnostic {
    APROP(Runtime)
    Scene3DDiagnosticCode code = Scene3DDiagnosticCode::None;
    APROP(Runtime)
    arp::core::pmr::String message;
};

AENUM()
enum class Scene3DParticipationSource {
    ComponentDriven,
};

AENUM()
enum class Scene3DRenderItemKind {
    Mesh,
    Terrain,
};

ASTRUCT(Construct = Default)

struct Scene3DRenderItem {
    APROP(Runtime)
    Scene3DRenderItemKind kind = Scene3DRenderItemKind::Mesh;
    APROP(Runtime)
    arp::scene::GameObjectId objectId;
    APROP(Runtime)
    arp::renderer::MeshHandle mesh;
    APROP(Runtime)
    TerrainHandle terrain;
    APROP(Runtime)
    arp::renderer::MaterialHandle material;
    APROP(Runtime)
    arp::scene::Transform worldTransform;
    APROP(Runtime)
    Bounds3D worldBounds;
    APROP(Runtime)
    arp::core::Vec4 baseColorTint{1.0f, 1.0f, 1.0f, 1.0f};
    APROP(Runtime)
    uint64_t sortKey = 0;
};

using Scene3DCameraBinding = arp::scene::ResolvedCamera;

ASTRUCT(Construct = Default)

struct Scene3DAnimationBinding {
    APROP(Runtime)
    arp::scene::GameObjectId objectId;
    const animation::AnimationClip* clip = nullptr;
    const animation::AnimatorControllerAsset* controller = nullptr;
};

ASTRUCT(Construct = Default)

struct Scene3DLightBinding {
    APROP(Runtime)
    arp::scene::GameObjectId objectId;
    APROP(Runtime)
    LightType type = LightType::Point;
    APROP(Runtime)
    arp::core::Vec3 worldPosition{};
    APROP(Runtime)
    arp::core::Vec3 worldDirection{0.0f, 1.0f, 0.0f};
    APROP(Runtime)
    arp::core::Vec3 color{1.0f, 1.0f, 1.0f};
    APROP(Runtime)
    float intensity = 1.0f;
    APROP(Runtime)
    float range = 0.0f;
    APROP(Runtime)
    float decay = 2.0f;
    APROP(Runtime)
    float spotConeAngle = 0.785398f;
    APROP(Runtime)
    float spotPenumbra = 0.2f;
    APROP(Runtime)
    bool castShadow = false;
};

class Scene3DRenderQueue {
public:
    void addItem(Scene3DRenderItem item);
    void addCamera(Scene3DCameraBinding camera);
    void addAnimation(Scene3DAnimationBinding animation);
    void addLight(Scene3DLightBinding light);

    void recordDisabledComponent() { ++disabledComponentCount_; }

    void recordDisabledComponents(size_t count) { disabledComponentCount_ += count; }

    void recordSkippedInvalidComponent() { ++skippedInvalidComponentCount_; }

    const arp::core::pmr::Vector<Scene3DRenderItem>& items() const { return items_; }

    const arp::core::pmr::Vector<Scene3DCameraBinding>& cameras() const { return cameras_; }

    const arp::core::pmr::Vector<Scene3DAnimationBinding>& animations() const
    {
        return animations_;
    }

    const arp::core::pmr::Vector<Scene3DLightBinding>& lights() const { return lights_; }

    size_t meshItemCount() const { return meshItemCount_; }

    size_t terrainItemCount() const { return terrainItemCount_; }

    size_t cameraCount() const { return cameras_.size(); }

    size_t animationBindingCount() const { return animations_.size(); }

    size_t lightCount() const { return lights_.size(); }

    size_t disabledComponentCount() const { return disabledComponentCount_; }

    size_t skippedInvalidComponentCount() const { return skippedInvalidComponentCount_; }

    Scene3DParticipationSource participationSource() const
    {
        return Scene3DParticipationSource::ComponentDriven;
    }

    bool usesSceneModeFlag() const { return false; }

    bool copiedStaticVertexDataInHotPath() const { return false; }

    size_t staticVertexDataCopyCount() const { return 0; }

    bool exposesNativeBackendHandles() const { return false; }

    size_t nativeBackendHandleCount() const { return 0; }

private:
    arp::core::pmr::Vector<Scene3DRenderItem> items_;
    arp::core::pmr::Vector<Scene3DCameraBinding> cameras_;
    arp::core::pmr::Vector<Scene3DAnimationBinding> animations_;
    arp::core::pmr::Vector<Scene3DLightBinding> lights_;
    size_t meshItemCount_ = 0;
    size_t terrainItemCount_ = 0;
    size_t disabledComponentCount_ = 0;
    size_t skippedInvalidComponentCount_ = 0;
};

class Scene3DExtractionResult {
public:
    static Scene3DExtractionResult success(Scene3DRenderQueue queue);
    static Scene3DExtractionResult failure(
        Scene3DDiagnosticCode code, arp::core::pmr::String message);

    bool ok() const { return diagnostic_.code == Scene3DDiagnosticCode::None; }

    const Scene3DDiagnostic& diagnostic() const { return diagnostic_; }

    const Scene3DRenderQueue& queue() const { return queue_; }

private:
    Scene3DExtractionResult(Scene3DDiagnostic diagnostic, Scene3DRenderQueue queue);

    Scene3DDiagnostic diagnostic_;
    Scene3DRenderQueue queue_;
};

} // namespace arp::scene3d

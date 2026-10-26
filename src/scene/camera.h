#pragma once

#include "3d/camera_parameters.h"
#include "core/math/math.h"
#include "core/memory/cpu_allocator.h"
#include "reflection/reflection_macros.h"
#include "scene/component.h"
#include "scene/diagnostics.h"
#include "scene/scene_traversal_snapshot.h"
#include "scene/transform.h"

namespace arp::scene {

AENUM()
enum class CameraProjection {
    Perspective,
    Orthographic,
};

AENUM()
enum class CameraViewHandedness {
    LeftHanded,
    RightHanded,
};

AENUM()
enum class CameraAspectPolicyKind {
    Fixed,
    MatchViewport,
};

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct CameraAspectPolicy {
    APROP(Serialize, Editor)
    CameraAspectPolicyKind kind = CameraAspectPolicyKind::Fixed;
    APROP(Serialize, Editor)
    float aspectRatio = 1.0f;

    static CameraAspectPolicy fixed(float aspectRatio);
    static CameraAspectPolicy matchViewport();
    bool valid() const;
};

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct ViewportRect {
    APROP(Serialize, Editor)
    float x = 0.0f;
    APROP(Serialize, Editor)
    float y = 0.0f;
    APROP(Serialize, Editor)
    float width = 1.0f;
    APROP(Serialize, Editor)
    float height = 1.0f;

    static ViewportRect normalized(float x, float y, float width, float height);
    bool valid() const;
};

ACLASS(Ownership = Value, Lifecycle = Value)

class RenderQueueMask {
public:
    static RenderQueueMask none();
    static RenderQueueMask queue2D();
    static RenderQueueMask queue3D();
    static RenderQueueMask world2DAnd3D();

    bool empty() const { return bits_ == 0; }

    bool includes2D() const { return (bits_ & 0x1u) != 0; }

    bool includes3D() const { return (bits_ & 0x2u) != 0; }

    uint32_t bits() const { return bits_; }

private:
    explicit RenderQueueMask(uint32_t bits) : bits_(bits) {}

    uint32_t bits_ = 0;
};

AENUM()
enum class CameraLoadOp {
    Load,
    Clear,
};

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct CameraClearIntent {
    APROP(Serialize, Editor)
    CameraLoadOp colorLoadOp = CameraLoadOp::Load;
    float clearColor[4]{0.0f, 0.0f, 0.0f, 1.0f};

    static CameraClearIntent load();
    static CameraClearIntent clear(float red, float green, float blue, float alpha);

    bool metadataOnly() const { return true; }
};

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct CameraComponentDesc {
    APROP(Serialize, Editor)
    CameraProjection projection = CameraProjection::Perspective;
    APROP(Serialize, Editor)
    CameraViewHandedness viewHandedness = CameraViewHandedness::LeftHanded;
    APROP(Serialize, Editor)
    float verticalFovDegrees = 60.0f;
    APROP(Serialize, Editor)
    float orthographicHeight = 16.0f;
    APROP(Serialize, Editor)
    float nearPlane = 0.1f;
    APROP(Serialize, Editor)
    float farPlane = 1000.0f;
    APROP(Serialize, Editor)
    CameraAspectPolicy aspectPolicy = CameraAspectPolicy::fixed(1.0f);
    APROP(Serialize, Editor)
    ViewportRect viewport = ViewportRect::normalized(0.0f, 0.0f, 1.0f, 1.0f);
    APROP(Serialize, Editor)
    RenderQueueMask visibleQueues = RenderQueueMask::queue3D();
    APROP(Serialize, Editor)
    int priority = 0;
    APROP(Serialize, Editor)
    CameraClearIntent clearIntent = CameraClearIntent::load();
    core::Camera camera;

    static CameraComponentDesc perspective(
        float verticalFovDegrees, float aspectRatio, float nearPlane, float farPlane);
    static CameraComponentDesc orthographic(
        float orthographicHeight, float nearPlane, float farPlane);
};

ACLASS(Ownership = Engine, Lifecycle = Explicit)

class CameraComponent final : public Component {
public:
    const char* scriptTypeName() const override { return "CameraComponent"; }

    CameraComponent() = default;
    explicit CameraComponent(CameraComponentDesc desc);

    AFUNC()

    std::uint64_t component_id() const { return id().value(); }
    AFUNC()

    std::uint64_t owner_id() const { return ownerId().value(); }
    AFUNC()

    bool enabled() const { return Component::enabled(); }
    AFUNC()

    void set_enabled(bool value) { Component::setEnabled(value); }

    const CameraComponentDesc& desc() const { return desc_; }

    CameraComponentDesc& desc() { return desc_; }

    arp::reflection::object editorAuthoringProperties() const override
    {
        return arp::reflection::object(desc_);
    }

    bool applyEditorAuthoringProperties(const arp::reflection::object& value) override
    {
        if (!value.is<CameraComponentDesc>())
            return false;
        desc_ = value.cast<CameraComponentDesc>();
        return true;
    }

    // The descriptor is the complete script-facing camera state. Derived
    // projection caches remain native and are rebuilt by CameraResolver.
    AFUNC()

    CameraComponentDesc script_desc() const { return desc_; }
    AFUNC()

    void set_script_desc(const CameraComponentDesc& value) { desc_ = value; }
    AFUNC()

    CameraProjection projection() const { return desc_.projection; }
    AFUNC()

    void set_projection(CameraProjection value) { desc_.projection = value; }
    AFUNC()

    float vertical_fov_degrees() const { return desc_.verticalFovDegrees; }
    AFUNC()

    void set_vertical_fov_degrees(float value) { desc_.verticalFovDegrees = value; }
    AFUNC()

    float near_plane() const { return desc_.nearPlane; }
    AFUNC()

    void set_near_plane(float value) { desc_.nearPlane = value; }
    AFUNC()

    float far_plane() const { return desc_.farPlane; }
    AFUNC()

    void set_far_plane(float value) { desc_.farPlane = value; }

private:
    CameraComponentDesc desc_;
};

AENUM()
enum class CameraDiagnosticCode {
    None,
    InvalidNearFar,
    InvalidViewport,
    InvalidAspectPolicy,
    EmptyRenderQueueMask,
    InvalidViewHandedness,
};

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct CameraDiagnostic {
    APROP(Runtime)
    CameraDiagnosticCode code = CameraDiagnosticCode::None;
    APROP(Runtime)
    arp::core::pmr::String message;
};

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct ResolvedCamera {
    APROP(Runtime)
    GameObjectId objectId;
    APROP(Runtime)
    ComponentId componentId;
    APROP(Runtime)
    CameraProjection projection = CameraProjection::Perspective;
    APROP(Runtime)
    CameraViewHandedness viewHandedness = CameraViewHandedness::LeftHanded;
    core::Camera camera;
    APROP(Runtime)
    Transform worldTransform;
    APROP(Runtime)
    ViewportRect viewport;
    APROP(Runtime)
    RenderQueueMask visibleQueues = RenderQueueMask::none();
    APROP(Runtime)
    CameraAspectPolicy aspectPolicy;
    APROP(Runtime)
    CameraClearIntent clearIntent;
    APROP(Runtime)
    int priority = 0;
    APROP(Runtime)
    float orthographicHeight = 0.0f;
    APROP(Runtime)
    float orthographicWidth = 0.0f;
    // --- Derived per-frame matrices/orientation (populated by CameraResolver
    // from worldTransform.rotation + the projection params). These are the
    // single source the render path reads instead of recomputing lookAt/
    // perspective at each consumer. The pose uses local -Z forward;
    // viewHandedness chooses the view-space depth and horizontal basis. ---
    APROP(Runtime)
    arp::core::Vec3 forward{0.0f, 0.0f, -1.0f};
    APROP(Runtime)
    arp::core::Mat4 viewMatrix;
    APROP(Runtime)
    arp::core::Mat4 projectionMatrix;
    // Light/visibility cullers use positive distance in front of the camera,
    // independent of view handedness. X/Y retain the rendering view basis.
    arp::core::Mat4 depthPositiveViewMatrix;
    float depthPositiveProjectionScale = 0.0f;
};

class CameraResolverResult {
public:
    static CameraResolverResult success(arp::core::pmr::Vector<ResolvedCamera> cameras);
    static CameraResolverResult failure(CameraDiagnosticCode code, arp::core::pmr::String message);

    bool ok() const { return diagnostic_.code == CameraDiagnosticCode::None; }

    const CameraDiagnostic& diagnostic() const { return diagnostic_; }

    const arp::core::pmr::Vector<ResolvedCamera>& cameras() const { return cameras_; }

    bool usesSpecialCameraObjectClass() const { return false; }

private:
    CameraResolverResult(
        CameraDiagnostic diagnostic, arp::core::pmr::Vector<ResolvedCamera> cameras);

    CameraDiagnostic diagnostic_;
    arp::core::pmr::Vector<ResolvedCamera> cameras_;
};

class Scene;

class CameraResolver {
public:
    CameraResolverResult resolve(const Scene& scene, const SceneTraversalSnapshot& snapshot) const;
    static CameraResolverResult validate(const CameraComponentDesc& desc);

private:
    ResolvedCamera resolveCamera(
        const CameraComponent& component, const SceneTraversalNode& node) const;
};

} // namespace arp::scene

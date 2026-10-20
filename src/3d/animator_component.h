#pragma once

#include "3d/bounds.h"
#include "core/callback.h"
#include "core/cpu_allocator.h"
#include "reflection/reflection_macros.h"
#include "resource/resource_manager.h"
#include "scene/component.h"
#include "scene/render_extract.h"

#include <cstddef>

namespace animation {

struct AnimationClip;
struct AnimatorControllerAsset;
class AnimatorControllerRuntime;
class SkinnedMeshPlayer;

} // namespace animation

namespace arp::assets {

struct AnimatedMeshAsset;
struct MeshAsset;

} // namespace arp::assets

namespace arp::scene3d {

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)
struct AnimatorEditorProperties {
    APROP(Editor, Doc = "resource:PrefabResource:optional")
    arp::core::pmr::String prefabResourceId{&arp::core::defaultCpuMemoryResource()};
    APROP(Editor)
    arp::core::pmr::String defaultStateName{&arp::core::defaultCpuMemoryResource()};
};

enum class AnimatorEvaluationDiagnosticCode {
    None,
    MissingAsset,
    MissingAnimator,
    MissingSkinnedMesh,
    MissingAnimationState,
    MissingActiveClip,
    SkinningFailed,
};

struct AnimatorEvaluationDiagnostic {
    AnimatorEvaluationDiagnosticCode code = AnimatorEvaluationDiagnosticCode::None;
    arp::core::pmr::String message{&arp::core::defaultCpuMemoryResource()};
};

struct AnimatorComponentDesc {
    const animation::AnimationClip* clip = nullptr;
    const arp::assets::AnimatedMeshAsset* character = nullptr;
    const animation::AnimatorControllerAsset* controller = nullptr;
    arp::core::pmr::String defaultStateName{&arp::core::defaultCpuMemoryResource()};
    std::size_t materialCount = 0;
};

struct AnimatorEvaluationRequest {
    arp::core::pmr::String stateName{&arp::core::defaultCpuMemoryResource()};
    float deltaSeconds = 0.0f;
};

/// Animation evaluation frame. GPU-skinning-only since Phase 3: the CPU-baked
/// mesh field was removed; the renderer consumes the immutable bind-pose
/// vertex stream plus the GPU skinning palette built from
/// SkinnedMeshPlayer::skinMatrices(). The frame carries only animation-state
/// metadata (bounds from the bind-pose mesh, clip name, clip time, joint
/// count) so callers can drive gameplay logic and diagnostics without
/// touching skinned vertices.
class AnimatorEvaluationFrame final {
public:
    AnimatorEvaluationFrame() = default;
    AnimatorEvaluationFrame(Bounds3D bounds,
        std::size_t materialCount,
        arp::core::pmr::String stateName,
        arp::core::pmr::String clipName,
        float clipTimeSeconds,
        std::size_t jointCount);

    const Bounds3D& bounds() const { return bounds_; }

    std::size_t materialCount() const { return materialCount_; }

    const arp::core::pmr::String& stateName() const { return stateName_; }

    const arp::core::pmr::String& clipName() const { return clipName_; }

    float clipTimeSeconds() const { return clipTimeSeconds_; }

    std::size_t jointCount() const { return jointCount_; }

    bool exposesBackendHandles() const { return false; }

private:
    Bounds3D bounds_;
    std::size_t materialCount_ = 0;
    arp::core::pmr::String stateName_{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String clipName_{&arp::core::defaultCpuMemoryResource()};
    float clipTimeSeconds_ = 0.0f;
    std::size_t jointCount_ = 0;
};

class AnimatorEvaluationResult final {
public:
    static AnimatorEvaluationResult success(AnimatorEvaluationFrame frame);
    static AnimatorEvaluationResult failure(
        AnimatorEvaluationDiagnosticCode code, const char* message);

    bool ok() const { return diagnostic_.code == AnimatorEvaluationDiagnosticCode::None; }

    const AnimatorEvaluationDiagnostic& diagnostic() const { return diagnostic_; }

    const AnimatorEvaluationFrame& frame() const { return frame_; }

private:
    AnimatorEvaluationDiagnostic diagnostic_;
    AnimatorEvaluationFrame frame_;
};

class AnimatorComponent final : public arp::scene::Component {
public:
    const char* scriptTypeName() const override { return "AnimatorComponent"; }

    explicit AnimatorComponent(AnimatorComponentDesc desc,
        arp::core::SharedCallback<arp::resource::ResourceManager*()> resourceManagerProvider = {});
    ~AnimatorComponent() override;

    const AnimatorComponentDesc& desc() const { return desc_; }

    arp::reflection::object editorAuthoringProperties() const override;
    bool applyEditorAuthoringProperties(const arp::reflection::object& value) override;

    AnimatorEvaluationResult evaluate(const AnimatorEvaluationRequest& request);
    const arp::core::pmr::String& editorBindingDiagnostic() const noexcept
    {
        return editorBindingDiagnostic_;
    }

    bool parsesSourceAssetsInUpdatePath() const { return false; }

    /// Self-describing render extraction: pushes a `scene3d.animation` Custom
    /// payload carrying the clip and controller pointers. The 3D extractor's
    /// sink interprets this typeId to add an animation binding to the queue.
    void extract(const arp::scene::RenderExtractContext& ctx,
        arp::scene::RenderExtractSink& sink) const override;

private:
    bool resolveEditorBinding(const AnimatorEditorProperties& properties,
        arp::core::SharedPtr<const arp::assets::AnimatedMeshAsset>& meshOwner,
        arp::core::SharedPtr<const animation::AnimatorControllerAsset>& controllerOwner,
        arp::core::pmr::String& diagnostics) const;
    bool refreshEditorBinding();
    void installEditorBinding(
        arp::core::SharedPtr<const arp::assets::AnimatedMeshAsset> meshOwner,
        arp::core::SharedPtr<const animation::AnimatorControllerAsset> controllerOwner);

    AnimatorComponentDesc desc_;
    AnimatorEditorProperties editorProperties_;
    arp::core::SharedCallback<arp::resource::ResourceManager*()> resourceManagerProvider_;
    arp::resource::ResourceManager* boundResourceManager_ = nullptr;
    arp::resource::ResourceGeneration boundPrefabGeneration_ = 0u;
    arp::resource::ResourceGeneration boundRuntimeGeneration_ = 0u;
    arp::core::pmr::String boundPrefabResourceId_{&arp::core::defaultCpuMemoryResource()};
    arp::core::SharedPtr<const arp::assets::AnimatedMeshAsset> meshOwner_;
    arp::core::SharedPtr<const animation::AnimatorControllerAsset> controllerOwner_;
    arp::core::UniquePtr<animation::AnimatorControllerRuntime> animator_;
    arp::core::UniquePtr<animation::SkinnedMeshPlayer> player_;
    arp::core::pmr::String currentStateName_{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String editorBindingDiagnostic_{&arp::core::defaultCpuMemoryResource()};
};

} // namespace arp::scene3d

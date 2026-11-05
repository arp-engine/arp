#include "3d/animator_component.h"

#include "3d/components.h" // Scene3DAnimationExtractPayload (extract contract)
#include "animation/animator_controller.h"
#include "animation/skinned_mesh_player.h"
#include "assets/glb_animated_mesh_importer.h"
#include "resource/prefab_asset_resources.h"

#include <utility>

namespace arp::scene3d {

namespace {

arp::core::pmr::String makeString(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

const arp::core::pmr::String* requestedStateName(
    const AnimatorComponentDesc& desc, const AnimatorEvaluationRequest& request)
{
    if (!request.stateName.empty()) {
        return &request.stateName;
    }
    if (!desc.defaultStateName.empty()) {
        return &desc.defaultStateName;
    }
    if (desc.controller != nullptr && !desc.controller->defaultStateName.empty()) {
        return &desc.controller->defaultStateName;
    }
    return nullptr;
}

Bounds3D boundsFromMesh(const arp::assets::MeshAsset& mesh)
{
    if (mesh.vertices.empty()) {
        return {};
    }

    auto minX = mesh.vertices.front().position[0];
    auto minY = mesh.vertices.front().position[1];
    auto minZ = mesh.vertices.front().position[2];
    auto maxX = minX;
    auto maxY = minY;
    auto maxZ = minZ;

    for (const auto& vertex : mesh.vertices) {
        const auto x = vertex.position[0];
        const auto y = vertex.position[1];
        const auto z = vertex.position[2];
        minX = x < minX ? x : minX;
        minY = y < minY ? y : minY;
        minZ = z < minZ ? z : minZ;
        maxX = x > maxX ? x : maxX;
        maxY = y > maxY ? y : maxY;
        maxZ = z > maxZ ? z : maxZ;
    }

    return Bounds3D::explicitBounds(
        {
            (minX + maxX) * 0.5f,
            (minY + maxY) * 0.5f,
            (minZ + maxZ) * 0.5f,
        },
        {
            (maxX - minX) * 0.5f,
            (maxY - minY) * 0.5f,
            (maxZ - minZ) * 0.5f,
        });
}

float clampedDeltaSeconds(const float deltaSeconds)
{
    return deltaSeconds < 0.0f ? 0.0f : deltaSeconds;
}

} // namespace

AnimatorEvaluationFrame::AnimatorEvaluationFrame(Bounds3D bounds,
    const std::size_t materialCount,
    arp::core::pmr::String stateName,
    arp::core::pmr::String clipName,
    const float clipTimeSeconds,
    const std::size_t jointCount)
    : bounds_(bounds), materialCount_(materialCount), stateName_(std::move(stateName)),
      clipName_(std::move(clipName)), clipTimeSeconds_(clipTimeSeconds), jointCount_(jointCount)
{}

AnimatorEvaluationResult AnimatorEvaluationResult::success(AnimatorEvaluationFrame frame)
{
    AnimatorEvaluationResult result;
    result.frame_ = std::move(frame);
    return result;
}

AnimatorEvaluationResult AnimatorEvaluationResult::failure(
    const AnimatorEvaluationDiagnosticCode code, const char* message)
{
    AnimatorEvaluationResult result;
    result.diagnostic_.code = code;
    result.diagnostic_.message = makeString(message);
    return result;
}

AnimatorComponent::AnimatorComponent(AnimatorComponentDesc desc,
    arp::core::SharedCallback<arp::resource::ResourceManager*()> resourceManagerProvider)
    : desc_(std::move(desc)), resourceManagerProvider_(std::move(resourceManagerProvider))
{
    if (desc_.controller != nullptr) {
        animator_ = arp::core::makeUnique<animation::AnimatorControllerRuntime>(*desc_.controller);
    }
    if (desc_.character != nullptr) {
        player_ = arp::core::makeUnique<animation::SkinnedMeshPlayer>(*desc_.character);
    }
}

AnimatorComponent::~AnimatorComponent() = default;

arp::reflection::object AnimatorComponent::editorAuthoringProperties() const
{
    return arp::reflection::object(editorProperties_);
}

namespace {
bool validateRuntimePayload(const arp::resource::PrefabRuntimePayloadResource* runtime,
    arp::core::StringView requestedState,
    arp::core::pmr::String& diagnostics)
{
    if (!runtime) {
        diagnostics = "ANIMATOR-RUNTIME-RESOURCE-NOT-READY";
        return false;
    }
    const auto* mesh = runtime->runtimeAsset.get();
    const auto* controller = runtime->controllerAsset.get();
    if (!mesh || !controller || mesh->skeleton.jointCount() == 0u) {
        diagnostics = "ANIMATOR-RUNTIME-PAYLOAD-INCOMPLETE";
        return false;
    }
    if (!runtime->defaultStateName.empty() && !controller->defaultStateName.empty()
        && runtime->defaultStateName != controller->defaultStateName) {
        diagnostics = "ANIMATOR-DEFAULT-STATE-MISMATCH";
        return false;
    }
    const auto stateName = requestedState.empty()
        ? arp::core::StringView(controller->defaultStateName)
        : requestedState;
    const auto state = std::find_if(
        controller->states.begin(), controller->states.end(), [&](const auto& candidate) {
            return candidate.name == stateName;
        });
    if (stateName.empty() || state == controller->states.end()) {
        diagnostics = "ANIMATOR-DEFAULT-STATE-UNKNOWN";
        return false;
    }
    const auto clip =
        std::find_if(mesh->clips.begin(), mesh->clips.end(), [&](const auto& candidate) {
            return candidate.name == state->clipName;
        });
    if (clip == mesh->clips.end()) {
        diagnostics = "ANIMATOR-STATE-CLIP-MISSING";
        return false;
    }
    const animation::SkinnedMeshPlayer candidatePlayer(*mesh);
    if (!candidatePlayer.valid()) {
        diagnostics = "ANIMATOR-SKINNED-MESH-INVALID";
        return false;
    }
    return true;
}
} // namespace

struct AnimatorComponent::ResolvedEditorBinding {
    arp::resource::ResourceManager* manager = nullptr;
    arp::resource::ResourceHandle<arp::resource::PrefabRuntimePayloadResource> runtime;
    arp::resource::ResourceGeneration prefabGeneration = 0;
};

const AnimatorComponentDesc& AnimatorComponent::desc() const
{
    const_cast<AnimatorComponent*>(this)->refreshResourceBinding();
    return desc_;
}

bool AnimatorComponent::bindResource(
    arp::resource::ResourceLease lease, arp::core::StringView defaultStateName)
{
    arp::core::pmr::String diagnostics(&arp::core::defaultCpuMemoryResource());
    if (defaultStateName.size() > 128u) {
        editorBindingDiagnostic_ = "ANIMATOR-DEFAULT-STATE-TOO-LONG";
        return false;
    }
    const auto* runtime = lease.get<arp::resource::PrefabRuntimePayloadResource>();
    if (!validateRuntimePayload(runtime, defaultStateName, diagnostics)) {
        editorBindingDiagnostic_ = std::move(diagnostics);
        return false;
    }
    installBorrowedViews(
        runtime->runtimeAsset.get(), runtime->controllerAsset.get(), defaultStateName);
    resourceLease_ = std::move(lease);
    leasedBinding_ = true;
    editorProperties_ = {};
    boundResourceManager_ = nullptr;
    boundPrefabResourceId_.clear();
    boundPrefabGeneration_ = boundRuntimeGeneration_ = 0;
    editorBindingDiagnostic_.clear();
    return true;
}

void AnimatorComponent::clearBorrowedViews()
{
    animator_.reset();
    player_.reset();
    desc_.character = nullptr;
    desc_.controller = nullptr;
    desc_.clip = nullptr;
    desc_.materialCount = 0;
    desc_.defaultStateName.clear();
    currentStateName_.clear();
}

void AnimatorComponent::clearResourceBinding()
{
    clearBorrowedViews();
    resourceLease_.reset();
    leasedBinding_ = false;
    editorProperties_ = {};
    boundResourceManager_ = nullptr;
    boundPrefabResourceId_.clear();
    boundPrefabGeneration_ = boundRuntimeGeneration_ = 0;
    editorBindingDiagnostic_.clear();
}

void AnimatorComponent::installBorrowedViews(const arp::assets::AnimatedMeshAsset* mesh,
    const animation::AnimatorControllerAsset* controller,
    arp::core::StringView requestedState)
{
    arp::core::UniquePtr<animation::AnimatorControllerRuntime> animator;
    arp::core::UniquePtr<animation::SkinnedMeshPlayer> player;
    arp::core::pmr::String state(&arp::core::defaultCpuMemoryResource());
    const animation::AnimationClip* clip = nullptr;
    if (mesh && controller) {
        state = requestedState.empty() ? arp::core::StringView(controller->defaultStateName)
                                       : requestedState;
        const auto selected = std::find_if(
            controller->states.begin(), controller->states.end(), [&](const auto& candidate) {
                return candidate.name == state;
            });
        if (selected != controller->states.end()) {
            const auto found =
                std::find_if(mesh->clips.begin(), mesh->clips.end(), [&](const auto& candidate) {
                    return candidate.name == selected->clipName;
                });
            if (found != mesh->clips.end())
                clip = &*found;
        }
        animator = arp::core::makeUnique<animation::AnimatorControllerRuntime>(*controller);
        player = arp::core::makeUnique<animation::SkinnedMeshPlayer>(*mesh);
    }
    clearBorrowedViews();
    animator_ = std::move(animator);
    player_ = std::move(player);
    desc_.character = mesh;
    desc_.controller = controller;
    desc_.clip = clip;
    desc_.materialCount = mesh ? mesh->materials.size() : 0;
    desc_.defaultStateName = std::move(state);
}

bool AnimatorComponent::applyEditorAuthoringProperties(const arp::reflection::object& value)
{
    if (!value.is<AnimatorEditorProperties>())
        return false;
    const auto& properties = value.cast<const AnimatorEditorProperties&>();
    if (properties.prefabResourceId.size() > 512u || properties.defaultStateName.size() > 128u)
        return false;
    ResolvedEditorBinding binding;
    arp::core::pmr::String diagnostics(&arp::core::defaultCpuMemoryResource());
    if (!resolveEditorBinding(properties, binding, diagnostics)) {
        editorBindingDiagnostic_ = std::move(diagnostics);
        return false;
    }
    auto stagedProperties = properties;
    installEditorBinding(binding, stagedProperties);
    editorProperties_ = std::move(stagedProperties);
    editorBindingDiagnostic_.clear();
    return true;
}

bool AnimatorComponent::resolveEditorBinding(const AnimatorEditorProperties& properties,
    ResolvedEditorBinding& binding,
    arp::core::pmr::String& diagnostics) const
{
    if (properties.prefabResourceId.empty())
        return true;
    auto* manager = resourceManagerProvider_ ? resourceManagerProvider_() : nullptr;
    if (!manager || !manager->isOwnerThread()) {
        diagnostics = "ANIMATOR-RESOURCE-MANAGER-UNAVAILABLE";
        return false;
    }
    const auto prefab = manager->find<arp::resource::PrefabResource>(properties.prefabResourceId);
    if (!prefab.ready()) {
        diagnostics =
            prefab.diagnostics().empty() ? "ANIMATOR-PREFAB-NOT-READY" : prefab.diagnostics();
        return false;
    }
    if (prefab.get()->runtimePayloadId.empty()) {
        diagnostics = "ANIMATOR-RUNTIME-PAYLOAD-ID-MISSING";
        return false;
    }
    const auto runtime =
        manager->find<arp::resource::PrefabRuntimePayloadResource>(prefab.get()->runtimePayloadId);
    if (!runtime.ready()) {
        diagnostics = runtime.diagnostics().empty() ? "ANIMATOR-RUNTIME-PAYLOAD-NOT-READY"
                                                    : runtime.diagnostics();
        return false;
    }
    if (!validateRuntimePayload(runtime.get(), properties.defaultStateName, diagnostics))
        return false;
    binding.manager = manager;
    binding.runtime = runtime;
    binding.prefabGeneration = prefab.generation();
    return true;
}

void AnimatorComponent::installEditorBinding(
    const ResolvedEditorBinding& binding, const AnimatorEditorProperties& properties)
{
    const auto* runtime = binding.runtime.get();
    arp::core::pmr::String prefabId(
        runtime ? arp::core::StringView(properties.prefabResourceId) : arp::core::StringView{},
        &arp::core::defaultCpuMemoryResource());
    installBorrowedViews(runtime ? runtime->runtimeAsset.get() : nullptr,
        runtime ? runtime->controllerAsset.get() : nullptr,
        properties.defaultStateName);
    resourceLease_.reset();
    leasedBinding_ = false;
    boundResourceManager_ = binding.manager;
    boundPrefabGeneration_ = binding.prefabGeneration;
    boundRuntimeGeneration_ = binding.runtime.generation();
    boundPrefabResourceId_ = std::move(prefabId);
}

bool AnimatorComponent::refreshResourceBinding()
{
    if (leasedBinding_) {
        if (resourceLease_.get<arp::resource::PrefabRuntimePayloadResource>())
            return true;
        clearBorrowedViews();
        editorBindingDiagnostic_ = "ANIMATOR-RUNTIME-RESOURCE-NOT-READY";
        return false;
    }
    return refreshEditorBinding();
}

bool AnimatorComponent::refreshEditorBinding()
{
    if (editorProperties_.prefabResourceId.empty())
        return true;
    auto* manager = resourceManagerProvider_ ? resourceManagerProvider_() : nullptr;
    if (!manager || !manager->isOwnerThread()) {
        installEditorBinding({}, editorProperties_);
        editorBindingDiagnostic_ = "ANIMATOR-RESOURCE-MANAGER-UNAVAILABLE";
        return false;
    }
    const auto prefab =
        manager->find<arp::resource::PrefabResource>(editorProperties_.prefabResourceId);
    const auto runtime = prefab.ready()
        ? manager->find<arp::resource::PrefabRuntimePayloadResource>(prefab.get()->runtimePayloadId)
        : arp::resource::ResourceHandle<arp::resource::PrefabRuntimePayloadResource>{};
    if (prefab.ready() && runtime.ready() && boundResourceManager_ == manager
        && boundPrefabResourceId_ == editorProperties_.prefabResourceId
        && boundPrefabGeneration_ == prefab.generation()
        && boundRuntimeGeneration_ == runtime.generation())
        return true;

    ResolvedEditorBinding binding;
    arp::core::pmr::String diagnostics(&arp::core::defaultCpuMemoryResource());
    if (!resolveEditorBinding(editorProperties_, binding, diagnostics)) {
        installEditorBinding({}, editorProperties_);
        editorBindingDiagnostic_ = std::move(diagnostics);
        return false;
    }
    installEditorBinding(binding, editorProperties_);
    editorBindingDiagnostic_.clear();
    return true;
}

void AnimatorComponent::extract(
    const arp::scene::RenderExtractContext& ctx, arp::scene::RenderExtractSink& sink) const
{
    if (!ctx.activeInHierarchy || !enabled()) {
        arp::scene::RenderExtractPayload marker;
        marker.kind = arp::scene::RenderExtractKind::Custom;
        marker.typeId = "scene3d.disabled";
        sink.accept(ctx, marker);
        return;
    }
    if (!const_cast<AnimatorComponent*>(this)->refreshResourceBinding()) {
        arp::scene::RenderExtractPayload marker;
        marker.kind = arp::scene::RenderExtractKind::Custom;
        marker.typeId = "scene3d.animation.unavailable";
        sink.accept(ctx, marker);
        return;
    }
    Scene3DAnimationExtractPayload domain{desc_.clip, desc_.controller};
    arp::scene::RenderExtractPayload payload;
    payload.kind = arp::scene::RenderExtractKind::Custom;
    payload.typeId = "scene3d.animation";
    payload.data = &domain;
    sink.accept(ctx, payload);
}

AnimatorEvaluationResult AnimatorComponent::evaluate(const AnimatorEvaluationRequest& request)
{
    if (!refreshResourceBinding()) {
        return AnimatorEvaluationResult::failure(AnimatorEvaluationDiagnosticCode::MissingAsset,
            "AnimatorComponent resource binding is unavailable.");
    }
    if (desc_.character == nullptr) {
        return AnimatorEvaluationResult::failure(AnimatorEvaluationDiagnosticCode::MissingAsset,
            "AnimatorComponent requires an animated mesh asset.");
    }
    if (desc_.controller == nullptr || !animator_) {
        return AnimatorEvaluationResult::failure(AnimatorEvaluationDiagnosticCode::MissingAnimator,
            "AnimatorComponent requires animator controller data.");
    }
    if (!player_ || !player_->valid()) {
        return AnimatorEvaluationResult::failure(
            AnimatorEvaluationDiagnosticCode::MissingSkinnedMesh,
            "AnimatorComponent requires valid skinned mesh data.");
    }

    const auto* stateName = requestedStateName(desc_, request);
    if (stateName == nullptr || stateName->empty()) {
        return AnimatorEvaluationResult::failure(
            AnimatorEvaluationDiagnosticCode::MissingAnimationState,
            "AnimatorComponent requires an explicit animation state.");
    }

    if (*stateName != currentStateName_) {
        if (!animator_->playState(*stateName)) {
            return AnimatorEvaluationResult::failure(
                AnimatorEvaluationDiagnosticCode::MissingAnimationState,
                "AnimatorComponent could not find the requested animation state.");
        }
        currentStateName_ = *stateName;
    }

    animator_->advance(clampedDeltaSeconds(request.deltaSeconds));
    const auto& clipName = animator_->activeClipName();
    if (clipName.empty()) {
        return AnimatorEvaluationResult::failure(
            AnimatorEvaluationDiagnosticCode::MissingActiveClip,
            "AnimatorComponent active animation state has no clip.");
    }

    if (!player_->update(clipName, animator_->clipTimeSeconds())) {
        return AnimatorEvaluationResult::failure(AnimatorEvaluationDiagnosticCode::SkinningFailed,
            "AnimatorComponent failed to evaluate skinned mesh pose.");
    }

    // GPU-skinning-only path (Phase 3): the frame carries animation-state
    // metadata only. Bounds come from the immutable bind-pose mesh -- the
    // renderer consumes the bind-pose vertex stream plus the GPU skinning
    // palette (built from SkinnedMeshPlayer::skinMatrices()) so no CPU-baked
    // mesh is exposed here.
    const auto& bindPoseMesh = desc_.character->mesh;
    return AnimatorEvaluationResult::success(AnimatorEvaluationFrame(boundsFromMesh(bindPoseMesh),
        desc_.materialCount,
        *stateName,
        clipName,
        animator_->clipTimeSeconds(),
        desc_.character->skeleton.jointCount()));
}

} // namespace arp::scene3d

#pragma once

#include "assets/glb_animated_mesh_importer.h"
#include "core/container/array_view.h"

namespace arp::assets {

enum class AnimatedMeshColorPolicy { Unspecified, RejectColor0, DiscardValidatedRgba };
enum class AnimatedMeshTangentPolicy { Unspecified, RequireAuthored, AuthoredOrMaterialUnused };

// animated-glb-source/1. Every budget is explicit and finite. The selected
// scene is a source index, not an inferred default or a host path convention.
struct AnimatedMeshSourceImportLimits final {
    std::uint64_t maxSourceBytes = 0, maxImportBytes = 0, maxDecodedBytes = 0;
    std::uint32_t maxJsonDepth = 0, maxJsonValues = 0, selectedScene = UINT32_MAX;

    struct Layout final {
        std::uint32_t maxBufferViews = 0, maxAccessors = 0;
        std::uint64_t maxElements = 0, maxScalars = 0;
        std::size_t maxDescriptorBytes = 0;
    } layout;

    struct Scene final {
        std::uint32_t maxNodes = 0, maxEdges = 0, maxScenes = 0, maxSceneRoots = 0;
        std::uint32_t maxSkins = 0, maxSkinJoints = 0, maxMeshInstances = 0;
        std::size_t maxWorkingBytes = 0;
    } scene;

    struct Animation final {
        std::uint32_t maxClips = 0, maxSamplers = 0, maxChannels = 0;
        std::uint32_t maxTimeKeys = 0, maxOutputKeys = 0, maxValueScalars = 0;
        std::size_t maxWorkingBytes = 0;
    } animation;

    struct Skeleton final {
        std::uint32_t maxJoints = 0, maxKeyframes = 0;
        std::size_t maxNameBytes = 0;
        float matrixElementError = -1;
    } skeleton;

    struct Geometry final {
        std::uint32_t maxVertices = 0, maxIndices = 0, maxPrimitives = 0;
        AnimatedMeshColorPolicy colorPolicy = AnimatedMeshColorPolicy::Unspecified;
        AnimatedMeshTangentPolicy tangentPolicy = AnimatedMeshTangentPolicy::Unspecified;
        float unitVectorError = -1, orthogonalityError = -1, weightSumError = -1;
    } geometry;

    struct Materials final {
        std::uint32_t maxMaterials = 0, maxTextures = 0, maxImages = 0;
        std::size_t maxMetadataBytes = 0, maxEncodedImageBytes = 0;
    } materials;
};

struct AnimatedMeshSourceImportResult final {
    bool success = false;
    // Always available on failure, even if the caller's quota cannot allocate
    // the optional owned diagnostic text.
    const char* diagnosticId = nullptr;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    AnimatedMeshAsset asset;
    std::uint64_t decodedBytes = 0, importQuotaPeakBytes = 0;
    std::uint32_t authoredTangentPrimitives = 0, unusedTangentPrimitives = 0;
};

ARP_API const char* validateAnimatedMeshSourceImportLimits(
    const AnimatedMeshSourceImportLimits& limits) noexcept;

// No file/network IO. Output owns all data and is converted once to engine LH.
// A finite active caller quota is reused only when no larger than maxImportBytes;
// otherwise admission fails. With no active quota, the importer owns one.
ARP_API AnimatedMeshSourceImportResult importAnimatedMeshFromGlbBytes(
    core::ArrayView<const std::uint8_t> bytes, const AnimatedMeshSourceImportLimits& limits);

} // namespace arp::assets

#pragma once
#include "core/api.h"
#include "core/cpu_allocator.h"
#include "resource/mesh_asset_resources.h"

#include <cstddef>
#include <cstdint>

namespace arp::editor {
enum class EditorViewportTextureAdmissionDiagnostic : std::uint8_t {
    None,
    SourceNotReady,
    MissingMaterialMetadata,
    UnsupportedMaterialSemantics,
    InvalidTextureReference,
    ConflictingColorSpace,
    InvalidTextureIdentity,
    InvalidImageBinding,
    DecodeFailed,
    DecodedByteBudgetExceeded,
    ExistingResourceConflict,
    ResourceManagerAdmissionFailed
};

struct EditorViewportTextureAdmissionResult {
    bool success = false;
    bool partiallyCommitted = false;
    std::size_t admittedCount = 0u;
    std::size_t reusedCount = 0u;
    EditorViewportTextureAdmissionDiagnostic diagnostic =
        EditorViewportTextureAdmissionDiagnostic::None;
    core::pmr::String detail{&core::defaultCpuMemoryResource()};
};

ARP_API EditorViewportTextureAdmissionResult admitEditorViewportGlbTextures(
    resource::ResourceManager& resources,
    const resource::MeshAssetResource& mesh,
    resource::ResourceGeneration sourceGeneration,
    std::size_t maxDecodedBytes);
ARP_API const char* editorViewportTextureAdmissionDiagnosticCode(
    EditorViewportTextureAdmissionDiagnostic diagnostic) noexcept;
} // namespace arp::editor

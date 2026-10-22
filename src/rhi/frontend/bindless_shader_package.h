#pragma once
#include "core/encoding/base64.h"
#include "rhi/gpu_bindless.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/semantics/bindless_initialized_graphics.h"
#include "shader/native_buffer_memory_declarations.h"
#include "shader/shader_reflection_cache.h"
#include "shader/target_buffer_memory_declarations.h"

#include <filesystem>

namespace arp::rhi {

// Explicit cold ordinary-buffer relations for selected authenticated entries.
// /1 accepts Emulated target proofs. /2 freezes Native source graphs and MSL
// relation intent, with no driver proof until selected pipeline preparation.
// It grants no backend, Root, operation, lifetime or completion capability.
struct BindlessPackageMemoryLoadRequest {
    struct Entry {
        core::StringView entry;
        shader::TargetBufferMemoryLoadRequest buffers;
        const shader::NativeBufferMemoryLoadRequest* native = nullptr;
    };

    std::uint32_t contractVersion = 0u;
    const Entry* entries = nullptr;
    std::size_t entryCount = 0u;
};

// Cold source declaration custody only. Every push entry needs an explicit
// bounded graph request. Target equivalence and GPU execution are independent.
struct BindlessPackagePushLoadRequest {
    struct Entry {
        core::StringView entry;
        shader::BufferDeclarationTypeLayouts::Budget sourceTypes;
        const shader::PushBinaryMemoryPlan* binary = nullptr;
        // Optional Emulated correspondence over every actual buffer plus push.
        const shader::TargetBufferMemoryLoadRequest* target = nullptr;
    };

    std::uint32_t contractVersion = 0u, maxBytes = 0u;
    const Entry* entries = nullptr;
    std::size_t entryCount = 0u;
};

struct BindlessPackagePushDeclaration {
    std::uint32_t sizeBytes = 0u;
    std::uint64_t occupiedBytes = 0u;
    ShaderStage stages = ShaderStage::None;
};

struct BindlessPackageLoadBudget {
    std::uint64_t maxFileBytes = 0;
    std::uint64_t maxTotalBytes = 0;
    std::uint32_t maxFiles = 0;
    std::uint32_t maxModules = 0;
    std::uint32_t maxBindings = 0;
    std::uint32_t maxTables = 0;
    const BindlessPackageMemoryLoadRequest* ordinaryMemory = nullptr;
    const BindlessPackagePushLoadRequest* push = nullptr;
};

struct BindlessPackageFile {
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    core::pmr::String sha256{&core::defaultCpuMemoryResource()};
    core::ByteBuffer bytes{&core::defaultCpuMemoryResource()};
};

struct BindlessPackageModule {
    semantics::BindlessInitializedGraphicsContract::Profile initializedGraphics;
    core::pmr::String entry{&core::defaultCpuMemoryResource()};
    shader::ShaderReflectionModule reflection;
    std::uint32_t sourceFile = 0;
    std::uint32_t spirvFile = 0;
    std::uint32_t metalFile = 0;
    std::uint32_t wgslFile = UINT32_MAX;
    std::uint32_t reflectionFile = 0;
    BindlessPackagePushDeclaration push;
    shader::TargetPushMemoryLayout targetPush;
    core::pmr::Vector<shader::TargetBufferMemoryLayout> ordinaryMemory{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<shader::SpirvBufferDeclarationLayout> ordinaryBinaryMemory{
        &core::defaultCpuMemoryResource()};
    shader::NativeBufferMemoryDeclarations nativeMemory;
};

// Verified and immutable shader bytes + typed metadata. This is a prerequisite
// for device-owned pipeline authorization, never a GPU execution ticket itself.
// Loading is startup/reload preparation only, outside recording/frame loops.
class ARP_API BindlessShaderPackage final : public core::SharedObjectIdentity {
    class ConstructionKey {
        friend class BindlessShaderPackage;

        // User-provided: '= default' remains aggregate-initializable in C++17.
        ConstructionKey() {}

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    explicit BindlessShaderPackage(ConstructionKey) : quota_(core::currentCpuMemoryQuota()) {}

    BindlessShaderPackage(const BindlessShaderPackage&) = delete;
    BindlessShaderPackage& operator=(const BindlessShaderPackage&) = delete;
    static std::shared_ptr<const BindlessShaderPackage> load(
        const std::filesystem::path& publication,
        const BindlessPackageLoadBudget&,
        core::pmr::String& diagnostic);
    static const char* originalOrdinaryBufferMemory(
        const std::shared_ptr<const BindlessShaderPackage>&,
        core::StringView entry,
        std::uint32_t set,
        std::uint32_t binding,
        shader::TargetBufferMemoryLayout&) noexcept;
    // Source/rule custody only; the selected native driver graph must still be
    // compared by the factory-owned pipeline. Foreign module pointers refuse.
    static const char* originalNativeBufferMemory(
        const std::shared_ptr<const BindlessShaderPackage>&,
        const BindlessPackageModule*,
        std::uint32_t set,
        std::uint32_t binding,
        const shader::NativeBufferMemoryDeclarations*&) noexcept;
    static const char* originalPushDeclaration(const std::shared_ptr<const BindlessShaderPackage>&,
        core::StringView entry,
        BindlessPackagePushDeclaration&,
        std::uint32_t version = 1u) noexcept;
    static const char* originalTargetPushMemory(const std::shared_ptr<const BindlessShaderPackage>&,
        core::StringView entry,
        shader::TargetPushMemoryLayout&,
        std::uint32_t version = 1u) noexcept;

    const BindlessShaderArtifact& artifact() const noexcept { return artifact_; }

    const core::pmr::String& sha256() const noexcept { return sha256_; }

    const core::pmr::Vector<BindlessPackageFile>& files() const noexcept { return files_; }

    const core::pmr::Vector<BindlessPackageModule>& modules() const noexcept { return modules_; }

    std::uint64_t sourceBytes() const noexcept { return sourceBytes_; }

    const auto& quota() const noexcept { return quota_; }

    // Set only by the closed compiler identity and all four real module
    // proofs. This authenticates the producer algorithm, never a native draw.
    std::uint32_t gpuVisibilityKernelContractVersion() const noexcept
    {
        return gpuVisibilityVersion_;
    }

    // The same producer package's authenticated Instance64/Mapping16 consumer,
    // including its vertex interface and explicit per-record push selector.
    std::uint32_t gpuVisibilityConsumerContractVersion() const noexcept
    {
        return gpuVisibilityConsumerVersion_;
    }

private:
    BindlessShaderArtifact artifact_;
    core::pmr::String sha256_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<BindlessPackageFile> files_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<BindlessPackageModule> modules_{&core::defaultCpuMemoryResource()};
    std::uint64_t sourceBytes_ = 0;
    std::uint32_t gpuVisibilityVersion_ = 0u;
    std::uint32_t gpuVisibilityConsumerVersion_ = 0u;
    std::shared_ptr<core::CpuMemoryQuota> quota_;
};

// Cold factory capture from the same descriptor used to create the payload.
// Borrowed pointers name original package modules, without another owner/store.
class ARP_API BindlessGraphicsPipelineEntries final {
public:
    // Function-local view for native stage mapping; validates original package
    // proof without granting pipeline or recording authority. Legacy append
    // retains its push refusal.
    static ValidationResult composeStage(const std::shared_ptr<const BindlessShaderPackage>&,
        core::StringView entry,
        ShaderStage,
        ShaderPackageDesc&,
        const BindlessPackageModule*&,
        std::uint32_t version = 1u);
    // Cold selected-entry composition from one authentic immutable package.
    // No backend factory/Root execution capability is granted by this view.
    static ValidationResult compose(const std::shared_ptr<const BindlessShaderPackage>&,
        core::StringView vertexEntry,
        core::StringView fragmentEntry,
        ShaderPackageDesc&,
        PipelineLayoutDesc&,
        BindlessGraphicsPipelineEntries&,
        std::uint32_t version = 1u);
    static const char* capture(const BindlessShaderPackage&,
        const RenderPipelineDesc&,
        BindlessGraphicsPipelineEntries&) noexcept;

    // Called by the factory-owned pipeline while its original package is alive.
    // Native binary facts and proved Emulated target facts remain distinct.
    const char* bufferRequirement(ShaderStage,
        std::uint32_t set,
        std::uint32_t binding,
        bool equivalentTarget,
        ShaderBufferMemoryRequirement&,
        std::uint32_t version = 1u) const noexcept;

    // Called only by the owning factory pipeline: NativeBinaryIdentity needs
    // its exact original SPIR-V consumption; otherwise a full target relation
    // is required. This helper alone issues no original/native authority.
    const char* bufferMemberMemory(ShaderStage,
        std::uint32_t set,
        std::uint32_t binding,
        bool nativeBinaryIdentity,
        ShaderBufferMemberMemory&,
        std::uint32_t version = 1u) const noexcept;

    const BindlessPackageModule* module(ShaderStage stage, std::uint32_t version) const noexcept
    {
        return version != 1u                 ? nullptr
            : stage == ShaderStage::Vertex   ? vertex_
            : stage == ShaderStage::Fragment ? fragment_
                                             : nullptr;
    }

private:
    const BindlessPackageModule* vertex_ = nullptr;
    const BindlessPackageModule* fragment_ = nullptr;
};

// Select only authenticated package modules. Names identify modules; the sealed
// versioned profile, never the spelling of those names, chooses the scope.
ARP_API semantics::BindlessInitializedGraphicsContract::Selection selectBindlessInitializedGraphics(
    const BindlessShaderPackage&, core::StringView vertex, core::StringView fragment) noexcept;

// Authenticate the pipeline's explicit selected modules against this original
// package before dereferencing them. Never select by merged layout similarity.
ARP_API const BindlessPackageModule* selectBindlessGraphicsEntryAccess(const BindlessShaderPackage&,
    const RenderPipeline&,
    core::StringView entry,
    ShaderStage& stage,
    const char*& diagnostic) noexcept;

// Compose only the selected, verified entries. Shared by validators and native
// preparation so layout visibility and entry ABI cannot drift between backends.
ARP_API bool appendBindlessShaderEntry(const BindlessShaderPackage&,
    std::string_view entry,
    ShaderStage,
    ShaderPackageDesc&,
    core::pmr::String& diagnostic,
    const BindlessPackageModule** selectedModule = nullptr);
// The returned view borrows the package's verified bytes. The caller must keep
// the package alive through native shader-module creation.
ARP_API bool selectBindlessEmulatedWgsl(const BindlessShaderPackage&,
    std::string_view entry,
    ShaderStage,
    core::StringView& bytes,
    core::pmr::String& diagnostic);
// Borrow the unique authenticated Compute module matching the issued pipeline
// interface. Missing or ambiguous correspondence fails; this creates no ticket.
ARP_API const BindlessPackageModule* selectBindlessComputeEntryAccess(
    const BindlessShaderPackage&, const ShaderReflectionDesc&, const char*& diagnostic) noexcept;
ARP_API ValidationResult validateBindlessAuthoredRenderState(
    const RenderPipelineDesc&, bool hasFragment);

// Complete sealed interface, including explicit ordinary sets. Cold layout
// composition grants no backend execution or descriptor-content authority.
ARP_API PipelineLayoutDesc bindlessPipelineLayout(const BindlessShaderArtifact&);

} // namespace arp::rhi

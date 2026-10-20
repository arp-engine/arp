#pragma once
#include "core/artifact_path.h"
#include "core/file_io.h"
#include "core/sha256.h"
#include "core/yaml_contract.h"

#include <array>
#include <filesystem>

namespace arp::shader {
enum class ShaderArtifactRole : std::uint32_t { Source, Spirv, Metal, Wgsl, Reflection, Count };

struct ShaderArtifactLoadError {
    const char* diagnostic;
};

inline void requireShaderArtifact(bool valid, const char* diagnostic)
{
    if (!valid)
        throw ShaderArtifactLoadError{diagnostic};
}

inline core::StringView shaderArtifactText(const core::ByteBuffer& bytes) noexcept
{
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

// One consumed-byte ledger, shared by module roles and delegated entry loads.
class ShaderArtifactInput final {
public:
    ShaderArtifactInput(std::uint64_t maxFileBytes, std::uint64_t maxTotalBytes) noexcept
        : maxFileBytes_(maxFileBytes), maxTotalBytes_(maxTotalBytes)
    {}

    core::ByteBuffer read(const std::filesystem::path& path)
    {
        requireShaderArtifact(remaining() != 0u, "SHADER-ENTRY-INPUT-BUDGET");
        auto file = core::readBinaryFile(path, std::min(maxFileBytes_, remaining()));
        requireShaderArtifact(file.success && !file.bytes.empty(), "SHADER-ENTRY-FILE-READ-FAILED");
        consumed_ += file.bytes.size();
        return std::move(file.bytes);
    }

    void debit(std::uint64_t bytes)
    {
        requireShaderArtifact(bytes <= remaining(), "SHADER-ENTRY-INPUT-BUDGET");
        consumed_ += bytes;
    }

    std::uint64_t consumed() const noexcept { return consumed_; }

    std::uint64_t remaining() const noexcept { return maxTotalBytes_ - consumed_; }

private:
    const std::uint64_t maxFileBytes_, maxTotalBytes_;
    std::uint64_t consumed_ = 0u;
};

// Shared strict role/path/hash loading. Diagnostic IDs deliberately preserve
// the existing SingleEntryShaderArtifact contract for delegated validation.
class ShaderArtifactFiles final {
public:
    struct File {
        core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
        core::pmr::String name{&core::defaultCpuMemoryResource()};
        core::pmr::String sha256{&core::defaultCpuMemoryResource()};
        core::ByteBuffer bytes{&core::defaultCpuMemoryResource()};
    };

    const File& at(std::size_t index) const { return files_.at(index); }

    const File& operator[](std::size_t index) const noexcept { return files_[index]; }

    // Explicit source-shared entry form. This is a role loader, not an
    // ordinary SingleEntry artifact or a synthetic source publication.
    void loadTargets(const core::YamlContract::Node& files,
        const std::filesystem::path& parent,
        ShaderArtifactInput& input)
    {
        using Y = core::YamlContract;
        constexpr std::array names{"spirv", "metal", "wgsl", "reflection"};
        constexpr std::array<core::StringView, 4> suffixes{
            ".spv", ".metal", ".wgsl", ".reflection.yaml"};
        requireShaderArtifact(
            files.is_mapping() && files.size() == names.size(), "SHADER-ENTRY-FILE-ROLES");
        for (std::size_t role = 0u; role < names.size(); ++role) {
            const auto& node = Y::required(files, names[role]);
            requireShaderArtifact(
                node.is_mapping() && node.size() == 2u, "SHADER-ENTRY-FILE-ROLE-FIELDS");
            auto& file = files_[role + 1u];
            loadPath(file, node, suffixes[role]);
            for (std::size_t previous = 1u; previous <= role; ++previous)
                requireShaderArtifact(
                    file.name != files_[previous].name, "SHADER-ENTRY-FILE-ALIAS");
            readPayload(file, parent, input);
        }
    }

    static File loadAvailableFile(const core::YamlContract::Node& node,
        const std::filesystem::path& parent,
        ShaderArtifactInput& input,
        core::StringView suffix)
    {
        requireShaderArtifact(
            node.is_mapping() && node.size() == 2u, "SHADER-ENTRY-FILE-ROLE-FIELDS");
        File file;
        loadPath(file, node, suffix);
        readPayload(file, parent, input);
        return file;
    }

    void load(const core::YamlContract::Node& files,
        const std::filesystem::path& parent,
        std::uint32_t version,
        ShaderArtifactInput& input)
    {
        using Y = core::YamlContract;
        constexpr std::array names{"source", "spirv", "metal", "wgsl", "reflection"};
        constexpr std::array<core::StringView, 5> suffixes{
            ".slang", ".spv", ".metal", ".wgsl", ".reflection.yaml"};
        requireShaderArtifact(
            files.is_mapping() && files.size() == names.size(), "SHADER-ENTRY-FILE-ROLES");
        if (version == 2u) {
            const auto& wgsl = Y::required(files, "wgsl");
            requireShaderArtifact(
                wgsl.is_mapping() && wgsl.contains("status"), "SHADER-ENTRY-PUBLICATION-CONTRACT");
        }
        for (std::size_t i = 0u; i < names.size(); ++i) {
            const auto& role = Y::required(files, names[i]);
            if (version == 3u && i == static_cast<std::size_t>(ShaderArtifactRole::Metal)) {
                requireShaderArtifact(role.is_mapping() && role.size() == 3u
                        && Y::text(role, "status") == "DiagnosticFailure"
                        && Y::text(role, "contract") == "MetalRuntimeLengthDiagnostic/1"
                        && Y::text(role, "diagnosticId")
                            == "SHADER-METAL-RUNTIME-LENGTH-UNSUPPORTED",
                    "SHADER-ENTRY-TARGET-DIAGNOSTIC-CONTRACT");
                files_[i].diagnostic = Y::text(role, "diagnosticId");
                continue;
            }
            if (version == 2u && i == static_cast<std::size_t>(ShaderArtifactRole::Wgsl)) {
                requireShaderArtifact(role.is_mapping() && role.size() == 3u
                        && Y::text(role, "status") == "DiagnosticFailure"
                        && ((Y::text(role, "contract") == "WgslBufferArrayDiagnostic/1"
                                && Y::text(role, "diagnosticId")
                                    == "SHADER-WGSL-BUFFER-ARRAY-UNSUPPORTED")
                            || (Y::text(role, "contract") == "WgslBufferArrayDiagnostic/2"
                                && Y::text(role, "diagnosticId")
                                    == "SHADER-WGSL-BUFFER-ARRAY-ABI-UNAVAILABLE")
                            || (Y::text(role, "contract") == "WgslGraphicsAtomicDiagnostic/1"
                                && Y::text(role, "diagnosticId")
                                    == "SHADER-WGSL-GRAPHICS-ATOMIC-UNSUPPORTED")),
                    "SHADER-ENTRY-TARGET-DIAGNOSTIC-CONTRACT");
                files_[i].diagnostic = Y::text(role, "diagnosticId");
                continue;
            }
            auto& file = files_[i];
            loadPath(file, role, suffixes[i]);
            for (std::size_t j = 0u; j < i; ++j)
                requireShaderArtifact(file.name != files_[j].name, "SHADER-ENTRY-FILE-ALIAS");
            readPayload(file, parent, input);
        }
    }

private:
    static void loadPath(File& file, const core::YamlContract::Node& node, core::StringView suffix)
    {
        file.name = core::YamlContract::text(node, "path");
        file.sha256 = core::YamlContract::text(node, "sha256");
        requireShaderArtifact(core::isFlatArtifactFilename(file.name)
                && file.name.size() > suffix.size()
                && core::StringView(file.name).substr(file.name.size() - suffix.size()) == suffix,
            "SHADER-ENTRY-FILE-ROLE-PATH");
    }

    static void readPayload(
        File& file, const std::filesystem::path& parent, ShaderArtifactInput& input)
    {
        file.bytes = input.read(parent / file.name.c_str());
        requireShaderArtifact(
            core::Sha256::matchesHexDigest(shaderArtifactText(file.bytes), file.sha256),
            "SHADER-ENTRY-FILE-HASH");
    }

    std::array<File, static_cast<std::size_t>(ShaderArtifactRole::Count)> files_;
};
} // namespace arp::shader

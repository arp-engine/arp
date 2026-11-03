#pragma once

#include "core/io/file_save.h"

namespace arp::tools::ai_asset_generator {

struct PromptManifestLimits {
    static constexpr std::uint64_t defaultDocumentBytes = 64 * 1024;
    static constexpr std::size_t maxAssets = 64, maxFieldBytes = 4096;
};

struct PromptAsset {
    PromptAsset() = default;
    PromptAsset(core::StringView name, core::StringView prompt, core::StringView file);

    PromptAsset(const PromptAsset& other) : PromptAsset() { *this = other; }

    PromptAsset& operator=(const PromptAsset&) = default;
    PromptAsset(PromptAsset&&) = default;
    PromptAsset& operator=(PromptAsset&&) = default;
    bool operator==(const PromptAsset&) const;

    core::pmr::String name{&core::defaultCpuMemoryResource()};
    core::pmr::String prompt{&core::defaultCpuMemoryResource()};
    core::pmr::String file{&core::defaultCpuMemoryResource()};
};

struct PromptManifest {
    PromptManifest() = default;

    PromptManifest(const PromptManifest& other) : PromptManifest() { *this = other; }

    PromptManifest& operator=(const PromptManifest&) = default;
    PromptManifest(PromptManifest&&) = default;
    PromptManifest& operator=(PromptManifest&&) = default;
    bool operator==(const PromptManifest&) const;

    core::pmr::String provider{"Meshy", &core::defaultCpuMemoryResource()};
    core::pmr::String outputDirectory{
        "artifacts/ai-assets/production-demo", &core::defaultCpuMemoryResource()};
    core::pmr::Vector<PromptAsset> assets{&core::defaultCpuMemoryResource()};
};

struct PromptManifestResult {
    PromptManifestResult() = default;

    PromptManifestResult(const PromptManifestResult& other) : PromptManifestResult()
    {
        *this = other;
    }

    PromptManifestResult& operator=(const PromptManifestResult&) = default;
    PromptManifestResult(PromptManifestResult&&) = default;
    PromptManifestResult& operator=(PromptManifestResult&&) = default;

    bool success = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    core::pmr::String yaml{&core::defaultCpuMemoryResource()};
    PromptManifest manifest;
};

// Offline tool contract only; scalar escaping stays in YamlContract.
class PromptManifestCodec final {
public:
    static PromptManifestResult encode(const PromptManifest&, std::uint64_t maxDocumentBytes);
    static PromptManifestResult decode(core::StringView yaml, std::uint64_t maxDocumentBytes);
};

struct Options {
    Options() = default;

    Options(const Options& other) : Options() { *this = other; }

    Options& operator=(const Options&) = default;
    Options(Options&&) = default;
    Options& operator=(Options&&) = default;

    bool selfTest = false, manifestOnly = false, valid = true;
    core::pmr::String outputDirectory{
        "artifacts/ai-assets/production-demo", &core::defaultCpuMemoryResource()};
    std::uint64_t maxDocumentBytes = PromptManifestLimits::defaultDocumentBytes;
};

struct Result {
    Result() = default;

    Result(const Result& other) : Result() { *this = other; }

    Result& operator=(const Result&) = default;
    Result(Result&&) = default;
    Result& operator=(Result&&) = default;

    bool success = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    core::io::FileSaveResult saved;
};

PromptManifest buildProductionDemoPromptManifest();
Options parseOptions(int argc, const char* const* argv);

// Explicit CLI intent, original FileService publication, no provider credentials.
class PromptManifestApplication final {
public:
    Result run(const Options&) const;
};

} // namespace arp::tools::ai_asset_generator

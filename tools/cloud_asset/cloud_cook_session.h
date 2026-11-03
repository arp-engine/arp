#pragma once

#include "core/io/file_service.h"

namespace arp::tools::cloud_asset {

struct CloudCookSessionDesc {
    CloudCookSessionDesc() = default;

    CloudCookSessionDesc(const CloudCookSessionDesc& other) : CloudCookSessionDesc()
    {
        *this = other;
    }

    CloudCookSessionDesc& operator=(const CloudCookSessionDesc&) = default;
    CloudCookSessionDesc(CloudCookSessionDesc&&) = default;
    CloudCookSessionDesc& operator=(CloudCookSessionDesc&&) = default;

    std::uint32_t contractVersion = 1;
    core::io::FileReadRequest source;
    core::PoolHandle outputMount;
    core::pmr::String outputPath{&core::defaultCpuMemoryResource()};
    std::uint64_t maxDocumentBytes = 0;
};

enum class CloudCookPhase {
    ReadingRecipe,
    ReadingBaseline,
    Preparing,
    ReadyToSave,
    Saving,
    Complete,
    Failed,
    Cancelled
};

// Tool-only coordination. FileService/queue outlive accepted callbacks; the
// callback retains State, never this object. All domain work uses the original
// cloud cooker/codec and runs in advance(), outside completion callbacks.
class CloudCookSession final {
public:
    CloudCookSession(core::io::FileService&, const CloudCookSessionDesc&);
    ~CloudCookSession();
    CloudCookSession(const CloudCookSession&) = delete;
    CloudCookSession& operator=(const CloudCookSession&) = delete;

    void advance() noexcept;
    void cancel() noexcept;
    CloudCookPhase phase() const noexcept;
    bool terminal() const noexcept;
    const char* diagnostic() const noexcept;
    core::io::FileSaveResult saveResult() const noexcept;
    core::io::FileVersionToken sourceIdentity() const noexcept;
    std::uint32_t cellCount() const noexcept;
    std::uint32_t algorithmVersion() const noexcept;
    std::size_t documentBytes() const noexcept;

private:
    struct State;
    core::SharedPtr<State> state_;
};

} // namespace arp::tools::cloud_asset

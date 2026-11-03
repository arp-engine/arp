#pragma once

#include "../common/startup_resource_task.h"
#include "content.h"
#include "content_bindings.h"

namespace arp::survivors {

// Owns the runtime registry and its Content handles together. After source/IO
// retirement the game may mutate this registry through its existing services.
class PreparedContent final {
public:
    PreparedContent() : content_(resources_) {}

    PreparedContent(const PreparedContent&) = delete;
    PreparedContent& operator=(const PreparedContent&) = delete;

    static core::SharedPtr<PreparedContent> prepare(const resource::ResourceLoadContext&,
        resource::ResourceFileOrigin,
        const GameConfig&,
        core::pmr::String& diagnostic);

    resource::ResourceManager& resources() noexcept { return resources_; }

    Content& content() noexcept { return content_; }

    const Content& content() const noexcept { return content_; }

    const core::pmr::String& sourceIdentity() const noexcept { return sourceIdentity_; }

private:
    resource::ResourceManager resources_;
    Content content_;
    core::pmr::String sourceIdentity_;
};

class ContentSourceTask final {
public:
    static constexpr std::size_t maxSourceFileBytes = 2u * 1024u * 1024u;
    static constexpr std::size_t maxTextureFileBytes = 1024u * 1024u;

    ContentSourceTask(core::io::FileService&,
        core::runtime::CompletionQueue&,
        core::PoolHandle mount,
        const GameConfig&,
        core::runtime::CancellationToken = {});

    void advance() noexcept { task_.advance(); }

    void cancel() noexcept { task_.cancel(); }

    examples::StartupResourcePhase phase() const noexcept { return task_.phase(); }

    std::string_view diagnostic() const noexcept { return task_.diagnostic(); }

    const char* diagnosticId() const noexcept { return task_.diagnosticId(); }

    core::io::FileIoStatus fileStatus() const noexcept { return task_.fileStatus(); }

    std::uint64_t readCount() const noexcept { return task_.readCount(); }

    core::SharedPtr<PreparedContent> preparation() const;

private:
    examples::StartupResourceTask task_;
};
} // namespace arp::survivors

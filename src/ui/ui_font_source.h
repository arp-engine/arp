#pragma once

#include "core/container/array_view.h"
#include "core/io/file_request.h"

#include <filesystem>

namespace arp::ui {

struct UiFontFaceDesc;

struct UiFontSourceDesc {
    core::pmr::String path{&core::defaultCpuMemoryResource()};
    core::io::FilePathScope pathScope = core::io::FilePathScope::HostFilesystem;
    core::PoolHandle mount;
    core::io::FileVersionToken version;
    std::size_t maxBytes = 0;
};

// Immutable font bytes retain their provenance and outlive every FT_Face user.
class ARP_API UiFontSource final {
    struct ConstructionKey {};

public:
    UiFontSource(ConstructionKey,
        UiFontSourceDesc desc,
        core::pmr::Vector<std::uint8_t> bytes,
        core::io::FileVersionToken contentVersion);
    UiFontSource(const UiFontSource&) = delete;
    UiFontSource& operator=(const UiFontSource&) = delete;
    static core::SharedPtr<const UiFontSource> create(UiFontSourceDesc desc,
        core::pmr::Vector<std::uint8_t> bytes,
        core::pmr::String& diagnostic);
    static core::SharedPtr<const UiFontSource> loadHost(
        const std::filesystem::path& path, std::size_t maxBytes, core::pmr::String& diagnostic);

    const UiFontSourceDesc& desc() const noexcept { return desc_; }

    core::ArrayView<const std::uint8_t> bytes() const noexcept
    {
        return {bytes_.data(), bytes_.size()};
    }

    const core::io::FileVersionToken& contentVersion() const noexcept { return contentVersion_; }

    bool matches(const UiFontSource& other) const noexcept;

private:
    UiFontSourceDesc desc_;
    core::pmr::Vector<std::uint8_t> bytes_;
    core::io::FileVersionToken contentVersion_;
};

// A supplied source always wins or fails; it never falls back to the host path.
ARP_API core::SharedPtr<const UiFontSource> acquireUiFontSource(
    const UiFontFaceDesc& desc, core::pmr::String& diagnostic);

// Rendering admission is pure: a path is provenance, never permission to read.
ARP_API bool validatePreparedUiFontFace(const UiFontFaceDesc& desc, core::pmr::String& diagnostic);

} // namespace arp::ui

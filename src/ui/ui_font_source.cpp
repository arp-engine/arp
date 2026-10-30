#include "ui/ui_font_source.h"

#include "core/io/file_io.h"
#include "core/io/file_read_contract.h"
#include "core/io/filesystem_path.h"
#include "ui/ui_font.h"

#include <utility>

namespace arp::ui {

UiFontSource::UiFontSource(ConstructionKey,
    UiFontSourceDesc desc,
    core::pmr::Vector<std::uint8_t> bytes,
    core::io::FileVersionToken contentVersion)
    : desc_(std::move(desc)), bytes_(std::move(bytes)), contentVersion_(contentVersion)
{}

core::SharedPtr<const UiFontSource> UiFontSource::create(
    UiFontSourceDesc desc, core::pmr::Vector<std::uint8_t> bytes, core::pmr::String& diagnostic)
{
    diagnostic.clear();
    core::io::FileReadRequest request;
    request.contractVersion = 1;
    request.path = desc.path;
    request.pathScope = desc.pathScope;
    request.mount = desc.mount;
    request.maxBytes = desc.maxBytes;
    const auto valid = core::io::FileReadContract::validate(request);
    if (valid.status != core::io::FileIoStatus::Success || desc.maxBytes == SIZE_MAX
        || (desc.pathScope != core::io::FilePathScope::HostFilesystem
            && desc.pathScope != core::io::FilePathScope::MountedResource)
        || (desc.pathScope == core::io::FilePathScope::HostFilesystem && desc.mount)
        || (desc.pathScope == core::io::FilePathScope::MountedResource
            && (!desc.mount || !core::FilePath::isSafeRelativePath(desc.path, desc.path.size())))) {
        diagnostic = "UI-FONT-SOURCE-LOCATION";
        return {};
    }
    if (bytes.empty() || bytes.size() > desc.maxBytes
        || bytes.get_allocator().resource() != &core::defaultCpuMemoryResource()) {
        diagnostic = "UI-FONT-SOURCE-BYTES-BUDGET";
        return {};
    }
    if (!desc.version.valid()
        || (desc.version.kind != core::io::FileVersionKind::None
            && desc.version.kind != core::io::FileVersionKind::ContentSha256)) {
        diagnostic = "UI-FONT-SOURCE-VERSION";
        return {};
    }
    core::Sha256 hash;
    hash.update({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
    const auto content =
        core::io::FileVersionEncoding::finish(hash, core::io::FileVersionKind::ContentSha256);
    if (desc.version.kind != core::io::FileVersionKind::None && desc.version != content) {
        diagnostic = "UI-FONT-SOURCE-CONTENT-MISMATCH";
        return {};
    }
    return core::makeShared<UiFontSource>(
        ConstructionKey{}, std::move(desc), std::move(bytes), content);
}

core::SharedPtr<const UiFontSource> UiFontSource::loadHost(
    const std::filesystem::path& path, std::size_t maxBytes, core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (!maxBytes || maxBytes == SIZE_MAX) {
        diagnostic = "UI-FONT-SOURCE-BUDGET";
        return {};
    }
    core::io::FileReadRequest request;
    request.contractVersion = 1;
    request.path = core::FilePath::utf8(path);
    request.pathScope = core::io::FilePathScope::HostFilesystem;
    request.maxBytes = maxBytes;
    if (core::io::FileReadContract::validate(request).status != core::io::FileIoStatus::Success) {
        diagnostic = "UI-FONT-SOURCE-LOCATION";
        return {};
    }
    auto read = core::readBinaryFile(path, maxBytes);
    if (!read.success) {
        diagnostic = "UI-FONT-SOURCE-READ: ";
        diagnostic += read.diagnostics;
        return {};
    }
    UiFontSourceDesc desc;
    desc.path = std::move(request.path);
    desc.maxBytes = maxBytes;
    return create(std::move(desc), std::move(read.bytes), diagnostic);
}

bool UiFontSource::matches(const UiFontSource& other) const noexcept
{
    return desc_.path == other.desc_.path && desc_.pathScope == other.desc_.pathScope
        && desc_.mount == other.desc_.mount && desc_.version == other.desc_.version
        && contentVersion_ == other.contentVersion_ && bytes_.size() == other.bytes_.size();
}

core::SharedPtr<const UiFontSource> acquireUiFontSource(
    const UiFontFaceDesc& desc, core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (!desc.maxSourceBytes || desc.maxSourceBytes == SIZE_MAX) {
        diagnostic = "UI-FONT-SOURCE-BUDGET";
        return {};
    }
    if (!desc.source)
        return UiFontSource::loadHost(desc.sourcePath, desc.maxSourceBytes, diagnostic);
    if (desc.source->desc().path != core::FilePath::utf8(desc.sourcePath)
        || desc.source->bytes().size() > desc.maxSourceBytes) {
        diagnostic = "UI-FONT-SOURCE-DESCRIPTOR-MISMATCH";
        return {};
    }
    return desc.source;
}

bool validatePreparedUiFontFace(const UiFontFaceDesc& desc, core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (!desc.source) {
        diagnostic = "UI-FONT-SOURCE-NOT-PREPARED";
        return false;
    }
    return static_cast<bool>(acquireUiFontSource(desc, diagnostic));
}

} // namespace arp::ui

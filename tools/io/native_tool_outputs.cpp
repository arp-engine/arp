#include "tools/io/native_tool_outputs.h"

#include "core/io/file_save_contract.h"
#include "core/io/filesystem_path.h"
#include "core/text/number_format.h"
#include "tools/io/cold_tool_files.h"

namespace arp::tools::io {
namespace {
struct PreparedOutput {
    std::filesystem::path path;
    core::pmr::Vector<std::uint8_t> bytes{&core::defaultCpuMemoryResource()};
    core::PoolHandle mount;
    core::io::FileVersionToken baseline;
    bool missing = true;
};

struct OutputMount {
    std::filesystem::path directory;
    core::PoolHandle mount;
};
} // namespace

NativeToolOutputResult NativeToolOutputWriter::save(const NativeToolOutputDesc& desc) const noexcept
{
    using namespace core::io;
    NativeToolOutputResult result;
    const auto fail = [&](NativeToolOutputFailure failure, const char* id) {
        result.failure = failure;
        result.diagnosticId = id;
        return result;
    };
    try {
        if (!desc.maxFiles || desc.files.empty() || !desc.files.data()
            || desc.files.size() > desc.maxFiles || !ColdToolFiles::validBudget(desc.maxTotalBytes)
            || (desc.encoding != NativeToolOutputEncoding::ExactBytes
                && desc.encoding != NativeToolOutputEncoding::LegacyNativeText))
            return fail(NativeToolOutputFailure::Options, "TOOL-OUTPUT-OPTIONS");
#if defined(__EMSCRIPTEN__)
        result.lastSave.fail(FileIoStatus::Unsupported, "TOOL-OUTPUT-NATIVE-ONLY");
        return fail(NativeToolOutputFailure::Save, "TOOL-OUTPUT-NATIVE-ONLY");
#endif
        core::pmr::Vector<PreparedOutput> prepared{&core::defaultCpuMemoryResource()};
        prepared.reserve(desc.files.size());
        std::uint64_t total = 0;
        for (const auto& file : desc.files) {
            if (file.path.empty() || file.bytes.empty())
                return fail(NativeToolOutputFailure::Options, "TOOL-OUTPUT-EMPTY-FILE");
            if (file.bytes.size() > desc.maxTotalBytes - total)
                return fail(NativeToolOutputFailure::Budget, "TOOL-OUTPUT-BUDGET");
            total += file.bytes.size();
            std::size_t encodedSize = file.bytes.size();
#if defined(_WIN32)
            if (desc.encoding == NativeToolOutputEncoding::LegacyNativeText)
                for (const auto byte : file.bytes)
                    if (byte == '\n') {
                        if (total == desc.maxTotalBytes)
                            return fail(NativeToolOutputFailure::Budget, "TOOL-OUTPUT-BUDGET");
                        ++total;
                        ++encodedSize;
                    }
#endif
            std::error_code error;
            auto path = std::filesystem::absolute(file.path, error).lexically_normal();
            if (error || path.filename().empty() || path.filename() == "."
                || path.filename() == "..")
                return fail(NativeToolOutputFailure::Options, "TOOL-OUTPUT-PATH");
            for (const auto& other : prepared)
                if (other.path == path)
                    return fail(NativeToolOutputFailure::Options, "TOOL-OUTPUT-DUPLICATE");
            PreparedOutput output;
            output.path = std::move(path);
            output.bytes.reserve(encodedSize);
            for (const auto byte : file.bytes) {
#if defined(_WIN32)
                if (desc.encoding == NativeToolOutputEncoding::LegacyNativeText && byte == '\n')
                    output.bytes.push_back('\r');
#endif
                output.bytes.push_back(static_cast<std::uint8_t>(byte));
            }
            prepared.push_back(std::move(output));
        }
        ColdToolFiles files(desc.maxTotalBytes, static_cast<std::uint32_t>(prepared.size()));
        core::pmr::Vector<OutputMount> mounts{&core::defaultCpuMemoryResource()};
        mounts.reserve(prepared.size());
        std::uint64_t baselineBytes = 0;
        for (auto& output : prepared) {
            const auto directory = output.path.parent_path();
            for (const auto& mounted : mounts)
                if (mounted.directory == directory)
                    output.mount = mounted.mount;
            if (!output.mount) {
                std::error_code error;
                std::filesystem::create_directories(directory, error);
                if (error)
                    return fail(NativeToolOutputFailure::Directory, "TOOL-OUTPUT-DIRECTORY");
                auto name = core::formatUnsigned(static_cast<std::uint64_t>(mounts.size()));
                output.mount = files.mountDirectory(name, directory, true);
                if (!output.mount
                    || !files.service().saveCapability(output.mount).observedBaselineReplace)
                    return fail(NativeToolOutputFailure::Save, "TOOL-OUTPUT-SAVE-UNSUPPORTED");
                mounts.push_back({directory, output.mount});
            }
            FileReadRequest request;
            request.contractVersion = 1;
            request.pathScope = FilePathScope::MountedResource;
            request.mount = output.mount;
            request.path = core::FilePath::utf8(output.path.filename());
            request.maxBytes = desc.maxTotalBytes;
            const auto baseline = files.read(request);
            output.missing = baseline.status == FileIoStatus::MissingFile;
            if (!output.missing) {
                if (baseline.status != FileIoStatus::Success)
                    return fail(NativeToolOutputFailure::Baseline,
                        baseline.diagnosticId ? baseline.diagnosticId : "TOOL-OUTPUT-BASELINE");
                if (baseline.bytes.size() > desc.maxTotalBytes - baselineBytes)
                    return fail(NativeToolOutputFailure::Budget, "TOOL-OUTPUT-BASELINE-BUDGET");
                baselineBytes += baseline.bytes.size();
                core::Sha256 hash;
                hash.update(
                    {reinterpret_cast<const char*>(baseline.bytes.data()), baseline.bytes.size()});
                output.baseline = FileVersionEncoding::finish(hash, FileVersionKind::ContentSha256);
            }
        }
        for (auto& output : prepared) {
            FileSaveRequest request;
            request.mount = output.mount;
            request.pathScope = FilePathScope::MountedResource;
            request.path = core::FilePath::utf8(output.path.filename());
            request.bytes = std::move(output.bytes);
            request.maxWriteBytes = desc.maxTotalBytes;
            request.precondition = output.missing ? FileSavePrecondition::MustBeMissing
                                                  : FileSavePrecondition::ContentSha256;
            request.expectedVersion = output.baseline;
            request.maxBaselineBytes = output.missing ? 0 : desc.maxTotalBytes;
            const auto expected = FileSaveContract::contentVersion(request);
            result.lastSave = files.save(std::move(request));
            if (result.lastSave.status != FileIoStatus::Success
                || result.lastSave.deliveryStatus != FileIoStatus::Success
                || result.lastSave.commitState != FileCommitState::Committed
                || result.lastSave.committedVersion != expected)
                return fail(NativeToolOutputFailure::Save,
                    result.lastSave.diagnosticId ? result.lastSave.diagnosticId
                                                 : "TOOL-OUTPUT-COMMIT");
            ++result.committedFiles;
        }
        files.retire();
        return result;
    } catch (const std::bad_alloc&) {
        return fail(NativeToolOutputFailure::Allocation, "TOOL-OUTPUT-ALLOCATION");
    } catch (...) {
        return fail(NativeToolOutputFailure::Save, "TOOL-OUTPUT-FAILED");
    }
}
} // namespace arp::tools::io

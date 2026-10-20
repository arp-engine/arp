#include "core/immutable_build_input_snapshot.h"

#include "core/file_io.h"
#include "core/process_runner.h"
#include "core/sha256.h"

#include <algorithm>
#include <array>
#include <fstream>

namespace arp::core {
namespace {

pmr::String sha256Digest(const ByteBuffer& digest)
{
    static constexpr char digits[] = "0123456789abcdef";
    pmr::String result("sha256:", &defaultCpuMemoryResource());
    result.reserve(7u + digest.size() * 2u);
    for (const auto byte : digest) {
        result.push_back(digits[(byte >> 4u) & 0x0fu]);
        result.push_back(digits[byte & 0x0fu]);
    }
    return result;
}

bool hashFile(const std::filesystem::path& path, pmr::String& fingerprint)
{
    ByteBuffer digest{&defaultCpuMemoryResource()};
    if (!Sha256::digestFile(path, digest))
        return false;
    fingerprint = sha256Digest(digest);
    return true;
}

bool safeLogicalPath(std::string_view value)
{
    if (value.empty())
        return false;
    const std::filesystem::path path(value);
    if (path.is_absolute())
        return false;
    return std::none_of(path.begin(), path.end(), [](const auto& component) {
        return component == "..";
    });
}

} // namespace

bool ImmutableBuildInputContract::add(
    std::string_view logicalName, std::string_view value, pmr::String& diagnostics)
{
    if (logicalName.empty()) {
        diagnostics = "immutable build input contract logical name is empty";
        return false;
    }
    pmr::String key(logicalName.data(), logicalName.size(), &defaultCpuMemoryResource());
    pmr::String content(value.data(), value.size(), &defaultCpuMemoryResource());
    if (!entries_.emplace(std::move(key), std::move(content)).second) {
        diagnostics = "immutable build input contract logical name is duplicated";
        return false;
    }
    return true;
}

pmr::String ImmutableBuildInputContract::canonicalText() const
{
    pmr::String result(&defaultCpuMemoryResource());
    for (const auto& [name, value] : entries_) {
        result += std::to_string(name.size());
        result.push_back(':');
        result += name;
        result.push_back(':');
        result += std::to_string(value.size());
        result.push_back(':');
        result += value;
        result.push_back('\n');
    }
    return result;
}

pmr::String ImmutableBuildInputContract::fingerprint() const
{
    return sha256Digest(Sha256::digest(canonicalText()));
}

ImmutableBuildInputSnapshot::ImmutableBuildInputSnapshot(std::filesystem::path root)
    : root_(std::move(root))
{}

bool ImmutableBuildInputSnapshot::captureFile(
    std::string_view logicalName, const std::filesystem::path& source, pmr::String& diagnostics)
{
    if (!safeLogicalPath(logicalName)) {
        diagnostics = "immutable build snapshot logical path is invalid";
        return false;
    }
    return captureFileInternal(logicalName, source, diagnostics);
}

bool ImmutableBuildInputSnapshot::captureExecutable(
    std::string_view logicalName, const std::filesystem::path& source, pmr::String& diagnostics)
{
    if (!captureFile(logicalName, source, diagnostics))
        return false;
#if !defined(_WIN32) && !defined(__CYGWIN__)
    std::error_code error;
    const auto permissions = std::filesystem::status(source, error).permissions();
    if (error || permissions == std::filesystem::perms::unknown) {
        diagnostics = "immutable build snapshot could not inspect executable permissions";
        return false;
    }
    std::filesystem::permissions(
        path(logicalName), permissions, std::filesystem::perm_options::replace, error);
    if (error) {
        diagnostics = "immutable build snapshot could not preserve executable permissions";
        return false;
    }
    files_.back().permissions = permissions;
    files_.back().tracksPermissions = true;
#endif
    auto versionPath = path(logicalName);
    versionPath += ".version-output";
    ProcessRunRequest versionRequest;
    versionRequest.executable = path(logicalName);
    versionRequest.arguments.emplace_back("--version");
    versionRequest.outputFile = versionPath;
    const auto versionProcess = ProcessRunner().run(versionRequest);
    const auto versionRead = readTextFile(versionPath);
    if (!versionProcess.ok() || !versionRead.success || versionRead.text.empty()) {
        diagnostics = "immutable build snapshot executable has no stable version output";
        return false;
    }
    files_.back().versionOutput = versionRead.text;
    return true;
}

bool ImmutableBuildInputSnapshot::captureFileInternal(
    std::string_view logicalName, const std::filesystem::path& source, pmr::String& diagnostics)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(source, error) || error
        || std::filesystem::is_symlink(source, error) || error) {
        diagnostics = "immutable build snapshot source must be a regular non-symlink file";
        return false;
    }
    for (const auto& existing : files_) {
        if (existing.logicalName == logicalName) {
            diagnostics = "immutable build snapshot logical path is duplicated";
            return false;
        }
    }
    std::ifstream input(source, std::ios::binary);
    if (!input.good()) {
        diagnostics = "immutable build snapshot could not open its source file";
        return false;
    }
    const auto target = path(logicalName);
    std::filesystem::create_directories(target.parent_path(), error);
    if (error) {
        diagnostics = "immutable build snapshot could not create its target directory";
        return false;
    }
    std::ofstream output(target, std::ios::binary | std::ios::trunc);
    if (!output.good()) {
        diagnostics = "immutable build snapshot could not create its target file";
        return false;
    }
    Sha256 hash;
    std::array<char, 64u * 1024u> buffer{};
    while (input.good()) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0) {
            output.write(buffer.data(), count);
            hash.update(StringView(buffer.data(), static_cast<std::size_t>(count)));
        }
    }
    output.flush();
    if (!input.eof() || !output.good()) {
        diagnostics = "immutable build snapshot could not publish captured bytes";
        return false;
    }
    FileRecord record;
    record.logicalName.assign(logicalName.data(), logicalName.size());
    record.source = source;
    record.fingerprint = sha256Digest(hash.finalize());
#if !defined(_WIN32) && !defined(__CYGWIN__)
    const auto permissions = std::filesystem::status(source, error).permissions();
    if (error || permissions == std::filesystem::perms::unknown) {
        diagnostics = "immutable build snapshot could not inspect source permissions";
        return false;
    }
    std::filesystem::permissions(
        target, permissions, std::filesystem::perm_options::replace, error);
    if (error) {
        diagnostics = "immutable build snapshot could not preserve source permissions";
        return false;
    }
    record.permissions = permissions;
    record.tracksPermissions = true;
#endif
    files_.push_back(std::move(record));
    return true;
}

bool ImmutableBuildInputSnapshot::captureDirectory(
    std::string_view logicalName, const std::filesystem::path& source, pmr::String& diagnostics)
{
    if (!safeLogicalPath(logicalName)) {
        diagnostics = "immutable build snapshot logical directory is invalid";
        return false;
    }
    std::error_code error;
    if (!std::filesystem::is_directory(source, error) || error
        || std::filesystem::is_symlink(source, error) || error) {
        diagnostics = "immutable build snapshot source must be a non-symlink directory";
        return false;
    }
    pmr::Vector<std::filesystem::path> paths{&defaultCpuMemoryResource()};
    for (std::filesystem::recursive_directory_iterator iterator(source, error), end;
        !error && iterator != end;
        iterator.increment(error)) {
        if (iterator->is_symlink(error)) {
            diagnostics = "immutable build snapshot rejects symlinked directory entries";
            return false;
        }
        if (error)
            break;
        if (iterator->is_regular_file(error))
            paths.push_back(iterator->path());
        if (error)
            break;
    }
    if (error) {
        diagnostics = "immutable build snapshot could not enumerate a directory";
        return false;
    }
    std::sort(paths.begin(), paths.end());
    DirectoryRecord directory;
    directory.logicalName.assign(logicalName.data(), logicalName.size());
    directory.source = source;
    for (const auto& file : paths) {
        const auto relative = file.lexically_relative(source).generic_string();
        if (relative.empty()) {
            diagnostics = "immutable build snapshot directory entry is not relative";
            return false;
        }
        pmr::String capturedName(
            logicalName.data(), logicalName.size(), &defaultCpuMemoryResource());
        capturedName.push_back('/');
        capturedName += relative;
        if (!captureFileInternal(capturedName, file, diagnostics))
            return false;
        directory.relativeFiles.emplace_back(relative.data(), relative.size());
    }
    directories_.push_back(std::move(directory));
    return true;
}

std::filesystem::path ImmutableBuildInputSnapshot::path(std::string_view logicalName) const
{
    return root_ / std::filesystem::path(logicalName);
}

pmr::String ImmutableBuildInputSnapshot::fingerprint() const
{
    pmr::Vector<const FileRecord*> ordered{&defaultCpuMemoryResource()};
    ordered.reserve(files_.size());
    for (const auto& file : files_)
        ordered.push_back(&file);
    std::sort(ordered.begin(), ordered.end(), [](const auto* lhs, const auto* rhs) {
        return lhs->logicalName < rhs->logicalName;
    });
    ImmutableBuildInputContract snapshotContract;
    pmr::String diagnostics{&defaultCpuMemoryResource()};
    for (const auto* file : ordered) {
        ImmutableBuildInputContract fileContract;
        if (!fileContract.add("content-sha256", file->fingerprint, diagnostics))
            return {};
        if (file->tracksPermissions) {
            const auto permissions =
                std::to_string(static_cast<unsigned long long>(file->permissions));
            if (!fileContract.add("permissions", permissions, diagnostics))
                return {};
        }
        if (!file->versionOutput.empty()
            && !fileContract.add("version-output", file->versionOutput, diagnostics)) {
            return {};
        }
        const auto value = fileContract.canonicalText();
        if (!snapshotContract.add(file->logicalName, value, diagnostics))
            return {};
    }
    return snapshotContract.fingerprint();
}

bool ImmutableBuildInputSnapshot::verifySourcesUnchanged(pmr::String& diagnostics) const
{
    for (const auto& file : files_) {
        pmr::String sourceFingerprint{&defaultCpuMemoryResource()};
        if (!hashFile(file.source, sourceFingerprint) || sourceFingerprint != file.fingerprint) {
            diagnostics = "immutable build snapshot source changed during tool execution: ";
            diagnostics += file.logicalName;
            return false;
        }
        pmr::String capturedFingerprint{&defaultCpuMemoryResource()};
        if (!hashFile(path(file.logicalName), capturedFingerprint)
            || capturedFingerprint != file.fingerprint) {
            diagnostics = "immutable build snapshot captured bytes drifted during tool execution: ";
            diagnostics += file.logicalName;
            return false;
        }
        if (file.tracksPermissions) {
            std::error_code permissionError;
            const auto permissions =
                std::filesystem::status(file.source, permissionError).permissions();
            if (permissionError || permissions != file.permissions) {
                diagnostics =
                    "immutable build snapshot executable permissions changed during tool execution: ";
                diagnostics += file.logicalName;
                return false;
            }
            permissionError.clear();
            const auto capturedPermissions =
                std::filesystem::status(path(file.logicalName), permissionError).permissions();
            if (permissionError || capturedPermissions != file.permissions) {
                diagnostics =
                    "immutable build snapshot captured executable permissions drifted during tool execution: ";
                diagnostics += file.logicalName;
                return false;
            }
        }
        if (!file.versionOutput.empty()) {
            auto versionPath = path(file.logicalName);
            versionPath += ".verify-version-output";
            ProcessRunRequest versionRequest;
            versionRequest.executable = path(file.logicalName);
            versionRequest.arguments.emplace_back("--version");
            versionRequest.outputFile = versionPath;
            const auto process = ProcessRunner().run(versionRequest);
            const auto read = readTextFile(versionPath);
            std::error_code cleanupError;
            std::filesystem::remove(versionPath, cleanupError);
            if (!process.ok() || !read.success || read.text != file.versionOutput) {
                diagnostics =
                    "immutable build snapshot executable version output drifted during tool execution: ";
                diagnostics += file.logicalName;
                return false;
            }
        }
    }
    for (const auto& directory : directories_) {
        pmr::Vector<pmr::String> current{&defaultCpuMemoryResource()};
        std::error_code error;
        for (std::filesystem::recursive_directory_iterator iterator(directory.source, error), end;
            !error && iterator != end;
            iterator.increment(error)) {
            if (iterator->is_regular_file(error)) {
                const auto relative =
                    iterator->path().lexically_relative(directory.source).generic_string();
                current.emplace_back(relative.data(), relative.size());
            }
            if (error)
                break;
        }
        std::sort(current.begin(), current.end());
        if (error || current != directory.relativeFiles) {
            diagnostics = "immutable build snapshot directory changed during tool execution: ";
            diagnostics += directory.logicalName;
            diagnostics += " source=";
            const auto sourceText = directory.source.generic_string();
            diagnostics.append(sourceText.data(), sourceText.size());
            return false;
        }
    }
    return true;
}

} // namespace arp::core

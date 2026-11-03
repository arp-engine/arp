#include "editor/workspace/editor_scene_persistence.h"

#include "core/io/filesystem_path.h"
#include "editor/io/editor_cold_host_read.h"

#include <system_error>

namespace arp::editor {
// One owner-thread request pool covers both reads and writes. Only the
// coordinator accesses the legacy writer; the UI thread only receives results.
struct EditorScenePersistence::Impl final {
    static constexpr std::uint32_t requestCapacity = 4;
    static constexpr std::size_t maxBytes = EditorSceneAuthoringCodec::maxDocumentBytes;

    struct Request {
        Impl* owner = nullptr;
        core::io::AtomicFileOperation operation = core::io::AtomicFileOperation::Read;
        core::pmr::String path;
        core::io::FileContentValidator validator = nullptr;
        std::optional<core::io::FileReplacePrecondition> expected;
        core::io::AtomicFileResult result;
        bool complete = false;
    };

    Impl()
        : requests(requestCapacity), completions({requestCapacity}), writer(1, maxBytes),
          worker({&completions, 1, requestCapacity, false})
    {}

    ~Impl()
    {
        // Accepted work advances autonomously. Keep request payloads and the
        // legacy writer alive until every queued operation has truly finished.
        worker.shutdown(core::runtime::WorkShutdownPolicy::Drain);
        completions.dispatch(requestCapacity);
        completions.shutdown();
    }

    static bool validate(
        const core::pmr::Vector<std::uint8_t>& bytes, core::pmr::String& diagnostic)
    {
        return core::io::VersionedYamlStore<SceneSchema>::validateContents(
            bytes, maxBytes, diagnostic);
    }

    RequestHandle submit(core::io::AtomicFileOperation operation,
        core::pmr::String path,
        core::pmr::Vector<std::uint8_t> bytes = {},
        core::io::FileContentValidator validator = nullptr,
        std::optional<core::io::FileReplacePrecondition> expected = std::nullopt)
    {
        diagnostic = nullptr;
        if (path.empty() || path.find('\0') != core::pmr::String::npos || bytes.size() > maxBytes
            || !worker.valid()) {
            diagnostic = "ARP-FILE-REQUEST-CONFIG";
            return {};
        }
        if (expected
            && (bytes.capacity() > maxBytes || expected->expectedBytes.capacity() > maxBytes
                || (expected->kind != core::io::FileReplacePrecondition::Kind::MustBeMissing
                    && expected->kind != core::io::FileReplacePrecondition::Kind::MatchBytes)
                || (expected->kind == core::io::FileReplacePrecondition::Kind::MustBeMissing
                    && !expected->expectedBytes.empty()))) {
            diagnostic = "ARP-FILE-PRECONDITION-CONFIG";
            return {};
        }
        Request pending;
        pending.owner = this;
        pending.operation = operation;
        pending.path = std::move(path);
        pending.validator = validator;
        pending.expected = std::move(expected);
        pending.result.bytes = std::move(bytes);
        const auto handle = requests.emplace(std::move(pending));
        if (!handle) {
            diagnostic = "ARP-FILE-REQUEST-CAPACITY";
            return {};
        }
        auto* request = requests.get(handle);
        try {
            if (worker.submit({execute, request, complete, request, {}}).accepted())
                return handle;
            diagnostic = "ARP-FILE-WORK-REJECTED";
        } catch (...) {
            diagnostic = "ARP-FILE-WORK-EXCEPTION";
        }
        requests.erase(handle);
        return {};
    }

    static core::runtime::WorkStatus execute(void* context)
    {
        auto& request = *static_cast<Request*>(context);
        auto& result = request.result;
        using namespace core::io;
        try {
            if (request.operation == AtomicFileOperation::Read) {
                FileReadRequest read;
                read.contractVersion = 1;
                read.pathScope = FilePathScope::HostFilesystem;
                read.path = std::move(request.path);
                read.maxBytes = maxBytes;
                auto acquired = EditorColdHostRead::read(read);
                if (acquired.status == FileIoStatus::MissingFile) {
                    result.missing = true;
                    result.diagnostic = "ARP-FILE-MISSING";
                } else if (acquired.status != FileIoStatus::Success) {
                    result.diagnostic =
                        acquired.diagnosticId ? acquired.diagnosticId : acquired.diagnostics;
                } else {
                    result.bytes = std::move(acquired.bytes);
                    result.success =
                        !request.validator || request.validator(result.bytes, result.diagnostic);
                }
            } else {
                auto& writer = request.owner->writer;
                const auto handle = request.expected
                    ? writer.replaceIfUnchanged(std::move(request.path),
                          std::move(result.bytes),
                          std::move(*request.expected))
                    : writer.replace(
                          std::move(request.path), std::move(result.bytes), request.validator);
                if (!handle) {
                    result.diagnostic = writer.diagnostic();
                } else {
                    // Cold background bridge, never an owner/frame wait. The
                    // next FIFO item starts only after real atomic completion.
                    while (!writer.take(handle, result))
                        std::this_thread::yield();
                }
            }
        } catch (...) {
            result.success = false;
            result.diagnostic = "ARP-FILE-WORK-EXCEPTION";
        }
        return result.success ? core::runtime::WorkStatus::Completed
                              : core::runtime::WorkStatus::Failed;
    }

    static void complete(void* context, core::runtime::WorkResult result)
    {
        auto& request = *static_cast<Request*>(context);
        if (result.status == core::runtime::WorkStatus::Cancelled) {
            request.result.success = false;
            request.result.diagnostic = "ARP-FILE-CANCELLED";
        }
        request.complete = true;
    }

    bool take(RequestHandle handle, core::io::AtomicFileResult& result)
    {
        completions.dispatch(requestCapacity);
        auto* request = requests.get(handle);
        if (!request) {
            diagnostic = "ARP-FILE-REQUEST-STALE";
            return false;
        }
        if (!request->complete)
            return false;
        result = std::move(request->result);
        requests.erase(handle);
        return true;
    }

    core::GenerationPool<Request> requests;
    core::runtime::CompletionQueue completions;
    core::io::AtomicFileService writer;
    core::runtime::WorkService worker;
    const char* diagnostic = nullptr;
};

EditorScenePersistence::EditorScenePersistence() : impl_(core::makeUnique<Impl>())
{}

EditorScenePersistence::~EditorScenePersistence() = default;

bool EditorScenePersistence::SceneSchema::validate(
    const core::YamlContract::Node& root, core::pmr::String& diagnostic)
{
    core::pmr::String yaml(&core::defaultCpuMemoryResource());
    try {
        const auto serialized = core::YamlContract::serialize(root);
        yaml.assign(serialized.data(), serialized.size());
    } catch (...) {
        diagnostic = "EDITOR-SCENE-YAML-SERIALIZE";
        return false;
    }
    return EditorSceneAuthoringCodec::validateStructure(yaml, diagnostic);
}

bool EditorScenePersistence::resolvePath(const std::filesystem::path& projectRoot,
    std::string_view relativePath,
    bool requireExistingFile,
    std::filesystem::path& resolved,
    const char*& diagnostic)
{
    if (!core::FilePath::isSafeRelativePath(relativePath)) {
        diagnostic = "EDITOR-SCENE-PATH-UNSAFE";
        return false;
    }
    std::error_code error;
    const auto root = std::filesystem::canonical(projectRoot, error);
    if (error || !std::filesystem::is_directory(root, error) || error) {
        diagnostic = "EDITOR-SCENE-PROJECT-ROOT";
        return false;
    }
    const auto candidate = root / std::filesystem::u8path(relativePath);
    if (requireExistingFile) {
        resolved = std::filesystem::canonical(candidate, error);
        if (error || !std::filesystem::is_regular_file(resolved, error) || error) {
            diagnostic = "EDITOR-SCENE-FILE-UNREADABLE";
            return false;
        }
    } else {
        const auto parent = std::filesystem::canonical(candidate.parent_path(), error);
        if (error || !std::filesystem::is_directory(parent, error) || error) {
            diagnostic = "EDITOR-SCENE-PARENT-UNAVAILABLE";
            return false;
        }
        resolved = parent / candidate.filename();
        if (std::filesystem::exists(resolved, error)) {
            if (error || !std::filesystem::is_regular_file(resolved, error) || error) {
                diagnostic = "EDITOR-SCENE-TARGET-NOT-FILE";
                return false;
            }
            resolved = std::filesystem::canonical(resolved, error);
            if (error) {
                diagnostic = "EDITOR-SCENE-TARGET-UNRESOLVABLE";
                return false;
            }
        } else if (error) {
            diagnostic = "EDITOR-SCENE-TARGET-STAT";
            return false;
        }
    }
    if (!core::FilePath::contains(root, resolved)) {
        diagnostic = "EDITOR-SCENE-PATH-ESCAPES-PROJECT";
        return false;
    }
    return true;
}

EditorScenePersistence::RequestHandle EditorScenePersistence::load(
    const std::filesystem::path& projectRoot,
    std::string_view relativePath,
    std::filesystem::path* resolvedTarget)
{
    diagnostic_ = nullptr;
    std::filesystem::path resolved;
    if (!resolvePath(projectRoot, relativePath, true, resolved, diagnostic_))
        return {};
    const auto request = impl_->submit(
        core::io::AtomicFileOperation::Read, core::FilePath::utf8(resolved), {}, Impl::validate);
    if (!request)
        diagnostic_ = impl_->diagnostic;
    if (request && resolvedTarget)
        *resolvedTarget = std::move(resolved);
    return request;
}

EditorScenePersistence::RequestHandle EditorScenePersistence::save(
    const std::filesystem::path& projectRoot,
    std::string_view relativePath,
    const core::pmr::String& yaml,
    bool overwriteExisting)
{
    diagnostic_ = nullptr;
    if (yaml.empty() || yaml.size() > EditorSceneAuthoringCodec::maxDocumentBytes) {
        diagnostic_ = "EDITOR-SCENE-DOCUMENT-LIMIT";
        return {};
    }
    std::filesystem::path resolved;
    if (!resolvePath(projectRoot, relativePath, false, resolved, diagnostic_))
        return {};
    std::error_code error;
    const bool exists = std::filesystem::exists(resolved, error);
    if (error) {
        diagnostic_ = "EDITOR-SCENE-TARGET-STAT";
        return {};
    }
    if (exists && !overwriteExisting) {
        diagnostic_ = "EDITOR-SCENE-OVERWRITE-REQUIRED";
        return {};
    }
    return saveResolved(resolved, yaml);
}

EditorScenePersistence::RequestHandle EditorScenePersistence::readResolved(
    const std::filesystem::path& target)
{
    diagnostic_ = nullptr;
    const auto request =
        impl_->submit(core::io::AtomicFileOperation::Read, core::FilePath::utf8(target));
    if (!request)
        diagnostic_ = impl_->diagnostic;
    return request;
}

EditorScenePersistence::RequestHandle EditorScenePersistence::saveResolved(
    const std::filesystem::path& target, const core::pmr::String& yaml)
{
    diagnostic_ = nullptr;
    if (yaml.empty() || yaml.size() > EditorSceneAuthoringCodec::maxDocumentBytes) {
        diagnostic_ = "EDITOR-SCENE-DOCUMENT-LIMIT";
        return {};
    }
    const auto request = impl_->submit(core::io::AtomicFileOperation::Replace,
        core::FilePath::utf8(target),
        {yaml.begin(), yaml.end()},
        Impl::validate);
    if (!request)
        diagnostic_ = impl_->diagnostic;
    return request;
}

bool EditorScenePersistence::take(RequestHandle handle, core::io::AtomicFileResult& result)
{
    return impl_->take(handle, result);
}

EditorScenePersistence::RequestHandle EditorScenePersistence::saveScript(
    const EditorSceneDocument& document,
    const EditorSceneScriptInputs& inputs,
    const EditorSceneScriptFilePreparation::Prepared& prepared,
    core::pmr::String& diagnostic)
{
    diagnostic_ = nullptr;
    diagnostic.clear();
    if (inputs.sources) {
        diagnostic_ = "EDITOR-SCRIPT-FILE-MOUNTED-SAVE-REQUIRES-SOURCE-HOST";
        diagnostic = diagnostic_;
        return {};
    }
    try {
        // Allocate all worker-owned payloads before final contextual admission.
        auto path = prepared.path();
        auto bytes = prepared.replacementBytes();
        core::io::FileReplacePrecondition expected(prepared.targetExisted()
                ? core::io::FileReplacePrecondition::Kind::MatchBytes
                : core::io::FileReplacePrecondition::Kind::MustBeMissing);
        expected.expectedBytes = prepared.previousBytes();
        if (!prepared.current(document, inputs, diagnostic))
            return {};
        const auto handle = impl_->submit(core::io::AtomicFileOperation::Replace,
            std::move(path),
            std::move(bytes),
            nullptr,
            std::move(expected));
        if (!handle) {
            diagnostic_ = impl_->diagnostic;
            diagnostic = diagnostic_ ? diagnostic_ : "EDITOR-SCRIPT-FILE-SUBMIT";
        }
        return handle;
    } catch (const std::exception&) {
        diagnostic_ = "EDITOR-SCRIPT-FILE-SUBMIT-EXCEPTION";
        diagnostic = diagnostic_;
        return {};
    }
}

EditorScenePersistence::RequestHandle EditorScenePersistence::readScript(
    const std::filesystem::path& projectRoot,
    std::string_view relativePath,
    std::filesystem::path* resolvedTarget)
{
    diagnostic_ = nullptr;
    std::filesystem::path resolved;
    if (!resolvePath(projectRoot, relativePath, true, resolved, diagnostic_))
        return {};
    const auto request = readResolved(resolved);
    if (!request)
        diagnostic_ = impl_->diagnostic;
    if (request && resolvedTarget)
        resolvedTarget->swap(resolved);
    return request;
}

} // namespace arp::editor

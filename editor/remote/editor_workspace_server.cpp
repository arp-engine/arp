#include "editor/remote/editor_workspace_server.h"

#include "core/io/filesystem_path.h"
#include "core/io/mounted_file_backend.h"
#include "core/io/native_directory_file_backend.h"
#include "core/math/unsigned_range.h"
#include "core/runtime/async_runtime.h"
#include "core/text/number_format.h"
#include "network/http/server.h"
#include "network/io_service.h"
#include "network/socket_runtime.h"

#include <thread>

namespace arp::editor {
struct EditorWorkspaceServer::State {
    struct RootProbe {
        core::io::FileQueryResult result;
        bool done = false;
    };

    network::SocketRuntime sockets;
    network::io_service service;
    network::io_context context{&service};
    core::UniquePtr<network::http::Server> server;
    core::UniquePtr<core::runtime::AsyncRuntime> files;
    core::UniquePtr<EditorWorkspaceProtocol> protocol;
    std::thread reactor;
    core::pmr::String diagnostic;
    bool receiving = false, stopping = false;
};

EditorWorkspaceServer::EditorWorkspaceServer() : state_(core::makeShared<State>())
{}

EditorWorkspaceServer::~EditorWorkspaceServer()
{
    stop();
}

bool EditorWorkspaceServer::start(
    std::string_view root, EditorWorkspaceProtocolDesc desc, std::uint16_t port)
{
    auto& s = *state_;
    if (s.server || s.stopping || !s.sockets.valid() || !core::FilePath::isAbsoluteHostPath(root)) {
        s.diagnostic = "WORKSPACE-SERVER-ROOT-OR-STATE";
        return false;
    }
    s.diagnostic = "WORKSPACE-SERVER-START";
    try {
        // Reject malformed host budgets before computing backend reservations
        // or allowing the HTTP parser to reserve its body/header storage.
        const auto entryBytes = sizeof(core::io::FileDirectoryEntry);
        if (!core::canMultiplyUnsigned(desc.maxFileBytes, std::size_t{3})
            || !core::canMultiplyUnsigned(std::size_t{desc.maxDirectoryEntries}, entryBytes)) {
            s.diagnostic = "WORKSPACE-SERVER-BUDGET-OVERFLOW";
            return false;
        }
        auto fileBytes = 3 * desc.maxFileBytes;
        for (const auto bytes : {desc.maxQueryBytes,
                 desc.maxDirectoryNameBytes,
                 std::size_t{desc.maxDirectoryEntries} * entryBytes}) {
            if (!core::canAddUnsigned(fileBytes, bytes)) {
                s.diagnostic = "WORKSPACE-SERVER-BUDGET-OVERFLOW";
                return false;
            }
            fileBytes += bytes;
        }
        network::http::ServerConfig config;
        if (!core::canAddUnsigned(
                desc.maxRequestBytes, config.session.parser_limits.max_header_bytes)) {
            s.diagnostic = "WORKSPACE-SERVER-BUDGET-OVERFLOW";
            return false;
        }
        core::io::NativeDirectoryFileBackendDesc native;
        native.rootPath = root;
        native.maxInFlight = 1;
        // Write payload, observed baseline and private replacement workspace;
        // queries additionally declare their own result/name/entry storage.
        native.maxBytesInFlight = fileBytes;
        native.maxPathBytes = 512;
        native.ioChunkBytes = 64 * 1024;
        native.directoryBufferBytes = 64 * 1024;
        native.maxScanEntries = desc.maxDirectoryEntries;
        native.maxScanNameBytes = desc.maxDirectoryNameBytes;
        native.allowObservedBaselineReplace = !desc.writeToken.empty();
        auto backend = core::makeShared<core::io::NativeDirectoryFileBackend>(native);
        const auto capability = backend->capability();
        if (!capability.completesThroughCompletionQueue || !capability.supportsContentVersion
            || !capability.supportsEnumeration) {
            s.diagnostic = "WORKSPACE-SERVER-NATIVE-CAPABILITY";
            return false;
        }
        auto mounts =
            core::makeShared<core::io::MountedFileBackend>(core::io::MountedFileBackendDesc{
                1, 1, native.maxBytesInFlight, 128, native.maxPathBytes});
        desc.mount = mounts->mount("workspace", backend);
        if (!desc.mount)
            return false;
        core::runtime::AsyncRuntimeDesc runtime;
        runtime.enableWork = runtime.enableThreadPoolFileBackend = false;
        runtime.completionQueueCapacity = runtime.completionBudget = 8;
        runtime.fileBackend = std::move(mounts);
        s.files = core::makeUnique<core::runtime::AsyncRuntime>(std::move(runtime));
        if (!s.files->valid())
            return false;
        // Register only an actually opened directory. Capability describes the
        // backend implementation; it cannot prove that this particular root exists.
        auto probe = core::makeShared<State::RootProbe>();
        auto retainedProbe = core::makeUnique<core::SharedPtr<State::RootProbe>>(probe);
        core::runtime::CancellationSource cancellation;
        core::io::FileQueryRequest query;
        query.pathScope = core::io::FilePathScope::MountedResource;
        query.mount = desc.mount;
        query.maxResultBytes = desc.maxQueryBytes;
        query.cancellation = cancellation.token();
        const auto accepted = s.files->fileService().query(query,
            {[](void* data, core::io::FileQueryResult result) noexcept {
                 using Retained = core::SharedPtr<State::RootProbe>;
                 core::UniquePtr<Retained> owner{static_cast<Retained*>(data),
                     core::CpuAllocatedDeleter<Retained>{
                         data, &core::destroyCpuAllocatedUnique<Retained>}};
                 (*owner)->result = std::move(result);
                 (*owner)->done = true;
             },
                retainedProbe.get()});
        if (accepted != core::io::FileIoStatus::Accepted)
            return false;
        retainedProbe.release();
        const auto rootDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!probe->done && std::chrono::steady_clock::now() < rootDeadline) {
            s.files->dispatch();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!probe->done || probe->result.status != core::io::FileIoStatus::Success
            || probe->result.metadata.kind != core::io::FileEntryKind::Directory) {
            cancellation.cancel();
            s.diagnostic = probe->result.diagnosticId ? probe->result.diagnosticId
                                                      : "WORKSPACE-SERVER-ROOT-NOT-READY";
            stop();
            return false;
        }
        config.max_connections = config.max_pending_requests = 4;
        config.max_routes = 2;
        config.session.parser_limits.max_body_bytes = desc.maxRequestBytes;
        config.session.parser_limits.max_buffer_bytes =
            desc.maxRequestBytes + config.session.parser_limits.max_header_bytes;
        s.server = core::makeUnique<network::http::Server>(&s.context, config);
        if (s.server->route(network::http::Method::Post, "/workspace/v2")
                != network::http::DiagnosticCode::none
            || s.server->route(network::http::Method::Options, "/workspace/v2")
                != network::http::DiagnosticCode::none
            || !s.server->start(network::endpoint::localhost<network::ipv4>(port)).success())
            return false;
        s.reactor = std::thread([retained = state_] {
            retained->service.run();
        });
        desc.expectedHost = "127.0.0.1:" + core::formatUnsigned(s.server->local_endpoint().port());
        s.protocol =
            core::makeUnique<EditorWorkspaceProtocol>(s.files->fileService(), std::move(desc));
        if (!s.protocol->valid()) {
            s.diagnostic = s.protocol->diagnostic();
            stop();
            return false;
        }
        s.diagnostic.clear();
        return true;
    } catch (...) {
        s.diagnostic = "WORKSPACE-SERVER-PREPARATION";
        stop();
        return false;
    }
}

void EditorWorkspaceServer::pump()
{
    auto& s = *state_;
    if (s.files)
        s.files->dispatch();
    s.context.dispatch();
    if (s.stopping || !running() || s.receiving)
        return;
    s.receiving = true;
    s.server->async_next_request([retained = state_](const network::http::SessionStatus& status,
                                     network::http::IncomingRequestPtr incoming) {
        auto& state = *retained;
        state.receiving = false;
        if (!status.success() || !incoming || !incoming->active() || state.stopping)
            return;
        state.protocol->handle(incoming->request(), [incoming](network::http::Response response) {
            if (incoming->active())
                incoming->async_respond(response, [](const network::http::SessionStatus&) {});
        });
    });
}

void EditorWorkspaceServer::stop()
{
    auto& s = *state_;
    s.stopping = true;
    if (s.protocol)
        s.protocol->shutdown();
    if (s.files)
        s.files->shutdown();
    if (s.server && s.server->running()) {
        s.server->stop();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!s.server->stopped() && std::chrono::steady_clock::now() < deadline) {
            s.context.dispatch();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    s.context.shutdown();
    if (s.reactor.joinable())
        s.reactor.join();
    s.context.dispatch();
    s.protocol.reset();
    s.files.reset();
    if (s.server && !s.server->stopped())
        s.diagnostic = "WORKSPACE-SERVER-STOP-INCOMPLETE";
}

bool EditorWorkspaceServer::running() const noexcept
{
    return state_->server && state_->server->running();
}

bool EditorWorkspaceServer::stopped() const noexcept
{
    return (!state_->server || state_->server->stopped()) && !state_->reactor.joinable();
}

std::uint16_t EditorWorkspaceServer::port() const noexcept
{
    return state_->server ? state_->server->local_endpoint().port() : 0;
}

std::string_view EditorWorkspaceServer::diagnostic() const noexcept
{
    return state_->diagnostic;
}
} // namespace arp::editor

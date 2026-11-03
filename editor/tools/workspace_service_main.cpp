#include "core/text/number_format.h"
#include "editor/project/project_identity.h"
#include "editor/remote/editor_workspace_server.h"

#include <array>
#include <atomic>
#include <charconv>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace {
std::atomic_bool stopped{false};

void stopSignal(int)
{
    stopped.store(true, std::memory_order_relaxed);
}

bool unsignedNumber(std::string_view input, std::uint64_t& value)
{
    const auto parsed = std::from_chars(input.data(), input.data() + input.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == input.data() + input.size();
}

// CLI admission only. The existing server/protocol remain responsible for
// cross-budget validation, allocations, capabilities and actual file IO.
class WorkspaceServiceBudgetArguments final {
public:
    enum class Result { Unknown, Invalid, Accepted };

    explicit WorkspaceServiceBudgetArguments(arp::editor::EditorWorkspaceProtocolDesc& target)
        : target_(target)
    {}

    Result parse(std::string_view key, std::string_view text)
    {
        using Desc = arp::editor::EditorWorkspaceProtocolDesc;

        struct Binding {
            std::string_view name;
            std::size_t Desc::* field;
        };

        constexpr std::array bindings{Binding{"--max-file-bytes", &Desc::maxFileBytes},
            Binding{"--max-request-bytes", &Desc::maxRequestBytes},
            Binding{"--max-response-bytes", &Desc::maxResponseBytes},
            Binding{"--preparation-bytes", &Desc::preparationBytes},
            Binding{"--max-query-bytes", &Desc::maxQueryBytes},
            Binding{"--max-directory-name-bytes", &Desc::maxDirectoryNameBytes}};
        for (const auto& binding : bindings)
            if (key == binding.name) {
                std::uint64_t value = 0;
                if (!unsignedNumber(text, value) || !value || value >= SIZE_MAX
                    || value > INT64_MAX)
                    return Result::Invalid;
                target_.*binding.field = static_cast<std::size_t>(value);
                return Result::Accepted;
            }
        if (key == "--max-directory-entries" || key == "--max-receipts") {
            std::uint64_t value = 0;
            if (!unsignedNumber(text, value) || !value || value > UINT32_MAX)
                return Result::Invalid;
            (key == "--max-directory-entries" ? target_.maxDirectoryEntries : target_.maxReceipts) =
                static_cast<std::uint32_t>(value);
            return Result::Accepted;
        }
        return Result::Unknown;
    }

private:
    arp::editor::EditorWorkspaceProtocolDesc& target_;
};
} // namespace

int main(int argc, char** argv)
{
    arp::editor::EditorWorkspaceProtocolDesc desc;
    WorkspaceServiceBudgetArguments budgets(desc);
    arp::core::pmr::String root;
    std::uint64_t port = 8094, ticks = 0, durationMs = 0;
    bool writes = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view key(argv[i]);
        if (key == "--write") {
            writes = true;
            continue;
        }
        if (i + 1 >= argc) {
            std::fputs("WORKSPACE-CLI-ARGUMENT\n", stderr);
            return 2;
        }
        const std::string_view value(argv[++i]);
        if (key == "--root")
            root = value;
        else if (key == "--id")
            desc.workspaceId = value;
        else if (key == "--origin")
            desc.allowedOrigin = value;
        else if (key == "--port") {
            if (!unsignedNumber(value, port) || port > UINT16_MAX)
                return 2;
        } else if (key == "--ticks") {
            if (!unsignedNumber(value, ticks))
                return 2;
        } else if (key == "--duration-ms") {
            if (!unsignedNumber(value, durationMs) || durationMs > INT32_MAX)
                return 2;
        } else {
            const auto result = budgets.parse(key, value);
            if (result != WorkspaceServiceBudgetArguments::Result::Accepted) {
                std::fputs(result == WorkspaceServiceBudgetArguments::Result::Invalid
                        ? "WORKSPACE-CLI-BUDGET\n"
                        : "WORKSPACE-CLI-ARGUMENT\n",
                    stderr);
                return 2;
            }
        }
    }
    if (const char* value = std::getenv("ARP_WORKSPACE_READ_TOKEN"))
        desc.readToken = value;
    if (writes) {
        if (const char* value = std::getenv("ARP_WORKSPACE_WRITE_TOKEN"))
            desc.writeToken = value;
        if (desc.writeToken.empty()) {
            std::fputs("WORKSPACE-CLI-WRITE-TOKEN-REQUIRED\n", stderr);
            return 2;
        }
    }
    arp::core::pmr::String diagnostic;
    if (!arp::editor::ProjectIdentity::create(
            "EditorWorkspaceSession/1", desc.sessionId, diagnostic)) {
        std::fprintf(stderr, "%s\n", diagnostic.c_str());
        return 2;
    }
    const auto id = desc.workspaceId, session = desc.sessionId;
    arp::editor::EditorWorkspaceServer server;
    if (!server.start(root, std::move(desc), static_cast<std::uint16_t>(port))) {
        std::fprintf(stderr, "%.*s\n", int(server.diagnostic().size()), server.diagnostic().data());
        return 2;
    }
    std::signal(SIGINT, stopSignal);
    std::signal(SIGTERM, stopSignal);
    std::printf("ARP workspace: http://127.0.0.1:%u/workspace/v2 id=%s session=%s\n",
        server.port(),
        id.c_str(),
        session.c_str());
    std::fflush(stdout);
    const auto started = std::chrono::steady_clock::now();
    for (std::uint64_t tick = 0;
        !stopped.load(std::memory_order_relaxed) && (!ticks || tick < ticks)
        && (!durationMs
            || std::chrono::steady_clock::now() - started < std::chrono::milliseconds(durationMs));
        ++tick) {
        server.pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    server.stop();
    return server.stopped() ? 0 : 1;
}

#include "tests/fixtures/workspace_backend_fixture.h"

int main()
{
    using namespace workspace_backend_fixture;
    runtime::CompletionQueue queue{{2}};
    auto d = descriptor("http://127.0.0.1:8094");
    for (unsigned i = 0; i < 7; ++i) {
        auto bad = d;
        switch (i) {
        case 0:
            bad.contractVersion = 2;
            break;
        case 1:
            bad.endpoint += "?x";
            break;
        case 2:
            bad.token = "short";
            break;
        case 3:
            bad.maxResponseBytes = 0;
            break;
        case 4:
            bad.preparationBytes = 0;
            break;
        case 5:
            bad.credentialOrigin += '/';
            break;
        case 6:
            bad.workspaceId = "../root";
            break;
        }
        WorkspaceFileBackend invalid(bad);
        assert(invalid.diagnostic() && !invalid.capability().supportsRead);
    }
    WorkspaceFileBackend backend(d);
    assert(!backend.diagnostic() && !backend.connected() && !backend.capability().supportsRead
        && !backend.saveCapability().operationReceipts);
    assert(backend.connectAsync({}, queue) ==
#if defined(__EMSCRIPTEN__)
        FileIoStatus::Unsupported
#else
        FileIoStatus::InvalidRequest
#endif
    );
    backend.shutdown();
    assert(!backend.capability().supportsRead);
    std::puts("PASS workspace configuration, closed/unconnected capability and execution gates");
}

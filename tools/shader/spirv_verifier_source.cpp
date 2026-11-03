#include "tools/shader/spirv_verifier_source.h"

#include "core/io/filesystem_path.h"
#include "tools/io/cold_tool_files.h"

namespace arp::tools::shader {

core::io::FileReadResult SpirvVerifierSource::acquire(const SpirvVerifierSourceDesc& desc)
{
    using namespace core::io;
    FileReadResult rejected;
    rejected.status = FileIoStatus::InvalidRequest;
    rejected.diagnosticId = "SHADER-VERIFIER-SOURCE-DESCRIPTOR";
    if (desc.path.empty() || !io::ColdToolFiles::validBudget(desc.maxBytes))
        return rejected;
    io::ColdToolFiles files(desc.maxBytes, 1);
    FileReadRequest request;
    request.contractVersion = 1;
    request.pathScope = FilePathScope::MountedResource;
    request.maxBytes = desc.maxBytes;
    if (!desc.httpBase.empty()) {
        request.mount = files.mountHttp("spirv", desc.httpBase);
        request.path = desc.path;
    } else {
        const auto path =
            std::filesystem::absolute(std::filesystem::u8path(desc.path.begin(), desc.path.end()))
                .lexically_normal();
        request.mount = files.mountDirectory("spirv", path.parent_path());
        request.path = core::FilePath::utf8(path.filename());
    }
    if (!request.mount)
        return rejected;
    auto result = files.read(request);
    files.retire();
    return result;
}
} // namespace arp::tools::shader

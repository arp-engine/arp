#pragma once

#include "core/io/file_text.h"
#include "ui/ui_document.h"

#include <ostream>

namespace arp::tools::asset_importer {

struct DocumentImportOptions {
    core::pmr::String material{&core::defaultCpuMemoryResource()};
    core::pmr::String gltf{&core::defaultCpuMemoryResource()};
    core::pmr::String legacyLayout{&core::defaultCpuMemoryResource()};
    core::pmr::String legacyPsd{&core::defaultCpuMemoryResource()};
    core::pmr::String legacyManifest{&core::defaultCpuMemoryResource()};
    core::pmr::String uiIr{&core::defaultCpuMemoryResource()};
    core::pmr::String outputUi{&core::defaultCpuMemoryResource()};
    core::pmr::String httpBase{&core::defaultCpuMemoryResource()};
    std::uint64_t maxDocumentBytes = 4u * 1024u * 1024u;
    bool legacyUiJson = false;
};

struct ToolUiDocumentResult {
    bool success = false;
    core::pmr::String yaml{&core::defaultCpuMemoryResource()};
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    std::size_t nodeCount = 0;
    std::uint32_t stableHash = 0;
};

// Tool authoring projection, not a second UI parser/serializer. All final YAML
// is produced and reloaded by the original UI domain builder and validator.
class ToolUiDocumentCompiler final {
public:
    static ToolUiDocumentResult compileIr(core::StringView source, std::size_t budget);
    static ToolUiDocumentResult compileLegacy(const ui::UiDocument&, std::size_t budget);
};

class DocumentImporter final {
public:
    int run(const DocumentImportOptions&, std::ostream& out, std::ostream& err) const;
};
} // namespace arp::tools::asset_importer

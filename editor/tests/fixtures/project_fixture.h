#pragma once

#include "core/memory/cpu_allocator.h"
#include "core/text/number_format.h"

#include <string_view>

namespace arp::editor::tests {

// Isolated test roots use this explicit identity. Body text stays raw so tests
// exercise the production parser rather than serializing with its own codec.
class ProjectFixture final {
public:
    static core::pmr::String document(
        std::string_view body, std::string_view metadata = "null", std::uint32_t version = 1)
    {
        core::pmr::String text("type: arp.project\ncontractVersion: ");
        core::appendUnsigned(text, version);
        text += "\nid: 4305ee93-3897-43ae-93da-ecbb0c569c83\n"
                "name: Editor project fixture\n"
                "engineContract: editor-project-fixture/1\n"
                "entryScene: null\nexecution: Interpreter\n"
                "scriptModules: []\neditorMetadata: ";
        text += metadata;
        text += '\n';
        text += body;
        return text;
    }
};

} // namespace arp::editor::tests

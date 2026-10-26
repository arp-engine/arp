#pragma once

#include "core/memory/cpu_allocator.h"

#include <string_view>

namespace arp::editor {
// Authoring operations allocate identity explicitly; codecs never infer it
// from a file path, object name or deserialization order.
class ProjectIdentity final {
public:
    static bool create(
        std::string_view domain, core::pmr::String& identity, core::pmr::String& diagnostic);
};
} // namespace arp::editor

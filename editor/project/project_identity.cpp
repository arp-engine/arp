#include "editor/project/project_identity.h"

#include "core/text/number_format.h"
#include "resource/resource_contract.h"
#include "resource/resource_manager.h"

#include <random>

namespace arp::editor {
bool ProjectIdentity::create(
    std::string_view domain, core::pmr::String& identity, core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (domain.empty()) {
        diagnostic = "EDITOR-IDENTITY-DOMAIN-REQUIRED";
        return false;
    }
    try {
        // Eight uint32 samples are an explicit 256-bit authoring nonce budget.
        // The existing UUID derivation provides the stable UUID byte format.
        std::random_device entropy;
        core::pmr::String seed;
        for (unsigned i = 0; i < 8; ++i) {
            core::appendUnsigned(seed, entropy());
            seed.push_back('/');
        }
        auto candidate = resource::deriveResourceUuid(seed, domain);
        if (!resource::isResourceUuid(candidate)) {
            diagnostic = "EDITOR-IDENTITY-FORMAT";
            return false;
        }
        identity = std::move(candidate);
        return true;
    } catch (...) {
        diagnostic = "EDITOR-IDENTITY-CREATE-FAILED";
        return false;
    }
}
} // namespace arp::editor

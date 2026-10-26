#pragma once

#include "core/hash/sha256.h"
#include "core/text/number_format.h"

#include <string_view>

namespace arp::editor::detail {

// Shared recovery observation framing, not a persistence format. The caller
// defines field order/schema; length prefixes keep arbitrary bytes unambiguous.
class RecoveryDigest final {
public:
    explicit RecoveryDigest(std::size_t bytes) : remaining_(bytes) {}

    void field(std::string_view bytes)
    {
        if (!valid_ || bytes.size() > remaining_) {
            valid_ = false;
            return;
        }
        remaining_ -= bytes.size();
        hash_.update(core::formatUnsigned(bytes.size()));
        hash_.update(":");
        hash_.update(bytes);
    }

    void number(std::uint64_t value) { field(core::formatUnsigned(value)); }

    bool valid() const noexcept { return valid_; }

    core::ByteBuffer finish() { return hash_.finalize(); }

private:
    core::Sha256 hash_;
    std::size_t remaining_;
    bool valid_ = true;
};

} // namespace arp::editor::detail

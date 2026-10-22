#pragma once
#include "core/encoding/yaml_contract.h"
#include "core/io/atomic_file_service.h"

namespace arp::core::io {
// Schema supplies type(), version(), validate(Node, diagnostic). Validation of
// both replacement and existing data executes on the file worker. A corrupt or
// unsupported document cannot be overwritten through this interface.
template <class Schema> class VersionedYamlStore final {
public:
    explicit VersionedYamlStore(AtomicFileService& files) : files_(files) {}

    PoolHandle load(pmr::String path) { return files_.read(std::move(path), validate); }

    PoolHandle save(pmr::String path, const pmr::String& yaml)
    {
        pmr::Vector<std::uint8_t> bytes(yaml.begin(), yaml.end());
        return files_.replace(std::move(path), std::move(bytes), validate);
    }

    bool take(PoolHandle handle, AtomicFileResult& result) { return files_.take(handle, result); }

private:
    static bool validate(const pmr::Vector<std::uint8_t>& bytes, pmr::String& diagnostic)
    {
        try {
            auto root = YamlContract::Node::deserialize(
                pmr::String(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
            if (YamlContract::text(root, "type") != Schema::type()
                || YamlContract::uint32(root, "contractVersion") != Schema::version()) {
                diagnostic = "ARP-SAVE-UNSUPPORTED-CONTRACT";
                return false;
            }
            return Schema::validate(root, diagnostic);
        } catch (...) {
            diagnostic = "ARP-SAVE-CORRUPT-YAML";
            return false;
        }
    }

    AtomicFileService& files_;
};
} // namespace arp::core::io

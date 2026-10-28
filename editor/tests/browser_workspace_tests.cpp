#include "core/diagnostics/diagnostic_error.h"
#include "core/encoding/yaml_contract.h"
#include "core/io/file_io.h"
#include "editor/web/browser_workspace.h"

#include <cassert>
#include <cstdio>

using namespace arp;

int main()
{
    using Y = core::YamlContract;
    auto file = core::readBinaryFile(
        std::filesystem::path(ARP_SOURCE_DIR) / "editor/web/project/project.arp.yaml");
    assert(file.success);
    auto document = Y::Node::mapping();
    document["type"] = "arp.editor.workspace";
    document["contractVersion"] = 1;
    document["scene"] = "";
    auto entry = Y::Node::mapping();
    entry["path"] = "project.arp.yaml";
    const auto base64 = core::Base64::encode(file.bytes);
    entry["base64"] = Y::Node::string_type(base64.begin(), base64.end());
    document["files"] = Y::Node::sequence();
    document["files"].get_value_ref<Y::Node::sequence_type&>().push_back(entry);
    auto source = Y::serialize(document);
    auto validate = [&](const Y::Node& node, bool accepted) {
        bool success = false;
        try {
            auto archive = editor::BrowserWorkspace::decode(Y::serialize(node));
            success = archive.files.size() == 1;
        } catch (const core::DiagnosticError&) {
        }
        assert(success == accepted);
    };
    validate(document, true);
    auto invalid = document;
    invalid["contractVersion"] = 2;
    validate(invalid, false);
    invalid = document;
    invalid["files"][0]["path"] = "../project.arp.yaml";
    validate(invalid, false);
    invalid = document;
    invalid["files"].get_value_ref<Y::Node::sequence_type&>().push_back(entry);
    validate(invalid, false);
    invalid = document;
    invalid["files"][0]["base64"] = "invalid!";
    validate(invalid, false);
    invalid = document;
    invalid["scene"] = "absent.yaml";
    validate(invalid, false);
    {
        auto warm = editor::BrowserWorkspace::decode(source);
    }
    const auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i) {
        {
            auto archive = editor::BrowserWorkspace::decode(source);
            assert(archive.files[0].bytes == file.bytes);
        }
        const auto current = core::defaultCpuAllocator().stats();
        assert(current.liveBytes == baseline.liveBytes);
        assert(current.allocationCount - current.deallocationCount
            == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts("PASS: strict workspace YAML/path/version and 20 ordinary core lifecycles");
}

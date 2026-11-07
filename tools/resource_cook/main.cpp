#include "core/io/atomic_file_service.h"
#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "reflection/binary_document.h"
#include "resource/cooked_resource.h"

#include <iostream>

int main(int argc, char** argv)
{
    if (argc != 4) {
        std::cerr
            << "usage: arp_resource_cook manifest.yaml source-root output-directory\n       arp_resource_cook --package package-directory output-directory\n       arp_resource_cook --document input.yaml output.arpbin\n";
        return 2;
    }
    try {
        if (std::string_view(argv[1]) == "--document") {
            auto input = arp::core::readTextFile(argv[2], 64u * 1024u * 1024u);
            if (!input.success)
                throw std::runtime_error(input.diagnostics.c_str());
            arp::reflection::SerializationOptions o;
            o.maxCollectionLength = 1024u * 1024u;
            o.maxValues = 4u * 1024u * 1024u;
            auto document = arp::reflection::serialization_value_from_yaml(
                arp::core::YamlContract::parseSingleDocument(input.text), o);
            auto bytes = arp::reflection::encode_serialization_value(document, o);
            if (!bytes.success)
                throw std::runtime_error(bytes.diagnostics.c_str());
            auto check = arp::reflection::decode_serialization_value(
                {bytes.bytes.data(), bytes.bytes.size()}, o);
            if (!check.success)
                throw std::runtime_error(check.diagnostics.c_str());
            auto saved = arp::core::io::replaceFileAtomically(
                arp::core::FilePath::utf8(argv[3]), bytes.bytes);
            if (!saved.success)
                throw std::runtime_error(saved.diagnostic.c_str());
            return 0;
        }
        auto codecs = arp::resource::makePrefabCookedResourceCodecs();
        auto result = std::string_view(argv[1]) == "--package"
            ? arp::resource::ResourceCookService().cookPackage(argv[2], argv[3], codecs)
            : arp::resource::ResourceCookService().cookManifest(argv[1], argv[2], argv[3], codecs);
        if (!result.success) {
            std::cerr << result.diagnostics << '\n';
            return 1;
        }
        std::cout << "cooked resources published\n";
        return 0;
    } catch (const arp::core::YamlContractError& e) {
        std::cerr << e.diagnostic << '\n';
        return 1;
    } catch (const arp::reflection::BinaryDocumentError& e) {
        std::cerr << e.diagnostic << '\n';
        return 1;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

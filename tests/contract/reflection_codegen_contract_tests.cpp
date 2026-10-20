#include "core/cpu_allocator.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

arp::core::pmr::String readText(const std::filesystem::path& path)
{
    std::ifstream input(path);
    assert(input.good());
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return arp::core::toPmrString(buffer.str());
}

void assertContains(const arp::core::pmr::String& haystack, const char* needle)
{
    assert(haystack.find(needle) != arp::core::pmr::String::npos);
}

void assertNotContains(const arp::core::pmr::String& haystack, const char* needle)
{
    assert(haystack.find(needle) == arp::core::pmr::String::npos);
}

} // namespace

int main()
{
    const auto metaPath = std::filesystem::path(ARP_REFLECTION_CODEGEN_META);
    assert(std::filesystem::exists(metaPath));

    const auto json = readText(metaPath);
    assertContains(json, "\"module\": \"reflection_codegen_fixture\"");
    assertContains(json, "\"cppName\": \"arp::tests::reflection_codegen::CodegenTransform\"");
    assertContains(json, "\"reflectName\": \"CodegenTransform\"");
    assertContains(json, "\"name\": \"x\"");
    assertContains(json, "\"cppType\": \"float\"");
    assertContains(json, "\"name\": \"translate\"");
    assertContains(json, "\"reflectName\": \"add\"");
    assertContains(json, "\"cppName\": \"arp::tests::reflection_codegen::DefaultNamedData\"");
    assertContains(json, "\"reflectName\": \"DefaultNamedData\"");
    assertNotContains(json, "\"reflectName\": \"Friendly Default Name\"");
    assertContains(json, "\"cppName\": \"arp::tests::reflection_codegen::CodegenAxis\"");
    assertContains(json, "\"values\": [\"X\", \"Y\", \"Z\"]");
    return 0;
}

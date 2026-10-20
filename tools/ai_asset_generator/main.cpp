#include "tools/ai_asset_generator/ai_asset_generator.h"

#include <iostream>

int main(int argc, char** argv)
{
    const auto options = arp::tools::ai_asset_generator::parseOptions(argc, argv);
    const auto result = arp::tools::ai_asset_generator::runGenerator(
        options, arp::tools::ai_asset_generator::Environment::fromProcess());
    if (!result.diagnostics.empty()) {
        (result.success ? std::cout : std::cerr) << result.diagnostics << '\n';
    }
    return result.success ? 0 : 1;
}

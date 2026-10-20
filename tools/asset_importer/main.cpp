#include "tools/asset_importer/asset_importer.h"

#include <iostream>

int main(int argc, char** argv)
{
    return arp::tools::asset_importer::runImporter(argc, argv, std::cout, std::cerr);
}

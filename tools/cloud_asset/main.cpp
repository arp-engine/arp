#include "tools/cloud_asset/cloud_cook_application.h"

int main(int argc, char** argv)
{
    return arp::tools::cloud_asset::CloudCookApplication{}.run(argc, argv);
}

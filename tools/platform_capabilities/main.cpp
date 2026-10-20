#include <iostream>

int main()
{
    std::cout << "Platform Capability Report\n";
    std::cout << "Windows: Vulkan desktop validation target\n";
    std::cout << "Android: Vulkan mobile production target\n";
    std::cout << "macOS: Metal desktop validation target\n";
    std::cout << "iOS: Metal mobile production target\n";
    std::cout << "Linux: Vulkan portability target\n";
    std::cout << "Vulkan: configured through VULKAN_SDK when available\n";
    std::cout << "Metal: contract-only until Apple runtime validation is available\n";
    return 0;
}

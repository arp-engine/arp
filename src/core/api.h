#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
#if defined(ARP_BUILDING_DLL)
#define ARP_API __declspec(dllexport)
#elif defined(ARP_STATIC_LINK)
#define ARP_API
#else
#define ARP_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define ARP_API __attribute__((visibility("default")))
#else
#define ARP_API
#endif

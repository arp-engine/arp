#pragma once

#include <string_view>

namespace arp::reflection::detail {

// NativeTypeSignature/1: extract the compiler's declared T binding, never a
// byte offset derived from another function name or another compiler's form.
// Reflected public names remain explicitly registered by their owning modules.
template <typename T> constexpr const char* typeSignatureFn() noexcept
{
#if defined(_MSC_VER)
    return __FUNCSIG__;
#elif defined(__GNUC__) || defined(__clang__)
    return __PRETTY_FUNCTION__;
#else
#error "REFLECTION-NATIVE-TYPE-SIGNATURE: unsupported compiler"
#endif
}

template <typename T> constexpr std::string_view getTypeName() noexcept
{
    constexpr std::string_view signature(typeSignatureFn<T>());
#if defined(_MSC_VER)
    constexpr std::string_view prefix = "typeSignatureFn<";
    constexpr std::string_view suffix = ">(void) noexcept";
#elif defined(__clang__)
    constexpr std::string_view prefix = "[T = ";
    constexpr std::string_view suffix = "]";
#elif defined(__GNUC__)
    constexpr std::string_view prefix = "[with T = ";
    constexpr std::string_view suffix = "]";
#endif
    constexpr auto marker = signature.find(prefix);
    constexpr auto end = signature.rfind(suffix);
    static_assert(marker != std::string_view::npos && end != std::string_view::npos,
        "REFLECTION-NATIVE-TYPE-SIGNATURE: unsupported signature form");
    constexpr auto begin = marker + prefix.size();
    static_assert(end > begin && end + suffix.size() == signature.size(),
        "REFLECTION-NATIVE-TYPE-SIGNATURE: invalid type binding");
    return signature.substr(begin, end - begin);
}

} // namespace arp::reflection::detail

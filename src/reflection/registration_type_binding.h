#pragma once

#include "reflection/registration_transaction.h"
#include "reflection/type.h"

#include <array>
#include <optional>

namespace arp::reflection {

namespace detail {

// These closed C++ shapes describe implementation storage, never a reflected
// domain class. In particular arbitrary structs, enums and base classes still
// require their owning module's explicit class/enum declaration.
template <class T, class = void> struct NativeBindingSupport {
    static bool declare(NameView source, RegistrationTransaction& transaction)
    {
        if constexpr (std::is_arithmetic_v<T>)
            return transaction.declareNativeSupportType(
                source, type_of<T>(), RegistrationNativeShape::Scalar);
        return true;
    }
};

template <class T>
struct NativeBindingSupport<T,
    std::void_t<typename T::ReflectionNativeTokenStorage,
        decltype(T::reflectionNativeTokenVersion)>> {
    static bool declare(NameView source, RegistrationTransaction& transaction)
    {
        using Storage = typename T::ReflectionNativeTokenStorage;
        static_assert(T::reflectionNativeTokenVersion == 1u && std::is_integral_v<Storage>
                && std::is_standard_layout_v<T> && std::is_trivially_copyable_v<T>
                && sizeof(T) == sizeof(Storage) && alignof(T) == alignof(Storage),
            "REFLECTION-NATIVE-TOKEN-STORAGE: unsupported declared storage contract");
        return transaction.declareNativeSupportType(
            source, type_of<T>(), RegistrationNativeShape::OpaqueToken);
    }
};

template <class T, std::size_t N> struct NativeBindingSupport<std::array<T, N>> {
    static bool declare(NameView source, RegistrationTransaction& transaction)
    {
        return NativeBindingSupport<T>::declare(source, transaction)
            && transaction.declareNativeSupportType(
                source, type_of<std::array<T, N>>(), RegistrationNativeShape::FixedArray);
    }
};

template <class T> struct NativeBindingSupport<core::pmr::Vector<T>> {
    static bool declare(NameView source, RegistrationTransaction& transaction)
    {
        return NativeBindingSupport<T>::declare(source, transaction)
            && transaction.declareNativeSupportType(
                source, type_of<core::pmr::Vector<T>>(), RegistrationNativeShape::Sequence);
    }
};

template <class K, class V, class C> struct NativeBindingSupport<core::pmr::Map<K, V, C>> {
    static bool declare(NameView source, RegistrationTransaction& transaction)
    {
        return NativeBindingSupport<K>::declare(source, transaction)
            && NativeBindingSupport<V>::declare(source, transaction)
            && transaction.declareNativeSupportType(
                source, type_of<core::pmr::Map<K, V, C>>(), RegistrationNativeShape::Mapping);
    }
};

template <class T> struct NativeBindingSupport<std::optional<T>> {
    static bool declare(NameView source, RegistrationTransaction& transaction)
    {
        return NativeBindingSupport<T>::declare(source, transaction)
            && transaction.declareNativeSupportType(
                source, type_of<std::optional<T>>(), RegistrationNativeShape::Optional);
    }
};

} // namespace detail

// Only generated metadata spellings are accepted by the importer. Raw strips
// outer cv/pointer/reference qualifiers; their existing contract remains at
// the use site. Alias resolution is performed by the C++ compiler itself.
template <class T> bool bindRegistrationNativeType(NameView sourceType)
{
    auto* transaction = detail::currentRegistrationTransaction();
    if (transaction == nullptr)
        return true;
    using Raw = detail::raw_type_t<T>;
    return transaction->bindNativeType(sourceType, type_of<Raw>())
        && detail::NativeBindingSupport<Raw>::declare(sourceType, *transaction);
}

} // namespace arp::reflection

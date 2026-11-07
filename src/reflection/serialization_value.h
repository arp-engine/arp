#pragma once

#include "core/memory/cpu_allocator.h"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace arp::reflection {
// Archive-owned, format-neutral values. No YAML nodes or domain objects are retained.
class SerializationValue {
public:
    enum class Kind : std::uint8_t {
        Null,
        Boolean,
        Integer,
        Unsigned,
        Real,
        String,
        Sequence,
        Mapping
    };
    using integer_type = std::int64_t;
    using float_number_type = double;
    using string_type = core::pmr::String;
    using sequence_type = core::pmr::Vector<SerializationValue>;
    using mapping_type = core::pmr::Map<SerializationValue, SerializationValue>;

    SerializationValue() = default;

    SerializationValue(bool v) : kind_(Kind::Boolean), unsigned_(v) {}

    template <class T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
    SerializationValue(T v)
        : kind_(std::is_unsigned_v<T> ? Kind::Unsigned : Kind::Integer), width_(sizeof(T)),
          signed_(static_cast<std::int64_t>(v)), unsigned_(static_cast<std::uint64_t>(v))
    {}

    SerializationValue(double v) : kind_(Kind::Real), real_(v) {}

    SerializationValue(float v) : kind_(Kind::Real), width_(4), real_(v) {}

    SerializationValue(std::string_view v)
        : kind_(Kind::String), string_(v, &core::defaultCpuMemoryResource())
    {}

    SerializationValue(const char* v) : SerializationValue(std::string_view(v)) {}

    template <class A>
    SerializationValue(const std::basic_string<char, std::char_traits<char>, A>& v)
        : SerializationValue(std::string_view(v.data(), v.size()))
    {}

    static SerializationValue mapping()
    {
        SerializationValue v;
        v.kind_ = Kind::Mapping;
        return v;
    }

    static SerializationValue sequence()
    {
        SerializationValue v;
        v.kind_ = Kind::Sequence;
        return v;
    }

    Kind kind() const { return kind_; }

    std::uint8_t width() const { return width_; }

    bool is_null() const { return kind_ == Kind::Null; }

    bool is_boolean() const { return kind_ == Kind::Boolean; }

    bool is_integer() const { return kind_ == Kind::Integer; }

    bool is_unsigned() const { return kind_ == Kind::Unsigned; }

    bool is_float_number() const { return kind_ == Kind::Real; }

    bool is_string() const { return kind_ == Kind::String; }

    bool is_sequence() const { return kind_ == Kind::Sequence; }

    bool is_mapping() const { return kind_ == Kind::Mapping; }

    bool is_scalar() const { return kind_ >= Kind::Boolean && kind_ <= Kind::String; }

    std::size_t size() const
    {
        return is_mapping() ? mapping_.size()
            : is_sequence() ? sequence_.size()
            : is_string()   ? string_.size()
                            : 0;
    }

    bool empty() const { return size() == 0; }

    auto begin() { return sequence_.begin(); }

    auto end() { return sequence_.end(); }

    auto begin() const { return sequence_.begin(); }

    auto end() const { return sequence_.end(); }

    bool contains(std::string_view key) const
    {
        return mapping_.find(SerializationValue(key)) != mapping_.end();
    }

    SerializationValue& operator[](const char* key) { return (*this)[std::string_view(key)]; }

    const SerializationValue& operator[](const char* key) const
    {
        return (*this)[std::string_view(key)];
    }

    template <class A>
    SerializationValue& operator[](const std::basic_string<char, std::char_traits<char>, A>& key)
    {
        return (*this)[std::string_view(key)];
    }

    template <class A>
    const SerializationValue& operator[](
        const std::basic_string<char, std::char_traits<char>, A>& key) const
    {
        return (*this)[std::string_view(key)];
    }

    SerializationValue& operator[](std::string_view key)
    {
        if (!is_mapping())
            throw std::runtime_error("expected mapping");
        return mapping_[SerializationValue(key)];
    }

    const SerializationValue& operator[](std::string_view key) const
    {
        return mapping_.at(SerializationValue(key));
    }

    SerializationValue& operator[](const SerializationValue& key) { return mapping_[key]; }

    const SerializationValue& operator[](const SerializationValue& key) const
    {
        return mapping_.at(key);
    }

    template <class I, std::enable_if_t<std::is_integral_v<I>, int> = 0>
    SerializationValue& operator[](I i)
    {
        return sequence_.at(i);
    }

    template <class I, std::enable_if_t<std::is_integral_v<I>, int> = 0>
    const SerializationValue& operator[](I i) const
    {
        return sequence_.at(i);
    }

    template <class T> T get_value() const
    {
        if constexpr (std::is_same_v<T, bool>) {
            if (!is_boolean())
                throw std::runtime_error("expected boolean archive value");
            return unsigned_ != 0;
        } else if constexpr (std::is_same_v<T, string_type>) {
            if (!is_string())
                throw std::runtime_error("expected string archive value");
            return string_;
        } else if constexpr (std::is_floating_point_v<T>) {
            if (!is_integer() && !is_unsigned() && !is_float_number())
                throw std::runtime_error("expected numeric archive value");
            return static_cast<T>(is_integer() ? double(signed_)
                    : is_unsigned()            ? double(unsigned_)
                                               : real_);
        } else if constexpr (std::is_unsigned_v<T>) {
            if ((!is_integer() && !is_unsigned()) || (is_integer() && signed_ < 0)
                || (is_unsigned() ? unsigned_ : static_cast<std::uint64_t>(signed_))
                    > std::numeric_limits<T>::max())
                throw std::runtime_error("unsigned archive value out of range");
            return static_cast<T>(is_integer() ? static_cast<std::uint64_t>(signed_) : unsigned_);
        } else {
            if ((!is_integer() && !is_unsigned())
                || (is_integer()
                    && (signed_ < std::numeric_limits<T>::min()
                        || signed_ > std::numeric_limits<T>::max()))
                || (is_unsigned()
                    && unsigned_ > static_cast<std::uint64_t>(std::numeric_limits<T>::max())))
                throw std::runtime_error("signed archive value out of range");
            return static_cast<T>(is_unsigned() ? static_cast<std::int64_t>(unsigned_) : signed_);
        }
    }

    template <class T> T get_value_ref()
    {
        using V = std::remove_cv_t<std::remove_reference_t<T>>;
        if constexpr (std::is_same_v<V, string_type>)
            return string_;
        else if constexpr (std::is_same_v<V, sequence_type>)
            return sequence_;
        else
            return mapping_;
    }

    template <class T> T get_value_ref() const
    {
        using V = std::remove_cv_t<std::remove_reference_t<T>>;
        if constexpr (std::is_same_v<V, string_type>)
            return string_;
        else if constexpr (std::is_same_v<V, sequence_type>)
            return sequence_;
        else
            return mapping_;
    }

    bool operator<(const SerializationValue& rhs) const
    {
        if (kind_ != rhs.kind_)
            return kind_ < rhs.kind_;
        switch (kind_) {
        case Kind::Null:
            return false;
        case Kind::Integer:
            return signed_ < rhs.signed_;
        case Kind::Unsigned:
        case Kind::Boolean:
            return unsigned_ < rhs.unsigned_;
        case Kind::Real:
            return real_ < rhs.real_;
        case Kind::String:
            return string_ < rhs.string_;
        default:
            throw std::runtime_error("archive map key must be scalar");
        }
    }

private:
    Kind kind_ = Kind::Null;
    std::uint8_t width_ = 8;
    std::int64_t signed_ = 0;
    std::uint64_t unsigned_ = 0;
    double real_ = 0;
    string_type string_{&core::defaultCpuMemoryResource()};
    sequence_type sequence_{sequence_type::allocator_type(&core::defaultCpuMemoryResource())};
    mapping_type mapping_{&core::defaultCpuMemoryResource()};
};
} // namespace arp::reflection

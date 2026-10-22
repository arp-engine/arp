// builtin.cpp -- mirrors ae::hybrid/builtin.h, adapted for arp::reflection.
// Registers hdict, hlist, String, int, int64, bool, double with magic methods.

#include "reflection/builtin.h"

#include "core/text/string_utils.h"
#include "reflection/function.h"
#include "reflection/instance.h"
#include "reflection/native_reference.h"
#include "reflection/object.h"
#include "reflection/property.h"
#include "reflection/registry.h"

#include <cmath>
#include <cstdint>
#include <string>

namespace arp::reflection {

void registerBuiltinReflectionTypes()
{
    // These are internal variant payloads shared by the host and every
    // dynamically loaded reflection module. Register their type identities
    // before a plugin transaction can construct or inspect one, so the
    // transaction only references process-owned identities.
    (void)type_of<function>();
    (void)type_of<property>();
    (void)type_of<enumeration>();
    (void)type_of<instance>();
    (void)type_of<class_info>();
    (void)type_of<std::nullptr_t>();
    (void)type_of<ByteBuffer>();
    (void)type_of<detail::NativeObjectReference>();

    registration::class_<hdict>("dict")
        .method("__new__",
            []() {
                return hdict{&core::defaultCpuMemoryResource()};
            })
        .method("__getitem__",
            [](hdict& self, const core::pmr::String& name) {
                auto found = self.find(object(name));
                return found == self.end() ? object::undefined() : found->second;
            })
        .method("__setitem__", [](hdict& self, const core::pmr::String& name, const object& value) {
            self[object(name)] = value;
        });

    registration::class_<hlist>("list")
        .method("__new__",
            []() {
                return hlist(hlist::allocator_type(&core::defaultCpuMemoryResource()));
            })
        .method("length",
            [](hlist& self) {
                return static_cast<int>(self.size());
            })
        .method("append",
            [](hlist& self, const object& value) {
                self.push_back(value);
            })
        .method("pop", [](hlist& self) -> object {
            if (self.empty()) {
                return object::undefined();
            }
            object value = self.back();
            self.pop_back();
            return value;
        });

    registration::class_<core::pmr::String>("str")
        .method("__add__",
            [](const core::pmr::String& self, const core::pmr::String& rhs) {
                core::pmr::String result{self, &core::defaultCpuMemoryResource()};
                result += rhs;
                return result;
            })
        .method("__str__",
            [](const core::pmr::String& self) {
                return self;
            })
        .method("__int__",
            [](const core::pmr::String& self) {
                return std::stoi(core::toStdString(self));
            })
        .method("__int64__",
            [](const core::pmr::String& self) {
                return static_cast<std::int64_t>(std::stoll(core::toStdString(self)));
            })
        .method("__float__",
            [](const core::pmr::String& self) {
                return std::stod(core::toStdString(self));
            })
        .method("__bool__",
            [](const core::pmr::String& self) {
                return !self.empty();
            })
        .method("__eq__",
            [](const core::pmr::String& self, const core::pmr::String& rhs) {
                return self == rhs;
            })
        .method("__lt__",
            [](const core::pmr::String& self, const core::pmr::String& rhs) {
                return self < rhs;
            })
        .method("upper",
            [](const core::pmr::String& self) {
                return core::toUpperPmr(self, &core::defaultCpuMemoryResource());
            })
        .method("lower",
            [](const core::pmr::String& self) {
                return core::toLowerPmr(self, &core::defaultCpuMemoryResource());
            })
        .method("strip",
            [](const core::pmr::String& self) {
                return core::stripPmr(self, &core::defaultCpuMemoryResource());
            })
        .method("find",
            [](const core::pmr::String& self, const core::pmr::String& needle) {
                return core::indexOf(self, needle);
            })
        .method("contains",
            [](const core::pmr::String& self, const core::pmr::String& needle) {
                return core::contains(self, needle);
            })
        .method("startswith",
            [](const core::pmr::String& self, const core::pmr::String& prefix) {
                return core::startsWith(self, prefix);
            })
        .method("endswith",
            [](const core::pmr::String& self, const core::pmr::String& suffix) {
                return core::endsWith(self, suffix);
            })
        .method("replace",
            [](const core::pmr::String& self,
                const core::pmr::String& from,
                const core::pmr::String& to) {
                return core::replaceAllPmr(self, from, to, &core::defaultCpuMemoryResource());
            })
        .method("split",
            [](const core::pmr::String& self, const core::pmr::String& sep) {
                auto parts = core::splitPmr(self, sep, &core::defaultCpuMemoryResource());
                auto list = hlist(hlist::allocator_type(&core::defaultCpuMemoryResource()));
                list.reserve(parts.size());
                for (auto& p : parts) {
                    list.emplace_back(std::move(p));
                }
                return list;
            })
        .method("join",
            [](const core::pmr::String& self, const hlist& parts) {
                std::pmr::vector<std::pmr::string> strings{&core::defaultCpuMemoryResource()};
                strings.reserve(parts.size());
                for (const auto& item : parts) {
                    strings.push_back(item.cast<const core::pmr::String&>());
                }
                return core::joinPmr(strings, self, &core::defaultCpuMemoryResource());
            })
        .method("format", [](const core::pmr::String& self, const hlist& args) {
            core::pmr::String result{&core::defaultCpuMemoryResource()};
            for (std::size_t i = 0; i < self.size(); ++i) {
                if (i + 1 < self.size() && self[i] == '{' && self[i + 1] == '}') {
                    result.append("{}");
                    i += 1;
                    continue;
                }
                if (self[i] == '{') {
                    std::size_t j = i + 1;
                    core::pmr::String idxStr{&core::defaultCpuMemoryResource()};
                    while (j < self.size() && self[j] != '}') {
                        idxStr.push_back(self[j]);
                        ++j;
                    }
                    if (j < self.size() && self[j] == '}' && !idxStr.empty()) {
                        auto idx = std::stoul(core::toStdString(idxStr));
                        if (idx < args.size()) {
                            auto strVal = args[idx].cast<const core::pmr::String&>();
                            result.append(strVal);
                        }
                        i = j;
                        continue;
                    }
                }
                result.push_back(self[i]);
            }
            return result;
        });

    auto registerInteger = []<typename T>(NameView name) {
        registration::class_<T>(name)
            .method("__neg__",
                [](T self) {
                    return -self;
                })
            .method("__add__",
                [](T self, T rhs) {
                    return self + rhs;
                })
            .method("__sub__",
                [](T self, T rhs) {
                    return self - rhs;
                })
            .method("__mul__",
                [](T self, T rhs) {
                    return self * rhs;
                })
            .method("__div__",
                [](T self, T rhs) -> object {
                    return rhs == 0 ? object::undefined() : object(self / rhs);
                })
            .method("__mod__",
                [](T self, T rhs) -> object {
                    return rhs == 0 ? object::undefined() : object(self % rhs);
                })
            .method("__and__",
                [](T self, T rhs) {
                    return self & rhs;
                })
            .method("__or__",
                [](T self, T rhs) {
                    return self | rhs;
                })
            .method("__xor__",
                [](T self, T rhs) {
                    return self ^ rhs;
                })
            .method("__andnot__",
                [](T self, T rhs) {
                    return self & ~rhs;
                })
            .method("__shl__",
                [](T self, T rhs) {
                    return self << rhs;
                })
            .method("__shr__",
                [](T self, T rhs) {
                    return self >> rhs;
                })
            .method("__eq__",
                [](T self, T rhs) {
                    return self == rhs;
                })
            .method("__lt__",
                [](T self, T rhs) {
                    return self < rhs;
                })
            .method("__float__",
                [](T self) {
                    return static_cast<double>(self);
                })
            .method("__int__",
                [](T self) {
                    return static_cast<int>(self);
                })
            .method("__int64__",
                [](T self) {
                    return static_cast<std::int64_t>(self);
                })
            .method("__bool__",
                [](T self) {
                    return self != 0;
                })
            .method("__str__", [](T self) {
                return core::toPmrString(std::to_string(self));
            });
    };

    registerInteger.operator()<int>("int");
    registerInteger.operator()<std::int64_t>("int64");

    registration::class_<bool>("bool")
        .method("__float__",
            [](bool self) {
                return static_cast<double>(self);
            })
        .method("__int__",
            [](bool self) {
                return self ? 1 : 0;
            })
        .method("__int64__",
            [](bool self) {
                return self ? std::int64_t{1} : std::int64_t{0};
            })
        .method("__str__",
            [](bool self) {
                return core::toPmrString(self ? "true" : "false");
            })
        .method("__eq__",
            [](bool self, bool rhs) {
                return self == rhs;
            })
        .method("__lt__", [](bool self, bool rhs) {
            return self < rhs;
        });

    registration::class_<double>("float")
        .method("__neg__",
            [](double self) {
                return -self;
            })
        .method("__add__",
            [](double self, double rhs) {
                return self + rhs;
            })
        .method("__sub__",
            [](double self, double rhs) {
                return self - rhs;
            })
        .method("__mul__",
            [](double self, double rhs) {
                return self * rhs;
            })
        .method("__div__",
            [](double self, double rhs) -> object {
                return rhs == 0.0 ? object::undefined() : object(self / rhs);
            })
        .method("__mod__",
            [](double self, double rhs) -> object {
                return rhs == 0.0 ? object::undefined() : object(std::fmod(self, rhs));
            })
        .method("__eq__",
            [](double self, double rhs) {
                return self == rhs;
            })
        .method("__lt__",
            [](double self, double rhs) {
                return self < rhs;
            })
        .method("__int__",
            [](double self) {
                return static_cast<int>(self);
            })
        .method("__int64__",
            [](double self) {
                return static_cast<std::int64_t>(self);
            })
        .method("__bool__",
            [](double self) {
                return self != 0.0;
            })
        .method("__str__", [](double self) {
            return core::toPmrString(std::to_string(self));
        });

    // Explicit stock-method contract: same-storage scalar comparison is
    // equivalent to these registered operators. Mixed/custom values retain
    // reflected conversion, and class_info::def invalidates method overrides.
    for (auto* cls : {class_of_raw<int>(),
             class_of_raw<std::int64_t>(),
             class_of_raw<bool>(),
             class_of_raw<double>(),
             class_of_raw<core::pmr::String>()}) {
        cls->builtinEquality_ = true;
        cls->builtinOrdering_ = true;
    }
}

} // namespace arp::reflection

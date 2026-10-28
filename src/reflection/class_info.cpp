// class_info.cpp -- mirrors ae::hybrid/src/class.cpp, adapted for arp::reflection.
//
// Key: call() reverts to original logic: get_attr -> is_function -> is_static
// -> insert inst -> call_v. No separate copyArgs.

#include "reflection/class_info.h"

#include "reflection/instance.h"
#include "reflection/property.h"

#include <stdexcept>
#include <utility>

namespace arp::reflection {

class_info::class_info(NameView name)
    : nativeName_(detail::makeString(name)), name_(detail::makeString(name))
{}

class_info::class_info(NameView name, value_t type, std::size_t size, std::size_t alignment)
    : class_info(name)
{
    type_ = type;
    size_ = size;
    alignment_ = alignment;
}

class_info::class_info(NameView name,
    value_t type,
    std::size_t size,
    std::size_t alignment,
    get_derived_info_func getDerivedInfo)
    : class_info(name, type, size, alignment)
{
    getDerivedInfo_ = getDerivedInfo;
}

bool class_info::isUncompletedNativeReference() const noexcept
{
    return !explicitlyDeclared_ && name_ == nativeName_ && dict_.empty() && bases_.empty()
        && castFuncs_.empty() && new_.is_undefined() && init_.is_undefined() && del_.is_undefined()
        && getitem_.is_undefined() && setitem_.is_undefined();
}

void class_info::completeNativeReference(class_info& staged) noexcept
{
    generatedContract_.swap(staged.generatedContract_);
    generatedSource_.swap(staged.generatedSource_);
    generatedStructure_.swap(staged.generatedStructure_);
    name_.swap(staged.name_);
    doc_.swap(staged.doc_);
    bases_.swap(staged.bases_);
    castFuncs_.swap(staged.castFuncs_);
    dict_.swap(staged.dict_);
    std::swap(new_, staged.new_);
    std::swap(init_, staged.init_);
    std::swap(del_, staged.del_);
    std::swap(getitem_, staged.getitem_);
    std::swap(setitem_, staged.setitem_);
    explicitlyDeclared_ = true;
}

bool class_info::is_base_of(const class_info* other) const
{
    if (other == nullptr) {
        return false;
    }
    for (auto* base : other->bases_) {
        if (base == this || is_base_of(base)) {
            return true;
        }
    }
    return false;
}

bool class_info::is_derived_from(const class_info* other) const
{
    return other != nullptr && other->is_base_of(this);
}

bool class_info::is_type(value_t reflectedType) const
{
    return type_ == reflectedType;
}

void* class_info::up_cast(const class_info* dst, void* ptr) const
{
    if (dst == nullptr || ptr == nullptr || !is_derived_from(dst)) {
        return nullptr;
    }
    for (std::size_t index = 0; index < bases_.size(); ++index) {
        if (bases_[index] == dst) {
            return castFuncs_[index](ptr);
        }
        if (bases_[index]->is_derived_from(dst)) {
            return bases_[index]->up_cast(dst, castFuncs_[index](ptr));
        }
    }
    return nullptr;
}

void* class_info::dynamic_cast_(const class_info* dst, void* ptr) const
{
    if (ptr == nullptr || dst == nullptr || dst == this) {
        return ptr;
    }
    if (getDerivedInfo_ != nullptr) {
        derived_info info = getDerivedInfo_(ptr);
        if (info.ptr != nullptr && info.cls == dst) {
            return info.ptr;
        }
        if (info.ptr != nullptr && info.cls != nullptr && info.cls->is_derived_from(dst)) {
            return info.cls->up_cast(dst, info.ptr);
        }
    }
    return up_cast(dst, ptr);
}

class_info& class_info::def(NameView name, const object& value)
{
    invalidateBuiltinComparisons(name);
    // Cached dispatch must follow the authoritative definition table. Keeping
    // the first binding here retains obsolete callable state after rebinds
    // and makes cached dispatch call a different function than get_field().
    if (name == "__new__") {
        new_ = value;
    } else if (name == "__init__") {
        init_ = value;
    } else if (name == "__del__") {
        del_ = value;
    } else if (name == "__getitem__") {
        getitem_ = value;
    } else if (name == "__setitem__") {
        setitem_ = value;
    }
    dict_[detail::makeString(name)] = value;
    return *this;
}

class_info& class_info::def_property(NameView name,
    const typeinfo* type,
    const object& getter,
    const object& setter,
    const object& pointerSetter,
    const object& indirectGetter,
    const object& indirectSetter,
    const class_info* indirectClass,
    bool indirectAllowDerived,
    bool indirectResultOwned,
    NameView doc,
    std::uint8_t hints)
{
    invalidateBuiltinComparisons(name);
    dict_[detail::makeString(name)] = object(property(name,
        type,
        doc,
        getter,
        setter,
        pointerSetter,
        indirectGetter,
        indirectSetter,
        indirectClass,
        indirectAllowDerived,
        indirectResultOwned,
        hints));
    return *this;
}

class_info& class_info::def_property(NameView name,
    const typeinfo* type,
    const object& getter,
    const object& setter,
    NameView doc,
    std::uint8_t hints)
{
    return def_property(name,
        type,
        getter,
        setter,
        object::undefined(),
        object::undefined(),
        object::undefined(),
        nullptr,
        false,
        false,
        doc,
        hints);
}

class_info& class_info::inherit(class_info* cls, cast_func func)
{
    if (cls == nullptr || func == nullptr || cls == this)
        throw std::invalid_argument("ARP-REFLECTION-BASE-CONTRACT");
    // Per-world plugin registration reuses process-owned class metadata.
    // Repeating the exact edge must not append another base or cast entry.
    for (std::size_t index = 0; index < bases_.size(); ++index) {
        if (bases_[index] != cls)
            continue;
        if (castFuncs_[index] != func)
            throw std::invalid_argument("ARP-REFLECTION-BASE-CAST-CONFLICT");
        return *this;
    }
    builtinEquality_ = false;
    builtinOrdering_ = false;
    bases_.push_back(cls);
    castFuncs_.push_back(func);
    return *this;
}

void class_info::invalidateBuiltinComparisons(NameView name)
{
    if (name == "__eq__" || name == "__getitem__")
        builtinEquality_ = false;
    if (name == "__lt__" || name == "__getitem__")
        builtinOrdering_ = false;
}

object class_info::get_field(NameView name) const
{
    auto found = dict_.find(detail::makeString(name));
    if (found != dict_.end() && !found->second.is_undefined()) {
        return found->second;
    }
    for (auto* base : bases_) {
        object value = base->get_field(name);
        if (!value.is_undefined()) {
            return value;
        }
    }
    return object::undefined();
}

core::pmr::Map<core::pmr::String, object> class_info::get_all_fields() const
{
    core::pmr::Map<core::pmr::String, object> fields{&core::defaultCpuMemoryResource()};
    for (auto* base : bases_) {
        auto baseFields = base->get_all_fields();
        for (auto& entry : baseFields) {
            fields[entry.first] = entry.second;
        }
    }
    for (const auto& entry : dict_) {
        fields[entry.first] = entry.second;
    }
    return fields;
}

core::pmr::Map<core::pmr::String, object> class_info::get_all_properties() const
{
    core::pmr::Map<core::pmr::String, object> fields{&core::defaultCpuMemoryResource()};
    for (auto* base : bases_) {
        auto baseFields = base->get_all_properties();
        for (auto& entry : baseFields) {
            fields[entry.first] = entry.second;
        }
    }
    for (const auto& entry : dict_) {
        if (entry.second.is_property()) {
            fields[entry.first] = entry.second;
        }
    }
    return fields;
}

object class_info::get_attr(const object& inst, NameView name) const
{
    object attr = object::undefined();
    if (getitem_.is_function()) {
        attr = getitem_.cast<function&>().call(inst, detail::makeString(name));
    }
    if (attr.is_undefined()) {
        attr = get_field(name);
    }
    if (attr.is_property()) {
        return attr.cast<property&>().get_value(inst);
    }
    if (!attr.is_undefined()) {
        return attr;
    }
    if (inst.is_object() && !inst.is_native_reference()) {
        return inst.cast<instance&>().__getitem__(name);
    }
    return attr;
}

object class_info::set_attr(const object& inst, NameView name, const object& value) const
{
    object attr = get_field(name);
    if (attr.is_property()) {
        return attr.cast<property&>().set_value(inst, value);
    }
    if (inst.is_object() && !inst.is_native_reference()) {
        return inst.cast<instance&>().__setitem__(name, value);
    }
    if (setitem_.is_function()) {
        return setitem_.cast<function&>().call(inst, detail::makeString(name), value);
    }
    return object::undefined();
}

object class_info::call(const object& inst, NameView func, core::pmr::Vector<object>& args) const
{
    object attr = get_attr(inst, func);
    if (!attr.is_function()) {
        return object::undefined();
    }
    function& reflected = attr.cast<function&>();
    if (reflected.is_static() && reflected.parameter_count() == args.size() + 1) {
        args.insert(args.begin(), inst);
    }
    return reflected.call_v(inst, args);
}

bool class_info::malloc(void*& ptr) const
{
    if (size_ == 0 || alignment_ == 0) {
        ptr = nullptr;
        return false;
    }
    ptr = core::defaultCpuMemoryResource().allocate(size_, alignment_);
    return ptr != nullptr;
}

void class_info::free(void* ptr) const
{
    if (ptr == nullptr || size_ == 0 || alignment_ == 0) {
        return;
    }
    core::defaultCpuMemoryResource().deallocate(ptr, size_, alignment_);
}

void class_info::destruct(void* ptr) const
{
    if (del_.is_function()) {
        del_.cast<function&>().call(ptr);
    }
}

object class_info::operator[](NameView name) const
{
    return get_field(name);
}

const core::pmr::String& class_info::name() const
{
    return name_;
}

void class_info::set_name(NameView name)
{
    name_ = detail::makeString(name);
}

value_t class_info::valueType() const
{
    return type_;
}

std::size_t class_info::size() const
{
    return size_;
}

std::size_t class_info::alignment() const
{
    return alignment_;
}

} // namespace arp::reflection

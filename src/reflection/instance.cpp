#include "reflection/instance.h"

#include <algorithm>
#include <memory_resource>

namespace arp::reflection {

InstanceFieldLayout::InstanceFieldLayout(const core::pmr::Vector<core::pmr::String>& fields)
    : names_(fields, &core::defaultCpuMemoryResource())
{
    std::sort(names_.begin(), names_.end());
    names_.erase(std::unique(names_.begin(), names_.end()), names_.end());
}

std::size_t InstanceFieldLayout::find(NameView name) const noexcept
{
    const auto found = std::lower_bound(names_.begin(), names_.end(), name, NameLess{});
    return found != names_.end() && NameView(*found) == name
        ? static_cast<std::size_t>(found - names_.begin())
        : names_.size();
}

struct instance::FieldStorage {
    std::shared_ptr<const InstanceFieldLayout> layout;
    core::pmr::Vector<object> declared{&core::defaultCpuMemoryResource()};
    std::pmr::monotonic_buffer_resource arena{&core::defaultCpuMemoryResource()};
    FieldMap dynamic{&arena};

    explicit FieldStorage(std::shared_ptr<const InstanceFieldLayout> names)
        : layout(std::move(names))
    {
        if (layout)
            declared.assign(layout->names().size(), object::undefined());
    }

    object* find(NameView key) noexcept
    {
        if (layout) {
            const auto index = layout->find(key);
            if (index < declared.size())
                return &declared[index];
        }
        const auto found = dynamic.find(key);
        return found != dynamic.end() ? &found->second : nullptr;
    }
};

instance::instance(const class_info* cls) : cls_(cls)
{}

instance::instance(const class_info* cls, const core::pmr::Vector<core::pmr::String>& fields)
    : instance(cls, fields.empty() ? nullptr : core::makeShared<InstanceFieldLayout>(fields))
{}

instance::instance(const class_info* cls, std::shared_ptr<const InstanceFieldLayout> layout)
    : cls_(cls)
{
    if (layout && !layout->names().empty())
        storage_ = core::makeUnique<FieldStorage>(std::move(layout));
}

instance::instance(const instance& other) : cls_(other.cls_), alive_(other.alive_)
{
    if (other.storage_) {
        storage_ = core::makeUnique<FieldStorage>(other.storage_->layout);
        storage_->declared = other.storage_->declared;
        for (const auto& entry : other.storage_->dynamic)
            storage_->dynamic.try_emplace(
                core::pmr::String(entry.first, &storage_->arena), entry.second);
    }
}

instance::instance(instance&& other) noexcept = default;
instance& instance::operator=(instance&& other) noexcept = default;
instance::~instance() = default;

instance& instance::operator=(const instance& other)
{
    if (this != &other) {
        instance copy(other);
        *this = std::move(copy);
    }
    return *this;
}

instance::FieldStorage& instance::fields()
{
    if (!storage_)
        storage_ = core::makeUnique<FieldStorage>(nullptr);
    return *storage_;
}

instance::FieldRange instance::slots() const noexcept
{
    return FieldRange(this);
}

const void* instance::fieldStorageAllocation() const noexcept
{
    return storage_.get();
}

const void* instance::fieldLayoutAllocation() const noexcept
{
    return storage_ ? storage_->layout.get() : nullptr;
}

instance::FieldRange::Iterator::Iterator(
    const instance* owner, std::size_t index, FieldMap::const_iterator dynamic)
    : owner_(owner), index_(index), dynamic_(dynamic)
{}

bool instance::FieldRange::Iterator::declaredFirst() const noexcept
{
    const auto& storage = *owner_->storage_;
    return index_ < storage.declared.size()
        && (dynamic_ == storage.dynamic.end()
            || NameLess{}(storage.layout->names()[index_], dynamic_->first));
}

instance::FieldEntry instance::FieldRange::Iterator::operator*() const noexcept
{
    const auto& storage = *owner_->storage_;
    if (declaredFirst())
        return {storage.layout->names()[index_], storage.declared[index_]};
    return {dynamic_->first, dynamic_->second};
}

instance::FieldRange::Iterator& instance::FieldRange::Iterator::operator++() noexcept
{
    if (declaredFirst())
        ++index_;
    else
        ++dynamic_;
    return *this;
}

bool instance::FieldRange::Iterator::operator!=(const Iterator& other) const noexcept
{
    return index_ != other.index_ || dynamic_ != other.dynamic_;
}

instance::FieldRange::Iterator instance::FieldRange::begin() const noexcept
{
    return Iterator(owner_,
        0,
        owner_->storage_ ? owner_->storage_->dynamic.begin() : FieldMap::const_iterator{});
}

instance::FieldRange::Iterator instance::FieldRange::end() const noexcept
{
    return Iterator(owner_,
        owner_->storage_ ? owner_->storage_->declared.size() : 0,
        owner_->storage_ ? owner_->storage_->dynamic.end() : FieldMap::const_iterator{});
}

instance::FieldRange::Iterator instance::FieldRange::find(NameView name) const noexcept
{
    if (!owner_->storage_)
        return end();
    const auto& storage = *owner_->storage_;
    const auto dynamic = storage.dynamic.lower_bound(name);
    if (storage.layout) {
        const auto index = storage.layout->find(name);
        if (index < storage.declared.size())
            return Iterator(owner_, index, dynamic);
    }
    if (dynamic != storage.dynamic.end() && NameView(dynamic->first) == name) {
        const auto index = storage.layout
            ? static_cast<std::size_t>(std::lower_bound(storage.layout->names().begin(),
                                           storage.layout->names().end(),
                                           name,
                                           NameLess{})
                  - storage.layout->names().begin())
            : 0;
        return Iterator(owner_, index, dynamic);
    }
    return end();
}

std::size_t instance::FieldRange::size() const noexcept
{
    return owner_->storage_ ? owner_->storage_->declared.size() + owner_->storage_->dynamic.size()
                            : 0;
}

void instance::invalidate() noexcept
{
    alive_ = false;
    storage_.reset();
}

const class_info* instance::class_() const
{
    return cls_;
}

object instance::__getitem__(NameView key)
{
    const auto* found = storage_ ? storage_->find(key) : nullptr;
    return found ? *found : object::undefined();
}

object* instance::findSlot(NameView key) noexcept
{
    return storage_ ? storage_->find(key) : nullptr;
}

object* instance::findDeclaredSlot(const InstanceFieldLayout* layout, std::size_t index) noexcept
{
    return storage_ && layout != nullptr && storage_->layout.get() == layout
            && index < storage_->declared.size()
        ? &storage_->declared[index]
        : nullptr;
}

object instance::__setitem__(NameView key, const object& value)
{
    auto& storage = fields();
    if (auto* found = storage.find(key))
        *found = value;
    else
        storage.dynamic.try_emplace(core::pmr::String(key, &storage.arena), value);
    return object::undefined();
}

object instance::operator[](NameView key)
{
    return __getitem__(key);
}

object& instance::slot(NameView key)
{
    auto& storage = fields();
    if (auto* found = storage.find(key))
        return *found;
    return storage.dynamic.try_emplace(core::pmr::String(key, &storage.arena)).first->second;
}

} // namespace arp::reflection

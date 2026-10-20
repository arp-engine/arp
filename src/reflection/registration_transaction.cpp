#include "reflection/registration_transaction.h"

#include "core/sha256.h"
#include "reflection/class_info.h"
#include "reflection/db.h"
#include "reflection/enum.h"
#include "reflection/function.h"
#include "reflection/property.h"
#include "reflection/registration_contract_format.h"
#include "reflection/type.h"

#include <algorithm>

namespace arp::reflection {

namespace {

thread_local RegistrationTransaction* activeTransaction = nullptr;

core::pmr::String makeString(NameView value)
{
    return core::pmr::String(value.data(), value.size(), &core::defaultCpuMemoryResource());
}

core::pmr::String canonicalBuiltinTypeName(NameView sourceType)
{
    struct BuiltinTypeName final {
        std::string_view source;
        std::string_view reflected;
    };

    static constexpr BuiltinTypeName names[] = {
        {"String", "str"},
        {"core::pmr::String", "str"},
        {"arp::core::pmr::String", "str"},
        {"hlist", "list"},
        {"arp::reflection::hlist", "list"},
        {"hdict", "dict"},
        {"arp::reflection::hdict", "dict"},
        {"std::int64_t", "int64"},
        {"int64_t", "int64"},
        {"double", "float"},
    };
    for (const auto& name : names) {
        if (sourceType == name.source)
            return makeString(name.reflected);
    }
    return makeString(sourceType);
}

void trimTypeName(std::string& value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        value.clear();
        return;
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    value = value.substr(first, last - first + 1u);
}

core::pmr::String sha256Text(const core::pmr::String& value)
{
    const auto digest = core::Sha256::digest(core::StringView(value.data(), value.size()));
    static constexpr char digits[] = "0123456789abcdef";
    core::pmr::String result("sha256:", &core::defaultCpuMemoryResource());
    result.reserve(7u + digest.size() * 2u);
    for (const auto byte : digest) {
        result.push_back(digits[(byte >> 4u) & 0x0fu]);
        result.push_back(digits[byte & 0x0fu]);
    }
    return result;
}

core::pmr::String typeContract(const typeinfo* type, bool isConst = false, bool isVolatile = false)
{
    core::pmr::String result(&core::defaultCpuMemoryResource());
    if (type == nullptr) {
        result = "void";
        return result;
    }
    if (isConst)
        result += "const ";
    if (isVolatile)
        result += "volatile ";
    if (type->class_() != nullptr && type->class_()->valueType() == undefined_t
        && type->class_()->size() == 0u && type->class_()->alignment() == 0u)
        result += "void";
    else if (type->is<object>())
        result += "Value";
    else if (type->is<bool>())
        result += "bool";
    else if (type->is<int>())
        result += "int";
    else if (type->is<std::int64_t>())
        result += "int64";
    else if (type->is<float>() || type->is<double>())
        result += "float";
    else if (type->is<core::pmr::String>())
        result += "str";
    else if (type->is<hlist>())
        result += "list";
    else if (type->is<hdict>())
        result += "dict";
    else if (type->class_() != nullptr)
        result += type->class_()->name();
    else
        result += "<unbound>";
    if (type->is_pointer())
        result += "*";
    if (type->is_lvalue_reference())
        result += "&";
    if (type->is_rvalue_reference())
        result += "&&";
    if (type->is_array())
        result += "[]";
    return result;
}

core::pmr::String memberContract(NameView name, const object& value)
{
    core::pmr::String result(&core::defaultCpuMemoryResource());
    if (value.is_function()) {
        const auto& fn = value.cast<const function&>();
        result = "function:";
        result.append(name.data(), name.size());
        result += "|static:";
        result += fn.is_static() ? "1" : "0";
        result += "|return:";
        result += typeContract(fn.return_type());
        result += "|params:";
        const auto parameters = fn.params_info();
        for (std::size_t index = 0; index < parameters.size(); ++index) {
            if (index != 0u)
                result += ",";
            result += typeContract(
                parameters[index].ti, parameters[index].is_const, parameters[index].is_volatile);
        }
        result += "|defaults:";
        result += std::to_string(fn.default_argument_count());
        return result;
    }
    if (value.is_property()) {
        const auto& prop = value.cast<const property&>();
        result = "property:";
        result.append(name.data(), name.size());
        result += "|type:";
        result += typeContract(prop.type());
        result += "|readonly:";
        result += prop.is_readonly() ? "1" : "0";
        result += "|pointer-setter:";
        result += prop.can_set_pointer_value() ? "1" : "0";
        result += "|indirect:";
        result += prop.has_indirect_value() ? "1" : "0";
        result += "|indirect-derived:";
        result += prop.indirect_allows_derived() ? "1" : "0";
        result += "|indirect-owned:";
        result += prop.indirect_uses_result_ownership() ? "1" : "0";
        if (prop.indirect_class() != nullptr) {
            result += "|indirect-class:";
            result += prop.indirect_class()->name();
        }
        return result;
    }
    result = "value:";
    result.append(name.data(), name.size());
    result += "|type:";
    result += std::to_string(static_cast<unsigned>(value.type()));
    if (value.type() == enumeration_t) {
        result += "|enum:";
        const auto& reflectedEnum = value.cast<const enumeration&>();
        result += reflectedEnum.name();
        for (const auto& [enumName, enumValue] : reflectedEnum.values()) {
            result += "|enum-value:";
            result += enumName;
            result += "=";
            result += std::to_string(enumValue);
        }
    }
    return result;
}

core::pmr::String classStructuralContract(NameView typeName, const class_info& info)
{
    core::pmr::String result(&core::defaultCpuMemoryResource());
    appendRegistrationContractField(result, "type-name", typeName);
    appendRegistrationContractField(result, "reflected-name", info.name());
    appendRegistrationContractField(
        result, "value-type", std::to_string(static_cast<unsigned>(info.valueType())));
    appendRegistrationContractField(result, "size", std::to_string(info.size()));
    appendRegistrationContractField(result, "alignment", std::to_string(info.alignment()));
    core::pmr::Vector<core::pmr::String> bases{&core::defaultCpuMemoryResource()};
    for (const auto* base : info.bases()) {
        if (base != nullptr)
            bases.push_back(base->name());
    }
    std::sort(bases.begin(), bases.end());
    for (const auto& base : bases)
        appendRegistrationContractField(result, "base", base);
    const auto fields = info.get_all_fields();
    for (const auto& [name, value] : fields) {
        const auto contract = memberContract(name, value);
        appendRegistrationContractField(result, "member", contract);
    }
    return result;
}

} // namespace

core::pmr::String ReflectionRegistrationContract::canonicalText() const
{
    core::pmr::String result(&core::defaultCpuMemoryResource());
    appendRegistrationContractField(result, "module", moduleIdentity);
    auto ordered = records;
    std::sort(ordered.begin(), ordered.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.key < rhs.key || (lhs.key == rhs.key && lhs.contract < rhs.contract);
    });
    for (const auto& record : ordered) {
        appendRegistrationContractField(result, "record-key", record.key);
        appendRegistrationContractField(result, "record-contract", record.contract);
    }
    return result;
}

core::pmr::String ReflectionRegistrationContract::fingerprint() const
{
    return sha256Text(canonicalText());
}

core::pmr::String RegistrationReceipt::canonicalText() const
{
    core::pmr::String result(&core::defaultCpuMemoryResource());
    appendRegistrationContractField(result, "owner", ownerIdentity);
    for (const auto& cls : classes) {
        core::pmr::String classContract(&core::defaultCpuMemoryResource());
        appendRegistrationContractField(classContract, "type-name", cls.typeName);
        appendRegistrationContractField(classContract, "reflected-name", cls.reflectedName);
        appendRegistrationContractField(
            classContract, "value-type", std::to_string(static_cast<unsigned>(cls.valueType)));
        appendRegistrationContractField(classContract, "size", std::to_string(cls.size));
        appendRegistrationContractField(classContract, "alignment", std::to_string(cls.alignment));
        appendRegistrationContractField(
            classContract, "ownership", std::to_string(static_cast<unsigned>(cls.ownership)));
        appendRegistrationContractField(
            classContract, "lifecycle", std::to_string(static_cast<unsigned>(cls.lifecycle)));
        appendRegistrationContractField(classContract, "module-export", cls.moduleExport);
        for (const auto& base : cls.bases)
            appendRegistrationContractField(classContract, "base", base);
        for (const auto& member : cls.members)
            appendRegistrationContractField(classContract, "member", member);
        appendRegistrationContractField(result, "class", classContract);
    }
    appendRegistrationContractField(result, "declarations", declarations.canonicalText());
    return result;
}

core::pmr::String RegistrationReceipt::fingerprint() const
{
    return sha256Text(canonicalText());
}

core::pmr::String RegistrationReceipt::reflectionContractFingerprint() const
{
    auto contract = *this;
    contract.ownerIdentity = declarations.moduleIdentity;
    return sha256Text(contract.canonicalText());
}

RegistrationTransaction::RegistrationTransaction(NameView ownerIdentity)
    : RegistrationTransaction(ownerIdentity, ownerIdentity)
{}

RegistrationTransaction::RegistrationTransaction(NameView moduleIdentity, NameView ownerIdentity)
    : ownerIdentity_(makeString(ownerIdentity))
{
    declarations_.moduleIdentity = makeString(moduleIdentity);
    // Builtins are process-owned and must never be captured by a plugin
    // transaction merely because this is the first reflection use.
    (void)db::instance();
    if (ownerIdentity_.empty())
        reject("reflection registration transaction requires an owner identity");
}

RegistrationTransaction::~RegistrationTransaction() = default;

class_info* RegistrationTransaction::findStagedClass(NameView typeName) const
{
    const auto found = classes_.find(makeString(typeName));
    return found == classes_.end() ? nullptr : found->second.get();
}

const typeinfo* RegistrationTransaction::findStagedType(NameView typeName) const
{
    const auto found = types_.find(makeString(typeName));
    return found == types_.end() ? nullptr : found->second.get();
}

class_info* RegistrationTransaction::registerReferencedClass(NameView typeName,
    value_t type,
    std::size_t size,
    std::size_t alignment,
    get_derived_info_func getDerivedInfo)
{
    if (committed_)
        return nullptr;
    if (auto* staged = findStagedClass(typeName))
        return staged;
    if (auto* global = db::rawInstance().find_class(typeName))
        return global;
    auto key = makeString(typeName);
    auto info = core::makeUnique<class_info>(key, type, size, alignment, getDerivedInfo);
    auto* result = info.get();
    classes_.emplace(std::move(key), std::move(info));
    return result;
}

class_info* RegistrationTransaction::declareClass(NameView typeName,
    value_t type,
    std::size_t size,
    std::size_t alignment,
    get_derived_info_func getDerivedInfo)
{
    if (committed_)
        return nullptr;
    const auto key = makeString(typeName);
    if (declaredClasses_.find(key) != declaredClasses_.end()) {
        reject("reflection registration transaction contains a duplicate class declaration");
        return findStagedClass(typeName);
    }
    if (db::rawInstance().find_class(typeName) != nullptr) {
        reject("reflection registration transaction cannot replace an existing class");
        return nullptr;
    }
    auto* result = findStagedClass(typeName);
    if (result == nullptr) {
        auto info = core::makeUnique<class_info>(key, type, size, alignment, getDerivedInfo);
        result = info.get();
        classes_.emplace(key, std::move(info));
    }
    declaredClasses_.insert(key);
    return result;
}

const typeinfo* RegistrationTransaction::registerType(
    NameView typeName, class_info* cls, std::uint64_t mask)
{
    if (committed_ || cls == nullptr)
        return nullptr;
    if (auto* staged = findStagedType(typeName))
        return staged;
    if (const auto* global = db::rawInstance().find_type(typeName))
        return global;
    auto key = makeString(typeName);
    auto info = core::makeUnique<typeinfo>(cls, mask);
    const auto* result = info.get();
    types_.emplace(std::move(key), std::move(info));
    return result;
}

bool RegistrationTransaction::setOwner(std::shared_ptr<void> owner)
{
    if (committed_ || !valid_ || owner == nullptr) {
        reject("reflection registration transaction requires a valid owner");
        return false;
    }
    if (!owners_.empty()) {
        reject("reflection registration transaction owner was already assigned");
        return false;
    }
    owners_.emplace(ownerIdentity_, std::move(owner));
    return true;
}

bool RegistrationTransaction::declareContractRecord(NameView key, NameView contract)
{
    if (committed_ || key.empty() || contract.empty()) {
        reject("reflection registration contract record is incomplete");
        return false;
    }
    const auto keyText = makeString(key);
    const auto duplicate = std::find_if(
        declarations_.records.begin(), declarations_.records.end(), [&](const auto& record) {
            return record.key == keyText;
        });
    if (duplicate != declarations_.records.end()) {
        reject("reflection registration contract contains a duplicate key");
        return false;
    }
    RegistrationContractRecord record;
    record.key = keyText;
    record.contract = makeString(contract);
    declarations_.records.push_back(std::move(record));
    return true;
}

bool RegistrationTransaction::declareClassContract(
    NameView key, NameView contract, const class_info* cls)
{
    return declareClassContract(key,
        contract,
        cls,
        RegistrationOwnership::Unspecified,
        RegistrationLifecycle::Unspecified,
        {});
}

bool RegistrationTransaction::declareClassContract(NameView key,
    NameView contract,
    const class_info* cls,
    RegistrationOwnership ownership,
    RegistrationLifecycle lifecycle,
    NameView moduleExport)
{
    if (cls == nullptr || !ownsClass(cls)) {
        reject("reflection registration class contract does not reference a staged class");
        return false;
    }
    if (!declareContractRecord(key, contract))
        return false;
    const auto keyText = makeString(key);
    if (!contractClasses_.emplace(keyText, cls).second) {
        reject("reflection registration class contract contains a duplicate class binding");
        return false;
    }
    core::pmr::String typeName(&core::defaultCpuMemoryResource());
    for (const auto& [candidateName, info] : classes_) {
        if (info.get() == cls) {
            typeName = candidateName;
            break;
        }
    }
    contractClassSnapshots_.emplace(keyText, classStructuralContract(typeName, *cls));
    ClassSemantics semantics;
    semantics.ownership = ownership;
    semantics.lifecycle = lifecycle;
    semantics.moduleExport = makeString(moduleExport);
    if (!classSemantics_.emplace(cls, std::move(semantics)).second) {
        reject("reflection registration class semantics contains a duplicate class binding");
        return false;
    }
    return true;
}

bool RegistrationTransaction::ownsClass(const class_info* value) const
{
    return std::any_of(classes_.begin(), classes_.end(), [value](const auto& entry) {
        return entry.second.get() == value;
    });
}

RegistrationReceipt RegistrationTransaction::receipt() const
{
    RegistrationReceipt result;
    result.ownerIdentity = ownerIdentity_;
    result.declarations = declarations_;
    for (const auto& [typeName, info] : classes_) {
        RegistrationClassReceipt cls;
        cls.typeName = typeName;
        cls.reflectedName = info->name();
        cls.valueType = info->valueType();
        cls.size = info->size();
        cls.alignment = info->alignment();
        for (const auto* base : info->bases_) {
            if (base != nullptr)
                cls.bases.push_back(base->name());
        }
        std::sort(cls.bases.begin(), cls.bases.end());
        const auto fields = info->get_all_fields();
        for (const auto& [name, value] : fields) {
            cls.members.push_back(memberContract(name, value));
        }
        const auto semantics = classSemantics_.find(info.get());
        if (semantics != classSemantics_.end()) {
            cls.ownership = semantics->second.ownership;
            cls.lifecycle = semantics->second.lifecycle;
            cls.moduleExport = semantics->second.moduleExport;
        }
        result.classes.push_back(std::move(cls));
    }
    return result;
}

const class_info* RegistrationTransaction::publicationClass(const class_info* staged) const
{
    if (staged == nullptr)
        return nullptr;
    for (const auto& [key, info] : classes_) {
        if (info.get() != staged)
            continue;
        return staged;
    }
    return staged;
}

bool RegistrationTransaction::validate(core::pmr::String& diagnostics) const
{
    if (!valid_) {
        diagnostics = diagnostics_;
        return false;
    }
    if (owners_.size() != 1u || owners_.begin()->first != ownerIdentity_
        || owners_.begin()->second == nullptr) {
        diagnostics = "reflection registration transaction has no publication owner";
        return false;
    }
    core::pmr::Set<core::pmr::String> reflectedNames{&core::defaultCpuMemoryResource()};
    for (const auto& [typeName, info] : classes_) {
        if (declaredClasses_.find(typeName) == declaredClasses_.end()) {
            diagnostics =
                "reflection registration transaction contains an undeclared referenced class: ";
            diagnostics += typeName;
            return false;
        }
        if (info == nullptr || info->name().empty()
            || !reflectedNames.insert(info->name()).second) {
            diagnostics =
                "reflection registration transaction has an invalid or duplicate reflected name";
            return false;
        }
        if (db::rawInstance().find_class(typeName) != nullptr
            || db::rawInstance().find_class(info->name()) != nullptr) {
            diagnostics = "reflection registration transaction reflected name already exists";
            return false;
        }
    }
    for (const auto& [typeName, info] : types_) {
        if (info == nullptr || info->class_() == nullptr) {
            diagnostics = "reflection registration transaction contains an invalid staged type: ";
            diagnostics += typeName;
            return false;
        }
        if (ownsClass(info->class_())) {
            const auto* stagedClass = info->class_();
            const auto declared =
                std::find_if(classes_.begin(), classes_.end(), [&](const auto& entry) {
                    return entry.second.get() == stagedClass
                        && declaredClasses_.find(entry.first) != declaredClasses_.end();
                });
            if (declared == classes_.end()) {
                diagnostics =
                    "reflection registration transaction staged a type for an undeclared class: ";
                diagnostics += typeName;
                return false;
            }
        }
    }
    if (requireDeclaredContracts_) {
        if (contractClasses_.size() != declaredClasses_.size()) {
            diagnostics = "reflection registration transaction class contract is incomplete";
            return false;
        }
        if (classSemantics_.size() != declaredClasses_.size()) {
            diagnostics = "reflection registration class semantics is incomplete";
            return false;
        }
        for (const auto& [key, cls] : contractClasses_) {
            const auto snapshot = contractClassSnapshots_.find(key);
            core::pmr::String typeName(&core::defaultCpuMemoryResource());
            for (const auto& [candidateName, info] : classes_) {
                if (info.get() == cls) {
                    typeName = candidateName;
                    break;
                }
            }
            if (typeName.empty() || snapshot == contractClassSnapshots_.end()
                || snapshot->second != classStructuralContract(typeName, *cls)) {
                diagnostics =
                    "reflection registration class changed after its generated contract declaration";
                return false;
            }
        }
    }
    return true;
}

bool RegistrationTransaction::commit(RegistrationReceipt& result, core::pmr::String& diagnostics)
{
    return commit({}, result, diagnostics);
}

bool RegistrationTransaction::commit(
    NameView expectedFingerprint, RegistrationReceipt& result, core::pmr::String& diagnostics)
{
    if (!validate(diagnostics) || committed_)
        return false;
    result = receipt();
    if (!expectedFingerprint.empty() && result.fingerprint() != makeString(expectedFingerprint)) {
        diagnostics = "reflection registration transaction receipt changed after validation";
        result = {};
        return false;
    }
    if (!db::rawInstance().commitRegistrationTransaction(*this, diagnostics))
        return false;
    committed_ = true;
    return true;
}

void RegistrationTransaction::reject(NameView diagnostics)
{
    if (!valid_)
        return;
    valid_ = false;
    diagnostics_ = makeString(diagnostics);
}

bool declareRegistrationContractRecord(NameView key, NameView contract)
{
    auto* transaction = detail::currentRegistrationTransaction();
    return transaction == nullptr || transaction->declareContractRecord(key, contract);
}

core::pmr::String canonicalRegistrationTypeContract(
    NameView sourceType, const core::pmr::Map<core::pmr::String, core::pmr::String>& reflectedNames)
{
    std::string value(sourceType.data(), sourceType.size());
    trimTypeName(value);
    std::string prefix;
    for (;;) {
        if (value.rfind("const ", 0u) == 0u) {
            prefix += "const ";
            value.erase(0u, 6u);
        } else if (value.rfind("volatile ", 0u) == 0u) {
            prefix += "volatile ";
            value.erase(0u, 9u);
        } else {
            break;
        }
        trimTypeName(value);
    }

    std::string suffix;
    for (;;) {
        trimTypeName(value);
        if (value.size() >= 2u && value.compare(value.size() - 2u, 2u, "&&") == 0) {
            suffix.insert(0u, "&&");
            value.erase(value.size() - 2u);
        } else if (!value.empty() && (value.back() == '&' || value.back() == '*')) {
            suffix.insert(suffix.begin(), value.back());
            value.pop_back();
        } else {
            break;
        }
    }
    trimTypeName(value);

    core::pmr::String base(value.data(), value.size(), &core::defaultCpuMemoryResource());
    auto reflected = reflectedNames.find(base);
    if (reflected == reflectedNames.end() && base.rfind("::", 0u) == 0u)
        reflected = reflectedNames.find(base.substr(2u));

    core::pmr::String result(prefix.data(), prefix.size(), &core::defaultCpuMemoryResource());
    result +=
        reflected == reflectedNames.end() ? canonicalBuiltinTypeName(base) : reflected->second;
    result.append(suffix.data(), suffix.size());
    return result;
}

core::pmr::String registrationTypeContract(const typeinfo* type, bool isConst, bool isVolatile)
{
    return typeContract(type, isConst, isVolatile);
}

bool declareRegistrationClassContract(NameView key, NameView contract, const class_info* cls)
{
    auto* transaction = detail::currentRegistrationTransaction();
    return transaction == nullptr || transaction->declareClassContract(key, contract, cls);
}

bool declareRegistrationClassContract(NameView key,
    NameView contract,
    const class_info* cls,
    RegistrationOwnership ownership,
    RegistrationLifecycle lifecycle,
    NameView moduleExport)
{
    auto* transaction = detail::currentRegistrationTransaction();
    return transaction == nullptr
        || transaction->declareClassContract(
            key, contract, cls, ownership, lifecycle, moduleExport);
}

RegistrationScope::RegistrationScope(RegistrationTransaction& transaction) noexcept
    : transaction_(&transaction), previous_(activeTransaction)
{
    if (previous_ != nullptr) {
        transaction.reject("nested reflection registration transactions are forbidden");
        transaction_ = nullptr;
        return;
    }
    activeTransaction = &transaction;
}

RegistrationScope::~RegistrationScope()
{
    if (transaction_ != nullptr && activeTransaction == transaction_)
        activeTransaction = previous_;
}

namespace detail {

RegistrationTransaction* currentRegistrationTransaction() noexcept
{
    return activeTransaction;
}

} // namespace detail

} // namespace arp::reflection

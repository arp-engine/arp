#include "reflection/native_reference.h"
#include "reflection/reflection.h"
#include "reflection/registration_type_binding.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

// Reflection contract tests must keep their checks active in Release builds.
#ifdef NDEBUG
#undef assert
#define assert(expression)                                                                         \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            std::fprintf(stderr,                                                                   \
                "Reflection registry contract failed: %s (%s:%d)\n",                               \
                #expression,                                                                       \
                __FILE__,                                                                          \
                __LINE__);                                                                         \
            std::abort();                                                                          \
        }                                                                                          \
    } while (false)
#endif

#include "reflection/serialization.h"

using namespace arp::reflection;

namespace {

enum class ReflectedProbeEnum : std::uint16_t {
    First = 7,
    Second = 12,
};

void readsEnumValuesThroughTypeErasedReflection()
{
    object value(ReflectedProbeEnum::Second);
    int integer = 0;
    assert(value.tryGetEnumInt(integer));
    assert(integer == 12);

    object scalar(12);
    assert(!scalar.tryGetEnumInt(integer));
}

void exactPayloadStorageProof()
{
    // Address comparison must not execute type_of or register reflection types.
    const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
    variant empty, integer(7), floating(0.5), wide(std::int64_t(9));
    int local = 11;
    const int immutable = 17;
    variant imported(local), constant(immutable);
    variant native(detail::NativeObjectReference{});
    assert(empty.exactPayloadPointer<int>() == nullptr);
    assert(integer.exactPayloadPointer<int>() && *integer.exactPayloadPointer<int>() == 7);
    assert(floating.exactPayloadPointer<int>() == nullptr
        && wide.exactPayloadPointer<int>() == nullptr);
    assert(native.exactPayloadPointer<int>() == nullptr
        && native.exactPayloadPointer<detail::NativeObjectReference>() != nullptr);
    assert(imported.exactPayloadPointer<int>() && *imported.exactPayloadPointer<int>() == 11);
    assert(constant.exactPayloadPointer<int>() == nullptr
        && constant.exactPayloadPointer<const int>()
        && *constant.exactPayloadPointer<const int>() == 17);
    variant copied(integer), moved(std::move(copied));
    assert(moved.exactPayloadPointer<int>() && *moved.exactPayloadPointer<int>() == 7);
    assert(integer.exactPayloadPointer<int>() != moved.exactPayloadPointer<int>());
    integer.swap(floating);
    assert(integer.exactPayloadPointer<int>() == nullptr && floating.exactPayloadPointer<int>()
        && *floating.exactPayloadPointer<int>() == 7);
    floating = variant(19);
    assert(*floating.exactPayloadPointer<int>() == 19);
    assert(arp::core::defaultCpuAllocator().stats().allocationCount == before);
    arp::core::pmr::String text("heap payload", &arp::core::defaultCpuMemoryResource());
    variant owned(std::move(text)), heapCopy(owned);
    const auto heapBefore = arp::core::defaultCpuAllocator().stats().allocationCount;
    const auto* a = owned.exactPayloadPointer<arp::core::pmr::String>();
    const auto* b = heapCopy.exactPayloadPointer<arp::core::pmr::String>();
    assert(a && b && a != b && *a == *b && *a == "heap payload");
    assert(owned.exactPayloadPointer<int>() == nullptr);
    assert(arp::core::defaultCpuAllocator().stats().allocationCount == heapBefore);
}

arp::core::pmr::String text(std::string_view value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

class B {
public:
    OBJECT_RTTI_IMPL(B);

    void print2() { printed2_ = true; }

    bool a_ = false;
    bool printed2_ = false;
};

class A : public B {
public:
    OBJECT_RTTI_IMPL(A);

    explicit A(int value) : value_(value) { ++constructed_; }

    A(const A& rhs) : B(rhs), value_(rhs.value_) { ++constructed_; }

    ~A() { ++destroyed_; }

    void print(const arp::core::pmr::String& content) { lastPrint_ = content; }

    int add(const int lhs, const int rhs) { return lhs + rhs; }

    static void test() { ++staticCalls_; }

    int value_ = 0;
    arp::core::pmr::String lastPrint_{&arp::core::defaultCpuMemoryResource()};

    static int constructed_;
    static int destroyed_;
    static int staticCalls_;
};

int A::constructed_ = 0;
int A::destroyed_ = 0;
int A::staticCalls_ = 0;

struct ReflectedListItem {
    int value = 0;
};

void pmrValueCopyPreservesSourceAllocator()
{
    arp::core::pmr::String source("allocator-preserving-reflection-value-copy-0123456789",
        &arp::core::defaultCpuMemoryResource());
    object copied(source);
    const auto* stored = copied.ptr<arp::core::pmr::String>();
    assert(stored != nullptr);
    assert(stored->get_allocator().resource() == &arp::core::defaultCpuMemoryResource());
    assert(arp::core::inspectCpuAllocationContaining(stored->data()).found);
}

struct ReflectedListRoot {
    hlist items{&arp::core::defaultCpuMemoryResource()};
};

struct TransactionRollbackType {
    int value = 0;
};

struct ClassCacheRollbackType {
    int value = 0;
};

struct TransactionCommitType {
    int value = 0;
};

struct TransactionDuplicateType {
    int value = 0;
};

struct TransactionReferencedHelper {
    int value = 0;
};

struct TransactionReferencedOwner {
    TransactionReferencedHelper helper;
};

struct TransactionSemanticType {
    int value = 0;
};

void verifiesCanonicalRegistrationTypeContracts()
{
    static_assert(detail::getTypeName<int>() == "int");
    static_assert(detail::getTypeName<std::array<float, 16>>() == "std::array<float, 16>");
    static_assert(detail::getTypeName<std::array<std::array<float, 2>, 3>>()
        == "std::array<std::array<float, 2>, 3>");
    assert(registrationTypeContract(type_of<std::array<float, 16>>())
        == text("std::array<float, 16>"));
    arp::core::pmr::Map<arp::core::pmr::String, arp::core::pmr::String> reflectedNames{
        &arp::core::defaultCpuMemoryResource()};
    reflectedNames.emplace(text("Example"), text("ReflectedExample"));
    assert(canonicalRegistrationTypeContract("String", reflectedNames) == text("str"));
    assert(
        canonicalRegistrationTypeContract("const hlist&", reflectedNames) == text("const list&"));
    assert(canonicalRegistrationTypeContract("std::int64_t", reflectedNames) == text("int64"));
    assert(canonicalRegistrationTypeContract("Example&&", reflectedNames)
        == text("ReflectedExample&&"));
    assert(registrationTypeContract(type_of<object>()) == text("Value"));
    assert(registrationTypeContract(type_of<bool>()) == text("bool"));
    assert(registrationTypeContract(type_of<int>()) == text("int"));
    assert(registrationTypeContract(type_of<std::int64_t>()) == text("int64"));
    assert(registrationTypeContract(type_of<float>()) == text("float"));
    assert(registrationTypeContract(type_of<double>()) == text("float"));
    assert(registrationTypeContract(type_of<arp::core::pmr::String>()) == text("str"));
    assert(registrationTypeContract(type_of<hlist>()) == text("list"));
    assert(registrationTypeContract(type_of<hdict>()) == text("dict"));
}

struct NativeBindingUnregistered {
    int value = 0;
};

void verifiesCompiledNativeTypeBindings()
{
    using Vector = arp::core::pmr::Vector<std::int32_t>;
    RegistrationTransaction transaction("compiled-native-bindings");
    arp::core::pmr::String expected32{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String expectedSize{&arp::core::defaultCpuMemoryResource()};
    {
        RegistrationScope scope(transaction);
        assert(bindRegistrationNativeType<std::int32_t>("std::int32_t"));
        assert(bindRegistrationNativeType<std::size_t>("std::size_t"));
        assert(bindRegistrationNativeType<Vector>("arp::core::pmr::Vector<std::int32_t>"));
        assert(bindRegistrationNativeType<std::vector<std::int32_t>>("std::vector<std::int32_t>"));
        assert(bindRegistrationNativeType<NativeBindingUnregistered>("NativeBindingUnregistered"));
        expected32 = registrationTypeContract(type_of<std::int32_t>());
        expectedSize = registrationTypeContract(type_of<std::size_t>());
    }
    auto receipt = transaction.receipt();
    assert(receipt.nativeTypeBindings.at(text("std::int32_t")) == expected32);
    assert(receipt.nativeTypeBindings.at(text("std::size_t")) == expectedSize);
    const auto pmr = text("arp::core::pmr::Vector<std::int32_t>");
    assert(receipt.nativeTypeBindings.at(pmr)
        != receipt.nativeTypeBindings.at(text("std::vector<std::int32_t>")));
    assert(canonicalRegistrationTypeContract(
               "const arp::core::pmr::Vector<std::int32_t>&", receipt.nativeTypeBindings)
        == text("const ") + receipt.nativeTypeBindings.at(pmr) + text("&"));
    const auto fingerprint = receipt.fingerprint();
    receipt.nativeTypeBindings.at(text("std::int32_t")) = "wrong-type";
    assert(receipt.fingerprint() != fingerprint);
    assert(transaction.setOwner(arp::core::makeShared<int>(1)));
    arp::core::pmr::String diagnostics{&arp::core::defaultCpuMemoryResource()};
    assert(!transaction.commit(receipt, diagnostics));
    assert(diagnostics.find("undeclared referenced class") != arp::core::pmr::String::npos);
    assert(
        db::rawInstance().find_class(detail::getTypeName<NativeBindingUnregistered>()) == nullptr);

    RegistrationTransaction duplicate("duplicate-native-bindings");
    {
        RegistrationScope scope(duplicate);
        assert(bindRegistrationNativeType<int>("int"));
        assert(!bindRegistrationNativeType<int>("int"));
    }
    assert(!duplicate.valid());
    assert(duplicate.diagnostics().find("duplicated") != arp::core::pmr::String::npos);
}

struct NativeCompletionBase {
    int value = 3;
    NativeCompletionBase* link = nullptr;
};

struct NativeCompletionDerived : NativeCompletionBase {
    int extra = 5;
};

void verifiesNativeReferenceCompletion()
{
    NativeCompletionDerived host;
    NativeCompletionBase base;
    auto native = object::native_reference(&host);
    auto baseNative = object::native_reference(&base);
    const auto* identity = native.class_();
    const auto* baseIdentity = baseNative.class_();
    const auto* pointerType = type_of<NativeCompletionDerived*>();
    assert(identity->get_all_fields().empty());
    {
        RegistrationTransaction rollback("native-completion-rollback");
        {
            RegistrationScope scope(rollback);
            registration::class_<NativeCompletionDerived>("DiscardedNativeCompletion")
                .property("extra", &NativeCompletionDerived::extra);
        }
        assert(native.class_() == identity && identity->get_all_fields().empty());
        assert(rollback.setOwner(arp::core::makeShared<int>(2)));
        RegistrationReceipt receipt;
        arp::core::pmr::String diagnostic{&arp::core::defaultCpuMemoryResource()};
        assert(!rollback.commit("wrong-receipt", receipt, diagnostic));
        assert(diagnostic.find("receipt changed") != arp::core::pmr::String::npos);
    }
    assert(identity->get_all_fields().empty());
    assert(db::rawInstance().find_class("DiscardedNativeCompletion") == nullptr);
    {
        RegistrationTransaction complete("native-completion-commit");
        {
            RegistrationScope scope(complete);
            registration::class_<NativeCompletionBase>("CompletedNativeBase")
                .property("value", &NativeCompletionBase::value)
                .property("link", &NativeCompletionBase::link);
            registration::class_<NativeCompletionDerived>("CompletedNativeDerived")
                .inherit<NativeCompletionBase>()
                .property("extra", &NativeCompletionDerived::extra);
        }
        const auto expected = complete.receipt().fingerprint();
        assert(complete.setOwner(arp::core::makeShared<int>(3)));
        RegistrationReceipt receipt;
        arp::core::pmr::String diagnostic{&arp::core::defaultCpuMemoryResource()};
        assert(complete.commit(expected, receipt, diagnostic));
    }
    assert(native.class_() == identity && baseNative.class_() == baseIdentity);
    assert(type_of<NativeCompletionDerived*>() == pointerType);
    assert(identity->name() == "CompletedNativeDerived");
    assert(identity->bases().front() == baseIdentity);
    const auto link = baseIdentity->get_field("link").cast<property>();
    assert(link.indirect_class() == baseIdentity);
    native.set("value", object(17));
    native.set("extra", object(19));
    assert(host.value == 17 && host.extra == 19);
    assert(native.cast<NativeCompletionDerived*>() == &host);
    assert(native.cast<NativeCompletionBase*>() == static_cast<NativeCompletionBase*>(&host));
    RegistrationTransaction duplicate("native-completion-duplicate");
    {
        RegistrationScope scope(duplicate);
        registration::class_<NativeCompletionDerived>("CompletedNativeDerived");
    }
    assert(!duplicate.valid());
    assert(duplicate.diagnostics().find("cannot replace an existing class")
        != arp::core::pmr::String::npos);

    RegistrationTransaction support("native-storage-support");
    support.requireDeclaredContracts();
    {
        RegistrationScope scope(support);
        assert((bindRegistrationNativeType<std::array<std::uint16_t, 13>>("StorageArray")));
        assert(bindRegistrationNativeType<arp::core::pmr::Vector<std::uint16_t>>("StorageVector"));
    }
    assert(support.setOwner(arp::core::makeShared<int>(4)));
    RegistrationReceipt receipt;
    arp::core::pmr::String diagnostic{&arp::core::defaultCpuMemoryResource()};
    assert(support.commit(receipt, diagnostic));
    assert(!receipt.nativeSupportTypes.empty());
}

struct GeneratedReuseValue {
    int value = 7;

    int get() const { return value; }
};

void verifiesGeneratedSchemaReuse()
{
    const auto source = text("sha256:") + arp::core::pmr::String(64u, 'a');
    const auto otherSource = text("sha256:") + arp::core::pmr::String(64u, 'b');
    const auto nativeName = detail::getTypeName<GeneratedReuseValue>();
    auto host = registration::class_<GeneratedReuseValue>("GeneratedReuseValue")
                    .construct<>()
                    .property("value", &GeneratedReuseValue::value)
                    .method("get", &GeneratedReuseValue::get);
    assert(declareGeneratedRegistrationClassContract("class:GeneratedReuseValue",
        "schema/1",
        host.info(),
        source,
        RegistrationOwnership::Value,
        RegistrationLifecycle::Value,
        ""));
    auto* identity = host.info();
    const auto* callback = identity->get_field("get").cast<function>().identity();
    auto attempt = [&](NameView digest, bool extra, bool success) {
        RegistrationTransaction transaction("generated-schema-reuse");
        transaction.requireDeclaredContracts();
        {
            RegistrationScope scope(transaction);
            assert(prepareGeneratedRegistrationClass(nativeName, "schema/1", digest));
            auto cls = registration::class_<GeneratedReuseValue>("GeneratedReuseValue");
            if (cls.info()) {
                cls.construct<>()
                    .property("value", &GeneratedReuseValue::value)
                    .method("get", &GeneratedReuseValue::get);
                if (extra)
                    cls.property("undeclared", &GeneratedReuseValue::value);
                assert(declareGeneratedRegistrationClassContract("class:GeneratedReuseValue",
                    "schema/1",
                    cls.info(),
                    digest,
                    RegistrationOwnership::Value,
                    RegistrationLifecycle::Value,
                    ""));
                assert(transaction.publicationClass(cls.info()) == identity);
                assert(transaction.publicationFunctionIdentity(
                           cls.info()->get_field("get").cast<function>().identity())
                    == callback);
            }
        }
        assert(transaction.setOwner(arp::core::makeShared<int>(5)) || !transaction.valid());
        RegistrationReceipt receipt;
        arp::core::pmr::String diagnostic{&arp::core::defaultCpuMemoryResource()};
        assert(transaction.commit(receipt, diagnostic) == success);
        if (success) {
            auto changed = receipt;
            changed.classes.front().generatedSource = otherSource;
            assert(changed.fingerprint() != receipt.fingerprint());
        }
        assert(class_of<GeneratedReuseValue>() == identity);
        assert(identity->get_field("get").cast<function>().identity() == callback);
        assert(identity->get_field("undeclared").is_undefined());
    };
    attempt(otherSource, false, false);
    attempt(source, true, false);
    attempt(source, false, true);
    identity->def("host_mutation", object(1));
    attempt(source, false, false);
}

struct GeneratedNativeBase {};

struct GeneratedNativeDerived : GeneratedNativeBase {};

void verifiesGeneratedNativeDependencyIdentity()
{
    const auto source = text("sha256:") + arp::core::pmr::String(64u, 'c');
    auto derived = registration::class_<GeneratedNativeDerived>("GeneratedNativeDerived")
                       .inherit<GeneratedNativeBase>();
    assert(declareGeneratedRegistrationClassContract("class:GeneratedNativeDerived",
        "schema/1",
        derived.info(),
        source,
        RegistrationOwnership::Value,
        RegistrationLifecycle::Value,
        ""));
    auto* base = class_of<GeneratedNativeBase>();
    registration::class_<GeneratedNativeBase>("NamedGeneratedBase");
    assert(base->name() == "NamedGeneratedBase");
    RegistrationTransaction transaction("generated-native-dependency");
    transaction.requireDeclaredContracts();
    {
        RegistrationScope scope(transaction);
        assert(prepareGeneratedRegistrationClass(
            detail::getTypeName<GeneratedNativeDerived>(), "schema/1", source));
        auto staged = registration::class_<GeneratedNativeDerived>("GeneratedNativeDerived")
                          .inherit<GeneratedNativeBase>();
        assert(staged.info());
        assert(declareGeneratedRegistrationClassContract("class:GeneratedNativeDerived",
            "schema/1",
            staged.info(),
            source,
            RegistrationOwnership::Value,
            RegistrationLifecycle::Value,
            ""));
    }
    assert(transaction.setOwner(arp::core::makeShared<int>(6)));
    RegistrationReceipt receipt;
    arp::core::pmr::String diagnostic{&arp::core::defaultCpuMemoryResource()};
    assert(transaction.commit(receipt, diagnostic));
    assert(class_of<GeneratedNativeDerived>() == derived.info());
    assert(derived.info()->bases().front() == base);
    assert(base->name() == "NamedGeneratedBase");
}

void verifiesRegistrationTransactions()
{
    const auto* functionType = type_of<function>();
    const auto* propertyType = type_of<property>();
    const auto* enumerationType = type_of<enumeration>();
    const auto* instanceType = type_of<instance>();
    const auto* classType = type_of<class_info>();
    const auto* nullType = type_of<std::nullptr_t>();
    const auto* bufferType = type_of<ByteBuffer>();
    const auto* nativeReferenceType = type_of<detail::NativeObjectReference>();
    if (functionType == nullptr || propertyType == nullptr || enumerationType == nullptr
        || instanceType == nullptr || classType == nullptr || nullType == nullptr
        || bufferType == nullptr || nativeReferenceType == nullptr) {
        std::abort();
    }
    {
        RegistrationTransaction transaction("reflection-test-infrastructure-reference");
        {
            RegistrationScope scope(transaction);
            if (type_of<function>() != functionType || type_of<property>() != propertyType
                || type_of<enumeration>() != enumerationType || type_of<instance>() != instanceType
                || type_of<class_info>() != classType || type_of<std::nullptr_t>() != nullType
                || type_of<ByteBuffer>() != bufferType
                || type_of<detail::NativeObjectReference>() != nativeReferenceType) {
                std::abort();
            }
        }
        if (!transaction.receipt().classes.empty())
            std::abort();
    }

    {
        RegistrationTransaction transaction("reflection-test-rollback");
        {
            RegistrationScope scope(transaction);
            registration::class_<TransactionRollbackType>("TransactionRollbackType")
                .construct<>()
                .property("value", &TransactionRollbackType::value);
        }
        const auto receipt = transaction.receipt();
        assert(receipt.classes.size() == 1u);
        assert(receipt.classes.front().reflectedName == text("TransactionRollbackType"));
        assert(db::instance().find_class("TransactionRollbackType") == nullptr);
    }
    assert(db::instance().find_class("TransactionRollbackType") == nullptr);

    {
        RegistrationTransaction transaction("class-cache-rollback");
        RegistrationScope scope(transaction);
        registration::class_<ClassCacheRollbackType>("ClassCacheDiscarded");
        assert(class_of<ClassCacheRollbackType>()->name() == "ClassCacheDiscarded");
    }
    assert(db::instance().find_class("ClassCacheDiscarded") == nullptr);
    {
        RegistrationTransaction transaction("class-cache-publish");
        {
            RegistrationScope scope(transaction);
            registration::class_<ClassCacheRollbackType>("ClassCachePublished");
            assert(class_of<ClassCacheRollbackType>()->name() == "ClassCachePublished");
        }
        RegistrationReceipt receipt;
        arp::core::pmr::String diagnostics;
        assert(transaction.setOwner(arp::core::makeShared<int>(1)));
        assert(transaction.commit(receipt, diagnostics));
    }
    assert(class_of<ClassCacheRollbackType>() == db::instance().find_class("ClassCachePublished"));
    const auto classLookupAllocations = arp::core::defaultCpuAllocator().stats().allocationCount;
    for (int attempt = 0; attempt < 100; ++attempt)
        assert(class_of<ClassCacheRollbackType>()->name() == "ClassCachePublished");
    assert(arp::core::defaultCpuAllocator().stats().allocationCount == classLookupAllocations);

    {
        RegistrationTransaction transaction("reflection-test-commit");
        auto owner = arp::core::makeShared<int>(42);
        std::weak_ptr<int> ownerLifetime = owner;
        {
            RegistrationScope scope(transaction);
            registration::class_<TransactionCommitType>("TransactionCommitType")
                .construct<>()
                .property("value", &TransactionCommitType::value);
        }
        RegistrationReceipt receipt;
        arp::core::pmr::String diagnostics{&arp::core::defaultCpuMemoryResource()};
        if (!transaction.setOwner(owner))
            std::abort();
        const bool committed = transaction.commit(receipt, diagnostics);
        if (!committed)
            std::abort();
        assert(committed);
        assert(receipt.classes.size() == 1u);
        assert(!receipt.fingerprint().empty());
        owner.reset();
        if (ownerLifetime.expired())
            std::abort();
    }
    assert(db::instance().find_class("TransactionCommitType") != nullptr);

    {
        RegistrationTransaction transaction("reflection-test-existing-type");
        {
            RegistrationScope scope(transaction);
            registration::class_<TransactionCommitType>("TransactionCommitType")
                .construct<>()
                .property("value", &TransactionCommitType::value);
        }
        if (transaction.valid())
            std::abort();
        RegistrationReceipt receipt;
        arp::core::pmr::String diagnostics{&arp::core::defaultCpuMemoryResource()};
        if (transaction.commit(receipt, diagnostics))
            std::abort();
        if (diagnostics.find("cannot replace an existing class") == arp::core::pmr::String::npos) {
            std::abort();
        }
    }

    {
        RegistrationTransaction transaction("reflection-test-duplicate");
        {
            RegistrationScope scope(transaction);
            registration::class_<TransactionDuplicateType>("TransactionDuplicateType")
                .property("value", &TransactionDuplicateType::value);
            registration::class_<TransactionDuplicateType>("TransactionDuplicateTypeAgain");
        }
        assert(!transaction.valid());
        RegistrationReceipt receipt;
        arp::core::pmr::String diagnostics{&arp::core::defaultCpuMemoryResource()};
        const bool committed = transaction.commit(receipt, diagnostics);
        if (committed)
            std::abort();
        assert(!committed);
    }
    assert(db::instance().find_class("TransactionDuplicateType") == nullptr);
    assert(db::instance().find_class("TransactionDuplicateTypeAgain") == nullptr);

    {
        RegistrationTransaction transaction("reflection-test-undeclared-reference");
        auto owner = arp::core::makeShared<int>(7);
        {
            RegistrationScope scope(transaction);
            registration::class_<TransactionReferencedOwner>("TransactionReferencedOwner")
                .property("helper", &TransactionReferencedOwner::helper);
        }
        RegistrationReceipt receipt;
        arp::core::pmr::String diagnostics{&arp::core::defaultCpuMemoryResource()};
        if (!transaction.setOwner(owner) || transaction.commit(receipt, diagnostics)) {
            std::abort();
        }
        if (diagnostics.find("undeclared referenced class") == arp::core::pmr::String::npos) {
            std::abort();
        }
        if (db::instance().find_class("TransactionReferencedOwner") != nullptr)
            std::abort();
    }

    {
        RegistrationTransaction transaction("reflection-test-receipt-mismatch");
        auto owner = arp::core::makeShared<int>(9);
        {
            RegistrationScope scope(transaction);
            registration::class_<TransactionReferencedOwner>("TransactionReceiptMismatch");
        }
        RegistrationReceipt receipt;
        arp::core::pmr::String diagnostics{&arp::core::defaultCpuMemoryResource()};
        if (!transaction.setOwner(owner)
            || transaction.commit("sha256:not-the-receipt", receipt, diagnostics)) {
            std::abort();
        }
        if (diagnostics.find("receipt changed after validation") == arp::core::pmr::String::npos) {
            std::abort();
        }
        if (db::instance().find_class("TransactionReceiptMismatch") != nullptr)
            std::abort();
    }

    {
        ReflectionRegistrationContract lhs;
        lhs.moduleIdentity = "contract-order";
        lhs.records.push_back({text("z"), text("last")});
        lhs.records.push_back({text("a"), text("first")});
        ReflectionRegistrationContract rhs;
        rhs.moduleIdentity = "contract-order";
        rhs.records.push_back({text("a"), text("first")});
        rhs.records.push_back({text("z"), text("last")});
        if (lhs.canonicalText() != rhs.canonicalText() || lhs.fingerprint() != rhs.fingerprint()) {
            std::abort();
        }
    }

    {
        RegistrationTransaction transaction("reflection-test-class-contract-mutation");
        transaction.requireDeclaredContracts();
        auto owner = arp::core::makeShared<int>(11);
        {
            RegistrationScope scope(transaction);
            auto cls = registration::class_<TransactionRollbackType>("TransactionContractMutation");
            cls.property("value", &TransactionRollbackType::value);
            if (!declareRegistrationClassContract(
                    "class:TransactionContractMutation", "descriptor", cls.info())) {
                std::abort();
            }
            cls.method("extra", []() {
                return 1;
            });
        }
        RegistrationReceipt receipt;
        arp::core::pmr::String diagnostics{&arp::core::defaultCpuMemoryResource()};
        if (!transaction.setOwner(owner) || transaction.commit(receipt, diagnostics)) {
            std::abort();
        }
        if (diagnostics.find("changed after its generated contract declaration")
            == arp::core::pmr::String::npos) {
            std::abort();
        }
    }

    {
        RegistrationTransaction transaction(
            "reflection-test-semantic-module", "sha256:reflection-test-semantic-artifact");
        transaction.requireDeclaredContracts();
        auto owner = arp::core::makeShared<int>(13);
        {
            RegistrationScope scope(transaction);
            auto cls = registration::class_<TransactionSemanticType>("TransactionSemanticType");
            cls.property("value", &TransactionSemanticType::value);
            if (!declareRegistrationClassContract("class:TransactionSemanticType",
                    "semantic-descriptor",
                    cls.info(),
                    RegistrationOwnership::Engine,
                    RegistrationLifecycle::Explicit,
                    "semantic-export")) {
                std::abort();
            }
        }
        RegistrationReceipt receipt;
        arp::core::pmr::String diagnostics{&arp::core::defaultCpuMemoryResource()};
        if (!transaction.setOwner(owner) || !transaction.commit(receipt, diagnostics)
            || receipt.ownerIdentity != text("sha256:reflection-test-semantic-artifact")
            || receipt.declarations.moduleIdentity != text("reflection-test-semantic-module")
            || receipt.classes.size() != 1u
            || receipt.classes.front().ownership != RegistrationOwnership::Engine
            || receipt.classes.front().lifecycle != RegistrationLifecycle::Explicit
            || receipt.classes.front().moduleExport != text("semantic-export")
            || receipt.reflectionContractFingerprint().empty()) {
            std::abort();
        }
    }
}

void registerReflectedListTypes()
{
    registration::class_<ReflectedListItem>("ReflectedListItem")
        .construct<>()
        .property("value", &ReflectedListItem::value);
    registration::class_<ReflectedListRoot>("ReflectedListRoot")
        .construct<>()
        .property("items", &ReflectedListRoot::items);
}

enum EProperHints {
    EDITOR = 1,
    SERIALIZER = 2,
};

class FixedCallProbe {
public:
    OBJECT_RTTI_IMPL(FixedCallProbe);
    int value = 0;

    int sum(int a, int b, int c, int d, int e) const { return value + a + b + c + d + e; }
};

void fixedCallsPreserveContractsWithoutArgumentAllocations()
{
    registration::class_<FixedCallProbe>("FixedCallProbe")
        .property("value", &FixedCallProbe::value);
    FixedCallProbe probe;
    const object receiver = object::native_reference(&probe);
    const object one(1), two(2), three(3), four(4), five(5), six(6);
    const auto& field = class_of<FixedCallProbe>()->get_field("value").cast<property&>();
    field.set_value(receiver, one);
    assert(field.get_value(receiver).cast<int>() == 1);
    auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
    for (int i = 0; i < 100; ++i) {
        assert(field.set_value(receiver, two).is_undefined());
        assert(probe.value == 2);
    }
    assert(arp::core::defaultCpuAllocator().stats().allocationCount == before);

    const object owned(FixedCallProbe{});
    assert(owned.cast<FixedCallProbe*>() == owned.ptr<FixedCallProbe>());
    assert(class_of<FixedCallProbe>() == type_of<FixedCallProbe>()->class_());
    assert(db::instance().find_class(detail::typeName<FixedCallProbe>())
        == class_of<FixedCallProbe>());
    before = arp::core::defaultCpuAllocator().stats().allocationCount;
    for (int i = 0; i < 100; ++i) {
        assert(class_of<FixedCallProbe>() == type_of<FixedCallProbe>()->class_());
        assert(owned.cast<FixedCallProbe*>() == owned.ptr<FixedCallProbe>());
    }
    assert(arp::core::defaultCpuAllocator().stats().allocationCount == before);

    int invoked = 0;
    function zero("zero", [&invoked]() {
        ++invoked;
    });
    function exact("exact", [&invoked](int a, int b, int c, int d, int e) {
        invoked = a + b + c + d + e;
    });
    zero.call();
    exact.call(one, two, three, four, five);
    before = arp::core::defaultCpuAllocator().stats().allocationCount;
    for (int i = 0; i < 100; ++i) {
        zero.call();
        exact.call(one, two, three, four, five);
        assert(invoked == 15);
    }
    assert(arp::core::defaultCpuAllocator().stats().allocationCount == before);
    assert(exact.call(one).is_undefined() && invoked == 15);
    assert(exact.call(one, two, three, four, five, six).is_undefined() && invoked == 15);

    function member("member", &FixedCallProbe::sum);
    assert(member.call(receiver, one, two, three, four, five).cast<int>() == 17);
    assert(member.call().is_undefined());
    assert(member.call(receiver, one).is_undefined());
    member.set_default_arguments(three, four, five);
    assert(member.call(receiver, one, two).cast<int>() == 17);
    function large("large", [](int a, int b, int c, int d, int e, int f) {
        return a + b + c + d + e + f;
    });
    assert(large.call(one, two, three, four, five, six).cast<int>() == 21);
    large.set_default_arguments(two, three, four, five, six);
    assert(large.call(one).cast<int>() == 21);

    receiver.invalidate_native_reference();
    assert(member.call(receiver, one, two, three, four, five).is_undefined());
    assert(field.set_value(receiver, one).is_undefined() && probe.value == 2);
}

void specialMemberReplacementReleasesOldBindings()
{
    class_info probe("special-replacement-contract");
    for (const auto* name : {"__new__", "__init__", "__del__", "__getitem__", "__setitem__"}) {
        std::weak_ptr<int> previous;
        {
            auto marker = arp::core::makeShared<int>(17);
            previous = marker;
            probe.def(name, object(function(name, [marker]() {
                return *marker;
            })));
        }
        assert(!previous.expired());
        probe.def(name, object::undefined());
        assert(previous.expired());
    }
    probe.def("__new__", object(function("__new__", []() {
        return 11;
    })));
    assert(probe.new_object().cast<int>() == 11);
    probe.def("__new__", object(function("__new__", []() {
        return 22;
    })));
    assert(probe.new_object().cast<int>() == 22);
    assert(probe.get_field("__new__").cast<function&>().call().cast<int>() == 22);
}

} // namespace

int main()
{
    specialMemberReplacementReleasesOldBindings();
    readsEnumValuesThroughTypeErasedReflection();
    exactPayloadStorageProof();
    (void)db::instance();
    fixedCallsPreserveContractsWithoutArgumentAllocations();
    {
        const object one(1), two(2), wide(std::int64_t(2));
        assert(one < two && !(two < one) && one == object(1));
        // The mixed-storage path preserves the reflected conversion contract.
        assert((one < wide) == one.class_()->call(one, "__lt__", wide).cast<bool>());
        hdict keys{&arp::core::defaultCpuMemoryResource()};
        for (int i = 0; i < 4; ++i)
            keys.emplace(object(i), object(i + 1));
        const object key(2);
        (void)keys.find(key);
        const auto before = arp::core::defaultCpuAllocator().stats();
        for (int i = 0; i < 100; ++i)
            assert(keys.find(key)->second.cast<int>() == 3);
        const auto after = arp::core::defaultCpuAllocator().stats();
        assert(after.allocationCount == before.allocationCount);
        const object a(text("a")), b(text("b"));
        assert(a < b && !(b < a) && a == object(text("a")));
        const object yes(true), no(false);
        assert(no < yes && !(yes < no));
    }
    pmrValueCopyPreservesSourceAllocator();
    {
        function callable("parameter-count", [](int x, int y) {
            return x + y;
        });
        assert(callable.parameter_count() == callable.params_info().size());
        const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
        for (int i = 0; i < 100; ++i)
            assert(callable.parameter_count() == 2);
        assert(arp::core::defaultCpuAllocator().stats().allocationCount == before);
    }
    verifiesCanonicalRegistrationTypeContracts();
    verifiesCompiledNativeTypeBindings();
    verifiesNativeReferenceCompletion();
    verifiesGeneratedSchemaReuse();
    verifiesGeneratedNativeDependencyIdentity();
    verifiesRegistrationTransactions();
    registerReflectedListTypes();

    const auto listResult = deserialize_from_yaml("type: ReflectedListRoot\n"
                                                  "fields:\n"
                                                  "  items:\n"
                                                  "    - type: ReflectedListItem\n"
                                                  "      fields:\n"
                                                  "        value: 7\n",
        {SerializationProfile::LegacyAllPropertiesV1});
    assert(listResult.success);
    auto* listRoot = listResult.value.cast<ReflectedListRoot*>();
    assert(listRoot != nullptr);
    assert(listRoot->items.size() == 1u);
    assert(listRoot->items[0].is<ReflectedListItem>());
    auto* listItem = listRoot->items[0].cast<ReflectedListItem*>();
    assert(listItem != nullptr);
    assert(listItem->value == 7);

    registration::class_<B>().method("print2", &B::print2).property("a_", &B::a_, "");

    registration::class_<A>("A")
        .inherit<B>()
        .construct<int>()
        .destruct()
        .method("print", &A::print)
        .method("test", &A::test)
        .method("add", &A::add, 1)
        .property("value_", &A::value_, "This is class A's property value_", EDITOR)
        .constant<EProperHints>("EProperHints",
            {
                enum_value("EDITOR", EDITOR),
                enum_value("SERIALIZER", SERIALIZER),
            });

    object inst = class_of<A>()->new_object(variant(10));
    assert(inst.cast<A*>()->value_ == 10);

    // Recreating world/plugin scopes must reuse the same inheritance edge.
    // This once grew both process-owned vectors on every Survivors restart.
    const auto inheritanceBytes = arp::core::defaultCpuAllocator().stats().liveBytes;
    for (int attempt = 0; attempt < 64; ++attempt)
        registration::class_<A>().inherit<B>();
    assert(class_of<A>()->bases().size() == 1u);
    assert(arp::core::defaultCpuAllocator().stats().liveBytes == inheritanceBytes);
    assert(
        class_of<A>()->up_cast(class_of<B>(), inst.cast<A*>()) == static_cast<B*>(inst.cast<A*>()));
    bool conflictingCastRejected = false;
    try {
        class_of<A>()->inherit(
            class_of<B>(), +[](void* pointer) -> void* {
                return pointer;
            });
    } catch (const std::invalid_argument& error) {
        conflictingCastRejected =
            std::string_view(error.what()) == "ARP-REFLECTION-BASE-CAST-CONFLICT";
    }
    assert(conflictingCastRejected && class_of<A>()->bases().size() == 1u);
    assert(
        class_of<A>()->up_cast(class_of<B>(), inst.cast<A*>()) == static_cast<B*>(inst.cast<A*>()));

    inst.call("print", text("hello"));
    assert(inst.cast<A*>()->lastPrint_ == text("hello"));

    inst.call("print2");
    assert(inst.cast<A*>()->printed2_);

    inst.call("test");
    assert(A::staticCalls_ == 1);

    assert(inst["value_"].cast<int>() == 10);
    assert(inst.call("add", 1).cast<int>() == 2);
    assert(inst.call("add", 1, 2).cast<int>() == 3);

    auto enumObject = inst["EProperHints"].cast<enumeration&>();
    assert(enumObject.get_enum_value("EDITOR") == EDITOR);
    assert(enumObject.get_enum_value("SERIALIZER") == SERIALIZER);

    object dict = class_of<hdict>()->new_object();
    dict.set("test", object(text("hello")));
    assert(dict["test"].cast<arp::core::pmr::String>() == text("hello"));

    object list = class_of<hlist>()->new_object();
    list.call("append", text("test"));
    list.call("append", 123);
    assert(list.call("length").cast<int>() == 2);
    assert(list.call("pop").cast<int>() == 123);
    assert(list.call("pop").cast<arp::core::pmr::String>() == text("test"));

    object numericText(text("100"));
    assert(numericText.cast<int>() == 100);

    assert(inst.class_()->get_field("value_").cast<property&>().check_hints(EDITOR));
    assert(!inst.class_()->get_field("value_").cast<property&>().check_hints(SERIALIZER));

    assert(variant(100).type()->is_type(type_trait_mask::is_arithmetic));
    assert(type_of<std::vector<int>>()->is_sequential_container());
    assert(type_of<std::shared_ptr<int>>()->is_wrapper());
    assert(type_of<std::unique_ptr<int>>()->is_wrapper());
    assert(type_of<std::weak_ptr<int>>()->is_wrapper());
    assert(type_of<int>()->is<int>());
    assert(!type_of<int>()->is<int&>());
    assert(type_of<int&>()->is<int&>());

    assert(!inst.is_base_of(class_of<B>()));
    assert(inst.is_derived_from(class_of<B>()));

    void* ptr = nullptr;
    const bool allocated = class_of<A>()->malloc(ptr);
    assert(allocated);
    if (!allocated || ptr == nullptr)
        return 1;
    class_of<A>()->construct(ptr, 200);
    assert(static_cast<A*>(ptr)->value_ == 200);
    class_of<A>()->destruct(ptr);
    class_of<A>()->free(ptr);

    inst.set("a_", object(true));
    assert(inst["a_"].cast<bool>());
    assert(inst["a_"].cast<int>() == 1);

    instance dynamicInstance(class_of<A>());
    dynamicInstance.slot("test1") = object(11);
    assert(dynamicInstance["test1"].cast<int>() == 11);

    // Stock category dispatch keeps exact storage and reflection boundaries.
    {
        const object a(text(std::string_view("a\0b", 3))), b(text(std::string_view("a\0c", 3)));
        assert(a < b && a == object(text(std::string_view("a\0b", 3))));
        auto* strings = class_of<arp::core::pmr::String>();
        const object originalLess = strings->get_field("__lt__");
        const object originalEqual = strings->get_field("__eq__");
        strings->def("__lt__",
            object(function("__lt__",
                [](const arp::core::pmr::String& left, const arp::core::pmr::String& right) {
                    return left > right;
                })));
        assert(b < a && !(a < b));
        assert(a == object(text(std::string_view("a\0b", 3))));
        strings->def("__eq__",
            object(function(
                "__eq__", [](const arp::core::pmr::String&, const arp::core::pmr::String&) {
                    return false;
                })));
        assert(!(a == a));
        strings->def("__lt__", originalLess);
        strings->def("__eq__", originalEqual);
        assert(a < b && a == a);
        const object wide(std::int64_t(7)), narrow(7);
        assert((wide == narrow) == wide.class_()->call(wide, "__eq__", narrow).cast<bool>());
        assert((wide < narrow) == wide.class_()->call(wide, "__lt__", narrow).cast<bool>());
    }

    // Method replacement must disable the builtin key comparison shortcut.
    auto* integers = class_of<int>();
    assert(integers->hasBuiltinOrdering());
    const object originalLess = integers->get_field("__lt__");
    integers->def("__lt__", object(function("__lt__", [](int left, int right) {
        return left > right;
    })));
    assert(!integers->hasBuiltinOrdering());
    assert(object(2) < object(1) && !(object(1) < object(2)));
    integers->def("__lt__", originalLess);
    assert(object(1) < object(2));

    return 0;
}

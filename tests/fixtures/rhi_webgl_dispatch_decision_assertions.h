// Real production Decision types only; no native success claims.
#pragma once
#include <cassert>
#include <cstdio>
#include <type_traits>
#include <utility>

namespace dispatch_test {
template <class T, class = void> struct HasDiagnosticId : std::false_type {};

template <class T>
struct HasDiagnosticId<T, std::void_t<decltype(std::declval<T>().diagnosticId)>> : std::true_type {
};

template <class T, class = void> struct HasDiagnostic : std::false_type {};

template <class T>
struct HasDiagnostic<T, std::void_t<decltype(std::declval<T>().diagnostic)>> : std::true_type {};

template <class T, class = void> struct HasMode : std::false_type {};

template <class T>
struct HasMode<T, std::void_t<decltype(std::declval<T>().mode)>> : std::true_type {};

template <class T, class = void> struct HasDisposition : std::false_type {};

template <class T>
struct HasDisposition<T, std::void_t<decltype(std::declval<T>().disposition)>> : std::true_type {};

template <class T, class = void> struct HasVersion : std::false_type {};

template <class T>
struct HasVersion<T, std::void_t<decltype(std::declval<T>().contractVersion)>> : std::true_type {};

#define DISPATCH_BOOL_FIELD(name)                                                                  \
    template <class T, class = void> struct Field_##name {                                         \
        static void visit(const T&, bool, bool) {}                                                 \
    };                                                                                             \
    template <class T> struct Field_##name<T, std::void_t<decltype(std::declval<T>().name)>> {     \
        static void visit(const T& value, bool denied, bool snapshot)                              \
        {                                                                                          \
            static_assert(std::is_same_v<std::decay_t<decltype(value.name)>, bool>);               \
            if (snapshot)                                                                          \
                std::printf(" " #name "=%d", value.name);                                          \
            if (denied && value.name)                                                              \
                std::fprintf(stderr, "unexpected permission " #name "\n");                         \
            assert(!denied || !value.name);                                                        \
        }                                                                                          \
    };
DISPATCH_BOOL_FIELD(acceptanceAvailable)
DISPATCH_BOOL_FIELD(acceptanceEligible)
DISPATCH_BOOL_FIELD(admissionAvailable)
DISPATCH_BOOL_FIELD(associationAvailable)
DISPATCH_BOOL_FIELD(attached_)
DISPATCH_BOOL_FIELD(callbacksIssued_)
DISPATCH_BOOL_FIELD(canceled)
DISPATCH_BOOL_FIELD(coldGraphImplemented)
DISPATCH_BOOL_FIELD(coldJoinAvailable)
DISPATCH_BOOL_FIELD(coldStorageAvailable)
DISPATCH_BOOL_FIELD(completionAvailable)
DISPATCH_BOOL_FIELD(completionCommitAvailable)
DISPATCH_BOOL_FIELD(compute)
DISPATCH_BOOL_FIELD(connectionAvailable)
DISPATCH_BOOL_FIELD(consumerIssued)
DISPATCH_BOOL_FIELD(contentExecutionAvailable)
DISPATCH_BOOL_FIELD(copyAvailable)
DISPATCH_BOOL_FIELD(cpuMetadataImplemented)
DISPATCH_BOOL_FIELD(creationAvailable)
DISPATCH_BOOL_FIELD(creationEligible)
DISPATCH_BOOL_FIELD(creationIssued_)
DISPATCH_BOOL_FIELD(currentMetadataImplemented)
DISPATCH_BOOL_FIELD(custodyAvailable)
DISPATCH_BOOL_FIELD(declarationImplemented)
DISPATCH_BOOL_FIELD(emissionAvailable)
DISPATCH_BOOL_FIELD(executable)
DISPATCH_BOOL_FIELD(executionAvailable)
DISPATCH_BOOL_FIELD(factoryAvailable)
DISPATCH_BOOL_FIELD(factoryEligible)
DISPATCH_BOOL_FIELD(freezeAvailable)
DISPATCH_BOOL_FIELD(grantsExecution)
DISPATCH_BOOL_FIELD(implemented)
DISPATCH_BOOL_FIELD(initializationAvailable)
DISPATCH_BOOL_FIELD(interfaceInspectionAvailable)
DISPATCH_BOOL_FIELD(issuanceComplete)
DISPATCH_BOOL_FIELD(issued)
DISPATCH_BOOL_FIELD(issuerStorageAvailable)
DISPATCH_BOOL_FIELD(mapAvailable)
DISPATCH_BOOL_FIELD(mappingAvailable)
DISPATCH_BOOL_FIELD(matched)
DISPATCH_BOOL_FIELD(mayWrite)
DISPATCH_BOOL_FIELD(metadata)
DISPATCH_BOOL_FIELD(native)
DISPATCH_BOOL_FIELD(nativeBindingImplemented)
DISPATCH_BOOL_FIELD(nativeConnectionAvailable)
DISPATCH_BOOL_FIELD(nativeCorrespondenceAvailable)
DISPATCH_BOOL_FIELD(nativeCreationImplemented)
DISPATCH_BOOL_FIELD(nativeExecutionAvailable)
DISPATCH_BOOL_FIELD(nativeExecutionImplemented)
DISPATCH_BOOL_FIELD(nativeFactBindingImplemented)
DISPATCH_BOOL_FIELD(nativeLossObservationAvailable)
DISPATCH_BOOL_FIELD(nativeMappingImplemented)
DISPATCH_BOOL_FIELD(nativeProjectionAvailable)
DISPATCH_BOOL_FIELD(nativeSourceImplemented)
DISPATCH_BOOL_FIELD(nativeSyncAvailable)
DISPATCH_BOOL_FIELD(observerPreparationAvailable)
DISPATCH_BOOL_FIELD(payloadAvailable)
DISPATCH_BOOL_FIELD(physicalLoss)
DISPATCH_BOOL_FIELD(positionalAdapterAvailable)
DISPATCH_BOOL_FIELD(positionalAvailable)
DISPATCH_BOOL_FIELD(preflightEligible)
DISPATCH_BOOL_FIELD(preparationAvailable)
DISPATCH_BOOL_FIELD(preparationIssued)
DISPATCH_BOOL_FIELD(privateAssociationAvailable)
DISPATCH_BOOL_FIELD(privateBudgetMappingImplemented)
DISPATCH_BOOL_FIELD(privateInitializationAvailable)
DISPATCH_BOOL_FIELD(privateInputValidationAvailable)
DISPATCH_BOOL_FIELD(privateJoinAvailable)
DISPATCH_BOOL_FIELD(privateStagingJoinAvailable)
DISPATCH_BOOL_FIELD(profilePairingAvailable)
DISPATCH_BOOL_FIELD(projectionAvailable)
DISPATCH_BOOL_FIELD(prototype)
DISPATCH_BOOL_FIELD(prototypeCold)
DISPATCH_BOOL_FIELD(prototypeColdBindings)
DISPATCH_BOOL_FIELD(prototypeColdIntent)
DISPATCH_BOOL_FIELD(prototypeColdResources)
DISPATCH_BOOL_FIELD(prototypeOptIn)
DISPATCH_BOOL_FIELD(prototypeStorage)
DISPATCH_BOOL_FIELD(prototypeUpload)
DISPATCH_BOOL_FIELD(publicExecutionAvailable)
DISPATCH_BOOL_FIELD(publicationAvailable)
DISPATCH_BOOL_FIELD(publishedConsumerImplemented)
DISPATCH_BOOL_FIELD(published_)
DISPATCH_BOOL_FIELD(queueObserved)
DISPATCH_BOOL_FIELD(ready)
DISPATCH_BOOL_FIELD(recordingAvailable)
DISPATCH_BOOL_FIELD(recordingIssuerAvailable)
DISPATCH_BOOL_FIELD(requiresStorageFormatSupport)
DISPATCH_BOOL_FIELD(reservationImplemented)
DISPATCH_BOOL_FIELD(resourceClosureAvailable)
DISPATCH_BOOL_FIELD(scopeStackUnproven)
DISPATCH_BOOL_FIELD(sealedIssuerAvailable)
DISPATCH_BOOL_FIELD(shaderExecutionAvailable)
DISPATCH_BOOL_FIELD(shutdownRequested_)
DISPATCH_BOOL_FIELD(sinkImplemented)
DISPATCH_BOOL_FIELD(sourceAdapterAvailable)
DISPATCH_BOOL_FIELD(sourceAvailable)
DISPATCH_BOOL_FIELD(started)
DISPATCH_BOOL_FIELD(submissionAvailable)
DISPATCH_BOOL_FIELD(success)
DISPATCH_BOOL_FIELD(suppressed)
DISPATCH_BOOL_FIELD(unmapIssued_)
DISPATCH_BOOL_FIELD(validatorOnly)
#undef DISPATCH_BOOL_FIELD

template <class T> void verify(const char* route, const T& value, bool denied, bool snapshot)
{
    // The unused historical descriptor-source projection has a frozen four-field
    // layout and a failure mode. Finite source consumers use separate diagnosed
    // decisions; do not add an ABI field to this metadata-only projection.
    static_assert(HasDiagnostic<T>::value || HasDiagnosticId<T>::value
        || std::is_same_v<T,
            arp::rhi::semantics::OriginalCommandDescriptorSourceLowering::Decision>);
    if constexpr (HasDiagnosticId<T>::value) {
        assert(!denied || (value.diagnosticId && *value.diagnosticId));
    }
    if (snapshot)
        std::printf("%s", route);
    if constexpr (HasDiagnostic<T>::value) {
        if (snapshot)
            std::printf(" diagnostic=%s", value.diagnostic ? value.diagnostic : "<null>");
        if (denied && (!value.diagnostic || !*value.diagnostic))
            std::fprintf(stderr, "%s missing diagnostic\n", route);
        assert(!denied || (value.diagnostic && *value.diagnostic));
    }
    if constexpr (HasMode<T>::value) {
        if (snapshot)
            std::printf(" mode=%d", static_cast<int>(value.mode));
        assert(!denied || value.mode == arp::rhi::CapabilityMode::DiagnosticFailure);
    }
    if constexpr (HasDisposition<T>::value) {
        if (snapshot)
            std::printf(" disposition=%d", static_cast<int>(value.disposition));
        assert(!denied || value.disposition == decltype(value.disposition)::DiagnosticFailure);
    }
    if constexpr (HasVersion<T>::value)
        if (snapshot)
            std::printf(" version=%u", value.contractVersion);
    Field_acceptanceAvailable<T>::visit(value, denied, snapshot);
    Field_acceptanceEligible<T>::visit(value, denied, snapshot);
    Field_admissionAvailable<T>::visit(value, denied, snapshot);
    Field_associationAvailable<T>::visit(value, denied, snapshot);
    Field_attached_<T>::visit(value, denied, snapshot);
    Field_callbacksIssued_<T>::visit(value, denied, snapshot);
    Field_canceled<T>::visit(value, denied, snapshot);
    Field_coldGraphImplemented<T>::visit(value, denied, snapshot);
    Field_coldJoinAvailable<T>::visit(value, denied, snapshot);
    Field_coldStorageAvailable<T>::visit(value, denied, snapshot);
    Field_completionAvailable<T>::visit(value, denied, snapshot);
    Field_completionCommitAvailable<T>::visit(value, denied, snapshot);
    Field_compute<T>::visit(value, denied, snapshot);
    Field_connectionAvailable<T>::visit(value, denied, snapshot);
    Field_consumerIssued<T>::visit(value, denied, snapshot);
    Field_contentExecutionAvailable<T>::visit(value, denied, snapshot);
    Field_copyAvailable<T>::visit(value, denied, snapshot);
    Field_cpuMetadataImplemented<T>::visit(value, denied, snapshot);
    Field_creationAvailable<T>::visit(value, denied, snapshot);
    Field_creationEligible<T>::visit(value, denied, snapshot);
    Field_creationIssued_<T>::visit(value, denied, snapshot);
    Field_currentMetadataImplemented<T>::visit(value, denied, snapshot);
    Field_custodyAvailable<T>::visit(value, denied, snapshot);
    Field_declarationImplemented<T>::visit(value, denied, snapshot);
    Field_emissionAvailable<T>::visit(value, denied, snapshot);
    Field_executable<T>::visit(value, denied, snapshot);
    Field_executionAvailable<T>::visit(value, denied, snapshot);
    Field_factoryAvailable<T>::visit(value, denied, snapshot);
    Field_factoryEligible<T>::visit(value, denied, snapshot);
    Field_freezeAvailable<T>::visit(value, denied, snapshot);
    Field_grantsExecution<T>::visit(value, denied, snapshot);
    Field_implemented<T>::visit(value, denied, snapshot);
    Field_initializationAvailable<T>::visit(value, denied, snapshot);
    Field_interfaceInspectionAvailable<T>::visit(value, denied, snapshot);
    Field_issuanceComplete<T>::visit(value, denied, snapshot);
    Field_issued<T>::visit(value, denied, snapshot);
    Field_issuerStorageAvailable<T>::visit(value, denied, snapshot);
    Field_mapAvailable<T>::visit(value, denied, snapshot);
    Field_mappingAvailable<T>::visit(value, denied, snapshot);
    Field_matched<T>::visit(value, denied, snapshot);
    Field_mayWrite<T>::visit(value, denied, snapshot);
    Field_metadata<T>::visit(value, denied, snapshot);
    Field_native<T>::visit(value, denied, snapshot);
    Field_nativeBindingImplemented<T>::visit(value, denied, snapshot);
    Field_nativeConnectionAvailable<T>::visit(value, denied, snapshot);
    Field_nativeCorrespondenceAvailable<T>::visit(value, denied, snapshot);
    Field_nativeCreationImplemented<T>::visit(value, denied, snapshot);
    Field_nativeExecutionAvailable<T>::visit(value, denied, snapshot);
    Field_nativeExecutionImplemented<T>::visit(value, denied, snapshot);
    Field_nativeFactBindingImplemented<T>::visit(value, denied, snapshot);
    Field_nativeLossObservationAvailable<T>::visit(value, denied, snapshot);
    Field_nativeMappingImplemented<T>::visit(value, denied, snapshot);
    Field_nativeProjectionAvailable<T>::visit(value, denied, snapshot);
    Field_nativeSourceImplemented<T>::visit(value, denied, snapshot);
    Field_nativeSyncAvailable<T>::visit(value, denied, snapshot);
    Field_observerPreparationAvailable<T>::visit(value, denied, snapshot);
    Field_payloadAvailable<T>::visit(value, denied, snapshot);
    Field_physicalLoss<T>::visit(value, denied, snapshot);
    Field_positionalAdapterAvailable<T>::visit(value, denied, snapshot);
    Field_positionalAvailable<T>::visit(value, denied, snapshot);
    Field_preflightEligible<T>::visit(value, denied, snapshot);
    Field_preparationAvailable<T>::visit(value, denied, snapshot);
    Field_preparationIssued<T>::visit(value, denied, snapshot);
    Field_privateAssociationAvailable<T>::visit(value, denied, snapshot);
    Field_privateBudgetMappingImplemented<T>::visit(value, denied, snapshot);
    Field_privateInitializationAvailable<T>::visit(value, denied, snapshot);
    Field_privateInputValidationAvailable<T>::visit(value, denied, snapshot);
    Field_privateJoinAvailable<T>::visit(value, denied, snapshot);
    Field_privateStagingJoinAvailable<T>::visit(value, denied, snapshot);
    Field_profilePairingAvailable<T>::visit(value, denied, snapshot);
    Field_projectionAvailable<T>::visit(value, denied, snapshot);
    Field_prototype<T>::visit(value, denied, snapshot);
    Field_prototypeCold<T>::visit(value, denied, snapshot);
    Field_prototypeColdBindings<T>::visit(value, denied, snapshot);
    Field_prototypeColdIntent<T>::visit(value, denied, snapshot);
    Field_prototypeColdResources<T>::visit(value, denied, snapshot);
    Field_prototypeOptIn<T>::visit(value, denied, snapshot);
    Field_prototypeStorage<T>::visit(value, denied, snapshot);
    Field_prototypeUpload<T>::visit(value, denied, snapshot);
    Field_publicExecutionAvailable<T>::visit(value, denied, snapshot);
    Field_publicationAvailable<T>::visit(value, denied, snapshot);
    Field_publishedConsumerImplemented<T>::visit(value, denied, snapshot);
    Field_published_<T>::visit(value, denied, snapshot);
    Field_queueObserved<T>::visit(value, denied, snapshot);
    Field_ready<T>::visit(value, denied, snapshot);
    Field_recordingAvailable<T>::visit(value, denied, snapshot);
    Field_recordingIssuerAvailable<T>::visit(value, denied, snapshot);
    Field_requiresStorageFormatSupport<T>::visit(value, denied, snapshot);
    Field_reservationImplemented<T>::visit(value, denied, snapshot);
    Field_resourceClosureAvailable<T>::visit(value, denied, snapshot);
    Field_scopeStackUnproven<T>::visit(value, denied, snapshot);
    Field_sealedIssuerAvailable<T>::visit(value, denied, snapshot);
    Field_shaderExecutionAvailable<T>::visit(value, denied, snapshot);
    Field_shutdownRequested_<T>::visit(value, denied, snapshot);
    Field_sinkImplemented<T>::visit(value, denied, snapshot);
    Field_sourceAdapterAvailable<T>::visit(value, denied, snapshot);
    Field_sourceAvailable<T>::visit(value, denied, snapshot);
    Field_started<T>::visit(value, denied, snapshot);
    Field_submissionAvailable<T>::visit(value, denied, snapshot);
    Field_success<T>::visit(value, denied, snapshot);
    Field_suppressed<T>::visit(value, denied, snapshot);
    Field_unmapIssued_<T>::visit(value, denied, snapshot);
    Field_validatorOnly<T>::visit(value, denied, snapshot);
    if (snapshot)
        std::puts("");
}
} // namespace dispatch_test

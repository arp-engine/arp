#include "ui/ui.h"

#include <cassert>

namespace {

bool hasCapability(const arp::ui::UiFrameworkContract& contract,
    arp::ui::UiFrameworkCapability capability,
    arp::ui::UiFrameworkStatusLabel status)
{
    const auto* record = contract.findCapability(capability);
    return record != nullptr && record->status == status;
}

void exposes_explicit_framework_completeness_capabilities_and_status_labels()
{
    const auto contract = arp::ui::UiFrameworkContract::contractComplete();

    assert(contract.status() == arp::ui::UiFrameworkStatusLabel::ContractComplete);
    assert(hasCapability(contract,
        arp::ui::UiFrameworkCapability::Layout,
        arp::ui::UiFrameworkStatusLabel::ContractComplete));
    assert(hasCapability(contract,
        arp::ui::UiFrameworkCapability::Style,
        arp::ui::UiFrameworkStatusLabel::ContractComplete));
    assert(hasCapability(contract,
        arp::ui::UiFrameworkCapability::Interaction,
        arp::ui::UiFrameworkStatusLabel::ContractComplete));
    assert(hasCapability(contract,
        arp::ui::UiFrameworkCapability::Text,
        arp::ui::UiFrameworkStatusLabel::ContractComplete));
    assert(hasCapability(contract,
        arp::ui::UiFrameworkCapability::Image,
        arp::ui::UiFrameworkStatusLabel::ContractComplete));
    assert(hasCapability(contract,
        arp::ui::UiFrameworkCapability::ResourceBinding,
        arp::ui::UiFrameworkStatusLabel::ContractComplete));
    assert(hasCapability(contract,
        arp::ui::UiFrameworkCapability::RenderExtraction,
        arp::ui::UiFrameworkStatusLabel::ContractComplete));
    assert(hasCapability(contract,
        arp::ui::UiFrameworkCapability::Diagnostics,
        arp::ui::UiFrameworkStatusLabel::ContractComplete));
    assert(hasCapability(contract,
        arp::ui::UiFrameworkCapability::HotPathConstraints,
        arp::ui::UiFrameworkStatusLabel::ContractComplete));

    assert(!contract.allowsRuntimeDesignSourceParsing());
    assert(!contract.exposesNativeBackendHandles());
    assert(!contract.allowsPerFrameSourceImport());
    assert(!contract.allowsPerFrameShaderCompilation());
    assert(contract.declaresBoundedRuntimeAllocation());

    assert(arp::ui::statusLabelName(arp::ui::UiFrameworkStatusLabel::ContractComplete)
        == arp::core::pmr::String("Contract Complete", &arp::core::defaultCpuMemoryResource()));
    assert(arp::ui::statusLabelName(arp::ui::UiFrameworkStatusLabel::PrototypeComplete)
        == arp::core::pmr::String("Prototype Complete", &arp::core::defaultCpuMemoryResource()));
    assert(arp::ui::statusLabelName(arp::ui::UiFrameworkStatusLabel::BackendValidated)
        == arp::core::pmr::String("Backend Validated", &arp::core::defaultCpuMemoryResource()));
    assert(arp::ui::statusLabelName(arp::ui::UiFrameworkStatusLabel::PlatformValidated)
        == arp::core::pmr::String("Platform Validated", &arp::core::defaultCpuMemoryResource()));
    assert(arp::ui::statusLabelName(arp::ui::UiFrameworkStatusLabel::ProductionReady)
        == arp::core::pmr::String("Production Ready", &arp::core::defaultCpuMemoryResource()));
}

void validates_missing_framework_capability_deterministically()
{
    arp::ui::UiFrameworkContract contract;
    contract.setStatus(arp::ui::UiFrameworkStatusLabel::ContractComplete);
    contract.addCapability({
        arp::ui::UiFrameworkCapability::Layout,
        arp::ui::UiFrameworkStatusLabel::ContractComplete,
        arp::core::pmr::String("layout-v1", &arp::core::defaultCpuMemoryResource()),
    });

    const auto result = arp::ui::UiFrameworkContractValidator().validate(contract);
    assert(!result.ok());
    assert(result.diagnostic().code == arp::ui::UiDiagnosticCode::MissingFrameworkCapability);
}

void validates_forbidden_hot_path_and_backend_boundary_flags()
{
    auto contract = arp::ui::UiFrameworkContract::contractComplete();
    contract.setAllowsRuntimeDesignSourceParsingForTest(true);
    auto result = arp::ui::UiFrameworkContractValidator().validate(contract);
    assert(!result.ok());
    assert(result.diagnostic().code == arp::ui::UiDiagnosticCode::InvalidFrameworkBoundary);

    contract = arp::ui::UiFrameworkContract::contractComplete();
    contract.setAllowsPerFrameShaderCompilationForTest(true);
    result = arp::ui::UiFrameworkContractValidator().validate(contract);
    assert(!result.ok());
    assert(result.diagnostic().code == arp::ui::UiDiagnosticCode::InvalidHotPathConstraint);
}

} // namespace

int main()
{
    exposes_explicit_framework_completeness_capabilities_and_status_labels();
    validates_missing_framework_capability_deterministically();
    validates_forbidden_hot_path_and_backend_boundary_flags();
    return 0;
}

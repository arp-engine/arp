# Resource Manager Model Governance

Status: Prototype Complete for the UI/Pika resource graph migration.
Production Ready is not claimed.

## Ownership Model

ResourceBase ownership is the typed runtime ownership model for new resources.
`ResourceBase` ownership is the source of truth for typed runtime resources.
Concrete resources carry identity, alias, status, display name, and explicit
dependencies through their `ResourceReference` and `ResourceBase::dependencies`.
After a typed load succeeds, `ResourceRecord::ownedResource` owns the ready
resource object and `ResourceHandle<T>` resolves from that typed storage first.

Resource status text is parsed through the shared typed `ResourceStatus`
contract. Unknown labels, status labels disallowed for a resource family, and
invalid status promotions produce deterministic diagnostics. Production
resource loaders must not maintain private free-form status parsers.

Canonical resource type and contract-version pairs are declared once. A
contract redirect is valid only when source and target metadata are explicit,
the resource type is stable, and the redirect is labelled `Legacy fallback`.
Dotted and hyphenated spellings are not interchangeable by convention.

`ResourceRecord` remains manager-owned state and cache. It mirrors metadata
from `ResourceBase`, tracks load state and diagnostics, and stores aliases for
deterministic lookup. It must not invent dependencies independently from the
typed resource or graph descriptor.

## Load Paths

The typed synchronous load path is
`ResourceManager::load<TResource>(ResourceLoadContext, Args...)`. It constructs
the concrete resource with `core::makeUnique`, upcasts to `ResourceBase`, and
uses the shared `loadResource` lifecycle path.

The typed asynchronous load path is
`ResourceManager::loadAsync<TResource>(WorkService&, ResourceAsyncLoadCompletion,
ResourceLoadContext, Args...)`. It submits one bounded work item and reuses the
same `loadResource` ownership and admission path as the synchronous API.

deterministic graph loading is expressed through `ResourceGraphRequest`.
Each graph node explicitly provides a `ResourceBase` descriptor and dependency
ids. The manager validates duplicate ids, duplicate aliases, missing dependency
nodes, dependency cycles, invalid UUIDs, and unsupported `Legacy fallback`
status before topological loading. No filename inference, directory scanning,
guessed fallback, or silent compatibility path is part of graph loading.

Asynchronous staged graph execution is owned by `ResourceGraphExecutor`.
It validates and topologically orders explicit graph nodes, acquires declared
file sources through `FileService`, performs YAML/image processing on
`WorkService`, admits typed resources, and publishes the final result through
`CompletionQueue`. `ResourceLoadContext::requirePreloadedSources` prevents a
file-backed resource from falling back to a blocking file helper on this path.
Node count, file-read count, source bytes, decoded bytes, and completion
dispatch budget are explicit. Cancellation, queue rejection, service shutdown,
dependency failure, bytes read/decoded, stage order, duration, and exactly-once
completion are observable telemetry.

## Reflection Compatibility

Reflection compatibility remains only for serialized legacy envelopes.
`loadObjectFromEnvelope`, `loadFromEnvelope`, `ResourceRecord::value`, and
`ResourceRecord::ownedObjects` are compatibility storage for existing reflected
payloads, not the lifecycle owner for new UI/Pika/renderer production
resources.

Deletion criteria: remove the reflection compatibility fields and envelope
admission helpers only after prefab/internal package resource families migrate
to typed `ResourceBase` descriptors or an explicit serialization adapter with
equivalent contract tests.

## Reload Generation Governance

Reload preserves the original generation on failure: `reloadResource` captures a
snapshot of the old record, attempts the new load, and rolls back to the
snapshot if the load fails or the content hash is unchanged. The generation
number advances only on successful commits with different content.

`ResourceReloadTelemetry` reports `generation`, `oldContentHash`,
`newContentHash`, `committed`, `rolledBack`, `retiredCount`, and invalidated
dependents. No partial ready generation is published: the resource remains in
its prior state or transitions to `Failed`, never `Ready` with incomplete data.

## Budget And Telemetry Governance

`ResourceBudgetConfig` defines explicit limits for graph nodes, decoded bytes,
upload bytes, glyph pages, reload generations, retained recovery bytes,
completion dispatch count, and diagnostics length. A limit of zero means
unbounded for that dimension.

Budget exhaustion produces deterministic `ResourceLoadResult` or
`ResourceReloadResult` diagnostics and increments `budgetExhaustionEvents_`.
Never an unbounded allocation or silent partial result.

The manager enforces graph-node and completion-dispatch budgets during graph
planning, propagates decoded-byte budgets into typed load contexts, enforces
reload-generation and retained-recovery-byte budgets during reload, and bounds
returned diagnostic payloads. Upload-byte and glyph-page budgets are enforced
by the renderer/UI runtime contracts that own those resources.

`ResourceManagerTelemetry` exposes a stable snapshot of the current budget
configuration and the cumulative exhaustion event counter. The snapshot is
suitable for smoke/CI evidence; all fields are primitives or nested plain
structs.

## Reflection Migration

Typed `ResourceBase` loading is the production path for UI and Pika resources.
`PrefabResourceFactory` provides typed prefab creation from envelopes.
`InternalAssetPackageReader::load()` dispatches through the factory when
`prefabResourceFactory().supports(entry.resourceType)` is true.

Reflection compatibility remains quarantined in
`compatibility::loadFromEnvelope()` for legacy envelope admission only.
`ResourceHandle<T>::find()` resolves only `ownedResource`; envelope-loaded
reflection storage (`ResourceRecord::value`) returns `type mismatch`.

Deletion criteria: remove reflection compatibility helpers after all prefab and
internal package resource families migrate to typed `ResourceBase` descriptors
or explicit serialization adapters with equivalent contract tests.

## Status Labels

Contract Complete means the public contract, deterministic diagnostics, and
focused unit/contract tests are green. It does not imply backend or platform
coverage.

Prototype Complete means the feature works for the accepted prototype path,
such as Pika visible UI startup, but still has known migration, lifecycle, or
validation gaps.

Backend Validated means the relevant backend behavior has focused validation
for each backend that claims support, with deterministic diagnostics for gaps.

Platform Validated means the feature has been validated on the target platform
lifecycle and deployment environment.

Production Ready requires contract, backend, platform, lifecycle, failure-mode,
performance, and smoke evidence. Current resource manager graph work must not
claim Production Ready while Pending Confirmation / External Validation items
remain open, and documentation must not claim Production Ready for this work.

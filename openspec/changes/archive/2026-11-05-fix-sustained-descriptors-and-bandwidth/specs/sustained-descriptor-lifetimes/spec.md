## ADDED Requirements

### Requirement: Supported descriptor lifetimes repeat correctly

Sustained replay SHALL support validated descriptor-set allocation/free and descriptor-pool-reset patterns without accumulating native allocations, restoring freed handles or retaining an earlier iteration's descriptor bindings. Pre-loop slots and loop-created lifetimes SHALL be distinguished. Other resource lifetime restrictions SHALL remain intact.

#### Scenario: Descriptor sets allocated in a measured frame
- **WHEN** a supported selected frame allocates and initializes descriptor sets before using them
- **THEN** repeated iterations retain correct descriptor values and bounded resource usage instead of failing the blanket allocation guard

#### Scenario: Descriptor sets are reset or recycled
- **WHEN** supported descriptor sets are invalidated by pool reset or freed and allocated again
- **THEN** stale lifetime state is removed and restoration never writes through an invalid descriptor handle

### Requirement: Unsupported restoration fails explicitly

Unproven descriptor forms SHALL fail with an actionable restoration reason. The application SHALL NOT bypass restoration checks merely to permit counter collection.

#### Scenario: Required baseline is unavailable
- **WHEN** the loop requires descriptor state that cannot be reconstructed safely
- **THEN** the session fails clearly without publishing successful profiling data

### Requirement: Native lifecycle behavior is locally verified

The implementation SHALL test repeated descriptor mutation/lifecycle paths and unchanged single-frame behavior using generated/local controls, without using the connected phone during implementation.

#### Scenario: Repeated descriptor state matches reference
- **WHEN** a local native descriptor-lifecycle control executes fresh and repeated allocation/update/free/reset sequences
- **THEN** descriptor values and mappings agree with the fresh-state reference and lifecycle validation reports successful bounded repetition

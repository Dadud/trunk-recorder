# Trunk Recorder Configuration Management API, draft implementation plan

## Summary

This document proposes an additive configuration management API for Trunk Recorder that allows external web UIs and management tools to read, validate, update, and apply configuration without replacing the existing `config.json` workflow.

The design is intentionally conservative:
- `config.json` remains the canonical persisted source of truth
- current CLI and Docker workflows continue to work unchanged
- the API is disabled by default
- compatibility with downstream projects, plugins, scripts, and wrappers is a hard requirement
- live apply behavior is explicit and must not pretend all settings can hot reload safely

## Problem statement

Today Trunk Recorder is configured primarily through `config.json` and related helper files such as talkgroup and unit tag CSVs. That works well for direct file-based administration, but it makes richer tools awkward. A web UI or external management layer currently has to act like a file editor and process wrapper instead of talking to a supported control surface.

At the same time, Trunk Recorder already has users, Docker deployments, plugins, scripts, and downstream projects built around the existing config-file model. Any new API must therefore be additive and must not break those existing integrations.

## Goals

- Add a secure, optional API for configuration management.
- Preserve `config.json` as the canonical persisted format.
- Cover the full config surface efficiently, including global settings, sources, systems, and helper file references.
- Support validation before persistence.
- Support explicit apply semantics so tools can know whether a change needs no action, reload, or restart.
- Keep the design compatible with current and future downstream tooling.

## Non-goals

- Replacing `config.json` with a new primary storage format.
- Requiring the API for normal Trunk Recorder operation.
- Shipping a built-in full web UI as part of Trunk Recorder.
- Forcing a plugin ecosystem rewrite.
- Claiming complete hot reload support before the runtime can safely honor it.

## Compatibility requirements

The following must remain true after the feature lands:

- Existing startup using `trunk-recorder --config=...` continues to work.
- Existing `config.json` semantics remain intact.
- Existing Docker and container workflows remain valid.
- Existing plugin loading and plugin config behavior remain valid.
- Existing scripts, wrappers, and downstream projects that only use files remain valid.
- Existing helper file formats such as talkgroup and unit tag CSVs remain valid.
- API support is disabled by default and opt-in.

## Security model

### Default posture

The API must be disabled by default.

When enabled, the default bind address should be `127.0.0.1` unless the operator explicitly chooses otherwise.

### Authentication

Mutation endpoints must require authentication. The initial design should use a simple token-based mechanism, for example a bearer token or `X-API-Key` header, because it is easy to deploy and does not force external infrastructure.

Read-only access may optionally support a separate token or remain fully authenticated in the first implementation. Simplicity is preferred over a complicated role model initially.

### Transport

The first implementation may remain HTTP-only if bound locally by default. For remote access, documentation should recommend placing the API behind a TLS-terminating reverse proxy.

### Mutation safety

All mutating operations must:
- validate input before persistence
- reject malformed or unsupported changes clearly
- use atomic file writes
- preserve the previous config revision or backup
- avoid any shell execution derived from API input

### Secret handling

Sensitive fields returned through read APIs should support redaction. Redaction should be the default for normal read responses unless a strong reason emerges otherwise.

## Design principles

- additive instead of disruptive
- schema-driven rather than ad hoc
- explicit instead of magical
- safe defaults
- operator control over apply behavior
- honest about reload versus restart requirements

## Proposed architecture

### 1. Reusable configuration core

Refactor config handling into a reusable internal layer, tentatively a `ConfigManager`, responsible for:
- loading from file
- parsing JSON
- validation
- normalization/default filling
- serialization
- atomic persistence
- config redaction for API responses
- change classification for apply behavior

This layer should be usable by both:
- the existing CLI startup path
- the new API surface

### 2. Metadata-driven config coverage

To cover every setting efficiently, define metadata for config fields instead of hand-writing bespoke endpoint logic per setting.

The metadata should describe:
- field path
- type
- default
- required versus optional
- validation rules
- secret/redaction policy
- whether the field affects startup only, reloadable runtime state, or helper-file content

This should cover:
- global config
- sources
- systems
- helper file references
- plugin config blobs where possible

### 3. Embedded management API

Add a lightweight embedded HTTP server behind an optional runtime enable flag.

The API server should be isolated from the signal-processing path as much as practical and should call into the config core instead of duplicating parsing and validation behavior.

### 4. Apply semantics

Changes should not automatically imply unsafe live mutation. The API should distinguish between:
- validation only
- persistence only
- persistence plus apply attempt
- restart required

The user or calling tool should receive explicit feedback about whether the current runtime can honor the requested change immediately.

## Proposed API surface

### Read endpoints

- `GET /api/v1/config`
  - returns the canonical persisted config, optionally redacted
- `GET /api/v1/config/schema`
  - returns config metadata/schema usable by external tools
- `GET /api/v1/runtime`
  - returns current runtime state and apply status
- `GET /api/v1/sources`
  - convenience read view of current source definitions
- `GET /api/v1/systems`
  - convenience read view of current system definitions

### Validation and mutation endpoints

- `POST /api/v1/config/validate`
  - validates candidate config without persistence
- `PUT /api/v1/config`
  - replaces full persisted config after validation
- `PATCH /api/v1/config`
  - applies partial updates after validation
- `POST /api/v1/config/apply`
  - applies pending persisted changes when safe, or reports restart required

### Helper file endpoints

- `GET /api/v1/systems/{id}/talkgroups`
- `PUT /api/v1/systems/{id}/talkgroups`
- `GET /api/v1/systems/{id}/unit-tags`
- `PUT /api/v1/systems/{id}/unit-tags`

### Health endpoints

- `GET /healthz`
- `GET /readyz`

## Full-setting coverage strategy

The API must cover the full config surface, but the implementation should not rely on hand-maintaining one custom endpoint per field. The recommended strategy is:

1. centralize config parsing and normalization
2. define metadata for all known fields
3. allow full-document reads and writes from day one
4. add patch support based on the same metadata registry
5. preserve plugin config blobs as pass-through JSON initially unless plugin-specific schema support is added later

This gives complete coverage while minimizing brittle endpoint sprawl.

## Helper file strategy

Talkgroup and unit tag files are part of the operational config surface even though they are not purely JSON settings. The API should treat them as first-class managed artifacts.

The initial implementation should:
- read and write them through dedicated endpoints
- preserve existing on-disk file formats
- validate obvious structural issues where practical
- avoid changing the current file semantics that downstream tools may already rely on

## Apply and reload strategy

This is the highest-risk part of the design and should be handled conservatively.

The API should classify changes into at least three outcomes:
- no apply needed
- reload supported
- restart required

The first implementation should prefer correctness over ambition. If a safe in-process reload path is not clearly available for a setting, the API should report that restart is required rather than attempt an unsafe partial mutation.

The design should not assume that every source, system, plugin, or SDR-related setting can be hot reloaded safely.

## Plugin compatibility strategy

Plugins are open-ended and may depend on current config behavior. To avoid breaking downstream projects:

- plugin config must remain supported in its current form
- plugin config should initially be treated as pass-through JSON with structural preservation
- plugin-specific schema validation can be added later if a plugin exposes metadata
- API changes must not require plugins to implement a new interface to keep working

## Implementation phases

### Phase 0, design and RFC

Expand this document into the agreed API contract and identify which runtime changes are realistically reloadable.

Deliverables:
- accepted design
- endpoint contract
- field metadata plan
- apply behavior classification rules

### Phase 1, config core refactor

Refactor current config loading logic into reusable parse, validate, normalize, serialize, and persist components.

Deliverables:
- no API yet
- startup behavior unchanged
- structured validation results available to code

### Phase 2, read-only API

Implement the optional API server with read-only endpoints.

Deliverables:
- disabled by default
- local bind by default
- config/schema/runtime endpoints
- redaction support

### Phase 3, mutation API

Implement validation and persistence endpoints.

Deliverables:
- validate endpoint
- full document write
- partial patch support
- auth enforcement
- atomic writes and backups

### Phase 4, apply behavior

Implement explicit apply semantics.

Deliverables:
- change classification
- apply endpoint
- restart-required responses where needed
- conservative reload path only where safe

### Phase 5, helper file APIs

Implement talkgroup and unit-tag management endpoints.

Deliverables:
- read/write helper file endpoints
- validation helpers
- compatibility with existing file formats

### Phase 6, docs and hardening

Document the API and finalize operational guidance.

Deliverables:
- usage docs
- security docs
- reverse proxy guidance
- compatibility guidance for downstreams

## Testing strategy

Because current automated test coverage is light, this feature should bring its own verification plan.

### Unit tests

Add tests for:
- parsing valid config
- rejecting invalid config
- unknown field handling rules
- normalization/default filling
- redaction behavior
- atomic persistence behavior
- patch merge logic
- apply classification logic

### Integration tests

Add integration coverage for:
- API disabled by default
- API enabled and bound locally
- auth required for mutation
- invalid token rejected
- valid full config accepted
- invalid full config rejected with structured errors
- patch updates accepted and persisted correctly
- helper file read/write behavior
- apply endpoint behavior for reloadable versus restart-required cases

### Regression verification

Confirm all of the following remain true:
- current CLI startup still works with unchanged config files
- current Docker build still works
- current file-based workflows still work
- plugin loading remains intact
- downstream file consumers remain unaffected

## CI and verification

The current GitHub Actions workflow primarily validates Docker builds. This feature needs additional CI coverage.

Recommended additions:
- compile/build job for the new API-enabled code path
- unit test job
- integration test job for the management API
- continued Docker build verification

When reviewing build output, continue checking the GitHub Docker build summary and artifacts so packaging regressions are caught early.

## Risks and mitigations

### Risk: breaking current users
Mitigation:
- additive design
- disabled by default
- config file remains canonical
- no forced workflow migration

### Risk: plugin regressions
Mitigation:
- preserve existing plugin config semantics
- treat plugin config conservatively
- avoid new mandatory plugin interfaces initially

### Risk: unsafe remote control surface
Mitigation:
- localhost default
- auth required for mutation
- explicit opt-in exposure
- document reverse proxy and TLS guidance

### Risk: claiming hot reload where runtime is not safe
Mitigation:
- classify changes honestly
- allow restart-required outcomes
- do not pretend every change is live-reloadable

### Risk: implementation sprawl
Mitigation:
- metadata-driven config coverage
- phase rollout
- small reviewable PRs

## Open questions

- Which config fields can truly reload safely without restart?
- What is the best embedded HTTP library choice for this repo’s build and dependency posture?
- How much plugin-specific schema support is realistic in v1?
- Should read responses redact secrets by default or support a separate privileged full-read mode?
- Should the first mutation version support only full-document writes before patch support, or both from the start?

## Recommended PR sequence

1. `docs: expand config API proposal into implementation plan`
2. `refactor(config): reusable parse validate normalize persist core`
3. `feat(api): optional read-only management API`
4. `feat(api): validated config write and patch support`
5. `feat(api): explicit apply and reload classification`
6. `feat(api): talkgroup and unit-tag helper file endpoints`
7. `test(api): unit and integration coverage`
8. `docs(api): usage, security, and compatibility guidance`

## Recommendation

The best upstream path is:
- preserve the current file-driven contract
- add a secure optional management layer
- prioritize compatibility and honesty over flashy live-reload claims
- implement in small reviewable steps

That will make the feature genuinely useful for web UIs and orchestration tools without destabilizing existing users or downstream projects.

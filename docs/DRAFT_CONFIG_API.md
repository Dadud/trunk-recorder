# Draft Config API Proposal

## Summary

Trunk Recorder is currently configured through a startup JSON file and related CSV files. This proposal adds a small management API for reading, validating, and updating configuration through structured endpoints, while preserving config-file compatibility.

## Goals

- Keep `config.json` as the canonical persisted format.
- Expose a supported API for tools and web UIs.
- Separate validation from apply/restart behavior.
- Avoid requiring callers to hand-edit JSON and CSV files.

## Proposed scope

### Read endpoints
- `GET /api/config`
- `GET /api/config/schema`
- `GET /api/systems`
- `GET /api/sources`
- `GET /api/runtime/status`

### Validation / mutation endpoints
- `POST /api/config/validate`
- `PUT /api/config`
- `PATCH /api/config`
- `POST /api/config/apply`

### Optional helper file endpoints
- `GET /api/systems/{id}/talkgroups`
- `PUT /api/systems/{id}/talkgroups`
- `GET /api/systems/{id}/unit-tags`
- `PUT /api/systems/{id}/unit-tags`

## Behavior

- `validate` checks a candidate config and returns structured errors/warnings without changing the running instance.
- `PUT /api/config` replaces persisted config.
- `PATCH /api/config` updates selected fields.
- `apply` decides whether a soft reload is possible, otherwise returns that restart is required.

## Internal design direction

- Extract config parsing into reusable load/serialize/validate functions.
- Introduce a `ConfigManager` abstraction instead of making `main.cc` the only entry point.
- Add a minimal embedded HTTP server behind a feature flag or optional build target.
- Keep the existing CLI startup path intact.

## Why this matters

This would let web UIs make real settings changes through an API instead of acting as a JSON editor layered over `config.json`.

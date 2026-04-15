# Trunk Recorder Management API

## Status

This is an additive, optional management API for Trunk Recorder.

Current behavior in this branch:
- disabled by default
- `config.json` remains canonical
- intended for local bind by default
- token auth required for mutation endpoints
- write paths validate before persistence
- config and helper-file writes use atomic replacement with backup behavior
- apply requests are explicit and currently conservative, many changes still imply restart required

## Config example

```json
{
  "api": {
    "enabled": true,
    "bind": "127.0.0.1",
    "port": 8765,
    "token": "replace-me"
  }
}
```

## Security notes

- Do not expose the API directly to the internet.
- Prefer `127.0.0.1` bind unless you are deliberately proxying it.
- Put TLS and any stronger auth in front of it if remote access is needed.
- Mutation endpoints require either `Authorization: Bearer <token>` or `X-API-Key: <token>`.

## Endpoint summary

### Health
- `GET /healthz`
- `GET /readyz`

### Config and runtime
- `GET /api/v1/runtime`
- `GET /api/v1/config`
- `GET /api/v1/config/schema`
- `GET /api/v1/sources`
- `GET /api/v1/systems`
- `POST /api/v1/config/validate`
- `PUT /api/v1/config`
- `PATCH /api/v1/config`
- `POST /api/v1/config/apply`

### Helper files
- `GET /api/v1/systems/{id}/talkgroups`
- `PUT /api/v1/systems/{id}/talkgroups`
- `GET /api/v1/systems/{id}/unit-tags`
- `PUT /api/v1/systems/{id}/unit-tags`

## Example calls

### Read redacted config

```bash
curl http://127.0.0.1:8765/api/v1/config
```

### Validate candidate config

```bash
curl \
  -H "Authorization: Bearer replace-me" \
  -H "Content-Type: application/json" \
  -d @config.json \
  http://127.0.0.1:8765/api/v1/config/validate
```

### Replace config

```bash
curl -X PUT \
  -H "Authorization: Bearer replace-me" \
  -H "Content-Type: application/json" \
  -d @config.json \
  http://127.0.0.1:8765/api/v1/config
```

### Patch config

```bash
curl -X PATCH \
  -H "Authorization: Bearer replace-me" \
  -H "Content-Type: application/json" \
  -d '{"api":{"bind":"127.0.0.1"}}' \
  http://127.0.0.1:8765/api/v1/config
```

### Apply pending changes

```bash
curl -X POST \
  -H "Authorization: Bearer replace-me" \
  http://127.0.0.1:8765/api/v1/config/apply
```

## Compatibility

This API does not replace the existing workflow.

These remain true:
- `trunk-recorder --config=...` still works
- direct file editing still works
- helper CSV formats stay unchanged
- plugins still use the existing config-file model
- Docker and wrapper flows are not forced onto the API

## Current limitations

- runtime apply is intentionally conservative and should be treated as persist plus explicit reload intent, not guaranteed live reconfiguration
- helper-file endpoints currently operate on referenced files, not a richer parsed object model
- schema output is lightweight and not yet a full metadata registry
- integration coverage still needs to grow

## Validation scope in this branch

Current validation covers a useful but still incomplete subset of the live config surface, including:
- top-level config shape
- API block shape and required token when enabled
- source driver/type and common numeric/integer fields
- recognized system types
- trunked-system control channel requirements
- conventional-system channels vs channelFile exclusivity rules
- helper-file reference field types
- audio postprocess object field types

This should be treated as a strong baseline, not a claim that every current and future config field is exhaustively metadata-modeled.

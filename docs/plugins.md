# Plugins

Omapixel Plugin API 1 is the executable boundary for external export plugins.
This document covers installation, inspection, invocation and plugin authoring.
There is no installer command, permission model, persistent process or Studio
integration. The local registry only inspects plugins; code runs only through an
explicit `plugin run` command.

## Manifest

Each plugin has `omapixel-plugin.json` at its root. The machine-readable shape is
[`plugin-manifest.schema.json`](plugin-manifest.schema.json). Unknown manifest
fields are rejected at every object level.

```json
{
  "schemaVersion": 1,
  "id": "example-exporter",
  "name": "Example exporter",
  "version": "0.1.0",
  "pluginApi": 1,
  "executable": "bin/exporter",
  "actions": [{"name": "png", "kind": "export"}]
}
```

- `schemaVersion` and `pluginApi` must use the literal JSON number token `1`.
  `pluginApi: 1` identifies this contract; an incompatible protocol revision
  requires a different API number.
  The published JSON Schema is structural: standard JSON Schema number semantics
  compare mathematically, so a generic validator may accept `1.0` as equal to
  `1`. `omapixel plugin check` is authoritative for this lexical rule and rejects
  fractional version tokens at runtime.
- `id` is a stable ASCII identifier matching `[a-z][a-z0-9-]{0,63}`.
- `name` is a non-empty string of at most 128 characters.
- `version` is an opaque, non-empty string of at most 128 characters. It is not
  interpreted as SemVer.
- `executable` is a slash-separated, relative path of at most 255 characters.
  It cannot be absolute, contain `\\`, controls, empty components, `.` or `..`.
  The resolved executable must be a regular executable file, not a symlink.
- `actions` is non-empty. Each action has a unique non-empty `name` (at most 128
  characters) and `kind` exactly equal to `export`; no other kind is supported.

## Workspace and request

The caller creates a private temporary workspace and validates the v2 document
before starting the plugin. The workspace contains `input/document.json` and an
empty `output/` directory. The snapshot is the v2 document described by
[`format.md`](format.md), not a legacy document or a caller-owned path.

The caller writes exactly one compact JSON object followed by `\n` to stdin:

```json
{"type":"request","requestId":"req-1","action":"png","document":"input/document.json","outputDir":"output","params":[{"key":"scale","value":"2"},{"key":"label","value":"two words"}]}
```

- `type` is exactly `request`.
- `requestId` is a non-empty opaque string of at most 64 ASCII characters. Every
  response record echoes it exactly.
- `action` names a manifest action.
- `document` is the fixed safe relative snapshot path `input/document.json`.
- `outputDir` is the fixed safe relative path `output`.
- `params` is a protocol array of `{key, value}` objects. Both fields are
  non-empty strings of at most 128 characters; entries may repeat and values are
  never typed or parsed. This is distinct from the CLI: each repeated
  `--param KEY=VALUE` flag becomes one protocol object, but duplicate CLI keys
  are rejected before the executable starts.

The caller, not the plugin, chooses the final `--out` destination. The plugin
only writes inside `output/` and reports the result filename relative to it.

## Local inspection

The CLI discovers plugin roots in this order:

1. Each non-empty entry in `OMAPIXEL_PLUGIN_PATH`, from left to right.
2. `$XDG_DATA_HOME/omapixel/plugins`, or `~/.local/share/omapixel/plugins` when
   `XDG_DATA_HOME` is unset.

An entry may be a plugin root or a directory containing plugin directories. The
first valid occurrence of an ID wins. Invalid manifests and later valid
duplicates remain diagnostics. Valid plugin output is sorted lexically by ID.

Installation is currently by copy: put a complete plugin directory in one of
those roots. There are no `install`, `update`, or `remove` commands.

```text
omapixel plugin list [--json]
omapixel plugin check <plugin-directory-or-id> [--json]
omapixel plugin run <plugin-id> <action-id> <document> --out <path> [--param key=value ...] [--json]
```

The JSON shape is stable and contains only valid winners plus diagnostics:

```json
{
  "plugins": [{
    "id": "example-exporter",
    "name": "Example exporter",
    "version": "0.1.0",
    "path": "/home/user/.local/share/omapixel/plugins/example-exporter",
    "executable": "run.sh",
    "actions": [{"name": "png", "kind": "export"}]
  }],
  "diagnostics": [{"path": "/path", "message": "..."}]
}
```

`plugin check` uses `{ "plugin": <plugin-or-null>, "diagnostics": [] }`.
Both commands only read manifests and filesystem metadata; they never start the
declared executable. Exit `0` means the requested data is valid, exit `1`
reports an invalid or absent plugin, and exit `2` reports command usage.

## JSONL response

Every non-empty stdout line is one compact JSON object. Stdout contains no logs.
The plugin may emit zero or more progress records, followed by exactly one
terminal result record:

```json
{"type":"progress","requestId":"req-1","message":"encoding","percent":50}
{"type":"result","requestId":"req-1","ok":true,"artifact":"sprite.png"}
```

Progress has a non-empty `message` of at most 256 characters and an optional
integer `percent` from 0 through 100. A successful result has exactly one
`artifact`, a safe relative path beneath `output/`, and no other result fields
besides `type`, `requestId`, `ok`, and `artifact`. The artifact must be one
regular non-symlink file and at most 64 MiB. A failed result has `ok: false` and
an `error` string of at most 1024 characters instead of `artifact`.

The process exit status must be zero only for a successful result. Malformed
JSONL, an unexpected stdout line, a missing or duplicate result, a mismatched
request ID, nonzero exit, crash, timeout, unsafe artifact path, symlink, or
limit violation is a failed invocation.

## Hard limits

| Limit | Value |
| --- | ---: |
| Execution timeout | 60 seconds |
| Cumulative stdout protocol budget | 1 MiB |
| Cumulative stderr diagnostics budget | 1 MiB |
| Manifest size | 16 MiB |
| Result artifacts | exactly 1 |
| Result artifact size | 64 MiB |

The fixture under `tests/fixtures/plugin/` and the plugin contract, discovery,
run and E2E tests exercise the same parser, registry and runner used by the CLI.

## One-shot run

`plugin run` is synchronous and starts the manifest executable directly, with
the private workspace as its working directory. The host retains exactly these
environment keys when present: `PATH`, `HOME`, `LANG`, `LC_ALL`, `LC_CTYPE`,
and `TMPDIR`. No other caller environment is inherited. `PATH` supports normal
shebang executables; the other keys preserve ordinary local locale and temporary
directory behavior. This is not a sandbox and does not contain a trusted plugin
that deliberately ignores the workspace contract.

The CLI publishes only after a successful zero exit and valid terminal result.
With `--json`, the terminal output is one compact object:

```json
{"ok":true,"plugin":"example-exporter","action":"png","out":"/tmp/export.bin"}
```

Failures use the same shape with `ok: false` and an `error` string; human
diagnostics remain on stderr. Malformed or duplicate `--param` keys are
rejected before the executable starts. Each CLI flag must use `KEY=VALUE`;
parameter keys and values are passed as strings and are limited to 128
characters each. Duplicate keys and malformed `KEY=VALUE` syntax are host
validation. Parameter names are otherwise opaque:
unknown-key rejection is deferred until a future manifest parameter schema exists.

## Trust and limits

Plugins are trusted, unsandboxed executables. The workspace and atomic publication
protect the caller's selected source and destination from contract failures; they
do not contain malicious plugin code or provide a security boundary. There is no
permission system, sandbox, installer/update/remove flow, Godot, Studio, or
Omarchy integration, transform/import action, multiple-artifact result,
parameter schema, or performance optimization beyond bounded I/O and avoiding
deadlocks in Plugin API 1.

From a source checkout, `mise run plugin-e2e` runs the success and failure matrix
against local test fixtures. It requires no installed third-party plugin,
display, network access or writes outside temporary directories.

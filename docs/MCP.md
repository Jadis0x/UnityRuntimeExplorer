# MCP integration

UnityRuntimeExplorer provides Model Context Protocol access to a live
Windows Unity game. It supports bounded automatic discovery, inspection,
managed writes, Unity object/component operations, method tracing, and managed
method invocation. The Explorer MCP menu independently controls each capability. The
MCP protocol and client transport remain outside the injected mod DLL.

## Architecture

```text
ChatGPT Desktop / Codex / Claude / another MCP host
                         |
                    MCP over stdio
                         |
       URK_UnityRuntimeExplorer_McpServer.exe
                         |
       local-only current-user Windows named pipe
                         |
        UnityRuntimeExplorer DLL bridge queue
                         |
           Unity main-thread RuntimeModel
                         |
             immutable Explorer Snapshot
```

The DLL starts a named-pipe listener after the Explorer runtime is ready. The
pipe rejects remote clients and its DACL permits the creating user and SYSTEM.
The helper discovers live Explorer instances through per-process records under
`%LOCALAPPDATA%\URK\UnityRuntimeExplorer\bridges`.

Snapshot-only requests read the same immutable snapshot used by the UI.
Operations that touch Unity or managed metadata are dispatched to the Unity
main thread. A helper process never loads Mono/IL2CPP metadata or dereferences a
managed object itself.

## Tool set

| Tool | Behavior |
| --- | --- |
| `runtime_status` | Runtime/backend, Unity version, scene, GC, revision, and diagnostic status. |
| `discover_runtime` | Searches live GameObjects and loaded types in one bounded pass. |
| `hierarchy_search` | Bounded name/path/tag/instance-ID/component/dynamic-behaviour search over the immutable hierarchy. |
| `find_game_objects` | Ranks candidates from name, path, component signatures, dynamic behaviour types, scene, activity, and optional semantic role; explicitly reports ambiguous results. |
| `get_selected_object` | Current Explorer selection and an opaque reference. |
| `inspect_game_object` | Identity, state, transform, and components without changing the UI selection. |
| `list_components` | Component types and opaque component references. |
| `read_member` | Reads one field or readable property of a component or managed object (`reference`), or a static member (`type_reference`). The member can be named by its real name, its alias, or a saved label. Property getters are game code and may have their own behavior. |
| `inspect_managed_object` | Reads bounded fields from component or managed-object references. Property getters execute only when explicitly requested. |
| `snapshot_object` | Remembers the member values of a component or managed object and returns only a `snapshot_reference`. Values are compared as plain data, never as reference tokens. At most sixteen snapshots are kept. |
| `diff_snapshot` | Returns only what changed since a snapshot: `changed` (`member`, `from`, `to`), `added`, `removed` and an `unchanged` count. `against` compares two snapshots, `rebase` makes the current state the new baseline, `epsilon` ignores small float noise. Object references are compared by their display text. |
| `read_array` | Pages a managed array and returns opaque references for reference elements. |
| `decode_byte_array` | Copies and decodes bounded `System.Byte[]` data without exposing managed memory. |
| `start_instance_scan` | Starts a direct Unity-object query or time-sliced reachable managed-graph scan for a type. |
| `get_instance_scan` | Returns scan progress and a bounded page of opaque instance references. |
| `search_types` | Searches loaded Mono/IL2CPP assemblies and returns opaque type references. |
| `search_members` | Searches fields, properties, and methods across bounded matching types. |
| `inspect_type` | Returns bounded field, property, method, and signature metadata. |
| `list_method_traces` | Lists active and retained trace sessions. Consecutive identical calls are grouped. |
| `get_method_trace` | Returns decoded callers, arguments, results, threads, and timing. Groups include their sequence range and repeat count. |
| `build_call_graph` | Aggregates captured caller-to-target edges, including calls folded into each group. |
| `get_activity_log` | Returns bounded Explorer activity and MCP audit events. |
| `build_reference_graph` | Bounded graph for the current Explorer selection. |
| `get_watch_history` | Current watches and bounded recent change events. |
| `export_diagnostic_bundle` | Writes to the Explorer's fixed `URK_Diagnostics` directory and returns only the path. |
| `list_labels` | Lists the labels and notes saved for this game's types and members. |

Navigation tools let a client move through the game without guessing names.
Every result carries references that lead one step further in, and
`describe_reference` and the `parent`/`owner` fields lead back out:

| Tool | Behavior |
| --- | --- |
| `list_scenes` | Loaded scenes with root counts, plus build scenes. |
| `browse_hierarchy` | One level of the GameObject tree: a scene's roots, or an object with its parent, ancestors (breadcrumb) and a page of children. |
| `select_game_object` | Selects an object in the overlay so the person watching sees it. Changes only the UI selection. |
| `describe_reference` | What a reference points to and whether it is still valid. A component reports its `owner_reference`; a managed object reports its type and source. |
| `list_assemblies` / `list_namespaces` | Browse loaded assemblies and their namespaces with type counts. |
| `get_type_hierarchy` | Base chain, interfaces, and a page of derived types for a type reference. |
| `read_static_members` | Static fields (and, when permitted, static properties) of a type. |

List tools take `offset` and report `has_more`. A result larger than the
28 KiB response budget is trimmed from its largest array and marked `truncated`
with `trimmed_arrays`, instead of failing. Page for the rest.

`batch` runs up to ten read tools (`calls: [{tool, arguments}]`) in one call and
returns `results` in the same order, each `{ok, result}` or `{ok, error}`.
Tracing, invocation and every write stay out of a batch, each sub-call is
validated and permission-checked like a normal call, and a batch stops
dispatching once its results pass about 24 KiB (the rest report
`batch_budget_exhausted`; send them again). Prefer it to separate calls: it
costs one model turn instead of one per call.

Lists of records are returned as tables, `{"cols":[names],"rows":[[values]]}`, which
state each key once instead of once per row. A null cell can also mean the field
was absent. Start the helper with `--verbose-output` to get plain objects.

`inspect_managed_object` hides members inherited from engine base types
(`UnityEngine.Object`, `Component`, `Behaviour`, `MonoBehaviour`, `System.Object`)
and reports `hidden_inherited`; pass `include_inherited` to see them.
All-null table columns are dropped.

Member values are lean: a scalar carries `value` without a repeated `display`, and
`readable` (true), `static` (false), `runtime_safe` (true) and a `declaring_type` equal
to the object's own type are omitted when they hold their default. Floats print
as the game shows them (`0.1`, not `0.10000000149011612`).

The published tool list is kept compact (no titles, minimal annotations, short
descriptions) because every session re-reads it.

`search_types`, `search_members`, and `inspect_type` report each saved
`label` (and `label_note`), plus an `alias` such as `m_7c1e2a` for any name
that cannot be read or typed. Their queries also match labels and notes.
List results stop short of the 64 KiB bridge limit and set `truncated`
instead of failing.

The helper always publishes the complete catalog so agents can plan and
self-discover. Explorer checks the current in-game permission on every call.
Three instrumentation tools control tracing:

| Tool | Behavior |
| --- | --- |
| `start_method_trace` | Installs a bounded trace hook for an opaque method reference returned by `inspect_type`. |
| `stop_method_trace` | Detaches a hook while retaining its captured records. |
| `clear_method_trace` | Clears retained records for one trace session. |

Tracing uses the Explorer's existing native `MethodTracer`; it captures real
game calls, not only calls initiated by Explorer. Captured records include the
resolved caller when available, target method, target instance description,
thread ID, elapsed time, decoded arguments, and decoded return value. Raw
addresses and raw ABI lanes are intentionally excluded from MCP responses.

`invoke_method` is accepted only while **Allow MCP method invocation** is
enabled in the game. It
uses a discovered method reference, an opaque target, at most sixteen bounded
JSON arguments, and one audited invocation per request. It carries
`readOnlyHint: false` and does not inherit tracing permission. Generic method
binding and runtime-specific opaque signatures remain rejected.

Full-access operations are:

| Tool | Behavior |
| --- | --- |
| `write_member` | Writes an instance field or property (`reference`) or a static one (`type_reference`) from bounded JSON, and returns the previous and the observed new value. |
| `undo_write` | Reverts the most recent `write_member` whose old value is a boolean or number. Up to 32 writes are remembered. Native pointer fields (`m_CachedPtr`, `IntPtr`) cannot be written. |
| `mutate_game_object` | Changes identity/state/transform, duplicates, or destroys a GameObject. |
| `manage_component` | Adds, removes, or enables components. |
| `load_scene` | Loads a build scene by index or name. |
| `set_label` | Saves a readable name and note for a type or member. The target is a type full name plus a member name, or the member's `alias`; methods are labelled per overload unless `all_overloads` is set. Unknown targets are rejected. |
| `remove_label` | Deletes a saved label, identified the same way. |

Labels never touch game state, but they persist and are shown to the person
using the overlay, so writing them follows the write permission.

Scripting, assembly loading, raw addresses, and arbitrary native execution
remain outside the MCP surface because they bypass Explorer's lifetime and type
checks.

## Build and run

Build the DLLs and helper together:

```powershell
cmake --preset clang-release
cmake --build --preset clang-release --parallel
```

Load the matching Explorer DLL through URKit, then configure an MCP client to
launch:

```text
C:\absolute\path\to\URK_UnityRuntimeExplorer_McpServer.exe
```

When exactly one Explorer-enabled game is running, the helper attaches to it.
If more than one is running, add `--game-pid <pid>` to the helper arguments.
The helper writes MCP messages only to stdout and diagnostics only to stderr.
When opened manually, it prints its version, target-selection mode, permission
mode, bridge status, and basic usage to the console before waiting for MCP
messages. MCP clients normally launch it automatically; users do not need to
keep a separate helper window open.

### MCP permissions in Explorer

Open Explorer with **F7** and use the **MCP** menu in the top menu bar. **Full access** enables automatic discovery, property getters,
writes, tracing, invocation, and destructive operations. **Read-only preset**
keeps bounded field discovery/inspection while disabling executable or mutating
paths. Individual toggles are stored under `[MCP]` in `URK_Explorer.ini`.

The injected bridge creates the capability context immediately before runtime
dispatch. No `_helper_allows_*` field is accepted from serialized requests.
The legacy `--allow-tracing` and `--allow-invocation` flags are accepted only
for configuration compatibility and no longer grant permissions.

## ChatGPT Desktop and Codex

Current OpenAI clients support local stdio MCP servers. In ChatGPT Desktop,
open **Settings > MCP servers > Add server**, select **STDIO**, enter the helper
path as the command, save, and restart. The ChatGPT desktop app, Codex CLI, and
Codex IDE extension share MCP configuration on the same Codex host. See the
[official OpenAI MCP guide](https://learn.chatgpt.com/docs/extend/mcp).

For Codex CLI:

```powershell
codex mcp add unity-runtime-explorer -- "C:\absolute\path\to\URK_UnityRuntimeExplorer_McpServer.exe"
codex mcp list
```

Equivalent `~/.codex/config.toml`:

```toml
[mcp_servers.unity-runtime-explorer]
command = "C:\\absolute\\path\\to\\URK_UnityRuntimeExplorer_McpServer.exe"
args = []
startup_timeout_sec = 10
tool_timeout_sec = 10
```

Use `args = ["--game-pid", "12345"]` when selecting one of several games.
Permissions do not require helper arguments. Use only `--game-pid` when a
specific game process must be selected:

```toml
args = ["--game-pid", "12345"]
```

## Claude Desktop and Claude Code

Claude Desktop accepts a local stdio server in `claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "unity-runtime-explorer": {
      "command": "C:\\absolute\\path\\to\\URK_UnityRuntimeExplorer_McpServer.exe",
      "args": []
    }
  }
}
```

Restart Claude Desktop after saving the file. For Claude Code:

```powershell
claude mcp add unity-runtime-explorer -- "C:\absolute\path\to\URK_UnityRuntimeExplorer_McpServer.exe"
claude mcp list
```

Anthropic's current MCP entry point links the product-specific setup guides:
[Anthropic MCP documentation](https://docs.anthropic.com/en/docs/mcp).

## Other local MCP clients

Configure a stdio server with the helper executable as `command` and optional
`--game-pid` arguments. A generic configuration is:

```json
{
  "mcpServers": {
    "unity-runtime-explorer": {
      "type": "stdio",
      "command": "C:\\absolute\\path\\to\\URK_UnityRuntimeExplorer_McpServer.exe",
      "args": []
    }
  }
}
```

The server implements the `server/discover` compatibility probe, strict MCP
lifecycle, `ping`, `tools/list`, and `tools/call` over bounded newline-delimited
stdio. MCP 2025-11-25 clients may use task-augmented tool calls plus
`tasks/list`, `tasks/get`, `tasks/result`, and `tasks/cancel`. Ordinary calls
support cancellation notifications and progress tokens.

## ChatGPT web, Claude.ai, and other remote clients

Do not expose the DLL's named pipe or bind a raw local listener to the public
internet. Hosted clients cannot launch this local stdio helper and require a
remote Streamable HTTP MCP endpoint.

For private OpenAI use, place a maintained stdio-to-Streamable-HTTP gateway on
the same machine, bind it only to loopback, and connect it through the
[OpenAI Secure MCP Tunnel](https://developers.openai.com/api/docs/guides/secure-mcp-tunnels).
The tunnel uses outbound HTTPS and avoids opening an inbound game-machine port.
For a public endpoint, terminate TLS at a maintained reverse proxy and require
OAuth 2.1 or a rotated bearer credential. Validate `Origin`, enforce a host
allowlist, preserve the tool rate limit, and keep the gateway's read-only tool
allowlist. The MCP transport specification requires Origin validation,
localhost binding for local HTTP servers, and authentication:
[MCP transport security](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports).

Claude's hosted/API connector likewise requires a publicly reachable HTTP MCP
server; local stdio is for Claude Desktop or Claude Code. Do not publish the
bridge discovery directory or forward the Windows named pipe.

An HTTP gateway is deliberately not bundled. Shipping an unauthenticated
ad-hoc web server beside a live game would weaken the process boundary. The
stdio tool core is transport-neutral, so a reviewed Streamable HTTP frontend
can reuse the same transport-neutral tool catalog without changing the DLL bridge.

## Security properties and limits

- The bridge is local-only and serves up to four helper connections at once, so
  two agents, or a restarted helper whose predecessor has not exited yet, do not
  lock each other out. A fifth client is refused with `bridge_busy`. Object,
  snapshot and trace references are shared between clients. Each connection has
  its own request budget.
- Object and component references are random 48-bit opaque tokens (`r_` plus 12 hex digits) stored only
  in the DLL. They survive ordinary hierarchy refreshes and expire on a scene
  generation change. Graph references additionally expire when their hierarchy
  revision changes.
- Managed-object references use bounded GC handles behind opaque tokens. Weak
  references report collection explicitly; invocation results are retained
  strongly only until their bounded opaque reference is evicted.
- `find_game_objects` exposes `signature_complete` per match and
  `incomplete_signatures` for the search. A missing result is not presented as
  proof of absence when component metadata could not be read.
- Serializers omit `pointer_text`, managed/native addresses, method pointers,
  and raw reference handles.
- Client requests and bridge messages are capped at 64 KiB; the stdio response
  envelope is capped at 128 KiB to carry structured content and its
  backward-compatible text representation. Searches, graph traversal,
  histories, and the request queue have independent bounds.
- JSON-RPC envelopes and tool arguments are type-checked against the published
  input schema before reaching the game process. Each tool also publishes an
  output schema and returns backward-compatible text plus structured content.
- The bridge paces requests with a cost-weighted token bucket (default 20
  tokens per second, burst 60). Snapshot reads cost 0.25, scans, searches and
  graph builds cost 3, everything else 1. A request that finds the bucket empty
  waits up to two seconds for a refill; only a longer wait is refused, with
  `rate_limited` and a `retry_after_ms` hint that the helper honours with up to
  two automatic retries. At most four requests are processed per game frame.
- A request waits at most eight seconds (default) for the Unity main thread.
  `RatePerSecond`, `Burst` and `TimeoutMs` under `[MCP]` in `URK_Explorer.ini`
  (also sliders in the MCP menu) change these limits.
- Every successful or failed MCP tool call is recorded in the Explorer flight
  recorder shown by the Activity Log/diagnostic bundle.
- Trace control requires the persisted in-game permission. Enabling tracing
  does not enable writes or invocation.
- Invocation requires its own persisted in-game permission.
  It is audited, non-read-only, bounded to one discovered method per request,
  and does not enable writes or native-address inputs.
- Trace sessions are capped at twelve and retain at most 1,024 records each.
  Overwritten-record and native-fault counts remain visible to the client.
- Hooks installed through MCP are automatically stopped when the helper has
  been disconnected for 30 seconds (a client restart reconnects in time) or the
  in-game tracing permission is disabled. Trace sessions
  started manually from the Explorer UI are not affected by MCP revocation.
- Error responses use stable categories and omit native fault addresses.
- There is no arbitrary command execution, assembly loading, scripting tool,
  raw address lookup, or generic method invocation surface.

`read_member` is explicit because a managed property getter executes game code.
Prefer fields when merely observing state, and avoid repeatedly sampling costly
properties.

## Troubleshooting

- **No live bridge found:** verify that the correct Mono/IL2CPP DLL is loaded by
  URKit and inspect `URKit_logs.log`.
- **Multiple bridges found:** pass `--game-pid` for the intended game process.
- **Reference expired:** object/component references survive ordinary hierarchy
  refreshes but expire after a scene-generation change. Search again and use
  the new token. Graph references remain revision-bound.
- **Ambiguous discovery:** inspect or observe the returned candidates. An empty
  result means the supplied evidence did not match; it is not proof that a
  conceptual object such as "the player" does not exist.
- **Timeout:** the game main thread may be paused or blocked. Resume it and
  retry; the helper does not call the managed runtime from a worker thread.
- **Rate limited:** rare now; the helper already waits and retries. If it still
  appears, raise **Requests per second** / **Burst** in the MCP menu, or
  prefer one bounded query over repeatedly requesting the same snapshot.
- **Member unavailable:** the runtime may have stripped metadata, the component
  may have been destroyed, or the member may be opaque/unsafe for generic
  inspection. The tool reports the failure rather than returning a default.
- **Tracing permission required:** enable it in the MCP menu.
- **Invocation permission required:** enable the separate in-game invocation
  option.
- **Caller shown as a module:** inspect likely caller types with `search_types`
  and `inspect_type`. On IL2CPP this adds those method names to the safe caller
  index. Mono deliberately avoids domain-wide JIT compilation, so some callers
  remain module-level locations.
- **Trace records overwritten:** the target method is too hot for the bounded
  1,024-record session buffer. Poll less often only affects reads; narrow the
  investigation to a more specific method or stop the trace after reproducing
  the event.

# MCP integration

UnityRuntimeExplorer 0.4.0 provides Model Context Protocol access to a live
Windows Unity game. It supports bounded automatic discovery, inspection,
managed writes, Unity object/component operations, method tracing, and managed
method invocation. Explorer Config independently controls each capability. The
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
| `read_member` | Explicitly samples one field or readable property. Property getters are game code and may have their own behavior. |
| `inspect_managed_object` | Reads bounded fields from component or managed-object references. Property getters execute only when explicitly requested. |
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
| `write_member` | Writes an instance field or writable property from bounded JSON. |
| `mutate_game_object` | Changes identity/state/transform, duplicates, or destroys a GameObject. |
| `manage_component` | Adds, removes, or enables components. |
| `load_scene` | Loads a build scene by index or name. |

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

Open Explorer with **F7** and use the **MCP access and automation** section in
Config. **Enable full access** enables automatic discovery, property getters,
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

An HTTP gateway is deliberately not bundled in 0.4.0. Shipping an unauthenticated
ad-hoc web server beside a live game would weaken the process boundary. The
stdio tool core is transport-neutral, so a reviewed Streamable HTTP frontend
can reuse the same transport-neutral tool catalog without changing the DLL bridge.

## Security properties and limits

- The bridge is local-only and limited to one helper connection at a time.
- Object and component references are random 128-bit opaque tokens stored only
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
- The bridge uses a token-bucket limit of five calls per second with a burst of
  twenty, and processes at most four requests per game frame.
- A request waits at most five seconds for the Unity main thread.
- Every successful or failed MCP tool call is recorded in the Explorer flight
  recorder shown by the Activity Log/diagnostic bundle.
- Trace control requires the persisted in-game permission. Enabling tracing
  does not enable writes or invocation.
- Invocation requires its own persisted in-game permission.
  It is audited, non-read-only, bounded to one discovered method per request,
  and does not enable writes or native-address inputs.
- Trace sessions are capped at twelve and retain at most 1,024 records each.
  Overwritten-record and native-fault counts remain visible to the client.
- Hooks installed through MCP are automatically stopped when the helper
  disconnects or the in-game tracing permission is disabled. Trace sessions
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
- **Rate limited:** reduce polling frequency. Prefer one bounded query over
  repeatedly requesting the same snapshot.
- **Member unavailable:** the runtime may have stripped metadata, the component
  may have been destroyed, or the member may be opaque/unsafe for generic
  inspection. The tool reports the failure rather than returning a default.
- **Tracing permission required:** enable the in-game Config option.
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

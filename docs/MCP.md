# MCP integration

UnityRuntimeExplorer 0.2.0 adds read-only Model Context Protocol access to a
live Windows Unity game. The design keeps the MCP protocol and transports out
of the injected mod DLL.

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
| `hierarchy_search` | Bounded name/tag/instance-ID search over the immutable hierarchy. |
| `get_selected_object` | Current Explorer selection and an opaque reference. |
| `inspect_game_object` | Identity, state, transform, and components without changing the UI selection. |
| `list_components` | Component types and opaque component references. |
| `read_member` | Explicitly samples one field or readable property. Property getters are game code and may have their own behavior. |
| `build_reference_graph` | Bounded graph for the current Explorer selection. |
| `get_watch_history` | Current watches and bounded recent change events. |
| `export_diagnostic_bundle` | Writes to the Explorer's fixed `URK_Diagnostics` directory and returns only the path. |

All tools carry the MCP `readOnlyHint`. `write_member` and `invoke_method` are
not registered. They are reserved for a future permission mode with a separate
capability, explicit user approval, and mandatory audit entries.

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

The server implements the `server/discover` compatibility probe, JSON-RPC MCP
initialization, `ping`, `tools/list`, and `tools/call` over newline-delimited
stdio.

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

An HTTP gateway is deliberately not bundled in 0.2.0. Shipping an unauthenticated
ad-hoc web server beside a live game would weaken the process boundary. The
stdio tool core is transport-neutral, so a reviewed Streamable HTTP frontend
can reuse the same nine schemas without changing the DLL bridge.

## Security properties and limits

- The bridge is local-only and limited to one helper connection at a time.
- Object and component references are random 128-bit opaque tokens stored only
  in the DLL. They expire when the scene generation or hierarchy revision
  changes.
- Serializers omit `pointer_text`, managed/native addresses, method pointers,
  and raw reference handles.
- Requests and responses are capped at 64 KiB. Searches, graph traversal,
  histories, and the request queue have independent bounds.
- The bridge uses a token-bucket limit of five calls per second with a burst of
  twenty, and processes at most four requests per game frame.
- A request waits at most five seconds for the Unity main thread.
- Every successful or failed MCP tool call is recorded in the Explorer flight
  recorder shown by the Activity Log/diagnostic bundle.
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
- **Reference expired:** repeat `hierarchy_search` after a scene or hierarchy
  change and use the new token.
- **Timeout:** the game main thread may be paused or blocked. Resume it and
  retry; the helper does not call the managed runtime from a worker thread.
- **Rate limited:** reduce polling frequency. Prefer one bounded query over
  repeatedly requesting the same snapshot.
- **Member unavailable:** the runtime may have stripped metadata, the component
  may have been destroyed, or the member may be opaque/unsafe for generic
  inspection. The tool reports the failure rather than returning a default.

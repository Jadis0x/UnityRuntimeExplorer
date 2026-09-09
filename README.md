[![Release](https://img.shields.io/github/v/release/Jadis0x/UnityRuntimeExplorer?label=Release)](https://github.com/Jadis0x/UnityRuntimeExplorer/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/Jadis0x/UnityRuntimeExplorer/total?label=Downloads)](https://github.com/Jadis0x/UnityRuntimeExplorer/releases)
[![Stars](https://img.shields.io/github/stars/Jadis0x/UnityRuntimeExplorer?style=flat&label=Stars)](https://github.com/Jadis0x/UnityRuntimeExplorer/stargazers)
[![Discord](https://img.shields.io/badge/Discord-Join%20Server-5865F2?logo=discord&logoColor=white)](https://discord.com/invite/XC7RUpGp6e)
[![Support](https://img.shields.io/badge/Support-GitHub%20Issues-blue?logo=github)](https://github.com/Jadis0x/UnityRuntimeExplorer/issues)
[![License](https://img.shields.io/github/license/Jadis0x/UnityRuntimeExplorer)](https://github.com/Jadis0x/UnityRuntimeExplorer/blob/main/LICENSE)

# UnityRuntimeExplorer

A runtime inspector for Windows Unity games. It loads as a URKit mod and lets
you look at a running game from the inside: scenes, GameObjects, components,
fields, references, live values, all without rebuilding anything.

It's built on [URKit](https://github.com/Jadis0x/URKit), which handles the
loader, Unity/Mono/IL2CPP bindings, hooks, and ImGui. UnityRuntimeExplorer is
the Explorer UI and inspection logic on top of that.

![UnityRuntimeExplorer](showcase/ss1.png)

![UnityRuntimeExplorer](showcase/ss2.png)

## What it does

It's a live hierarchy/inspector for the running game: scenes, hidden roots,
and `DontDestroyOnLoad` objects, searchable by name, tag, or instance ID.
From there you can inspect GameObjects, components, fields, properties, and
methods, edit values on the spot, copy/paste transforms, duplicate or delete
components, follow a reference straight into the Object Inspector, or invoke
a method directly.

The thing that goes beyond just poking at values is method tracing: hook a
managed method and watch its callers, arguments, return values, and captured
objects as it actually gets called, live. There's also an optional MCP
server, so an AI client can drive the same inspection (and even tracing)
instead of you clicking through it by hand.

UI is ImGui, dockable, runs on DX11, DX12, and OpenGL. Everything is read
from the runtime's own type metadata instead of a hardcoded per-game list, so
an operation that isn't safe on a given object gets reported as unavailable
instead of the tool silently doing the wrong thing.

## Which DLL to use

Two builds are produced, one per runtime backend:

```text
URK_Il2cpp_UnityRuntimeExplorer.dll   # IL2CPP games
URK_Mono_UnityRuntimeExplorer.dll     # Mono games
```

Use whichever matches the game. Mixing them up won't work.

Whether it works at all also depends on the game's Unity version, its
generated metadata, and what runtime exports it ships. Mono games in
particular need the right embedding exports.

## Installing

Grab URKit from the [v0.4.0 release](https://github.com/Jadis0x/URKit/releases/tag/v0.4.0)
(the proxy DLL, or `URKitInjector.dll` for the injector setup below). You don't
need `urk-sdk.exe` unless you're building your own URKit mods.

Then grab the Explorer DLL matching the game's runtime (Mono or IL2CPP).

### Standard proxy setup

1. Pick one proxy (`version.dll`, `winhttp.dll`, or `winmm.dll`) matching
   something the game already imports. Only one, not all three.
2. Put that proxy DLL next to the game's executable.
3. Make a `Mods` folder next to the executable.
4. Drop the matching Explorer DLL into `Mods`.
5. Launch the game.
6. Press **F7** to toggle the Explorer.

### Proxy-free (injector) setup

1. Grab `URKitInjector.dll` from the URKit release.
2. No proxy DLL or `Mods` folder needed.
3. Inject `URKitInjector.dll` into the game (Windows x64 only).
4. Pick your URKit `.ini` when prompted.
5. Pick the matching Explorer DLL.

Don't inject either Explorer DLL directly, it has to be loaded by URKit or
the injector as a plugin.

If nothing shows up, check `URKit_logs.log` next to the game exe. It'll
usually tell you if the proxy name, backend, or a runtime export is wrong.

## Building it yourself

Windows only, with CMake + Ninja + Clang.

You'll need:

- Windows 10+, x64
- CMake 3.28+
- LLVM/Clang
- Ninja
- Internet on first configure (pulls ImGui and other dependencies)

```powershell
cmake --preset clang-release
cmake --build --preset clang-release --parallel
```

Debug build:

```powershell
cmake --preset clang-debug
cmake --build --preset clang-debug --parallel
```

Output lands in:

```text
out/build/clang-release/URK_Il2cpp_UnityRuntimeExplorer.dll
out/build/clang-release/URK_Mono_UnityRuntimeExplorer.dll
out/build/clang-release/URK_UnityRuntimeExplorer_McpServer.exe
```

Tests:

```powershell
ctest --test-dir out/build/clang-release --output-on-failure
```

## MCP (optional)

If you want an AI client to be able to look at (or poke) the running game,
there's an MCP server for that. It's a separate process, not part of the
injected DLL:

1. The Explorer DLL runs in-game and owns all runtime access.
2. A local named pipe carries requests over to the Unity main thread.
3. `URK_UnityRuntimeExplorer_McpServer.exe` talks JSON-RPC over stdio to your
   MCP client.

The server finds running Explorer instances via:

```text
%LOCALAPPDATA%\URK\UnityRuntimeExplorer\bridges
```

If only one compatible game is running it attaches automatically; with
several running, pass `--game-pid <pid>`.

Permissions live in Explorer's **Config** tab, that's the real gate. Clients
can't grant themselves more access just by asking for it.

| Tool | What it's for |
| --- | --- |
| `runtime_status` | Backend, scene, GC, revision, diagnostics. |
| `discover_runtime` | One-shot search over GameObjects and loaded types. |
| `hierarchy_search` | Search by name, path, tag, instance ID, component, or behaviour type. |
| `find_game_objects` | Rank objects by name, components, scene, activity, role. |
| `get_selected_object` | Whatever's selected in the Explorer. |
| `inspect_game_object` | Identity, state, transform, components. |
| `list_components` | Component types and references on an object. |
| `read_member` | Read one specific field or property. |
| `inspect_managed_object` | Walk fields on a component or managed object (getters are opt-in). |
| `read_array` | Page through a managed array. |
| `decode_byte_array` | Decode a byte array as MessagePack, JSON, text, or hex. |
| `start_instance_scan` / `get_instance_scan` | Scan for managed instances and page through results. |
| `search_types` | Search loaded Mono/IL2CPP types and assemblies. |
| `search_members` | Search fields, properties, and methods across types. |
| `inspect_type` | Fields, properties, methods, signatures. |
| `list_method_traces` / `get_method_trace` | List and read active/retained method traces. |
| `build_call_graph` | Turn captured calls into a caller→target graph. |
| `get_activity_log` | Recent Explorer activity and MCP audit log. |
| `build_reference_graph` | Reference graph for the current selection. |
| `get_watch_history` | Watched values and recent changes. |
| `export_diagnostic_bundle` | Dump a diagnostic bundle to disk. |
| `write_member` | Write a field or property. |
| `mutate_game_object` | Rename, retag, relayer, activate, transform, duplicate, destroy. |
| `manage_component` | Add, remove, enable components. |
| `load_scene` | Load a scene by index or name. |

Object/component/type/trace references handed to the client are opaque tokens.
Raw pointers and native addresses never leave the process.

Config also has quick **Enable full access** / **Read-only** presets, plus
separate toggles for discovery, property getters, writes, tracing, invoking
methods, and destructive operations. Calls are validated, rate-limited, and
logged. Raw pointer access, native code execution, and assembly loading are
not exposed over MCP at all, since they'd bypass Explorer's object model
entirely.

A typical session looks like:

```text
runtime_status
  -> discover_runtime / find_game_objects / search_members
  -> inspect_game_object / inspect_type / start_instance_scan
  -> inspect_managed_object / read_array / decode_byte_array
  -> start_method_trace
  -> (go trigger the behavior in-game)
  -> get_method_trace / build_call_graph
  -> stop_method_trace
```

Ready-made client configs:

- [Claude Desktop](docs/examples/claude-desktop-config.json)
- [Codex](docs/examples/codex-config.toml)
- [Other stdio MCP clients](docs/examples/generic-mcp-config.json)

Swap in the real path to `URK_UnityRuntimeExplorer_McpServer.exe`, and add
`"--game-pid", "<pid>"` to `args` if you've got more than one game running.

Full setup, security notes, troubleshooting, and remote/tunnel info live in
[docs/MCP.md](docs/MCP.md).

## Compatibility

Inspection depends entirely on the target game's metadata and runtime layout,
so the same type or method can behave differently from one game to the next.
A member can be unavailable because it's stripped, the object is dead, or the
operation just isn't safe to do generically.

Tracing, live edits, and method calls can affect game state or crash it. Use a
restartable session and keep backups of anything that matters.

## Troubleshooting

**Explorer won't open**
- Make sure URKit loaded the DLL matching the game's Mono/IL2CPP backend.
- Check `URKit_logs.log` next to the game exe.
- Double check the proxy name and `Mods` layout.
- Press **F7** after reaching the main menu or a loaded scene, not before.

**MCP says no bridge available**
- Start the game (with the Explorer DLL loaded) before starting the MCP client.
- Check the helper exe path in your client config.
- More than one game running? Set `--game-pid <pid>`.
- Look in `%LOCALAPPDATA%\URK\UnityRuntimeExplorer\bridges` and in
  `URKit_logs.log`.

**An MCP reference expired**
- Re-run `find_game_objects` or `hierarchy_search` after a scene change.
  Object/component references survive normal hierarchy refreshes; graph
  references don't.

**A member won't read**
- It might be a property with side effects, stripped metadata, or attached to
  a destroyed object. The tool tells you it failed rather than making
  something up.

## Project status

Still actively developed. If something doesn't work on your game, an issue
with the following helps a lot:

- runtime (IL2CPP or Mono) and Unity version, if known
- the relevant bit of `URKit_logs.log`
- the type/method signature involved
- what you expected vs. what actually happened

## License

Copyright (c) 2026 Jadis0x. All rights reserved.

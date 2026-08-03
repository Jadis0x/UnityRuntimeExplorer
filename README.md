[![Views](https://hits.sh/github.com/Jadis0x/UnityRuntimeExplorer.svg?style=flat-square&label=views&color=blue)](https://hits.sh/github.com/Jadis0x/UnityRuntimeExplorer/)
[![Downloads](https://img.shields.io/github/downloads/Jadis0x/UnityRuntimeExplorer/total?style=flat-square&label=downloads&color=blue)](https://github.com/Jadis0x/UnityRuntimeExplorer/releases)

# UnityRuntimeExplorer

UnityRuntimeExplorer is a live runtime inspection and exploration tool for
Windows Unity games.

Its purpose is to expose the running game's scene hierarchy, GameObjects,
components, managed fields, properties, methods, references, and runtime
values in one place. It is intended for understanding and testing a game's
live state without rebuilding the game or writing a separate one-off inspector
for each project.

It is built with the [URKit](https://github.com/Jadis0x/URKit) native C++ SDK,
which generates the project and provides the Unity runtime services, hooks,
main-thread access, and ImGui integration used by the Explorer. It supports
both IL2CPP and Mono builds through separate DLLs.

![UnityRuntimeExplorer](showcase/ss1.png)

![UnityRuntimeExplorer](showcase/ss2.png)

## Project purpose

UnityRuntimeExplorer gives a Unity game a general-purpose runtime workspace:

- inspect what is currently loaded in the scene;
- understand how objects and components are connected;
- read and edit supported managed values while the game is running;
- invoke methods and trace their calls, arguments, and return values;
- inspect runtime-specific data through the metadata exposed by IL2CPP or Mono.

It is meant to be a reusable base for exploring different Unity games, not a
game-specific mod menu or a complete modding framework.

## What it can do

- Browse loaded scenes, hidden roots, and `DontDestroyOnLoad` objects.
- Search GameObjects by name, tag, or instance ID.
- Inspect GameObjects, components, fields, properties, and methods.
- Edit supported values at runtime.
- Copy and paste local transforms from the Inspector or the Hierarchy context
  menu.
- Duplicate, delete, enable, disable, and add components when the target game
  allows it.
- Follow managed object references and open returned objects in the Object
  Inspector.
- Invoke methods with supported signatures.
- Trace managed methods and inspect callers, arguments, return values, raw ABI
  data, and captured value types.
- Focus the camera on an object and highlight it in the game.
- Use a dockable ImGui interface with DX11, DX12, and OpenGL render paths.

The important part is that the Explorer uses the runtime metadata available in
the game. It does not pretend that every game has the same classes, layouts, or
Mono/IL2CPP behavior. If a type is not safe to read or write, the UI keeps the
metadata visible and reports the limitation instead of silently guessing.

## How it works

UnityRuntimeExplorer is a generated URKit mod project built on top of the URKit
SDK. The SDK supplies the common Mono/IL2CPP runtime layer, Unity wrappers,
hooks, main-thread callbacks, and ImGui support that the Explorer uses.

URKit also provides the loader workflows used to run the generated mod: the
normal proxy/`Mods` setup and the optional `URKitInjector.dll` external loader.
The Explorer DLL is the generated mod itself; it is not a separate injector.

There are two plugins:

```text
URK_Il2cpp_UnityRuntimeExplorer.dll
URK_Mono_UnityRuntimeExplorer.dll
```

Use the one that matches the game. The Explorer then talks to the appropriate
IL2CPP or Mono runtime layer while sharing the same Inspector and UI code.

## Installation

Install [URKit](https://github.com/Jadis0x/URKit) first. The current proxy,
`Mods`, injector, and compatibility details are documented in the URKit
repository. Then choose the matching workflow below for the generated Explorer
mod.

### Mods folder / proxy workflow

1. Create a `Mods` folder next to the game executable.
2. Put the correct URKit proxy next to the executable. Keep the original proxy
   filename; do not copy every proxy variant.
3. Put exactly one Explorer DLL in `Mods`:

   ```text
   URK_Il2cpp_UnityRuntimeExplorer.dll   # IL2CPP game
   URK_Mono_UnityRuntimeExplorer.dll     # Mono game
   ```

4. Start the game through the normal URKit launch path.
5. Press **F7** to open or close the Explorer.

### Injector workflow

The optional `URKitInjector.dll` workflow can load the generated mod without a proxy
or a `Mods` folder:

1. Inject `URKitInjector.dll` into the supported Windows x64 game.
2. Select the URKit `.ini` file when prompted.
3. Select the Explorer DLL matching the game's runtime.

Do not inject the Explorer DLL directly. It must be loaded by URKit.

If nothing appears, check `URKit_logs.log` beside the game executable first.
The log usually makes a wrong proxy name, wrong backend, or missing runtime API
obvious.

## Building

The project is built on Windows with CMake, Ninja, and Clang. The first configure
fetches ImGui from GitHub, so network access is needed once.

Requirements:

- Windows 10 or newer, x64
- CMake 3.28+
- LLVM/Clang
- Ninja

From the repository root:

```powershell
cmake --preset clang-release
cmake --build --preset clang-release --parallel
```

The DLLs are written to:

```text
out/build/clang-release/URK_Il2cpp_UnityRuntimeExplorer.dll
out/build/clang-release/URK_Mono_UnityRuntimeExplorer.dll
```

Run the test suite with:

```powershell
ctest --test-dir out/build/clang-release --output-on-failure
```

## A note about compatibility

This is a runtime tool, so compatibility depends on the game, its Unity
version, its generated metadata, and the URKit runtime that loads the mod.
The same method name can have a different ABI or different managed types in
another game. This is why the Inspector resolves metadata at runtime instead
of relying on a hardcoded list of game types.

Tracing and live editing can make a game unstable. Use it on a game and save
data that you can restart.

Mono support also depends on the standard embedding exports shipped by the
game. If a game removes an export required by an advanced feature, the Explorer
reports that capability as unavailable.

## Project status

This project is still evolving. The core scene/object Inspector is useful now,
but there are many Unity versions, stripped runtimes, generated bridge types,
and unusual value layouts to deal with. Compatibility reports, trace examples,
and small reproducible cases are more useful than generic “doesn't work”
reports.

If you open an issue, include:

- game runtime: IL2CPP or Mono
- Unity version if known
- the relevant part of `URKit_logs.log`
- the method/type signature involved
- what the Explorer showed and what you expected

## License

Copyright (c) 2026 Jadis0x. All rights reserved.

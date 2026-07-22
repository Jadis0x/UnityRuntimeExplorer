# UnityRuntimeExplorer

UnityRuntimeExplorer is a small runtime inspector for Unity games. It runs
inside a game through [URKit](https://github.com/Jadis0x/URKit) and gives you a
live view of the game's scenes, GameObjects, components, fields, properties,
and methods.

![UnityRuntimeExplorer showcase](showcase/ss1.png)

The project is still early, but the basic IL2CPP side is now in place. The
current release is focused on **Windows x64 IL2CPP games**. Mono support is
planned, but it will come after the IL2CPP runtime and inspection layer are in a
more stable state.

## Why this exists

Most Unity runtime tools either focus on one game or make you work with a
large amount of game-specific setup. The goal here is to have a small,
reusable explorer that can be loaded by URKit and used to understand what a
Unity game is doing while it is running.

It is useful for exploring scenes, checking component data, testing small
runtime changes, and debugging game-specific behavior. It is not intended to
be a complete modding framework.

## Current features

- Browse loaded scenes, hidden roots, and `DontDestroyOnLoad` objects.
- Search for GameObjects and inspect their components.
- Select, duplicate, delete, enable, and disable GameObjects.
- Edit supported Transform values and other supported fields and properties.
- Add components where the target runtime allows it.
- Inspect methods and invoke signatures that can be handled safely.
- Follow references to other GameObjects from the inspector.
- Focus the game camera on a selected object and restore the previous camera
  position.
- Highlight selected objects in the game and show useful camera/distance
  information.
- Trace a selected native IL2CPP method with a limited call history.
- Use a dockable ImGui interface with DX11, DX12, and OpenGL backends.

The inspector deliberately refuses to guess when an IL2CPP type or method
signature is too runtime-specific to handle safely. In those cases it shows
the available metadata instead of attempting an unsafe read, write, or call.

## Requirements

- Windows 10 or later, 64-bit.
- A Unity game built with IL2CPP.
- [URKit v0.1.0 or later](https://github.com/Jadis0x/URKit) installed next to
  the game executable.
- A URKit proxy DLL that is compatible with the target game.

The proxy is important. A proxy that works with one game or Unity version may
not work with another one. This project currently does not support Mono games.

## Installation

First install URKit and make sure it loads correctly without this mod.

Then:

1. Create a `Mods` folder next to the game's executable.
2. Copy `URK_Il2cpp_UnityRuntimeExplorer.dll` into that folder.
3. Start the game.
4. Press **F7** to open or close the Explorer menu.

If nothing appears, check `URKit_logs.log` next to the game executable. Recent
URKit updates also support injecting/loading the plugin into the target game,
so the Explorer no longer has to be installed only through the original
`Mods`-folder workflow. It still requires URKit and a proxy that matches the
target game.

## Building from source

The project is built with CMake, Ninja, and Clang on Windows. ImGui is fetched
from GitHub during the first configure, so the first build needs network
access.

You will need:

- CMake 3.28 or newer
- LLVM/Clang
- Ninja

From the repository root, run:

```powershell
cmake --preset clang-release
cmake --build --preset clang-release --parallel
```

The resulting DLL will be at:

```text
out/build/clang-release/URK_Il2cpp_UnityRuntimeExplorer.dll
```

For a clean Release build:

```powershell
Remove-Item -LiteralPath .\out\build\clang-release -Recurse -Force
cmake --preset clang-release
cmake --build --preset clang-release --parallel
```

## Limitations

This is a runtime tool, so the result depends on the game, its Unity version,
its IL2CPP metadata, and the URKit proxy being used. Calling methods or editing
live objects can also make a game unstable. Use it with a game and save data
you can afford to restart.

Mono support is on the roadmap. The current work is intentionally focused on
building a reliable IL2CPP foundation first; once that layer is in better shape,
the project can be extended to support Mono runtimes as well.

## Roadmap

- Stabilize the IL2CPP inspection and invocation layer.
- Add support for more Unity and game-specific type signatures.
- Add Mono support after the IL2CPP foundation is ready.
- Improve compatibility across Unity versions.

Copyright (c) 2026 Jadis0x. All rights reserved.

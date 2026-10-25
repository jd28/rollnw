[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![ci](https://github.com/jd28/rollnw/actions/workflows/ci.yml/badge.svg)](https://github.com/jd28/rollnw/actions/workflows/ci.yml)
[![CodeQL](https://github.com/jd28/rollnw/actions/workflows/codeql-analysis.yml/badge.svg)](https://github.com/jd28/rollnw/actions/workflows/codeql-analysis.yml)
[![codecov](https://codecov.io/gh/jd28/rollnw/branch/main/graph/badge.svg?token=79PNROEEUU)](https://codecov.io/gh/jd28/rollnw)

# rollnw

rollnw is a C++ framework for RPG tools and runtimes, inspired by Neverwinter
Nights and growing toward a modern authored game and toolset.

The project is in *active development*. Pin a Git revision and rebuild consumers
when updating; APIs and the C++ ABI can change. See the [release guide](docs/releases.md)
for snapshot and tool versioning.

## [nw](https://jd28.github.io/rollnw/)

The shared C++ library for content formats, resource management, game objects,
and rules. Its [renderer](lib/nw/render/README.md) brings NWN and glTF
assets into a common runtime with PBR materials, skinned animation, particles,
lighting, and shadows. These libraries underpin the tools below and can be used
in other applications.

## [smalls](lib/nw/smalls/docs/index.md)

A statically typed, embeddable scripting language for game rules and tooling.
Modules, generics, arrays, and maps support reusable scripts that connect to
the library through native functions. The
[VS Code extension](tools/vscode-smalls/README.md) provides syntax highlighting
and language-server support for writing and navigating Smalls code.

## [client](tools/client/README.md)

The module viewer and authoring workbench, installed as `rollnw-client`.
Import a module into an editable project, explore its areas and resources,
and edit objects and terrain with previews, undo, and save. Focused editors
cover creature appearance and progression, item properties, inventory, and
other supported object data.

**Watch the demos:** [Overview](https://youtu.be/1zftndVT2Is) · [Area Editing](https://youtu.be/PvT9pjiYuU0) · [Item Interactions](https://youtu.be/EhH9qhWr8ok)

## [mudl](tools/mudl/README.md)

A model and effects viewer for NWN and glTF assets. Inspect model hierarchies,
play animations and effects, and check materials through the shared renderer.
Its command-line tools also produce repeatable screenshots, turntable sequences,
and fixed-step rendering benchmarks, including offscreen runs.

## Build

Requires CMake 3.21+ and a C++20 compiler. Renderer tools currently only run on Linux and Windows.
For a Linux renderer build with Ninja:

```sh
cmake --preset linux-renderer-dev
cmake --build --preset linux-renderer-dev --target rollnw-client mudl
./build-renderer/tools/client/rollnw-client
```

Use `windows-renderer-dev` for Windows. Library development presets are
`linux-dev`, `macos-dev`, and `windows-dev`; see [CMakePresets.json](CMakePresets.json)
for the available configurations.

To work with NWN content, install NWN:EE and any required hak files. If discovery
fails, set `NWN_ROOT` to the game installation and `NWN_HOME` to its user directory.
In the toolset, choose **Import Module...** on Home. See the
[toolset guide](tools/client/README.md#getting-started) for the workflow.

## More

[Documentation](https://jd28.github.io/rollnw/) ·
[Tests](tests/) ·
[Local issues](issues/README.md)

MIT licensed. Thanks to BioWare, Beamdog, and the open-source projects that make
rollnw possible; dependencies retain their own licenses.

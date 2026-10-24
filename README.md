[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![ci](https://github.com/jd28/rollnw/actions/workflows/ci.yml/badge.svg)](https://github.com/jd28/rollnw/actions/workflows/ci.yml)
[![CodeQL](https://github.com/jd28/rollnw/actions/workflows/codeql-analysis.yml/badge.svg)](https://github.com/jd28/rollnw/actions/workflows/codeql-analysis.yml)
[![codecov](https://codecov.io/gh/jd28/rollnw/branch/main/graph/badge.svg?token=79PNROEEUU)](https://codecov.io/gh/jd28/rollnw)

# rollnw

rollnw is a C++ framework for RPG tools and runtimes, inspired by Neverwinter
Nights and growing toward a modern authored game and toolset.

- **[nw](https://jd28.github.io/rollnw/)** —  library for content formats, resources,
  rules, and a [Vulkan renderer](lib/nw/render/README.md) with PBR materials,
  animation, particles, lighting, and shadows.
- **[smalls](lib/nw/smalls/docs/index.md)** — a statically typed scripting language
  for game rules and tooling, with a [VS Code extension](tools/vscode-smalls/README.md).
- **[client](tools/client/README.md)** — [Demo](https://youtu.be/1zftndVT2Is) — import modules, explore areas, and edit
  objects and terrain with previews, undo, and save.
- **[mudl](tools/mudl/README.md)** — inspect models and effects, capture screenshots,
  and benchmark rendering with NWN and glTF assets.

The project is in active development. Pin a Git revision and rebuild consumers
when updating; APIs and the C++ ABI can change. See the [release guide](docs/releases.md)
for snapshot and tool versioning.

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

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![ci](https://github.com/jd28/rollnw/actions/workflows/ci.yml/badge.svg)](https://github.com/jd28/rollnw/actions?query=workflow%3Aci)
[![CodeQL](https://github.com/jd28/rollnw/actions/workflows/codeql-analysis.yml/badge.svg)](https://github.com/jd28/rollnw/actions/workflows/codeql-analysis.yml)
[![codecov](https://codecov.io/gh/jd28/rollnw/branch/main/graph/badge.svg?token=79PNROEEUU)](https://codecov.io/gh/jd28/rollnw)

# rollNW

rollNW is an homage to Neverwinter Nights in C++. It started as reusable NWN infrastructure and is broadening toward support for a modern authored RPG toolset/game: content formats, rules, scripting, rendering validation, networking foundations, and the runtime services needed to make those pieces authorable and playable. See the [docs](https://jd28.github.io/rollnw/) and [tests](https://github.com/jd28/rollnw/tree/main/tests) for more info. Opening an IDE is going to get the most current view.

**Transition warning:** rollNW is in active transition. Older docs and APIs may show a narrower NWN-library cross section of the project. Until there is a real release, assume APIs and subsystem boundaries can move.

## Features

- Smalls is a statically-typed scripting language designed for RPG tooling. It is the active rules/script authoring path and explores replacing NWScript-style workflows with modern features (modules, generics, first-class arrays/maps) while staying small enough to embed in rollNW-based tools. See the [docs](https://jd28.github.io/rollnw/smalls/) for a language spec.
- The beginnings of a novel rules system and combat engine built on Smalls.
- [`nw::gfx`](https://jd28.github.io/rollnw/gfx/), a thin Vulkan-focused rendering layer intended to support renderer experiments, headless graphics validation, and eventual authored-toolset/game viewports.
- [`nw::render`](https://jd28.github.io/rollnw/render/), the 3D renderer built on `nw::gfx`: Forward+ lighting, common model instances for glTF and NWN-derived assets, skinned animation through ozz, particles/VFX, shadows, screenshots, and focused parity tests for moving legacy content toward a modern PBR path.
- Objects (i.e. Creatures, Waypoints, etc) are implemented at a toolset level. In other words, their features cover blueprints and area instances, with support for effects and item properties. Loading objects from resman or the filesystem is transparent whether the source is GFF or JSON. See the [legacy object notes](https://jd28.github.io/rollnw/objects/) for runtime handle/loading context.
- A Resource Manager that can load all NWN containers (e.g. erf, key, nwsync) and also Zip files.
- Implementations of pretty much every NWN file format
- An implementation of NWN's localization system focused on utf8 everywhere.
- A recursive descent [NWScript Parser](https://jd28.github.io/rollnw/script/). This remains useful for NWN compatibility, but new rules and scripting experiments are moving toward Smalls.

## Tools

- [`mudl`](https://jd28.github.io/rollnw/mudl/), a renderer-backed NWN model/spell/VFX viewer and headless capture tool used to validate creature assembly, particle and programmable FX playback, glTF/PBR rendering, and the asset paths a future toolset needs to trust. Renderer design notes live under the [renderer docs](https://jd28.github.io/rollnw/render/), including the [particle system overview](https://jd28.github.io/rollnw/render/particle_system.html).

## Goals

- aims to implement the reusable RPG, toolset, resource, rendering, networking, and runtime foundations for a modern authored game inspired by NWN, with current effort on the practical pieces (rules, content formats, renderer validation, authoring workflows) a playable toolset/game will need.
- focuses on authoring and usage, instead of doing things the Aurora Engine Way.
- follows [utf8 everywhere](https://utf8everywhere.org/).
- hews as close to [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines) as possible.
- aims to be as easily bindable as possible to other languages.  I.e. only library specific or STL types at API boundaries.

## Building / Installing

The library uses [CMakePresets.json](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html) as its build system. The naming convention for non-ci presets is `{platform}[-renderer][-{build tool}]-dev[-debug]`. Renderer presets build the platform's supported graphics tools. Renderer and `mudl` builds are supported on Linux and Windows only; macOS is not supported for the renderer because MoltenVK is not wired up yet. In debug presets, build files are written to a debug build directory. On macOS and Linux, Ninja is the build tool of choice so it is omitted. On Windows the default build tool is Visual Studio 2022.

Examples:
```
$ cmake --preset=linux-dev
$ cmake --preset=linux-renderer-dev
$ cmake --preset=macos-dev-debug
$ cmake --preset=windows-vs2019-dev-debug
$ cmake --preset=windows-renderer-dev
$ cmake --preset=windows-dev
```

Once your configuration is done, everything is the same between platforms. To build:
```
$ cmake --build --preset=default
$ cmake --build --preset=debug
```

To install all binaries and test data to local `bin` dir (build or build-debug):
```
$ cmake --install build --prefix=.
$ cd bin
$ ./rollnw_benchmark
```

To run ctest:

```
$ ctest --preset=default
```

## History

A lot of what's here was written in the 2011-2015 range as part of personal minimalist toolset, modernized and with new EE stuff added.  In some sense, it's a work of historical fiction -- it's what I'd have suggested at the start of NWN:EE: get the game and the community on the same set of libraries. Similarly to an older project that asked what if Bioware had stuck with Lua? The answer to that was pretty positive: a decade ahead, at least, of nwscript.

## Credits

- [Bioware](https://bioware.com), [Beamdog](https://beamdog.com) - The game itself
- [abseil](https://abseil.io/) - Foundational
- [GoogleTest](https://github.com/google/googletest) - Testing
- [glm](https://www.opengl.org/sdk/libs/GLM/) - Mathematics
- [Vulkan](https://www.vulkan.org/), [Dear ImGui](https://github.com/ocornut/imgui) - Graphics and tool UI
- [loguru](https://github.com/emilk/loguru), [fmt](https://github.com/fmtlib/fmt) - Logging
- [stbi_image](https://github.com/nothings/stb), [NWNExplorer](https://github.com/virusman/nwnexplorer), [SOIL2](https://github.com/SpartanJ/SOIL2/) - Image/Texture loading.
- [inih](https://github.com/benhoyt/inih) - INI, SET parsing
- [nholmann_json](https://github.com/nlohmann/json) - JSON
- [libiconv](https://www.gnu.org/software/libiconv/), [boost::nowide](https://github.com/boostorg/nowide) - i18n, string conversion
- [Python-Markdown](https://python-markdown.github.io/) - documentation rendering

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![linux](https://github.com/jd28/libnw/actions/workflows/linux.yml/badge.svg)](https://github.com/jd28/libnw/actions?query=workflow%3Alinux)
[![macos](https://github.com/jd28/libnw/actions/workflows/macos.yml/badge.svg)](https://github.com/jd28/libnw/actions?query=workflow%3Amacos)
[![windows](https://github.com/jd28/libnw/actions/workflows/windows.yml/badge.svg)](https://github.com/jd28/libnw/actions?query=workflow%3Awindows)
[![Language grade: C/C++](https://img.shields.io/lgtm/grade/cpp/g/jd28/libnw.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/jd28/libnw/context:cpp)
[![CodeQL](https://github.com/jd28/libnw/actions/workflows/codeql-analysis.yml/badge.svg)](https://github.com/jd28/libnw/actions/workflows/codeql-analysis.yml)

# libnw

```
We shall not cease from exploration
And the end of all our exploring
Will be to arrive where we started
And know the place for the first time.
   --T.S. Eliot, Little Gidding, The Four Quartets.
```

libnw is a simple modern static C++ library for Neverwinter Nights(:EE) file formats and objects, that

* focuses on usage, above all.  Most of NWN's file formats are incredibly simple, a library implementing them should be no different.
* focuses on live objects, not serialized formats.  Converting NWN(:EE) file formats to textual representations or modifying serialized formats (e,g. GFF) in situ are well handled by other tools/libraries.
* follows the principles of [utf8 everywhere](https://utf8everywhere.org/).  This is aspirational and a huge can of worms.  Currently, here, to help facilitate this goal color codes are sanitized to \<cXXXXXX\>, where XX is hexidecimal.
* hews as close to [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines) as possible.

Find the status of [File Formats](lib/nw/formats/README.md), [Containers](lib/nw/resources/README.md), [Objects](lib/nw/objects/README.md) in their respective READMEs.

## Quickstart

[![Open in Gitpod](https://gitpod.io/button/open-in-gitpod.svg)](https://gitpod.io/#https://github.com/jd28/libnw)

[Github CodeSpaces](https://github.com/features/codespaces) will be added when it becomes publicly available.

## Building

libnw uses cmake as it's build system and more specifically [CMakePresets.json](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html).

Install [vcpkg](https://github.com/microsoft/vcpkg).  Note that windows build requires VS Studio 2019 (Community Edition), but it doesn't have to.  All vcpkg packages required will be built automatically.

The example below is building on Linux, but it's virtually the same for all platforms.  The only difference is the configuration preset.

```
$ export VCPKG_ROOT=path/to/vcpkg
$ export NWN_ROOT=path/to/game
$ export NWN_USER=path/to/nwn-user-dir
$ cd path/to/libnw
$ cmake --preset default-test
$ cmake --build --preset default
$ ctest --preset default
```

## History

A lot of what's here was written in the 2011-2015 range as part of personal minimalist toolset, modernized and with new EE stuff added.  In some sense, it's a work of historical fiction -- it's what I'd have suggested at the start of NWN:EE: get the game and the community on the same set of libraries.  Similarly to an older project that asked ["what if Bioware had stuck with Lua?"](https://solstice.readthedocs.io/en/latest/).  The awnser to that was pretty positive: a decade ahead, at least, of nwscript.

## General Warning

This library is a work-in-progress.  There will be serious refactorings and until there is a real release, it should be assumed the library is in flux.

There is no documentation besides the source itself, unless something is found that isn't a javadoc-esque thing, that's likely how it will remain.

If you've somehow found your way here by accident, you should head to [neverwinter.nim](https://github.com/niv/neverwinter.nim), those are the official tools of NWN:EE and can handle basic workflows out of the box.

## Moonshots

At some point it became fashionable in the NWN community to passive aggressively ask "why?" to every idea.  But, to paraphrase Tennyson, ours isn't to question why, it's but to do and die and learn and maybe make neat things.  In that spirit, here is a list of crazy projects that this library hopes to facilitate and that all fly in the face of "WHY?".

* A nwscript [Language Server](https://en.wikipedia.org/wiki/Language_Server_Protocol)
* A modern, cross-platform nwexplorer.
* And, of course, the ever elusive open source NWN Toolset, with plugins, scripting, and a built-in console.

## Credits

- [Bioware](https://bioware.com), [Beamdog](https://beamdog.com) - The game itself
- [vcpkg](https://github.com/microsoft/vcpkg) - Package Management
- [abseil](https://abseil.io/) - Foundational
- [Catch2](https://github.com/catchorg/Catch2) - Testing
- [loguru](https://github.com/emilk/loguru), [fmt](https://github.com/fmtlib/fmt) - Logging
- [glm](https://www.opengl.org/sdk/libs/GLM/) - Mathematics
- [stbi_image](https://github.com/nothings/stb), [NWNExplorer](https://github.com/virusman/nwnexplorer), [SOIL2](https://github.com/SpartanJ/SOIL2/) - Image/Texture loading.
- [inih](https://github.com/benhoyt/inih) - INI, SET parsing
- [nholmann_json](https://github.com/nlohmann/json) - JSON
- [toml++](https://github.com/marzer/tomlplusplus/) - For settings.tml

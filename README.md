[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![linux](https://github.com/jd28/libnw/actions/workflows/linux.yml/badge.svg)](https://github.com/jd28/libnw/actions?query=workflow%3Alinux)
[![macos](https://github.com/jd28/libnw/actions/workflows/macos.yml/badge.svg)](https://github.com/jd28/libnw/actions?query=workflow%3Amacos)
[![windows](https://github.com/jd28/libnw/actions/workflows/windows.yml/badge.svg)](https://github.com/jd28/libnw/actions?query=workflow%3Awindows)
[![Language grade: C/C++](https://img.shields.io/lgtm/grade/cpp/g/jd28/libnw.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/jd28/libnw/context:cpp)
[![CodeQL](https://github.com/jd28/libnw/actions/workflows/codeql-analysis.yml/badge.svg)](https://github.com/jd28/libnw/actions/workflows/codeql-analysis.yml)
[![codecov](https://codecov.io/gh/jd28/libnw/branch/main/graph/badge.svg?token=79PNROEEUU)](https://codecov.io/gh/jd28/libnw)

# libnw

```
We shall not cease from exploration
And the end of all our exploring
Will be to arrive where we started
And know the place for the first time.
   --T.S. Eliot, Little Gidding, The Four Quartets.
```

libnw is a simple modern static C++ library for Neverwinter Nights (and some Enhanced Edition) file formats and objects, that

- aims to implement an RPG engine inspired by NWN, excluding graphics and networking.
- focuses on usage, instead doing things the Aurora Engine Way.
- follows [utf8 everywhere](https://utf8everywhere.org/).
- hews as close to [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines) as possible.
- aims to be as easily bindable as possible to other languages.  I.e. only library specific or STL types at API boundaries.

The library will be renamed as there was already an Open Knights project named [libnw](https://sourceforge.net/projects/openknights/files/libnw/).

## Quickstart - Open VS Code in your Browser

[![Open in Gitpod](https://gitpod.io/button/open-in-gitpod.svg)](https://gitpod.io/#https://github.com/jd28/libnw)

[Github Codespaces](https://github.com/features/codespaces) is available to those in the beta.

## Building

libnw uses cmake as its build system and more specifically [CMakePresets.json](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html).

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

## Using

While the library is far from done, basic usage would be as follows.

```cpp
#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/profiles/nwn1/Profile.hpp>

int main(int argc, char* argv[])
{
   // Initialize logger
   nw::init_logger(argc, argv);

   // Sets config for the system, paths, version, etc.
   auto info = nw::probe_nwn_install();
   nw::kernel::config().initialize({
       info.version,
       info.install,
       info.user,
   });

   // Initializes all systems
   nw::kernel::services().start();

   // Loads a game profile, in this case something vaguely like NWN1
   nw::kernel::load_profile(new nwn1::Profile);

   // Do neat stuff

   return 0;
}
```

## History

A lot of what's here was written in the 2011-2015 range as part of personal minimalist toolset, modernized and with new EE stuff added.  In some sense, it's a work of historical fiction -- it's what I'd have suggested at the start of NWN:EE: get the game and the community on the same set of libraries.  Similarly to an older project that asked ["what if Bioware had stuck with Lua?"](https://solstice.readthedocs.io/en/latest/).  The awnser to that was pretty positive: a decade ahead, at least, of nwscript.

## General Warning

This library is a work-in-progress.  There will be serious refactorings and until there is a real release, it should be assumed the library is in flux.

If you've somehow found your way here by accident, you should head to [neverwinter.nim](https://github.com/niv/neverwinter.nim), those are the official tools of NWN:EE and can handle basic workflows out of the box.

## Moonshots

You make ask yourself, why?  But, to paraphrase Tennyson, ours isn't to question why, it's but to do and die and learn and maybe make neat things.  In that spirit, here is a list of crazy projects that this library hopes to facilitate and that all fly in the face of "WHY?".

* A nwscript [Language Server](https://en.wikipedia.org/wiki/Language_Server_Protocol)
* A modern, cross-platform nwexplorer.
* And, of course, the ever elusive open source NWN Toolset, with plugins, scripting, and a built-in console.

## Credits

- [Bioware](https://bioware.com), [Beamdog](https://beamdog.com) - The game itself
- [vcpkg](https://github.com/microsoft/vcpkg) - Package Management
- [abseil](https://abseil.io/) - Foundational
- [Catch2](https://github.com/catchorg/Catch2) - Testing
- [flecs](https://github.com/SanderMertens/flecs) - Entity-Component-System
- [glm](https://www.opengl.org/sdk/libs/GLM/) - Mathematics
- [loguru](https://github.com/emilk/loguru), [fmt](https://github.com/fmtlib/fmt) - Logging
- [stbi_image](https://github.com/nothings/stb), [NWNExplorer](https://github.com/virusman/nwnexplorer), [SOIL2](https://github.com/SpartanJ/SOIL2/) - Image/Texture loading.
- [inih](https://github.com/benhoyt/inih) - INI, SET parsing
- [nholmann_json](https://github.com/nlohmann/json) - JSON
- [toml++](https://github.com/marzer/tomlplusplus/) - For settings.tml
- [libiconv](https://www.gnu.org/software/libiconv/), [boost::nowide](https://github.com/boostorg/nowide) - i18n, string conversion

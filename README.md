[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![linux](https://github.com/jd28/rollnw/actions/workflows/linux.yml/badge.svg)](https://github.com/jd28/rollnw/actions?query=workflow%3Alinux)
[![macos](https://github.com/jd28/rollnw/actions/workflows/macos.yml/badge.svg)](https://github.com/jd28/rollnw/actions?query=workflow%3Amacos)
[![windows](https://github.com/jd28/rollnw/actions/workflows/windows.yml/badge.svg)](https://github.com/jd28/rollnw/actions?query=workflow%3Awindows)
[![python](https://github.com/jd28/rollnw/actions/workflows/python.yml/badge.svg)](https://github.com/jd28/rollnw/actions?query=workflow%3Apython)
[![PyPI version](https://badge.fury.io/py/rollnw.svg)](https://badge.fury.io/py/rollnw)
[![Language grade: C/C++](https://img.shields.io/lgtm/grade/cpp/g/jd28/rollnw.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/jd28/rollnw/context:cpp)
[![CodeQL](https://github.com/jd28/rollnw/actions/workflows/codeql-analysis.yml/badge.svg)](https://github.com/jd28/rollnw/actions/workflows/codeql-analysis.yml)
[![codecov](https://codecov.io/gh/jd28/rollnw/branch/main/graph/badge.svg?token=79PNROEEUU)](https://codecov.io/gh/jd28/rollnw)

# rollNW

```
We shall not cease from exploration
And the end of all our exploring
Will be to arrive where we started
And know the place for the first time.
   --T.S. Eliot, Little Gidding, The Four Quartets.
```

rollNW is a simple modern static C++/Python library for Neverwinter Nights (and some Enhanced Edition) file formats and objects, that

- aims to implement an RPG engine inspired by NWN, excluding graphics and networking.
- focuses on usage, instead of doing things the Aurora Engine Way.
- follows [utf8 everywhere](https://utf8everywhere.org/).
- hews as close to [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines) as possible.
- aims to be as easily bindable as possible to other languages.  I.e. only library specific or STL types at API boundaries.

See the [docs](https://jd28.github.io/rollnw/) and [tests](https://github.com/jd28/rollnw/tree/main/tests) for more info.

**This library is a work-in-progress.  There will be serious refactoring and until there is a real release, it should be assumed the library is a work-in-progress.**

## Features

- An [ASCII Model Parser](https://jd28.github.io/rollnw/structure/model.html)
- A recursive decent [NWScript Parser](https://jd28.github.io/rollnw/structure/script.html)
- Implementations of pretty much every [NWN File Format](https://jd28.github.io/rollnw/structure/formats.html)
- A Resource Manager that can load all NWN containers (e.g. erf, key, nwsync) and also Zip files.
- An implementation of NWN's [Localization System](https://jd28.github.io/rollnw/structure/i18n.html) focused on
  utf8 everywhere.
- A [Serialization System](https://jd28.github.io/rollnw/structure/serialization.html) that can transparently
  handle GFF or JSON.
- The beginnings of a novel [Rules System](https://jd28.github.io/rollnw/structure/rules.html) designed for
  easily adding, overriding, expanding, or removing any rule and reasonable performance

## Quickstart - Open VS Code in your Browser

[![Open in Gitpod](https://gitpod.io/button/open-in-gitpod.svg)](https://gitpod.io/#https://github.com/jd28/rollnw)

[Github Codespaces](https://github.com/features/codespaces) is available to those in the beta.

## History

A lot of what's here was written in the 2011-2015 range as part of personal minimalist toolset, modernized and with new EE stuff added.  In some sense, it's a work of historical fiction -- it's what I'd have suggested at the start of NWN:EE: get the game and the community on the same set of libraries.  Similarly to an older project that asked ["what if Bioware had stuck with Lua?"](https://solstice.readthedocs.io/en/latest/).  The answer to that was pretty positive: a decade ahead, at least, of nwscript.

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
- [glm](https://www.opengl.org/sdk/libs/GLM/) - Mathematics
- [loguru](https://github.com/emilk/loguru), [fmt](https://github.com/fmtlib/fmt) - Logging
- [stbi_image](https://github.com/nothings/stb), [NWNExplorer](https://github.com/virusman/nwnexplorer), [SOIL2](https://github.com/SpartanJ/SOIL2/) - Image/Texture loading.
- [inih](https://github.com/benhoyt/inih) - INI, SET parsing
- [nholmann_json](https://github.com/nlohmann/json) - JSON
- [toml++](https://github.com/marzer/tomlplusplus/) - For settings.tml
- [libiconv](https://www.gnu.org/software/libiconv/), [boost::nowide](https://github.com/boostorg/nowide) - i18n, string conversion
- [doxygen](https://doxygen.nl/), [sphinx](https://www.sphinx-doc.org/en/master/), [breathe](https://breathe.readthedocs.io/en/latest/) - documentation

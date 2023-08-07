[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![ci](https://github.com/jd28/rollnw/actions/workflows/ci.yml/badge.svg)](https://github.com/jd28/rollnw/actions?query=workflow%3Aci)
[![CodeQL](https://github.com/jd28/rollnw/actions/workflows/codeql-analysis.yml/badge.svg)](https://github.com/jd28/rollnw/actions/workflows/codeql-analysis.yml)
[![codecov](https://codecov.io/gh/jd28/rollnw/branch/main/graph/badge.svg?token=79PNROEEUU)](https://codecov.io/gh/jd28/rollnw)
[![Documentation Status](https://readthedocs.org/projects/rollnw/badge/?version=latest)](https://rollnw.readthedocs.io/en/latest/?badge=latest)
[![PyPI version](https://badge.fury.io/py/rollnw.svg)](https://badge.fury.io/py/rollnw)

# rollNW

rollNW is an homage to Neverwinter Nights in C++ and Python.  See the [docs](https://rollnw.readthedocs.io/en/latest/) and [tests](https://github.com/jd28/rollnw/tree/main/tests) for more info, or open an IDE in browser in the quickstart section below.

**This library is a work-in-progress.  There will be serious refactoring and until there is a real release, it should be assumed the library is a work-in-progress.**

## Features

- The beginnings of a novel [Rules System](https://rollnw.readthedocs.io/en/latest/structure/rules.html) designed for easily adding, overriding, expanding, or removing any rule and reasonable performance
- A [combat engine](https://github.com/jd28/rollnw/blob/main/profiles/nwn1/combat.cpp) built on the above that's nearing being able to simulate melee battles.
- Objects (i.e. Creatures, Waypoints, etc) are implemented at a toolset level.  Or in other words their features cover blueprints, area instances, with support for effects and item properties.  They are still missing some new EE things.  Player Characters are read only, for now.
- A recursive decent [NWScript Parser](https://rollnw.readthedocs.io/en/latest/structure/script.html)
- Implementations of pretty much every [NWN File Format](https://rollnw.readthedocs.io/en/latest/structure/formats.html)
- An [Model Parser](https://rollnw.readthedocs.io/en/latest/structure/model.html).  See the [mudl](https://github.com/jd28/mudl) model viewer side project.
- A Resource Manager that can load all NWN containers (e.g. erf, key, nwsync) and also Zip files.
- An implementation of NWN's [Localization System](https://rollnw.readthedocs.io/en/latest/structure/i18n.html) focused on utf8 everywhere.

## Goals

- aims to implement an RPG engine inspired by NWN, excluding graphics and networking.
- focuses on usage, instead of doing things the Aurora Engine Way.
- follows [utf8 everywhere](https://utf8everywhere.org/).
- hews as close to [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines) as possible.
- aims to be as easily bindable as possible to other languages.  I.e. only library specific or STL types at API boundaries.

## Quickstart - Open VS Code in your Browser

[![Open in Gitpod](https://gitpod.io/button/open-in-gitpod.svg)](https://gitpod.io/#https://github.com/jd28/rollnw)

[Github Codespaces](https://github.com/features/codespaces) is available to those in the beta.

## History

A lot of what's here was written in the 2011-2015 range as part of personal minimalist toolset, modernized and with new EE stuff added.  In some sense, it's a work of historical fiction -- it's what I'd have suggested at the start of NWN:EE: get the game and the community on the same set of libraries.  Similarly to an older project that asked ["what if Bioware had stuck with Lua?"](https://solstice.readthedocs.io/en/latest/).  The answer to that was pretty positive: a decade ahead, at least, of nwscript.

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

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

# libnw

```
We shall not cease from exploration
And the end of all our exploring
Will be to arrive where we started
And know the place for the first time.
   --T.S. Eliot, Little Gidding, The Four Quartets.
```

libnw is a simple modern C++ library for NWN(:EE) file formats, that

* provides a place to learn and re-learn about NWN and modern C++ projects.
* might be used to make neat stuff.
* hews as close to [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines) as possible.

and what it's not,

* a library for converting NWN(:EE) file formats to textual representations.
* a library for modifying seralized formats (e,g. GFF) in situ.
* a library competing with, or a commentary on, any other project, library, and/or authors thereof.

A lot of what's here was written in the 2011-2015 range as part of personal minimalist toolset, modernized and with new EE stuff added.  In some sense, it's a work of historical fiction now and will be the basis for other such projects, as such it doesn't aim at any particular practical use. Similar to an older project that asked ["what if Bioware had stuck with Lua?"](https://solstice.readthedocs.io/en/latest/)

If you've somehow found your way here by accident, you should head to [neverwinter.nim](https://github.com/niv/neverwinter.nim), those are the official tools of NWN:EE and can handle basic workflows out of the box.

## Status

### Containers

| Type    |  Read      | Write  | In Situ Modifications
| ------- | ---------- | ------ | ----------------------|
| Key/Bif | Yes | Never | Never |
| ERF | Yes | Maybe | Maybe |
| NWSync | Yes | Never | Never |
| Directories | Yes | Yes (file format dependant) | N/A |

### File Formats

| Type    |  Read      | Write  | Notes
| ------- | ---------- | ------ | --------------------------------------------------------
|   2da   |    Yes     |   Yes  |
|   bmp   |    Yes     |   Yes  | [stbi_image](https://github.com/nothings/stb)
|   dds   |    Yes     |   Yes  | Both standard and Bioware (thanks to Torlack) DDS files can be read, only standard written. Conversions: png, tga
|   gif   |    Yes     |   Yes  | [stbi_image](https://github.com/nothings/stb)
|   gff   |    Yes     |   No   | The current implementation is non-allocating and read-only.
|   ini   |    Yes     |   No   | [inih](https://github.com/benhoyt/inih)
|   jpg   |    Yes     |   Yes  | [stbi_image](https://github.com/nothings/stb)
|   json  |    Yes     |   Yes  | [nholmann_json](https://github.com/nlohmann/json)
|   mdl   |    No      |   No   | Parsing easy, do anything with it hard
|   mtr   |    No      |   No   | Parsing easy, do anything with it hard
|   plt   |    No      |   No   | Parsing easy, do anything with it hard
|   png   |    Yes     |   Yes  | [stbi_image](https://github.com/nothings/stb)
|   set   |    Yes     |   No   | [inih](https://github.com/benhoyt/inih) - it's just an ini file.
|   tga   |    Yes     |   Yes  | [stbi_image](https://github.com/nothings/stb). Conversions: png, dds
|   tlk   |    No      |   No   | To be merged
|   tml   |    Yes     |   Yes  | [toml++](https://github.com/marzer/tomlplusplus/)
|   txi   |    No      |   No   | Parsing easy, do anything with it hard

Note in the case of image formats, everything [stbi_image](https://github.com/nothings/stb) supports, this library could support.  This has no goal of being any kind of useful conversion library.

## TODOs

* Github Actions / CI
* Merge in more file formats
* GitPod / Github Codespaces integration
* Decide on exceptions vs object poisoning
* Other stuff

## Credits

- [Bioware](https://bioware.com), [Beamdog](https://beamdog.com) - The game itself
- [vcpkg](https://github.com/microsoft/vcpkg) - Package Management
- [abseil](https://abseil.io/) - Foundational
- [Catch2](https://github.com/catchorg/Catch2) - Testing
- [loguru](https://github.com/emilk/loguru), [fmt](https://github.com/fmtlib/fmt) - Logging
- [stbi_image](https://github.com/nothings/stb), [NWNExplorer](https://github.com/virusman/nwnexplorer), [SOIL2](https://github.com/SpartanJ/SOIL2/) - Image/Texture loading.
- [inih](https://github.com/benhoyt/inih) - INI, SET parsing
- [nholmann_json](https://github.com/nlohmann/json) - JSON
- [toml++](https://github.com/marzer/tomlplusplus/) - For settings.tml

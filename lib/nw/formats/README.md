# libnw - File Formats

One thing that makes NWN(:EE) so great is that it's a local optima of power and simplicity.  It's file formats are no different many of them -- at the reader level -- can be implemented as a thin wrapper over a handful of casts.

Where necessary the reading will be implemented seperatly from the writing and likewise from the 'rithmatic, unless it doesn't affect the usage or performance characteristics of one or the other.  E,g. tlk can easily be made read/write/modifiable with the exact same performance characteristics, Gff cannot.  It's better to seperate them than convolute the implementations of all of them.

| Type    |  Read      | Write  | Notes
| ------- | ---------- | ------ | --------------------------------------------------------
|   2da   |    Yes     |   Yes  |
|   bmp   |    Yes     |   Yes  | [stbi_image](https://github.com/nothings/stb)
|   dds   |    Yes     |   Yes  | Both standard and Bioware (thanks to Torlack) DDS files can be read, only standard written. Conversions: png, tga
|   gif   |    Yes     |   Yes  | [stbi_image](https://github.com/nothings/stb)
|   gff   |    Yes     |   No   | The current implementation is non-allocating, unless strings are read, and read-only.  Conversions: gff -> JSON.
|   ini   |    Yes     |   No   | [inih](https://github.com/benhoyt/inih)
|   jpg   |    Yes     |   Yes  | [stbi_image](https://github.com/nothings/stb)
|   json  |    Yes     |   Yes  | [nholmann_json](https://github.com/nlohmann/json)
|   mdl   |    No      |   No   | Parsing easy, do anything with it hard
|   mtr   |    No      |   No   | Parsing easy, do anything with it hard
|   nss   |    Yes     |   No   | A lexer and recursive decent parser, as is.  It can create a parse tree, no type checking, no real AST.  Handles errors very poorly.  Maybe someday.  Robert Nystrom's wonderful book [Crafting Interpreters](https://craftinginterpreters.com/) was huge source of inspiration.
|   plt   |    No      |   No   | Parsing easy, do anything with it hard
|   png   |    Yes     |   Yes  | [stbi_image](https://github.com/nothings/stb)
|   set   |    Yes     |   No   | [inih](https://github.com/benhoyt/inih) - it's just an ini file.
|   tga   |    Yes     |   Yes  | [stbi_image](https://github.com/nothings/stb). Conversions: png, dds
|   tlk   |    Yes     |   No   | A 10 minute, easy-bake, read-only implementation.. for now.
|   tml   |    Yes     |   Yes  | [toml++](https://github.com/marzer/tomlplusplus/)
|   txi   |    No      |   No   | Parsing easy, do anything with it hard

Note in the case of image formats, everything [stbi_image](https://github.com/nothings/stb) supports, this library could support.  This has no goal of being any kind of useful conversion library.

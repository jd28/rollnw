formats
=======

One thing that makes NWN(:EE) so great is that it's a local optima of
power and simplicity. It's file formats are no different many of them -
at the reader level - can be implemented as a thin wrapper over a
handful of casts.

Where necessary the reading will be implemented separately from the
writing and likewise from the 'rithmatic, unless it doesn't affect the
usage or performance characteristics of one or the other. E,g. tlk can
easily be made read/write/modifiable with the exact same performance
characteristics, Gff cannot. It's better to separate them than convolute
the implementations of all of them.

+------+-------+-------+-----------------------------------------------------------------------------------------+
| Type | Read  | Write |                                          Notes                                          |
+======+=======+=======+=========================================================================================+
| 2da  | Yes   | Yes   |                                                                                         |
+------+-------+-------+-----------------------------------------------------------------------------------------+
| bmp  | Yes   | Yes   | `stbi_image <https://github.com/nothings/stb>`__                                        |
+------+-------+-------+-----------------------------------------------------------------------------------------+
| dds  | Yes   | Yes   | Both standard and Bioware (thanks to Torlack)                                           |
|      |       |       | DDS files can be read, onlyes standard written.                                         |
|      |       |       | Conversions: png, tga                                                                   |
+------+-------+-------+-----------------------------------------------------------------------------------------+
| gif  | Yes   | Yes   | `stbi_image <https://github.com/nothings/stb>`__                                        |
+------+-------+-------+-----------------------------------------------------------------------------------------+
| ini  | Yes   | No    | `inih <https://github.com/benhoyt/inih>`__                                              |
+------+-------+-------+-----------------------------------------------------------------------------------------+
| jpg  | Yes   | Yes   | `stbi_image <https://github.com/nothings/stb>`__                                        |
+------+-------+-------+-----------------------------------------------------------------------------------------+
| json | Yes   | Yes   | `nholmann_json <https://github.com/nlohmann/json>`__                                    |
+------+-------+-------+-----------------------------------------------------------------------------------------+
| mdl  | ASCII | ASCII | See `model docs <https://jd28.github.io/rollnw/structure/model.html>`__                 |
+------+-------+-------+-----------------------------------------------------------------------------------------+
| mtr  | No    | No    | Parsing easy, do anything with it hard                                                  |
+------+-------+-------+-----------------------------------------------------------------------------------------+
| plt  | No    | No    | Parsing easy, do anything with it hard                                                  |
+------+-------+-------+-----------------------------------------------------------------------------------------+
| png  | Yes   | Yes   | `stbi_image <https://github.com/nothings/stb>`__                                        |
|      |       |       |                                                                                         |
+------+-------+-------+-----------------------------------------------------------------------------------------+
| set  | Yes   | No    | `inih <https://github.com/benhoyt/inih>`__ -                                            |
|      |       |       | it's just an ini file.                                                                  |
+------+-------+-------+-----------------------------------------------------------------------------------------+
| tga  | Yes   | Yes   | `stbi_image <https://github.com/nothings/stb>`__                                        |
|      |       |       | Conversions: png, dds                                                                   |
+------+-------+-------+-----------------------------------------------------------------------------------------+
| tml  | Yes   | Yes   | `toml++ <https://github.com/marzer/tomlplusplus/>`__                                    |
+------+-------+-------+-----------------------------------------------------------------------------------------+
| txi  | No    | No    | Parsing easy, do anything with it hard                                                  |
+------+-------+-------+-----------------------------------------------------------------------------------------+

Note in the case of image formats, everything
`stbi_image <https://github.com/nothings/stb>`__ supports, this library
could support. This has no goal of being any kind of useful conversion
library.

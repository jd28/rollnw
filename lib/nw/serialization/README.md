# Serialization

## Definitions

* **profile** - NWN has three different (de)serialization profiles:

    * **blueprint** - UTC, UTT, etc, etc.  BIC is included here, though not a blueprint itself.
    * **instance** - Instances of blueprints stored in an area's GIT file.
    * **savegame** - All game and object state.  This is outside of the scope of this library.. for now.

* **type** - C++ types corresponding to gff serialization types.
    * `uint8_t`
    * `int8_t`
    * `uint16_t`
    * `int16_t`
    * `uint32_t`
    * `int32_t`
    * `uint64_t`
    * `int64_t`
    * `float`
    * `double`
    * `std::string`
    * `Resref`
    * `LocString`
    * `ByteArray`

* **struct** is a collection of key-value pairs, where the key is a 16 character string and the value is one of the above types (almost).

* **list** is a list solely of structs, this follows the Gff pattern.

* **gffjson** refers to the nwn-lib/neverwinter.nim json format that mimics gff.

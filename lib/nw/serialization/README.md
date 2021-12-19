# Serialization

## Definitions

* **profile** - NWN has three different (de)serialization profiles:

    * **blueprint** - UTC, UTT, etc, etc.  BIC is included here, though not a blueprint itself.
    * **instance** - Instances of blueprints stored in an area's GIT file.
    * **savegame** - All game and object state.  This is outside of the scope of this library.. for now.

* **type** - C++ types corresponding to GFF serialization types.
    * `uint8_t` - Also convertible to `bool`
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
    * Scoped Enumerations are convertible when their underlying type matches the GFF type.

* **struct** is a collection of key-value pairs, where the key is a 16 character string and the value is one of the above types (almost).

* **list** is a list solely of structs, this follows the GFF pattern.

* **gffjson** refers to the nwn-lib/neverwinter.nim json format that mimics GFF.  The extent to which this is supported by the library is an open issue.

* **json** refers specifically to libnw json serialization.  This very closely mimics the structure of a given object, such that if you load the JSON into another language, or a dynamic language that can construct arbitrary objects from JSON, the usage is identical or analogous to the C++ objects.

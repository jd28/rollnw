# i18n

The **i18n** module provides support for internationalization, Tlk reading/writing, and conversions between default NWN encodings and UTF-8 (see below).

## Philosphy

The module follows the principles of [UTF-8 everywhere](https://utf8everywhere.org/).
Or in other words, ordinary C++ string types, `std::string`, `std::string_view`, etc. *must* be in UTF-8.  The only exception are:

* wide character types (`std::wstring`, `u16string`, `u32string`, etc) which are never used.
* `std::filesystem::path` which is, per the standard, stored in native encoding.  E.g., on Linux, UTF-8; on windows, UCS-2; etc.  Some platform specific conversions to UTF-8 are therefore necessary.

There is *no* caching or fixed caching policies at this layer of the library.

## Supported Languages

* English (CP1252)
* French (CP1252)
* German (CP1252)
* Italian (CP1252)
* Spanish (CP1252)
* Polish (CP1250)
* Korean (CP949) - Unsupported by NWN:EE
* Chinese Traditional (CP936) - Unsupported by NWN:EE
* Chinese Simplified (CP950) - Unsupported by NWN:EE
* Japanese (CP932) - Unsupported by NWN:EE

## Further Reading

* [UTF-8 everywhere](https://utf8everywhere.org/)
* [The Absolute Minimum Every Software Developer Absolutely, Positively Must Know About Unicode and Character Sets (No Excuses!)](https://www.joelonsoftware.com/2003/10/08/the-absolute-minimum-every-software-developer-absolutely-positively-must-know-about-unicode-and-character-sets-no-excuses/)
* [TLK File Format](https://neverwintervault.org/project/nwn1/other/bioware-aurora-engine-file-format-specifications)

## Credits

* [libiconv](https://www.gnu.org/software/libiconv/)
* [boost::nowide](https://github.com/boostorg/nowide)

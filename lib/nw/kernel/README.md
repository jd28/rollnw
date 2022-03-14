# kernel

The `kernel` module provides submodules for handling global resources and services.  It's designed around some explicit goals:

* Every service should be easily overrideable to allow for [parallel implementation](http://sevangelatos.com/john-carmack-on-parallel-implementations/).
* Any function or object that can modify global state must be contained in this module for easy search/grepability.

## Strings

The `Strings` module provides access to dialog/custom TLKs and localized strings.  It also provides a mechanism for interning commonly used strings.

### Basic Interface

```c++
    virtual std::string get(const LocString& locstring, bool feminine = false) const;
    virtual std::string get(uint32_t strref, bool feminine = false) const;
    virtual InternedString intern(std::string_view str);
    virtual void load_custom_tlk(const std::filesystem::path& path);
    virtual void load_dialog_tlk(const std::filesystem::path& path);
    LanguageID global_language() const noexcept;
    void set_global_language(LanguageID language) noexcept;
```

# kernel

The `kernel` module provides submodules for handling global resources and services.  It's designed around some explicit goals:

* Every service should be easily overrideable to allow for [parallel implementation](http://sevangelatos.com/john-carmack-on-parallel-implementations/).
* Any function or object that can modify global state must be contained in this module for easy search/grepability.

### Service Load Order

- `Config`
- `Strings`
- `Resources`
- `Rules`

Once all services are loaded, the kernel will call `service->intialize()` in the case default construction
isn't enough.

### Usage

The below is an example of how it might be used as is, of course at some point.. there will be a `nw::kernel::load_module(...)` to abstract this.

```cpp
int main(int argc, char* argv[])
{
    nw::init_logger(argc, argv);
    // This could fail due to the probes not being that thourough.  Set env vars
    // NWN_ROOT and NWN_USER to be certain.
    nw::kernel::config().initialize({nw::probe_nwn_install()});
    nw::kernel::services().start();
    // ...
}
```

## Config

The `Config` module provides access to installation info, path aliases, Ini and Toml settings.

-------------------

## Objects
> Provides a fairly basic and fairly incomplete object management system.  Lookup is O(1), just an index into a `std::deque`.
> If creating and deleting a *massive* amount of objects it will slowly start to leak in 64bit increments.

<br>

```cpp
virtual void clear();
```
> Clears all objects
>
> warning: **All** objects and references are invalidated.  If direct references to an object were
> retained and attempted to be used, the only outcome will be a segfault.

<br>

```cpp
virtual void destroy(ObjectHandle handle);
```
> Destroys object

<br>

```cpp
virtual ObjectBase* get(ObjectHandle handle) const;
```
> Gets object

<br>

```cpp
virtual void initialize();
```
> Initializes object system.
>
> note: in the current implementation this does nothing.

<br>

```cpp
virtual Module* initialize_module();
```
> Intializes the loaded module
> warning: `nw::kernel::resman().load_module(...)` **must** be called before this.

<br>

```cpp
virtual ObjectHandle load(std::string_view resref, ObjectType type);
```
> Instantiates object from a blueprint
>
> note: will call kernel::resources()

<br>

```cpp
    virtual Area* load_area(Resref area);
```
> Loads an area

<br>

```cpp
virtual bool valid(ObjectHandle handle) const;
```
> Tests if object is valid
>
> note: This is a simple nullptr check around Objects::get.  Not calling this before
> the latter is **not** undefined behavior.

-------------------

## Resources
> Provides resource management features.  Analogous NWN's ResMan.

 <br>

```cpp
virtual bool add_container(Container* container, bool take_ownership = true);
```
> Adds already created container and optionally takes ownership.

 <br>

```cpp
virtual void initialize();
```
> Initializes resources management system

<br>

```cpp
virtual bool load_module(std::filesystem::path path, std::string_view manifest = {});
```
> Loads module container, either an Erf or a directory.  Nothing more.

<br>

```cpp
virtual void load_module_haks(const std::vector<std::string>& haks);
```
> Loads hak files

<br>

```cpp
virtual void unload_module();
```
> Unloads module

<br>

-------------------

## Rules

-------------------

## Strings
> Provides access to dialog/custom TLKs and localized strings.  It also provides a mechanism for interning
> commonly used strings.

<br>

```cpp
virtual std::string get(const LocString& locstring, bool feminine = false) const;
```
> Gets string by LocString
> note: if Tlk strref, use that; if not look in localized strings.  [TODO] This is backwards!!

<br>

```cpp
virtual std::string get(uint32_t strref, bool feminine = false) const;
```
> Gets string by Tlk strref

<br>

```cpp
virtual void initialize();
```
> Initializes strings system

<br>

```cpp
virtual InternedString intern(std::string_view str);
```
> Interns a string
>
> note: Multiple calls to `intern` with the same string will and must return
> the same exact underlying string, such that equality can be determined
> by a comparison of pointers.

<br>

```cpp
virtual void load_custom_tlk(const std::filesystem::path& path);
```
> Loads a modules custom Tlk and feminine version if available

<br>

```cpp
virtual void load_dialog_tlk(const std::filesystem::path& path);
```
> Loads a dialog Tlk and feminine version if available

<br>

```cpp
LanguageID global_language() const noexcept;
```
> Gets the language ID that is considered 'default'
>
> note: This determines the character encoding of strings as they are stored
> in game resources, TLK, GFF, etc.  In EE the only encoding that isn't CP1252
> is Polish, so generally safe to not worry too much.

<br>

```cpp
void set_global_language(LanguageID language) noexcept;
```
> Sets the language ID that is considered 'default'

<br>

```cpp
virtual void unload_custom_tlk();
```
> Unloads a modules custom Tlk and feminine version if available

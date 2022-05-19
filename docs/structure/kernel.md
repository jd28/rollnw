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

-------------------

## Resources
> Provides resource management features.  Analogous NWN's ResMan.

-------------------

## Rules

Provides access rules loaded from 2DA and a mechanism for caching other 2DA files.

### Not Yet Implemented

* The vast bulk of it..
* `ruleset.2da`

-------------------

## Strings
> Provides access to dialog/custom TLKs and localized strings.  It also provides a mechanism for interning
> commonly used strings.

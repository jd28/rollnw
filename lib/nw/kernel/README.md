# kernel

The `kernel` module provides submodules for handling global resources and services.  It's designed around some explicit goals:

* Every service should be easily overrideable to allow for [parallel implementation](http://sevangelatos.com/john-carmack-on-parallel-implementations/).
* Any function or object that can modify global state must be contained in this module for easy search/grepability.

### Service Load Order

- `Config`
- `Strings`
- `Resources`

Once all services are loaded, the kernel will call `service->intialize()` in the case default construction
isn't enough.

### Usage

The below is an example of how it might be used as is, of course at some point.. there will be a `nw::kernel::load_module(...)` to abstract this.

```c++
int main(int argc, char* argv[])
{
    nw::init_logger(argc, argv);
    // This could fail due to the probes not being that thourough.  Set env vars
    // NWN_ROOT and NWN_USER to be certain.
    nw::kernel::config().initialize({nw::probe_nwn_install()});
    nw::kernel::services().start();
```

## Config

The `Config` module provides access to installation info, path aliases, Ini and Toml settings.

## Resources

The `Resources` module is similar to NWN's ResMan.

## Strings

The `Strings` module provides access to dialog/custom TLKs and localized strings.  It also provides a mechanism for interning commonly used strings.

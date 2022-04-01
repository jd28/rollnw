# kernel

The `kernel` module provides submodules for handling global resources and services.  It's designed around some explicit goals:

* Every service should be easily overrideable to allow for [parallel implementation](http://sevangelatos.com/john-carmack-on-parallel-implementations/).
* Any function or object that can modify global state must be contained in this module for easy search/grepability.

## Config

The `Config` module provides access to installation info, path aliases, Ini and Toml settings.

## Strings

The `Strings` module provides access to dialog/custom TLKs and localized strings.  It also provides a mechanism for interning commonly used strings.

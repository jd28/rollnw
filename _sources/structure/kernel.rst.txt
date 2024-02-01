kernel
======

The ``kernel`` module provides submodules for handling global resources
and services. It's designed around some explicit goals:

-  Every service should be easily overrideable to allow for `parallel
   implementation <http://sevangelatos.com/john-carmack-on-parallel-implementations/>`__.
-  Every service should be decoupled from the kernel itself.
-  Any function or object that can modify global state must be contained
   in this module for easy search/grepability.

Services
--------

Config
~~~~~~

The ``Config`` service provides access to installation info.

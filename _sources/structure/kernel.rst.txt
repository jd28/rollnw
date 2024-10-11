kernel
======

The ``kernel`` module provides submodules for handling global resources
and services. It's designed around some explicit goals:

-  Any function or object that can modify global state must be contained
   in this module for easy search/grepability. ``using nwk = nw::kernel`` is an acceptable abreviation.
-  Every service should be easily overrideable to allow for `parallel
   implementation <http://sevangelatos.com/john-carmack-on-parallel-implementations/>`__.
-  Every service should be decoupled from the kernel itself.

Config
~~~~~~

:cpp:struct:`nw::kernel::Config`

Services
--------

EffectSystem
------------

EventSystem
-----------

FactionSystem
-------------

ModelCache
----------

ObjectSystem
------------

Rules
-----

Strings
-------

TilesetRegistry
---------------

TwoDACache
----------

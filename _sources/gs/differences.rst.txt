Differences
===========

Changed
~~~~~~~

1. All resource names (i.e. resrefs, extensions) and resource paths are coerced to lower-case.
   On macOS, Windows, this generally makes no difference.  On Linux, converting filenames, paths, etc,
   to lower-case has always been the best policy.

Unsupported
~~~~~~~~~~~

#. NWN(:EE or 2) configuration files for a couple reasons:

   * NWN:EE introduced a lot of needless complexity with ``settings.tml`` and it wasn't a particularly good choice to begin with.
   * If values are necessary they can be read first by some consumer of the library.

#. The concept of path aliases, i.e. "HAK:", "HD0:", etc.

Differences
===========

Changed
~~~~~~~

1. All resource names (i.e. resrefs, extensions) and resource paths are coerced to lower-case.
   On macOS, Windows, this generally makes no difference.  On Linux, converting filenames, paths, etc,
   to lower-case has always been the best policy.

Removed
~~~~~~~

1. The concept of path aliases.

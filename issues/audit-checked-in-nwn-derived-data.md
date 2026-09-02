# Audit checked-in NWN-derived data

## Problem

The repository already tracks generated rows below
`lib/nw/smalls/scripts/nwn1/data`. Their provenance and redistribution status
need an explicit repository decision. This issue records the question; it does
not make a legal determination. The current tracked set contains 2,111 files
and occupied approximately 8.3 MB when measured for the appearance tranche.

The appearance migration does not add to that corpus. Its rows are materialized
from the active resource set at runtime, generated only as ignored local review
artifacts, and covered by synthetic LSP fixtures. The nine creature movement
rates are authored directly as an NWN1 SmallS profile rule and generate no row
files.

## Required evidence

1. Inventory every tracked generated directory and identify its source.
2. Separate authored rules from values copied or mechanically transformed from
   an installed game data set.
3. Identify tests, packaging, and LSP behavior that still depend on tracked
   generated rows.
4. Obtain the appropriate licensing review for any third-party-derived content
   proposed for continued distribution.

## Decision boundary

Do not add new migrated game-derived row snapshots while this is unresolved.
Removing existing tracked rows or repository history is a separate, explicitly
approved change. If removal is selected, replace required regression coverage
with minimal synthetic fixtures rather than another copy of the source corpus.

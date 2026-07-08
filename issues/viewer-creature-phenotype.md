# Viewer Creature Phenotype Resolution

## Problem

Creature preview assembly hardcodes phenotype 0, so any creature with a
non-default phenotype (fat/large body variants, etc.) resolves wrong or
missing body-part/robe/cloak models. The bug is duplicated verbatim in the
diagnostic-report path because that path re-implements the assembly flow.
Found in the 2026-07 gfx/render audit; verified in source.

## Findings

- `lib/nw/render/viewer/preview_scene.cpp:3729-3737`:

      int phenotype = 0;
      auto* phenotype_tda = nw::kernel::twodas().get("phenotype");
      if (phenotype_tda) {
          phenotype = 0;
      }

  The 2DA is fetched and discarded; phenotype stays 0 unconditionally.
  `nw::CreatureAppearance` (`lib/nw/objects/Appearance.hpp:45`) carries the
  real `Phenotype phenotype` field, and `resolve_creature_part_model()`
  (`preview_scene.cpp:2910-2918`) formats part resrefs as
  `p{sex}{race}{phenotype}_{token}{part:03d}` — phenotype is load-bearing.
- Duplicated verbatim at `preview_scene.cpp:~5688` in
  `add_dynamic_creature_report`: the report path independently
  re-implements ~150 lines of the body-part/robe/cloak resolution flow,
  which is how the same bug landed twice.

## Fix

- Use the creature's actual appearance phenotype in both sites; use the
  `phenotype` 2DA to validate/fall back the index (or drop the dead fetch).
- Unify the assembly and report paths over one resolution function so a
  future fix cannot need applying twice.

## Verification

mudl preview of a creature with a non-default phenotype (before: wrong or
missing parts; after: correct `p{sex}{race}{pheno}_*` resrefs), and the
load report listing the same resrefs the scene actually loaded.

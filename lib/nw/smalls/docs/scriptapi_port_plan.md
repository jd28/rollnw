# ScriptAPI Port Plan

## Goals

- Move most of `nwn1::scriptapi` to Smalls modules.
- Keep typed script surfaces (`core.constants` newtypes/constants) instead of untyped integer APIs.
- Preserve runtime ownership safety at the VM/engine boundary.

## Phase 1: Pure Constructors (Low Risk)

- Status: in progress for `core.effects`.
- Scope: constructors that only build data and do not require object/equip/world context.
- Current examples: ability/ac/attack/save/skill modifiers, damage constructors, haste, temp HP, concealment/miss chance.

## Phase 2: Contextual Application (Medium Risk)

- Status: in progress via `core.object`, `core.creature`, and initial `core.item` helpers.

- Introduce explicit contextual APIs for equip and object-targeted behavior.
- Candidate module split:
  - `core.effects`: constructors and lifecycle primitives.
  - `core.item`: item-property and equip-context helpers.
  - `core.creature`: target-aware application/removal helpers.
- Requirement: expose object handle types in Smalls native modules (not only effect handles).

## Equip-Semantics Rule

- Do not model equip behavior as raw constructor + `core.effects.apply`.
- Equip-derived effects must carry engine context (owner, slot, source item/property) and should be engine-owned.
- `destroy(effect)` remains VM-owned-only; engine-owned/borrowed handles no-op.

## Phase 3: Broad ScriptAPI Coverage

- Migrate additional domains in thin wrappers that delegate to existing `nwn1::scriptapi` helpers where possible.
- Add behavior-parity tests per domain.
- Add type-safety tests (newtype acceptance, raw integer rejection where required).

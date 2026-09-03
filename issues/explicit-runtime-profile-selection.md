# Explicit Runtime Profile Selection

Status: complete.

## Problem

`ConfigOptions::profile` defaults to `"nwn1"`, and the process-global `Config`
contains that value before any consumer initializes configuration. Starting
ordinary game-mode kernel services therefore selects the NWN1 C++ profile,
adds `stdlib/nwn1`, derives `nwn1.propsets`, and requires that module even when
the executable never chose a profile.

This was the pre-change state. The implementation now represents absence with
`std::optional`, rejects game startup before service initialization, and has no
C++ `GameProfile` injection path.

This makes an omitted decision look like an intentional NWN1 dependency. The
recent LSP failure exposed the coupling directly, and `mudl` reproduced it when
its packaged NWN1 scripts were not discoverable. `mudl` does intentionally
consume NWN objects and should select and ship NWN1; that packaging defect is
separate from the implicit default.

## Observed Data

- `lib/nw/kernel/Config.hpp` initializes `ConfigOptions::profile` to `"nwn1"`.
- `lib/nw/kernel/Kernel.cpp` constructs `nwn1::Profile` whenever game-mode
  services start with the default root and an NWN game version. Any other root
  currently fails as unsupported.
- `lib/nw/smalls/runtime.cpp` derives the profile package directory and required
  propset module from that root during game-mode runtime initialization.
- Most executable startup paths do not call `Config::initialize` with explicit
  options. They inherit the process-global default before calling
  `services().start()`.
- `ServiceMode::language` already proves that a profile-free service graph is
  useful and does not require a propset schema or `GameProfile`.

The input is one startup choice: a profile root, or no profile. The output is
the service graph and package/module requirements for the process lifetime.
Selection occurs once at startup; there is no per-frame or per-object cost.

## Direction

Remove the implicit profile selection. A consumer that needs NWN1 must choose
`nwn1` explicitly before starting game-mode services. A consumer that needs
only language services must select `ServiceMode::language` and no profile.

The profile root remains a logical package prefix, not a filesystem path.
Package discovery and packaging remain the responsibility of consumers that
select a profile.

Do not add profile auto-detection, a plugin registry, fallback search order, or
an inferred profile based on game/install filenames. Those add states without
removing the missing decision.

## Required Behavior

- No selected profile is representable without borrowing the name of a real
  profile.
- Starting game-mode services without an explicit profile fails once, before
  service initialization, with an actionable diagnostic.
- An invalid profile root is rejected during configuration.
- `ServiceMode::language` starts without a profile and never derives or loads a
  profile package or propset module.
- Every NWN1 executable and test that needs game services selects `nwn1`
  explicitly.
- There is no consumer-provided C++ profile object that can disagree with the
  configured package root.

## Implementation Audit

1. Inventory every `services().create()` and `services().start()` call and
   classify it as language-only or profile-backed.
2. Change the configuration representation so absence is explicit; do not use
   an empty string as both "not selected" and "invalid selected root" without a
   documented boundary.
3. Move the missing-profile rejection to game-service creation, before the
   service array is populated.
4. Update NWN1 consumers to configure `nwn1` at their startup boundary.
5. Add tests for absent, invalid, explicit NWN1, language-only, and
   user-supplied-profile cases.

## Cost

The dominant cost is startup-call-site migration and tests. Runtime execution
cost is unchanged because profile selection and module derivation still happen
once. Maintenance cost decreases by making each executable's dependency
visible at its composition root.

## Done

Done means no process-global configuration value silently selects NWN1, all
profile-backed consumers select their profile explicitly, language-only tools
start without profile packages, and the startup matrix above is covered by
tests.

Evidence against this direction would be a real supported consumer that cannot
know its required profile at startup. In that case, record the concrete input
that determines the profile and design an explicit selection transform for
that data rather than restoring a default.

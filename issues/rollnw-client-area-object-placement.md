# rollnw | client Area Object Placement

Status: complete (2026-09-08) for Creature, Placeable, and Item authoring.
Automated checks passed and the user confirmed the desktop behavior looks good.
Large-area profiling and the release walkthrough remain on the
[release checklist](rollnw-client-release-demo.md); runtime transfers remain
separate. This is the durable record for placement, ground-item pose, and selection.

## Behavior and limits

UTC/UTP/UTI project-tree drops instantiate one detached live object, preview it
translucently, then commit that same handle as one undoable area membership
change. Pointer motion changes render spatial rows, not live object state or
assets. Cancel destroys the uncommitted object; undo detaches and owns it; redo
re-admits and attaches it. Inventory/store workbench routing retains priority.
This is blueprint authoring, not inventory-to-world transfer.

Tile-mesh hits provide full XYZ pointer targets, not placement authority.
Creatures require strict navigation admission at the requested position, with
known clearance; invalid targets reject rather than snap. Placeables and items
allow free authored Z and non-walkable positions within area XY bounds.
Forward edits and redo validate against current state; undo restores history,
and import does not reject pre-existing authored positions. Mixed batches test
creatures against the pre-commit area.

ASSUMPTION: the existing play-preview navigation contract defines creature
admission — affects clearance and surface validity, not whole-body collision.
It uses terrain, active static obstacles, and authored PERSPACE; fixed 2 m agent
height remains unchanged. Creature avoidance and preserving navigability after
subsequent scenery edits are outside this contract.

Area-item rendering applies `RotateOnGround` (0: identity, 1: +90 degrees Y,
2: +90 degrees X), then aligns the combined model bottom to authored Z.
ASSUMPTION: authored Z is the support-plane height for loaded and newly placed
items — affects presentation only. One cached correction feeds draw, bounds,
shadows, and picking; saved spatial rows, standalone previews, and equipped
visuals are unchanged. Game and toolset area rendering use this same path.

Pointer selection preserves direct mesh hits. On a miss or debug-footprint
hit, a nearby-item bounds check enables nearest-first samples in a six-pixel
disk; nearer object/tile geometry blocks assisted hits. Exact world-ray and
candidate queries remain unchanged. Six pixels is an initial usability choice;
one-pixel sampling cannot guarantee every subpixel feature at every alignment.
Selection never authorizes gameplay pickup/drop.

Area inspectors stay 440 px wide; standalone item inspectors remain 560 px.
Object movement requires a five-pixel pointer threshold. Click/jitter does no
placement validation or mutation; viewport changes, invalid/outside coordinates,
and camera inputs cancel the gesture before changing its coordinate frame.

## Data contracts and ownership

The desktop client has one UI thread, one ObjectManager, and one active area
viewport. The common pointer batch is one row; loading, preview, membership,
spatial updates, navigation queries, and pointer selection use plural paths.
Source assets/geometry are stable during a gesture; candidate transforms and
pointer/camera inputs vary. Broader batch/scene distributions are not measured.

- Blueprint load: contiguous `AreaObjectBlueprintPlacement` resource/spatial
  rows produce detached generational handles in order. ObjectManager owns
  storage; the caller owns detached lifetimes until membership transfers them
  to undo. Unsupported/missing resources or failed loads reject the batch and
  destroy its loaded prefix.
- Preview append: borrowed Creature/Placeable/Item handles and opacity in
  `(0, 1)` produce scene-owned rows tagged with those exact handles. Validate
  and build the complete batch before append; failure leaves the scene intact.
  Invalid/unrenderable item bounds reject visual construction. Ground rotation
  publication accepts only 0..2 atomically; missing cells default to 0.
- Membership/admission: borrowed, unique live handles and `ObjectSpatialState`
  rows must match one live area, with finite transforms, positive scale, and
  in-bounds XY. Stale identities/counts, duplicate/existing membership, wrong
  areas, and allocation failures reject before insertion. Admission returns
  whole-batch success or a failed index/status/diagnostic, without changing
  live rows, undo, dirty state, or files. Failed duplicates release owned clones;
  failed placement leaves detached inputs with the caller.
- Surface trace: borrowed finite rays plus scene-owned flat tile ranges and
  world-space triangles produce caller-owned parallel XYZ/normal/distance/index/
  status rows, without query allocation. Invalid counts, non-finite/zero rays,
  malformed ranges, or out-of-range spans reject. Invalid, degenerate, and
  downward source triangles are dropped once during cache construction.
- Navigation source: one live area materializes owned flat
  `AreaNavigationSource` arrays from existing WOK/PWK/DWK and surface tables.
  This is a singleton source for many queries. Missing required terrain/visuals,
  malformed present walkmeshes, build/capacity failures, or unknown PERSPACE
  reject creatures. Missing/empty PWK/DWK retains the adapter's no-obstacle
  meaning; unknown surfaces remain non-walkable. Overlay failure clears the
  optional display without weakening admission.
- Creature partition: input indices are sorted by erosion class. Clearance is
  `PERSPACE * max(scale.x, scale.y) + 0.1 m`, rounded up to 0.125 m cells;
  non-finite or unrepresentable classes reject. Each demanded class builds
  once per batch; only the last world is retained. Non-creatures skip navigation.
  Strict borrowed XYZ queries produce parallel `NavStatus` rows: XY over a
  polygon within 0.0001 m epsilon, Z within one build cell plus epsilon
  (currently 0.1001 m). Invalid protocols reset provided outputs to rejected;
  off-surface positions return `off_mesh`. No snapping, allocation, or agent
  registration occurs in this query.
- Pointer selection: borrowed pixel rows, camera, viewport, scene, and record
  cache produce parallel selection rows without changing scene/camera/live
  state. Non-finite/out-of-viewport inputs and malformed counts reject; the
  caller owns selection state. The active primary-pointer gesture is a true
  singleton; object updates remain batched.

`AreaPlacementNavigation` cannot copy/move: its world borrows its owned source's
address and must die first. Gestures retain it at a stable address, invalidated
by area identity/mutation epoch; resource/scene changes end the gesture. Commands
build fresh state, and redo captures full generational area identity.
Cold source import reuses existing Area pointers and third-party ownership;
hot traversal uses contiguous indices and existing indexed mesh lookups.
Handles check live identity rather than adding a per-motion object-list scan.

## Cost and retained evidence

Blueprint/model loading happens once per ghost. Scene-cache rebuilds occur at
append and commit/cancel, not on pointer motion. The 16x16 `test_area` fixture
uses 256 ranges, 144,155 triangles, and 5,197,772 bytes for the tile-surface cache.
Ground pose reduces precomputed part bounds once per visual build and adds
8 bytes per source row (20 to 28 on the tested 64-bit Linux build).

Cold Recast construction dominates creature admission; the gesture retains one
source/radius-class world. Class sorting is O(C log C), duplicate checking
O(N squared) for current small batches. Linear input traversal and indexed
polygon lookup retain normally stable identity/class branches. In the small
`DockerDemo` fixture, cold source/build/admission took 2,874 us and 1,000 cached
admissions took 200 us total. Different operations, not a speedup comparison;
neither includes rendering or event dispatch.

Pointer assistance adds up to 112 exact sample queries plus tile checks for item
hits, per click rather than per frame. A read-only smoke test used actual
`the_awakening/loading` assets: `aagspear` parts 44/24/41 needed a 0.0328356 m
lift; representative helmet `armhe015` needed 0.15078598 m. Both stayed grounded
and mesh-pickable after commit, rebuild, and refresh without saved-position edits.

With 20 render records, an 800x600 viewport, yaw 45/pitch 60 degrees, sweeps around
the projected spear center used 697 points (x -40..40 step 2, y -8..8 step 1):

| Camera distance | Exact / assisted spear hits | Exact / assisted mean |
| --- | --- | --- |
| 20 m | 13 / 166 | 2.41 / 120.25 us |
| 40 m | 5 / 140 | 1.55 / 106.42 us |

These are single-run Release CPU means on Linux x86_64, AMD Ryzen AI 9 HX 370.
Exact timing excludes ray construction; assistance amortizes camera matrices
across the batch. They establish greater click coverage at extra query cost,
not single-click latency bounds or large-scene performance. The user confirmed
selection improved and subsequently reported the desktop behavior looks good.

The jump regression reproduced item selection shrinking the viewport by 120 px
(1148 to 1028 at a 1600 px context; 448 to 328 at 900 px). Stable inspector width
and threshold gating now pass that regression. Release client/test build checks
passed on 2026-09-08, and all 149 selected tests passed across the normal and
GPU-enabled runs: layout/gesture/camera, placement/navigation/undo/save, base-item
publication, selection, and rendering, including game/toolset live-state parity.
The 11 tests initially skipped for unavailable sandbox GPU access passed on the
GPU-enabled rerun. Earlier placement/navigation checks also passed
ASan + UBSan with leak detection disabled; pose/pointer/gesture follow-ups were
not rerun under sanitizers.

Implementation lives in `tools/client/{main,object_edits,area_navigation}`,
`lib/nw/nav/NavWorld`, and `lib/nw/render/viewer/{preview_scene,session}`.
Regression cases belong in `tests/rollnw_client_*`, `tests/nav_world.cpp`, and
`tests/render_viewer_*`, not separate per-fix documents.

## Separate follow-ups

- Complete the [release walkthrough and large-area profiling](rollnw-client-release-demo.md).
  Automated small-area query means do not establish responsiveness.
- Resolve [missing creature PERSPACE policy](nav-missing-perspace-policy.md);
  the corpus audit found 838 finite rows (0.01--6.0 m) and 14,262 missing rows.
- Implement [authoritative runtime placement and transfers](runtime-world-placement.md)
  from actual game rules. Renderer grounding and editor validation are reusable
  pieces, not runtime ownership transfer or permission checks.

The implementation reuses live objects, indexed scene rows, shared navigation,
and existing undo; it does not add an object mirror, physics engine, saved
placement schema, or runtime per-drop navigation rebuild.

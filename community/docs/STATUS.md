# Implementation status and work needing help

Updated: 2026-09-20 (live testing session). Development version: **1.5.8-ai.4**.
Development handed over from Codex to Claude on 2026-09-19; see DEVELOPMENT-HANDOFF.md
(private repository only; not part of this public snapshot).

**The full roadmap is not complete and no full release is accepted.** The AC29
Windows source builds. Current offline results: 67 Python checks and three Debug
C++ suites, passing. **Live Archicad testing has now begun** (2026-09-20, against a
real disposable project) - see "Live testing session" below for what's been confirmed
against a running instance versus what's still build-only. Undo and save/reopen
acceptance have not been systematically exercised beyond one incident-recovery case
(see below). The installed add-on and MCP have not been replaced.

## Implemented in this continuation

Fourteen new registered commands cover beam/column segments and tapered ends,
native dimension anchors, selected text-run formatting, drawing-frame placement,
native 2D plan primitives, roof geometry, session targeting and operation receipts,
and copying selected saved view settings including native custom resources.

Existing commands additionally gained single-plane roof edits; loaded-library
selection and correct parameter initialization for objects/lamps; revision guards
for opening parts; reliable attribute-selector errors; profile topology/target
validation; independent dimension-note ownership; partial morph-creation errors;
and drawing-title parameter copying during relink. Full details and limits:
[NATIVE_AI4_COMMANDS.md](NATIVE_AI4_COMMANDS.md).

| Area | Current implemented capability | Remaining work or proof |
|---|---|---|
| Contracts/execution | Runtime registration/schema discovery, preserved outer transaction errors, selected field checks, same-session operation receipts | Full field/SDK-call coverage audit, failure injection, legacy-client acceptance, durable interruption reconciliation; no universal atomicity |
| Model context | Bounded element filters, native zone/property/bounds context, geometry/quantities, native plan primitives and roof polygons | Exact finished/core/trimmed face and construction context, scene-output feasibility, larger-project benchmarks |
| Walls/slabs/transforms | Reference controls, polygonal walls, guarded slab vertices/holes/topology and straight-edge offsets, move/rotate/mirror/resize/copy/repeat/stretch | Native/UI junction-order discrepancy, curved topology gaps, dependent openings/relationships and Undo |
| Beams/columns/roofs | Shared section/shape/story/display edits, holes, ordered segments/tapers/cuts/schemes, single- and multi-plane roof edits | Native segmented/profile cases, hole clearing, topology and complete field-level acceptance |
| Meshes/morphs/profiles | Existing geometry authoring/replacement remains; profiles already support caller-authored newSkins; morph surface follow-up errors and profile validation improved | Remaining field/error review and native topology/quantity/identity acceptance |
| Libraries | Loaded catalogue, ancestry, parameters/allowed values and stored previews; explicit target identity for openings and object/lamp creation | Packaged/legacy/multilingual library testing, host/subtype/dependency coverage, opening rehosting |
| Resources/roles | Layers/combinations, building materials, fills, surfaces, composites/profiles/pens, native metadata/options/renovation commands; safer overwrite resolution | Complete compare/reuse and dependency coverage across resource families; two synthetic specifications and repeatable setup acceptance |
| Annotations | Native hotspot descriptors, chain/style edits, text/label content and uniform styles, selected existing run formatting; section range, marker and presentation edits | Semantic core/finished-face/jamb completeness, arbitrary text-range splitting, full section/elevation marker/extent controls and native association tests |
| Views/sheets | Configured/reusable views/folders/sheets/masters; selected native custom settings copying; drawing frames/placement/title context; relink parameter copying | Title placement/relink preservation, custom-resource authoring completeness, reopened views/sheets, drawing updates and actual exports |
| Recovery/testing | Project/database/session targeting; bounded operation receipts; portable annotation/modelling/extended fixtures; source inventory | Native lost-response/rollback/default-restoration tests, durable cross-restart recovery, integrated room-to-sheet workflow, other SDKs and macOS |

The source inventory now lists **286 registrations and 245 literal input schemas**
(regenerated 2026-09-20; up from 282/241 with this session's four new commands:
`GetRenovationFilters`, `SetActiveRenovationFilter`, `FindDuplicateElements`,
`ExplainElement`), including inherited commands. Comparison with original Tapir 1.5.8
identifies at least 50 added registrations overall and no removals. This is neither an
SDK field-coverage count nor a count of proven capabilities - though this session's live
testing (see above) has now exercised a real, if partial, slice of that catalogue
against a running instance, not just offline schema checks. The installed runtime
catalogue remains authoritative. No percentage is assigned: coverage has not been
exhaustively measured.

## Native test dependency

A separately saved disposable AC29 project and exact path are needed. The prepared
native_extended_fixture.py checks segments, associative dimensions following a wall
edit, single-plane roofs, selective text styles and repeated operation IDs. It has
not run live. Existing native_annotation_fixture.py and native_modelling_fixture.py
remain available. The scripts never save and clean up known fixture IDs; missing
responses require journal inspection. A visible room/sheet test and save/reopen
exercise are still required, beyond these scripts.

## Scope and sharing

The agreed programme covers core architectural modelling, project resources, annotations, views, layouts and existing publishing configurations. MCP is a later separate
project. Office standards, briefs and naming conventions remain external. PDF
explosion/cleanup is separate. Advanced stairs, railings, curtain walls, shells,
publisher/schedule authoring, expanded MEP, opening rehosting and packaged/
legacy library behaviour are deferred indefinitely by user decision
(2026-09-20) — out of scope for this build, left for a later, separate build.

**Core scope for this build is now considered feature-complete** as of the
2026-09-20 correctness and coverage sweeps (see below): every element family
in the agreed programme has been checked for false-success bugs and for
read/write field parity, with all findings fixed. What remains before this
build itself is "finished" is live testing in real Archicad, not further
implementation — see "Native test dependency" above.

Source sharing is authorized preparation only. BUILD_EVIDENCE.json and SOURCE_MANIFEST.json identify the exact sharing snapshot. No remote publication has occurred. Preserve upstream
MIT attribution. Do not publish private working history, models, SDKs or journals.

## Current implementation phase — 2026-09-19T14:07:53.8060000+02:00

Testing is deferred to a separate later phase by user instruction. The previously recorded test results predate OffsetSlabEdge and do not cover that addition. The new command offsets a straight slab edge while retaining adjoining edge lines and delegates the in-place edit to MoveSlabVertices. A verified original-source comparison and the functional guide support community explanation; full field-level reconciliation remains incomplete.

## Codex's final four commits, documented by Claude — 2026-09-19T22:35:00+02:00

Commits a4b71a1, baf0bd4, f4e9fb7 and 6489afd (15:24–19:51 CEST) added: durable
guarded-operation file receipts; saved custom layer/pen authoring and AC29 custom
dimension formats/core model view options; resource reuse/compare across layers,
combinations, materials, surfaces, composites, profiles, pens, line types and fills;
annotation run splitting for explicit text ranges; section/elevation main-marker
selection from loaded compatible parts; classification descendant-failure propagation
and typed property-group policies (Error/ReuseIfMatching/Overwrite); SetSlabEdgeArc
and EditSlabTopology SplitEdge/MergeEdges; SetSlabEdgeSettings for per-edge vertical/
custom trims and surface overrides; ChangeDrawingLink native clip-polygon preservation
(circular arcs, multiple contours, no flattening); the new PositionDrawingTitles
command; exact circular-arc extrema in GetDrawingFrames/PositionDrawings bounds; and
GetWallReferenceGeometry/GetDimensionAnchors wall-layer boundary context (coordinate
coincidence only, not semantic core/finish association). None of these were offline-
tested by Codex before handover. Claude re-ran the full offline suite after taking
over and fixed one regression (a schema-extraction test bug, not a functional bug)
it exposed. Full detail for each item is in the private DEVELOPMENT-HANDOFF.md; this
entry exists so this public status document is not left behind the actual source.

## Claude's false-success correctness sweep — 2026-09-20

Systematic review of every native command that reports execution success, looking
for cases where a discarded or unchecked SDK return value let a command report
success without the requested change actually landing. Four confirmed and fixed,
each rebuilt (AC29 RelWithDebInfo) and re-passing the full 67-check offline suite:
`SetDetailsOfElements` drwIndex/draw-order changes (ElementCommands.cpp), GDL
parameter size changes on Object/Lamp/Window/Door/Skylight (ElementGDLParameter
Commands.cpp), `RenameNavigatorItem`'s `newId`/custom-layout-number path
(NavigatorCommands.cpp), and Favorite classification/category/property application
(FavoritesCommands.cpp). The remainder of the command surface — including every
file with no shared transaction helper (Annotation, TextRun, DimensionAnchor,
DrawingPlacement, WallReference, PolygonalWall, NativeHotspot, PlanGeometry,
GuardedExecution, AssemblySegment, and the Mesh/Morph extended-element commands)
and a codebase-wide grep for discarded mutating-SDK-call return values — was
checked and found sound; several commands already carry full readback
verification. This does not constitute field-level acceptance testing (still
deferred per user instruction), only a targeted audit for this specific bug class.

## Claude's read/write coverage sweep — 2026-09-20

Family-by-family check across the in-scope element list (walls, slabs, beams,
columns, roofs, meshes, morphs, libraries, resources, annotations, views,
sheets, layouts): for each, whether every field returned by its Get command is
also settable through its Modify/Set command, since a read-only field silently
blocks a common editing task without ever surfacing as an error. One confirmed
gap: a Zone's (room's) name, number and category could be read via
GetDetailsOfElements but not changed after creation - only stamp
position/angle were editable. Fixed by adding `name`, `numberStr` and
`categoryAttributeId` to SetDetailsOfElements' Zone typeSpecificDetails,
matching the fields already accepted at CreateZones time (commit a385b42).
Walls, slabs, beams, columns, roofs, windows, doors, morphs, meshes,
attributes/resources, classification systems/items, property
groups/definitions/values, library discovery, associative dimensions, and
view settings (including rotation, structure display, renovation filter and
custom layer/pen sets) were checked field-by-field against their creation/read
counterparts and found to already have full read/write parity.

## Live testing session — 2026-09-20

First live testing against a real running Archicad instance (AC29), connected over
Archicad's own JSON port with the add-on loaded, against a disposable test project
containing real starter building content. Findings, in order:

- **Zone rename/renumber/category** (the a385b42 fix above): confirmed working after
  resolving an unrelated hidden-layer create-time issue along the way (see below).
- **Draw order** (`SetDetailsOfElements` `drawIndex`) and **GDL parameter sizing**: both
  confirmed working.
- **Favorite application was completely broken on AC29** - every real favorite tested
  against a real element failed with a type-mismatch error, despite the add-on's own
  `GetFavoritesByType` confirming the types matched. Root cause: the AC26+ code path
  compared the entire `API_ElemType` struct (including `variationID`, meant for
  structural sub-variants) instead of just `typeID`. Fixed (commit dc3a5c3); retested,
  confirmed working for realistic cases.
- **New elements could silently land on a hidden layer** at creation time (inherited
  from whatever Archicad's UI tool default happened to be), making them immediately
  unmodifiable via the API with no indication anything was wrong. Fixed with an
  automatic fallback to the mandatory "Archicad" layer when the default would be hidden
  (commit d018aa0) - the first attempt at this fix had a placement bug that made it a
  no-op (found retesting, fixed in b731948).
- **`DeleteElements` had two separate, serious bugs**, found deleting test elements
  during cleanup:
  1. A batch delete swept away real project content, not just the intended targets -
     Archicad's own `ACAPI_Element_Delete` automatically extends a delete to every other
     member of a Group if any target belongs to one, which the command never accounted
     for or guarded against. Recovered via a clean project reopen; user visually
     confirmed no real content was lost. Fixed (commit 7d5a9d7): validates every element
     exists and refuses the whole request if anything is grouped, unless the caller
     explicitly opts in.
  2. Separately, `ACAPI_Element_Delete` can report success while silently not deleting
     an element on a hidden layer - confirmed for certain by having the user delete the
     same two elements manually in Archicad's UI, which worked immediately, proving this
     was specific to the add-on's delete path. Fixed (commit 936ab4f): reads every
     target back after a "successful" delete and reports a specific failure if anything
     still exists.
- **A native Archicad dialog can block the entire JSON command interface.** Creating a
  roof on a story other than whatever was active in Archicad's UI triggered a native
  "Elements have been created ... on currently unseen Stories" Information dialog, which
  blocks every subsequent command until a human dismisses it. No general "suppress all
  dialogs" API exists (checked - the SDK's few `silentMode` flags are narrowly scoped to
  specific functions, not element creation broadly). Attempted mitigation (commit
  ea1886c): a new shared `EnsureStoryIsActive()` switches the active floor plan
  window/database to match an element's target story before creating it there, applied
  everywhere floorIndex is independently settable (the shared element-creation path,
  `CreateRoofs`, `CreateMorphs`). **Retested live 2026-09-21 with this build installed -
  the dialog still occurred** (`CreateRoofs` at `level=3.0` on a 5-story project timed
  out; `API.IsAlive` then failed with "there is an open modal dialog: Information"),
  so this fix does not actually prevent the incident. Working theory (unconfirmed):
  `ACAPI_Window_ChangeWindow`/`ChangeCurrentDatabase` called from a background HTTP
  command-dispatch thread may not update whatever internal state the "unseen stories"
  checker keys off. **Confirmed directly**: calling `ChangeWindow` (the pre-existing
  command, not just the new fix) with `storyIndex` in isolation reports `success:true`
  but leaves `actStory` unchanged - a false-success bug in the underlying mechanism
  itself, not something `EnsureStoryIsActive()` can work around. **Decision
  (2026-09-21): not pursuing a code fix.** A native Win32 auto-dismiss for the dialog
  would produce the same end result as the user simply checking "Don't show this
  dialog again" on the dialog itself - neither approach can distinguish AI-triggered
  from manually-triggered creation, so there's no correctness benefit to the more
  complex approach. `EnsureStoryIsActive()` is left in the source as harmless (it
  fails safely, creation proceeds regardless) but is confirmed non-functional for its
  stated purpose. **User-facing consequence**: anyone using the AI to create elements
  on a currently non-visible story will hit this dialog once; the fix is a one-time
  manual "Don't show this dialog again" click in Archicad, not something the add-on
  can set on a user's behalf. This needs to be called out explicitly in end-user
  setup/onboarding documentation, not left as a silent surprise.
- **New commands from this same session** (`nativeFieldFilters` on `GetElementContext`,
  `FindDuplicateElements`, `ExplainElement`) were all tested live for the first time and
  confirmed working exactly as designed, including edge cases (tolerance boundaries,
  unsupported-field error messages, positional alignment across merged results).
- **FIXED and live-verified 2026-09-21**: `SetViewSettings` rejected unrelated field
  changes (e.g. `drawingScale` alone) on any view with a pre-existing invalid stored
  zoom rectangle, because validation re-checked the entire stored view state rather
  than only what the caller touched. Two-part fix: (1) only validate scale/zoom if the
  caller actually supplied them this call; (2) when the caller didn't touch them and
  the stored value is invalid, drop it instead of forcing
  `ACAPI_Navigator_ChangeNavigatorView` to write back known-broken stale data (which
  fails on its own). Verified live on three different real views sharing the identical
  corrupt-zoom condition - all now accept unrelated changes cleanly.
- **New, separate, unresolved finding surfaced while verifying the above**: the
  project's "Persp 1"/"Persp 2" views (duplicated across FR/EN/AU language folders)
  fail `SetViewSettings` unconditionally, even with a completely empty settings
  payload - `ACAPI_Navigator_ChangeNavigatorView` returns `APIERR_BADNAME` regardless
  of what's requested. Confirmed NOT the zoom/scale issue above (empty payload alone
  reproduces it). Not investigated further this session - logged for follow-up.
- **Layout custom numbering** (`RenameNavigatorItem`'s `newId`): previously logged as an
  unresolved `APIERR_BADNAME` failure. **RESOLVED 2026-09-21**: retested on a fresh
  standard-template project and it worked correctly on every value tried (including an
  empty string), confirmed via readback. The original failure was most likely a
  test-methodology error - `GetNavigatorItemTree` returns the Layout Book's root
  `BookItem` one level above the real `LayoutItem` nodes, and targeting that root guid
  instead of an actual layout produces an immediate, similar-looking failure. No code
  defect found; no fix needed.

Full blow-by-blow findings, including exact repro steps and error codes, are in the
private working test plan (not part of this public snapshot).

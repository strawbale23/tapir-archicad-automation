# What the Tapir AI interface update changes

Updated: 20 September 2026 (live testing session).
Development source: 1.5.8-ai.4. Native add-on implementation is largely complete for the
agreed core scope; **live testing against a real running Archicad instance began
2026-09-20** and is ongoing - see STATUS.md's "Live testing session" section for
findings. This document describes code and intended functionality; it does not declare
a completed release.

## Purpose

Extend Tapir so an AI client can obtain useful Archicad project information,
create and revise native architectural elements, configure project resources,
and produce editable drawings. The same interface remains usable by scripts
and conventional automation. No particular AI provider, office template or
project brief is built into the add-on.

Tapir already provides substantial modelling and automation functionality. This
project extends that foundation. It does not claim to have introduced every
command present in this repository, or to make plan interpretation part of the
native add-on.

## Reading the change history

Three categories must stay separate:

1. **Inherited functionality:** code already available from Tapir or the earlier
   local development baseline. Its presence is not a new feature of ai.4.
2. **Implemented changes:** additions or modifications present in this source.
3. **Planned work:** required functionality that is still incomplete. This is
   listed explicitly below and must not be presented as delivered.

The [comparison with original Tapir 1.5.8](UPSTREAM_COMPARISON.md) now identifies
43 added command registrations across all our changes, with none removed from
the original 236. The 73 original native C++/header/JSON files match the saved
GitHub tree for revision ce033d6bdcc90b538b3c5f7ab62f676099b96823. Existing commands
have also changed. This comparison establishes code differences; the complete
field-by-field account of functionality remains work package F01.

## Functionality added in ai.4

The following are fourteen new command registrations in ai.4. Thirteen were
added earlier; OffsetSlabEdge is the subsequent floor-editing addition.

| Added commands | What a client can do | Architectural use |
|---|---|---|
| `GetAssemblySegments`, `SetAssemblySegments` | Read and edit the ordered segments of a beam or column, including section sizes, tapered ends, materials/profiles, cuts and fixed or proportional segment lengths. | Revise a multi-segment beam or tapered column as a native element. |
| `OffsetSlabEdge` | Move one straight floor edge a specified perpendicular distance, retaining adjoining edge lines and the existing slab. | Widen or shorten a floor without asking the AI to reconstruct the complete outline. |
| `GetDimensionAnchors` | Obtain supported native floor-plan points and edges in a form usable by associative dimension commands. | Attach dimension witnesses to native geometry instead of inventing unrelated coordinates. |
| `GetAnnotationFormatting`, `SetAnnotationRunStyles` | Inspect paragraph/run formatting and change selected existing text runs. | Apply emphasis, font, size or pen to part of an annotation without replacing its text. |
| `GetDrawingFrames`, `PositionDrawings` | Read drawing-frame bounds and title context; place frame edges or centres at explicit paper coordinates. | Position linked drawings on sheets using millimetres and known frame extents. |
| `GetPlanPrimitives` | Read native lines, polylines, arcs, circles and text from an identified database, in bounded pages. | Supply the AI with linework and text from a worksheet or other source prepared by a separate PDF-processing tool. |
| `GetRoofGeometry` | Read roof construction and contours, with slope, pivot and side for single-plane roofs. | Inspect a roof before constructing an edit request. |
| `CopyViewSettings` | Copy selected groups of saved settings between compatible public views, including native custom settings. | Reuse a template view's configuration while retaining the destination view's identity and source. |
| `GetAutomationSession`, `ExecuteGuardedCommand`, `GetOperationReceipt` | Identify the active project/database/session and associate supported operations with IDs and retained results. | Recover a response lost during communication without immediately repeating the same creation request. |

## Existing functionality changed in ai.4

| Area | Previous problem or limitation addressed | Implemented change |
|---|---|---|
| Object and lamp creation | Selecting a part could leave parameters inherited from a different tool default; names could be ambiguous. | Exact unique loaded-name selection or explicit part identity; initialize from the selected part's parameters. Favourites remain optional and must identify the same part. |
| Doors and windows | A loaded index/main GUID alone may not identify the intended revision. | Optional loaded-part revision checks for creation/replacement and corrected parameter-memory disposal. |
| Object dimensions | An unsupported height parameter could be ignored. | Reject unsupported height assignment and invalid dimensions explicitly. |
| Roof revision | Single-plane roofs lacked the same editing path as multi-plane roofs. | Edit single-plane slope, pivot and side; improve story/level, construction and polygon handling. |
| Associative dimensions | Clients needed to construct low-level witness descriptors themselves. | Creation and chain editing accept the descriptors returned by `GetDimensionAnchors`; witness-note text has independent ownership. |
| Section/elevation editing | Existing controls exposed only a small subset of presentation settings. | Add horizontal range, middle/end marker placement, reference IDs, line types, cut-fill pens, vector hatching/shadows, transparency, uncut-surface fill and story-line/marker settings. Read these settings and use optional revision guards. |
| Attribute creation/overwrite | Failed lookup of an explicit target could result in unintended creation. | Preserve lookup errors; correct index schemas; improve layer-combination dependency handling and building-material value/reference checks. |
| Complex profiles | Invalid skin/edge references and geometry could be skipped or mishandled. | Reject invalid targets, contours, dependencies and empty replacements; retain existing geometry when its read fails. Authoring new profile skins already existed. |
| Morph creation | A subsequent requested surface change could fail after geometry creation. | Return a partial-creation result with the created element ID and the follow-up error. |
| Drawing relinking | Replacement could lose configured title parameters. | Pass the original title parameters to the replacement and reject an unreadable configured title instead of substituting defaults. Full title placement preservation remains incomplete. |

## Floor-edge movement: scope and use

Read the floor outline with GetSlabTopology, choose its contour and edge, and
provide the observed modification stamp, a distance in metres and a maximum
allowed endpoint movement. For example, 0.10 means 100 mm to the left when
looking from the edge's first vertex to its next vertex; -0.10 means right.
The direction follows the returned outline order, not an assumed inside/outside.

The adjoining edge lines stay in place; their corner points move to meet the
offset edge. The existing slab is edited rather than deleted and recreated.
The operation supports straight-edged outlines and holes. It rejects curved
slabs, unsuitable parallel corners, collapsed/reversed adjacent edges and
movement beyond the caller's limit. General polygon validity remains subject
to Archicad's native edit. Whole-outline offsets and automatic wall-following
are not included in this operation. Native use is part of the later test phase.

An example is provided in archicad-addon/Examples/OffsetSlabEdge.py. It reads the
specified project and prints the proposed request by default; --apply performs
the edit. It does not save or automatically retry a write.

## Earlier development work retained in this source

The ai.4 increment builds on earlier work around runtime command/schema discovery,
bounded element queries, geometry and quantities, wall reference controls,
polygonal walls, slab topology editing, transformations, loaded-library discovery,
project resources, annotations, saved views and layouts. These are cumulative
development areas, not thirteen additional new features. See
[STATUS.md](STATUS.md) for the cumulative implementation position and the source
history for attribution of individual changes.

## Remaining implementation scope

These are contributor work packages, not claims of delivered functionality.
Existing working code should be reused before adding a command.

| ID | Remaining implementation work | Relevant source entry points |
|---|---|---|
| F01 | Reconcile the original audit with create/read/edit fields, units, defaults and SDK limitations for each agreed element family. Produce a finite coverage register rather than equating command count with coverage. | `tools/inventory_native_contracts.py`, command schemas and the family implementations below |
| F02 | Complete relevant wall/slab/core-element editing gaps: junction-order interpretation, curved topology, dependent openings and construction relationships, and remaining family-specific fields. | `WallReferenceCommands.cpp`, `SlabTopologyCommands.cpp`, `ExtendedElementCommands.cpp`, `AssemblySegmentCommands.cpp` |
| F03 | Complete useful construction context, particularly supported finished/core faces, opening jambs and relationships needed to place or revise elements accurately. | `ElementCommands.cpp`, `DimensionAnchorCommands.cpp`, `PlanGeometryCommands.cpp` |
| F04 | Deferred indefinitely by user decision (2026-09-20): opening rehosting (moving an existing window/door to a different host wall) and packaged/legacy/multilingual library part behaviour. Both need real SDK investigation before any implementation is attempted; out of scope for this build. | `LibraryCommands.cpp`, `ElementGDLParameterCommands.cpp`, `ElementCreationCommands.cpp` |
| F05 | Complete attribute comparison/reuse and dependency handling so repeated project setup can reuse matching resources and report conflicts. | `AttributeCommands.cpp`, `ProjectCommands.cpp`, `NativeProjectCommands.cpp` |
| F06 | Complete relevant semantic dimension anchors, arbitrary text-range formatting, and remaining section/elevation marker and broken/distant extent controls. | `AnnotationCommands.cpp`, `TextRunCommands.cpp`, `DimensionAnchorCommands.cpp`, `SectionCommands.cpp` |
| F07 | Complete saved-view custom-setting authoring and drawing title/placement preservation needed for predictable sheet production. Retain use of existing publisher configurations. | `NavigatorCommands.cpp`, `DocumentCreationCommands.cpp`, `DrawingPlacementCommands.cpp` |
| F08 | Complete native interruption/recovery support, cancellation and result handling across the agreed command scope. Current operation receipts only survive the current add-on/project session. | `GuardedExecutionCommands.cpp`, `CommandBase.cpp`, individual command implementations |

F01 is required to turn these work packages into an exact field-level completion
list. Until that reconciliation is complete, phrases such as “all modelling
commands covered” or “full SDK support” are not justified. New discoveries must
be recorded against a work package, with an explicit reason for any scope change.

## Boundaries

- The MCP connector is a later, separate project.
- PDF conversion, explosion and cleanup are a separate project. Tapir exposes
  native source geometry; the AI interprets architectural intent.
- Office standards, naming conventions, templates and project briefs remain
  external inputs.
- Advanced stairs, railings, curtain walls, shells, publisher/schedule
  configuration authoring and expanded MEP workflows are deferred indefinitely
  by user decision (2026-09-20) — advanced features for a later, separate
  build, not part of this build's scope.
- Opening rehosting and packaged/legacy/multilingual library part behaviour
  (F04) are likewise deferred indefinitely by the same decision, pending real
  SDK investigation before any future attempt.
- Higher-level AI planning and workflow orchestration belong in the later client
  layer; native Archicad capabilities and necessary execution support belong here.

## How contributors can help

Choose a work package above and identify the concrete missing behaviour. Describe
the architectural operation first, then the proposed command or field change and
its SDK basis. State whether the change adds a capability, extends an existing
one, fixes an error or changes compatibility. Preserve upstream attribution and
keep office-specific resources out of the implementation.

For every completed change, update this functional account and the technical
command notes with: previous behaviour, new behaviour, affected commands, an
example use, supported limits and compatibility implications. This creates a
release narrative that the community can understand and maintain.

Testing and acceptance have now begun as a live, ongoing phase (2026-09-20) - a real
running Archicad instance, connected over its own JSON port, against a disposable test
project. This is a real but partial slice of the full catalogue, not exhaustive field
coverage; findings are recorded in STATUS.md's "Live testing session" section, not
duplicated here. No community publication is implied by preparing these documents.

## Development handover — 2026-09-19T22:35:00+02:00

Codex (this document's prior author) stopped after commit 6489afd; Claude took
over development. Five more commands/behaviours landed after this table was last
updated (SetSlabEdgeArc, EditSlabTopology SplitEdge/MergeEdges, SetSlabEdgeSettings,
PositionDrawingTitles, and section main-marker selection, among other changes) —
see [NATIVE_AI4_COMMANDS.md](NATIVE_AI4_COMMANDS.md)'s entries dated 2026-09-19
16:05 and 19:51 CEST for their functional description, and
[STATUS.md](STATUS.md) for current registration counts (282/241, 46 added vs
upstream). This table above was not renumbered/expanded for those five to avoid
rewriting it under time pressure; treat NATIVE_AI4_COMMANDS.md as the current
source of truth until this table is reconciled.

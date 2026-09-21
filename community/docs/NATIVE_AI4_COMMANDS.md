# Native AI interface additions in 1.5.8-ai.4

Updated: 2026-09-19T13:19:40+02:00 (Europe/Berlin).

Development source and AC29 Windows build. This is not an accepted full release.
Read the installed GetCommandContracts response before constructing requests.
Legacy commands remain directly callable; MCP is a separate project.

## Added commands

| Commands | Native behaviour | Limits and remaining proof |
|---|---|---|
| GetAssemblySegments / SetAssemblySegments | Read and replace the ordered segments of a beam or column, with sizes, tapered ends, construction, cut angles and fixed/proportional lengths | At most 100 segments. Edits require the observed parent modification stamp. sourceIndex selects which existing segment supplies unmentioned settings. Custom per-instance profiles retain their indexing and construction. Native identity, joins and Undo need acceptance. |
| GetDimensionAnchors | Discover supported native floor-plan hotspots and return complete witnessPoint descriptors for dimension creation or chain editing | Descriptors guard database, element revision, hotspot index and coordinate. Supported native point/edge kinds are labelled; this does not infer every finished/core face or jamb. |
| GetAnnotationFormatting / SetAnnotationRunStyles | Read paragraph/run formatting; edit selected existing runs while preserving text and ranges | Text and text labels only. Protected autotext is rejected. No arbitrary run splitting or new character-range authoring. Up to 100 edits. |
| GetDrawingFrames / PositionDrawings | Read layout frame bounds and title context; position frame edges/centres in paper millimetres, with a dry-run preview | Current layout and observed revisions required. Independent grouped drawing placement, curved frames and multipage drawings are rejected. Title bounds are a separate native query and may return an error. |
| GetPlanPrimitives | Read native lines, polylines, arcs, circles and text from an identified current database | Sorted-GUID pagination, up to 100 results and 1,000 candidates inspected per page. Polylines expose native coordinate indices, contour ends and arcs. Payload limits do not eliminate native whole-type enumeration. No PDF conversion or architectural interpretation. |
| GetRoofGeometry | Read roof identity, story/level, construction, thickness, contour or pivot polygon and single-plane slope/pivot/side | Coordinates in project metres; angles in radians. Query limits are enforced. Multi-plane level/edge settings are not all exposed by this query. |
| GetAutomationSession / ExecuteGuardedCommand / GetOperationReceipt | Target a saved project, database and add-on session; retain a bounded receipt for each operation ID and replay its result without executing the same request again | In-memory receipts only. No crash persistence, undo-state guarantee or universal transaction atomicity. The installed command allowlist is returned by session discovery. |

CopyViewSettings adds selective native copying between saved public views of the
same kind. Select ModelViewOptions, Layers, Scale, Dimensions, Pens,
StructureDisplay, ZoomAndRotation, Renovation, GraphicOverrides or Rendering.
It preserves destination identity and source links, retains custom data rather
than replacing it with a guessed named setting, and reports per-target non-atomic
results. Native readback is returned; customSettingsVerified remains false pending
full native/visual acceptance. Source custom settings that cannot be read are
rejected before any target write.

## Existing command changes

- CreateObjects and CreateLamps accept either an exact unique libraryPartName or an explicit libraryPart with index, main guid and optional ownUnID revision. They load parameters from the selected part rather than retaining another tool default's parameter memo. A favourite may supply parameters only when it refers to that same part. Incompatible, missing, template or ambiguous parts fail explicitly.
- Door/window creation and replacement optionally check ownUnID as well as the loaded index/main GUID. Library parameter handles use the SDK's nested-parameter disposer.
- Object dimensions require positive finite metres and a supported scalar ZZYZX height parameter. A missing height parameter is an error, not an ignored dimension.
- ModifyRoofs supports single-plane slope, pivot and side changes in addition to multi-plane editing. Story changes require an explicit level. Roof-class-specific fields, slopes, thicknesses and polygon allocation failures are checked. Topology replacement is not a promise of individual vertex association preservation.
- CreateAssociativeDimensions and EditDimensionChain accept witnessPoint descriptors from GetDimensionAnchors. Witness note text is independently owned rather than sharing one pointer across witnesses.
- Attribute overwrite indices are integers, not strings. Failed explicit selectors and native lookup errors no longer fall through to unintended resource creation. Layer-combination updates preserve unmentioned flags and reject invalid/duplicate layer references. Building-material physical values and dependencies are checked.
- Profile updates preserve existing geometry on read failure by rejecting the write. Unknown skins/edges, invalid references, invalid contours and empty replacements fail instead of being silently skipped. The existing newSkins implementation already authors profile polygons; it is not limited to cloning profiles.
- Morph creation reports a failed/unconfirmed requested surface as createdWithError, retaining the created element ID. The common ElementIdOrError contract now includes that partial-creation form.
- Drawing relinking passes the original title parameter memo into the replacement and refuses to silently fall back to defaults if configured title data cannot be read. Title placement, automatic parameter recalculation and full relink preservation still require native acceptance; the legacy conservative warning remains true.

## Guarded execution protocol

1. Call GetAutomationSession and retain its sessionId, project.projectPath and databaseId. Check guardedExecutionAvailable and guardedCommands.
2. Allocate a new operationId and construct one ExecuteGuardedCommand request containing expectedSessionId, expectedProjectPath, expectedDatabaseId, operationId, commandName and parameters. Retain the exact request.
3. A status of returned means the native command returned. Inspect result, including its item errors and partial-result metadata. It does not independently mean the desired model state was achieved.
4. If the response is lost, query GetOperationReceipt with the same session and operation ID. Repeating the identical serialized request within that session returns the stored result with replayed=true and performs no second write. Changed requests under the same ID are rejected. JSON key reordering is not guaranteed to produce an identical serialization.
5. started, unknownOutcome and resultTooLarge require inspection. A new session, missing receipt or process restart does not prove an earlier write failed. Do not blindly repeat creation. Receipts are not a statement about model state after Undo or later edits.

Limits: 128 retained operation IDs, one million characters per request/result, and an eight-million-character retained-payload budget. Capacity is checked before a new write. Receipts are never evicted to make a repeated ID executable. Cancellation is checked before dispatch; individual command cancellation still depends on its implementation. Native SDK execution remains on the main thread.

## Test entry points

The portable native_extended_fixture.py exercises segments/tapers, native wall-anchor dimensions following an edited wall, single-plane roof edits, text-run formatting and repeated operation IDs. It requires the exact path of a disposable saved project, the correct installed version and --run for writes. It records returned IDs before raising partial-result errors, cleans up its own fixtures, and never saves. Without --run it performs read-only preflight.

The fixture is prepared, not live-tested. Plan/3D inspection, Undo, default restoration, transaction failure, loaded-library variations, view/layout reopening and exported files remain acceptance requirements. The older native_modelling_fixture.py and native_annotation_fixture.py remain available.

## SDK basis

Native dimension hotspot conversion follows Graphisoft's [API FAQ](https://graphisoft.github.io/archicad-api-devkit/_f_a_q.html) and [API_Base documentation](https://archicadapi.graphisoft.com/documentation/api_base). Drawing title parameter handling follows the AC29 DevKit Element_Test/Element_Drawing.cpp example. API_DrawingType defines its polygon in layout coordinates. These references support the implementation method; they do not replace live tests of this build.


## Section/elevation presentation — 2026-09-19T13:32:50.7097704+02:00

ModifySectionSettings now accepts an optional expectedModificationStamp, horizontal range (Infinite, Limited, ZeroDepth), Middle/Ends marker placement and a presentation object. This covers reference IDs, native line-type identities, line visibility, cut-fill pens, text sizes, vector hatching/shadows, transparency, uncut-surface fills and story-line/marker display. GetSectionSettings returns these settings and the revision stamp. Limited depth retains existing geometry or uses the supplied straight geometry. Symbolic marker library parameters can override segment presentation; marker-part replacement and broken/distant-line authoring remain incomplete. The extended fixture includes section creation, settings changes and readback, but has not run live.

## Handover note — 2026-09-19T22:35:00+02:00 (Claude)

Codex stopped (user out of usage credits) after commit 6489afd; Claude took over
development at 22:20 CEST. The five entries below (SetSlabEdgeArc through
GetWallReferenceGeometry/GetDimensionAnchors boundary context) describe commits
a4b71a1, baf0bd4 and f4e9fb7, written by Codex in DEVELOPMENT-HANDOFF.md but never
copied into this public-facing command reference. Claude wrote these entries from
that source, not from re-reading the C++ directly line by line; treat field lists
as indicative pending Claude's own source review of each command. None of this work
was offline-tested before handover; Claude re-ran the full offline suite afterward
and fixed one test-harness regression it exposed (commit 2b0e204), unrelated to the
functional content described here.

## Slab edge arcs and topology splitting/merging — 2026-09-19T16:05:04+02:00

SetSlabEdgeArc bows a straight slab contour edge into a circular arc (or an arc
back toward straight) given an observed revision, targeting one edge by contour/edge
index. EditSlabTopology gained two further actions: SplitEdge divides a straight or
circular edge at a caller-supplied fraction along its length; MergeEdges combines
two adjacent collinear straight edges, or two adjacent arcs sharing the same circle
(centre match within 1e-7 metres), back into one edge. All three require the
observed modification stamp and reject requests where native vertex order changed
underneath the caller, consistent with MoveSlabVertices/OffsetSlabEdge.

## Per-edge slab settings — 2026-09-19T16:05:04+02:00

SetSlabEdgeSettings changes vertical/custom edge trims and side-surface overrides on
individual slab edges, checking the observed revision before writing and reading
back the full polygon (vertices, arcs, native vertex IDs) afterward. GetSlabTopology
now also returns each edge's outgoing trim and surface override data, not just
coordinates. See archicad-addon/Examples/SetSlabEdgeSettings.py for a non-executing
example of the request shape.

## Drawing title positioning and curved clip preservation — 2026-09-19T19:51:38+02:00

PositionDrawingTitles is a new command that repositions the title of an existing
drawing independently of ChangeDrawingLink, using the observed APINeig_DrawingTitle
point, a layout/revision check, paper-millimetre target coordinates and a dryRun
option. It drags the native title part and reads back point/frame/origin/angle/ratio
inside the undoable operation; it does not run automatically after a relink.
Separately, ChangeDrawingLink now borrows the original drawing's native clipping
polygon when creating the replacement, preserving circular arcs and multiple
contours instead of flattening them; multi-page source drawings are rejected
outright. GetDrawingFrames and PositionDrawings compute exact circular-arc extrema
for frame bounds (rejecting degenerate/near-zero/full-circle sweeps) and expose
title hotspot indexes and native sub-part descriptors; titles are still excluded
from the frame bounds themselves. None of this drawing/title work has run against
a live Archicad instance.

## Wall-layer boundary context for dimensions and references — 2026-09-19T19:51:38+02:00

GetWallReferenceGeometry's includeLayerGeometry option exposes straight vertical
cross-section lines for basic or composite walls — outside/inside lines, individual
layer/material/core/finish roles, and contiguous composite core boundary lines —
derived from the SDK's documented reference offset, wall flip and composite skin
order (checking the layer thickness sum). These are untrimmed cross-sections, not
junction/SEO/opening-cut faces or true dimension references; profiled and slanted
walls report an explicit error rather than an approximation. At most 20 walls per
call. GetDimensionAnchors' includeWallBoundaryContext labels existing native
dimension hotspots that coincide (within 1e-7 metres) with those untrimmed boundary
segments — this is coordinate coincidence, not proof of semantic core/finish
association, and the underlying native witness descriptor and its revision/
coordinate checks are unchanged.

## Section/elevation main and story markers, classification and property-group policy — 2026-09-19T16:05:04+02:00

ModifySectionSettings gained a mainMarker option that replaces a section/elevation's
main marker library part through its parent element (Archicad 27+), using the
selected loaded part's default GDL parameters and deliberately resetting any
existing custom marker parameters; section geometry and visibility are otherwise
retained, and the observed modification stamp is required. Story markers are
inspected as part of this same continuation but not replaced by the main-marker
edit. Separately: classification-item creation now propagates descendant failures
(every root gets a result; created system/item identities return as `{guid}`
objects with their input index), and property-group creation supports explicit
Error/ReuseIfMatching/Overwrite(description) collision policies, with
UpdatePropertyDefinitions able to edit metadata, classification availability,
editability and typed/expression defaults under the same strict default-value
parsing used elsewhere (preventing dropped enum values or incompatible types).

## Straight slab-edge offset — 2026-09-19T14:07:53.8060000+02:00

OffsetSlabEdge takes elementId, expectedModificationStamp, contourIndex, edgeIndex, distance and maximumEndpointMovement. Distances are metres; positive is left of the directed contour edge, negative right. It intersects the offset line with the original neighbouring edge lines, checks movement/collapse limits, then delegates two native vertex moves. Curved slabs are explicitly unsupported. GetSlabTopology now returns modificationStamp. Existing slab memo reads check coordinate, contour and auxiliary-memory lengths before indexing. General polygon validity is determined by the native edit. No native tests were performed in this implementation phase. See Examples/OffsetSlabEdge.py for a read-only-by-default caller example.

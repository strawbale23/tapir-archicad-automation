# Comparison with original Tapir 1.5.8

Updated: 2026-09-19T14:09:39.502157+02:00 (local timezone offset included).

Original revision: `ce033d6bdcc90b538b3c5f7ab62f676099b96823`. All 73 original native C++/header/JSON files match the saved GitHub tree hashes.

Original registrations: **236**. Current: **279**. Added: **43**. Removed: **0**.

These counts describe registered C++ command classes. They are not a completion percentage. Changes to existing commands are additional work and are not counted as new commands.

## Added command registrations

| Command class | Group |
|---|---|
| `CheckLibraryPartAncestryCommand` | libraryCommands |
| `CopyViewSettingsCommand` | navigatorCommands |
| `CreateMasterLayoutsCommand` | navigatorCommands |
| `CreatePolygonalWallsCommand` | elementCommands |
| `EditDimensionChainCommand` | elementCommands |
| `EditSlabTopologyCommand` | elementCommands |
| `ExecuteGuardedCommandCommand` | developerCommands |
| `GetAnnotationDetailsCommand` | elementCommands |
| `GetAnnotationFormattingCommand` | elementCommands |
| `GetAssemblySegmentsCommand` | elementCommands |
| `GetAutomationSessionCommand` | developerCommands |
| `GetCommandContractsCommand` | developerCommands |
| `GetDimensionAnchorsCommand` | elementCommands |
| `GetDrawingFramesCommand` | elementCommands |
| `GetElementContextCommand` | elementCommands |
| `GetElementHotspotsCommand` | elementCommands |
| `GetLibraryPartParametersCommand` | elementCommands |
| `GetLibraryPartPreviewCommand` | libraryCommands |
| `GetNativeComponentQuantitiesCommand` | elementCommands |
| `GetNativeQuantitiesCommand` | elementCommands |
| `GetNativeQuantityDefinitionsCommand` | elementCommands |
| `GetOperationReceiptCommand` | developerCommands |
| `GetPlanPrimitivesCommand` | elementCommands |
| `GetPolygonalWallGeometryCommand` | elementCommands |
| `GetRoofGeometryCommand` | elementCommands |
| `GetSectionSettingsCommand` | elementCommands |
| `GetSlabTopologyCommand` | elementCommands |
| `GetWallReferenceGeometryCommand` | elementCommands |
| `ModifyDimensionSettingsCommand` | elementCommands |
| `ModifyLabelsCommand` | elementCommands |
| `ModifyPolygonalWallGeometryCommand` | elementCommands |
| `ModifySectionSettingsCommand` | elementCommands |
| `ModifyTextsCommand` | elementCommands |
| `MoveSlabVerticesCommand` | elementCommands |
| `OffsetSlabEdgeCommand` | elementCommands |
| `PositionDrawingsCommand` | elementCommands |
| `SearchLibraryPartsCommand` | libraryCommands |
| `SetAnnotationRunStylesCommand` | elementCommands |
| `SetAnnotationTextStyleCommand` | elementCommands |
| `SetAssemblySegmentsCommand` | elementCommands |
| `SetElementRenovationStatusCommand` | elementCommands |
| `StretchElementAtHotspotCommand` | elementCommands |
| `TransformElementsCommand` | elementCommands |

## Changes to existing input descriptions

35 directly readable input descriptions changed. 9 existing command descriptions need manual comparison because they are inherited or assembled in code. 31 commands directly inherit CommandBase's default schema method in both versions and are listed separately.

The companion JSON file lists the affected commands and added/removed property paths. An unchanged property name does not mean unchanged behaviour. Values, constraints and native implementation also require review.

## Native files changed

- `archicad-addon/Sources/3DCutPlaneCommands.cpp`
- `archicad-addon/Sources/AddOnMain.cpp`
- `archicad-addon/Sources/AddOnVersion.hpp`
- `archicad-addon/Sources/ApplicationCommands.cpp`
- `archicad-addon/Sources/AttributeCommands.cpp`
- `archicad-addon/Sources/AttributeCommands.hpp`
- `archicad-addon/Sources/ClassificationCommands.cpp`
- `archicad-addon/Sources/CommandBase.cpp`
- `archicad-addon/Sources/CommandBase.hpp`
- `archicad-addon/Sources/DesignOptionCommands.cpp`
- `archicad-addon/Sources/DeveloperTools.cpp`
- `archicad-addon/Sources/DeveloperTools.hpp`
- `archicad-addon/Sources/DocumentCreationCommands.cpp`
- `archicad-addon/Sources/DocumentCreationCommands.hpp`
- `archicad-addon/Sources/ElementCommands.cpp`
- `archicad-addon/Sources/ElementCommands.hpp`
- `archicad-addon/Sources/ElementCreationCommands.cpp`
- `archicad-addon/Sources/ElementGDLParameterCommands.cpp`
- `archicad-addon/Sources/ElementGDLParameterCommands.hpp`
- `archicad-addon/Sources/ElementGroupingCommands.cpp`
- `archicad-addon/Sources/ExtendedElementCommands.cpp`
- `archicad-addon/Sources/ExtendedElementCommands.hpp`
- `archicad-addon/Sources/FavoritesCommands.cpp`
- `archicad-addon/Sources/KeynoteCommands.cpp`
- `archicad-addon/Sources/LibraryCommands.cpp`
- `archicad-addon/Sources/LibraryCommands.hpp`
- `archicad-addon/Sources/MEPCommands.cpp`
- `archicad-addon/Sources/NavigatorCommands.cpp`
- `archicad-addon/Sources/NavigatorCommands.hpp`
- `archicad-addon/Sources/NotificationCommands.cpp`
- `archicad-addon/Sources/ProjectCommands.cpp`
- `archicad-addon/Sources/PropertyCommands.cpp`
- `archicad-addon/Sources/RFIX/Images/CommonSchemaDefinitions.json`
- `archicad-addon/Sources/SolidElementOperationCommands.cpp`
- `archicad-addon/Sources/TeamworkCommands.cpp`

## Meaning and limits

This is an exact comparison of the selected source files and registered classes, with line endings normalized. It does not identify the author of every intermediate change, establish SDK field coverage or establish native model behaviour. Use the functional guide for architectural explanations and the Git history for individual contributions.

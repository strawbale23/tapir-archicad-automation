"""Native request example. Updated 19 September 2026, 19:49 CEST.

Prints an example only; it does not connect to or modify Archicad. Populate the
identities, revision and title hotspot from GetDrawingFrames on the active layout.
Run this as a separate native command after any drawing creation/relink transaction.
Coordinates are layout paper millimetres. dryRun=True inspects without moving.
This example has not been exercised against an Archicad project.
"""
import json

request = {
    "command": "API.ExecuteAddOnCommand",
    "parameters": {
        "addOnCommandId": {
            "commandNamespace": "TapirCommand",
            "commandName": "PositionDrawingTitles",
        },
        "addOnCommandParameters": {
            "expectedDatabaseId": {"guid": "00000000-0000-0000-0000-000000000000"},
            "dryRun": True,
            "titles": [{
                "elementId": {"guid": "00000000-0000-0000-0000-000000000000"},
                "expectedModificationStamp": "REPLACE_WITH_OBSERVED_REVISION",
                "hotspotIndex": 0,
                "expectedPositionMillimetres": {"x": 40, "y": 30},
                "positionMillimetres": {"x": 40, "y": 20},
            }],
        },
    },
}

if __name__ == "__main__":
    print(json.dumps(request, indent=2))

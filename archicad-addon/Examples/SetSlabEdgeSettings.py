"""Native request example. Updated 19 September 2026, 16:04 CEST.

Reads nothing and changes nothing when run: prints a JSON command example.
Replace the example identity and revision with GetSlabTopology output before
sending the request through Archicad's API.ExecuteAddOnCommand interface.
This is not a tested fixture, an MCP command, or a project save operation.
"""
import json

request = {
    "command": "API.ExecuteAddOnCommand",
    "parameters": {
        "addOnCommandId": {
            "commandNamespace": "TapirCommand",
            "commandName": "SetSlabEdgeSettings",
        },
        "addOnCommandParameters": {
            "elementId": {"guid": "00000000-0000-0000-0000-000000000000"},
            "expectedModificationStamp": "REPLACE_WITH_OBSERVED_REVISION",
            "edges": [{
                "contourIndex": 0,
                "edgeIndex": 0,
                "trim": {"type": "Vertical"},
                "sideMaterial": {"overridden": False},
            }],
        },
    },
}

if __name__ == "__main__":
    print(json.dumps(request, indent=2))

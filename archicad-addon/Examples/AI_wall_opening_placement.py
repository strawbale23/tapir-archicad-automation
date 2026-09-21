"""Request examples for development build 1.5.8-ai.1; prints JSON, makes no model changes."""
import json
wall_id = {"guid": "REPLACE_WITH_TARGET_WALL_GUID"}
requests = [
    ("GetWallReferenceGeometry", {"elements": [{"elementId": wall_id}]}),
    ("ModifyWalls", {"wallsWithDetails": [{"elementId": wall_id, "flipped": True, "junctionOrder": 250}]}),
    ("CreateDoors", {"doorsData": [{
        "ownerWallId": wall_id, "width": .9, "height": 2.1,
        "favoriteName": "REPLACE_WITH_TARGET_PROJECT_DOOR_FAVOURITE",
        "placement": {"from": "Start", "anchor": "NearestJamb", "distance": .15}
    }]}),
    ("ModifyWindows", {"windowsWithDetails": [{
        "elementId": {"guid": "REPLACE_WITH_TARGET_WINDOW_GUID"},
        "width": 1.2,
        "placement": {"from": "End", "anchor": "NearestJamb", "distance": .3}
    }]})
]
if __name__ == "__main__":
    print(json.dumps(requests, indent=2))


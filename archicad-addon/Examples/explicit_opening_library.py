"""Payload example, not an automatic live mutation.
Discover the target project and library inventory first. Substitute its current
index and GUID together. Supply the existing target wall's GUID separately.
"""
def double_door_payload(wall_guid, library_record):
    return {'doorsData':[{
        'ownerWallId':{'guid':wall_guid},
        'libraryPart':{'index':library_record['index'],'guid':library_record['guid']},
        'width':1.2,'height':2.4,'sillHeight':0,
        'placement':{'from':'Start','anchor':'NearestJamb','distance':.319057954}
    }]}

# Send to native TapirCommand.CreateDoors, then GetDetailsOfElements on returned
# IDs. Verify native type Door, libPart.ownUnID, size, host and placement, then
# inspect the actual plan and 3D. The MCP wrapper also needs its schema refreshed
# before it can forward this new libraryPart field.

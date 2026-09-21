#include "NativeHotspotCommands.hpp"
#include "MigrationHelper.hpp"
#include <cmath>
#include <string>

static GS::UniString HotspotStamp (const API_Elem_Head& head)
{
    return GS::UniString (std::to_string (head.modiStamp).c_str ());
}

GS::Optional<GS::UniString> GetElementHotspotsCommand::GetInputParametersSchema () const
{
    return R"({"type":"object","properties":{
        "elementId":{"$ref":"#/ElementId"},"offset":{"type":"integer","minimum":0},
        "limit":{"type":"integer","minimum":1,"maximum":100}
    },"required":["elementId"],"additionalProperties":false})";
}

GS::Optional<GS::UniString> GetElementHotspotsCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{
        "elementId":{"$ref":"#/ElementId"},"databaseId":{"$ref":"#/DatabaseId"},
        "modificationStamp":{"type":"string"},"coordinateSystem":{"const":"NativeElementHotspotCoordinates"},"units":{"const":"metres"},
        "hotspots":{"type":"array","items":{"type":"object","properties":{
            "hotspotIndex":{"type":"integer"},"coordinate":{"$ref":"#/Coordinate3D"},
            "nativeElementId":{"$ref":"#/ElementId"},"nativeNeigId":{"type":"integer"},"nativeSubIndex":{"type":"integer"},
            "nativePartType":{"type":"integer"},"nativePartIndex":{"type":"integer"}
        },"required":["hotspotIndex","coordinate","nativeElementId","nativeNeigId","nativeSubIndex","nativePartType","nativePartIndex"],"additionalProperties":false}},
        "total":{"type":"integer"},"nextOffset":{"type":"integer"},"hasMore":{"type":"boolean"}
    },"required":["elementId","databaseId","modificationStamp","coordinateSystem","units","hotspots","total","nextOffset","hasMore"],"additionalProperties":false})";
}

GS::ObjectState GetElementHotspotsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl&) const
{
    API_Elem_Head head = {}; head.guid = GetGuidFromArrayItem ("elementId", parameters);
    GSErrCode err = ACAPI_Element_GetHeader (&head);
    if (err != NoError) return CreateErrorResponse (err, "Cannot read hotspot owner.");
    Int32 offset=0, limit=50; parameters.Get ("offset", offset); parameters.Get ("limit", limit);
    if (offset < 0 || limit < 1 || limit > 100) return CreateErrorResponse (APIERR_BADPARS, "Invalid hotspot page limits.");
    API_DatabaseInfo database = {};
    err = ACAPI_Database_GetCurrentDatabase (&database);
    if (err != NoError) return CreateErrorResponse (err, "Cannot identify hotspot database context.");
    GS::Array<API_ElementHotspot> hotspots;
    err = ACAPI_Element_GetHotspots (head.guid, &hotspots);
    if (err != NoError) return CreateErrorResponse (err, "Cannot obtain native hotspots for this element in the current context.");
    GS::ObjectState result = CreateElementIdObjectState (head.guid);
    result.Add ("databaseId", CreateGuidObjectState (DatabaseIdResolver::Instance ().GetIdOfDatabase (database)));
    result.Add ("modificationStamp", HotspotStamp (head));
    result.Add ("coordinateSystem", "NativeElementHotspotCoordinates"); result.Add ("units", "metres");
    const auto& add = result.AddList<GS::ObjectState> ("hotspots");
    Int32 cursor = offset;
    while (cursor < static_cast<Int32> (hotspots.GetSize ()) && cursor-offset < limit) {
        const auto& point = hotspots[cursor];
        add (GS::ObjectState ("hotspotIndex", cursor, "coordinate", Create3DCoordinateObjectState (point.second),
            "nativeElementId", CreateGuidObjectState (point.first.guid), "nativeNeigId", static_cast<Int32> (point.first.neigID),
            "nativeSubIndex", point.first.inIndex, "nativePartType", static_cast<Int32> (point.first.elemPartType), "nativePartIndex", point.first.elemPartIndex));
        ++cursor;
    }
    result.Add ("total", static_cast<Int32> (hotspots.GetSize ()));
    result.Add ("nextOffset", cursor); result.Add ("hasMore", cursor < static_cast<Int32> (hotspots.GetSize ()));
    return result;
}

GS::Optional<GS::UniString> StretchElementAtHotspotCommand::GetInputParametersSchema () const
{
    return R"({"type":"object","properties":{
        "elementId":{"$ref":"#/ElementId"},"expectedDatabaseId":{"$ref":"#/DatabaseId"},
        "expectedModificationStamp":{"type":"string","pattern":"^[0-9]+$"},
        "hotspotIndex":{"type":"integer","minimum":0},"expectedCoordinate":{"$ref":"#/Coordinate3D"},
        "vector":{"$ref":"#/Coordinate3D"}
    },"required":["elementId","expectedDatabaseId","expectedModificationStamp","hotspotIndex","expectedCoordinate","vector"],"additionalProperties":false})";
}

GS::Optional<GS::UniString> StretchElementAtHotspotCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{
        "elementId":{"$ref":"#/ElementId"},"success":{"type":"boolean"},"status":{"type":"string"},
        "verification":{"const":"nativeHotspotCoordinateReadBack"},"coordinate":{"$ref":"#/Coordinate3D"},"modificationStamp":{"type":"string"}
    },"required":["elementId","success","status","verification","coordinate","modificationStamp"],"additionalProperties":false})";
}

GS::ObjectState StretchElementAtHotspotCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl&) const
{
    API_Elem_Head head = {}; head.guid = GetGuidFromArrayItem ("elementId", parameters);
    GSErrCode err = ACAPI_Element_GetHeader (&head);
    if (err != NoError) return CreateErrorResponse (err, "Cannot read stretch target.");
    GS::UniString stamp; parameters.Get ("expectedModificationStamp", stamp);
    if (stamp != HotspotStamp (head)) return CreateErrorResponse (APIERR_BADPARS, "Element changed; inspect hotspots again.");
    API_DatabaseInfo database = {};
    err = ACAPI_Database_GetCurrentDatabase (&database);
    if (err != NoError) return CreateErrorResponse (err, "Cannot identify current database.");
    if (DatabaseIdResolver::Instance ().GetIdOfDatabase (database) != GetGuidFromArrayItem ("expectedDatabaseId", parameters))
        return CreateErrorResponse (APIERR_BADPARS, "Database changed since hotspot inspection.");
    if (!ACAPI_Element_Filter (head.guid, APIFilt_IsEditable | APIFilt_InMyWorkspace | APIFilt_HasAccessRight))
        return CreateErrorResponse (APIERR_BADPARS, "Stretch target is not editable.");
    API_Guid group = APINULLGuid;
    if (ACAPI_Grouping_GetGroup (head.guid, &group) == NoError && group != APINULLGuid)
        return CreateErrorResponse (APIERR_BADPARS, "Single-hotspot stretch does not support grouped elements.");
    GS::Array<API_ElementHotspot> hotspots;
    err = ACAPI_Element_GetHotspots (head.guid, &hotspots);
    if (err != NoError) return CreateErrorResponse (err, "Cannot resolve current native hotspot.");
    Int32 index=-1; parameters.Get ("hotspotIndex", index);
    const auto* expected = parameters.Get ("expectedCoordinate"); const auto* vector = parameters.Get ("vector");
    if (index < 0 || index >= static_cast<Int32> (hotspots.GetSize ()) || expected == nullptr || vector == nullptr)
        return CreateErrorResponse (APIERR_BADPARS, "Invalid hotspot index or coordinates.");
    const auto before = Get3DCoordinateFromObjectState (*expected), delta = Get3DCoordinateFromObjectState (*vector);
    const auto equal = [] (const API_Coord3D& a, const API_Coord3D& b) {
        return std::isfinite (a.x) && std::isfinite (a.y) && std::isfinite (a.z) &&
            std::abs (a.x-b.x) <= 1e-8 && std::abs (a.y-b.y) <= 1e-8 && std::abs (a.z-b.z) <= 1e-8;
    };
    const auto& point = hotspots[index];
    if (point.first.guid != head.guid || !equal (before, point.second) || !equal (delta, delta))
        return CreateErrorResponse (APIERR_BADPARS, "Hotspot identity/coordinate changed or vector is non-finite.");
    API_EditPars edit = {}; edit.typeID = APIEdit_Stretch; edit.withDelete = true;
    edit.begC = point.second; edit.endC = {point.second.x+delta.x, point.second.y+delta.y, point.second.z+delta.z};
    if (!equal (edit.endC, edit.endC)) return CreateErrorResponse (APIERR_BADPARS, "Stretch target coordinate is non-finite.");
    GS::Array<API_Neig> targets; targets.Push (point.first);
    err = ACAPI_CallUndoableCommand ("Stretch native hotspot", [&] () -> GSErrCode { return ACAPI_Element_Edit (&targets, edit); });
    if (err != NoError) return CreateErrorResponse (err, "Native stretch failed; committed state is not confirmed.");
    if (targets.GetSize () != 1 || targets[0].guid != head.guid)
        return CreateErrorResponse (APIERR_GENERAL, "Stretch returned unexpected target identities; inspect before retrying.");
    API_Coord3D actual = {};
#ifdef ServerMainVers_2700
    err = ACAPI_Element_NeigToCoord (&targets[0], &actual);
#else
    err = ACAPI_Goodies (APIAny_NeigToCoordID, &targets[0], &actual);
#endif
    if (err == NoError) err = ACAPI_Element_GetHeader (&head);
    if (err != NoError) return CreateErrorResponse (err, "Stretch ran but hotspot readback failed; inspect before retrying.");
    const bool matches = equal (actual, edit.endC);
    GS::ObjectState result = CreateElementIdObjectState (head.guid);
    result.Add ("success", matches); result.Add ("status", matches ? "applied" : "notConfirmed");
    result.Add ("verification", "nativeHotspotCoordinateReadBack");
    result.Add ("coordinate", Create3DCoordinateObjectState (actual)); result.Add ("modificationStamp", HotspotStamp (head));
    return result;
}

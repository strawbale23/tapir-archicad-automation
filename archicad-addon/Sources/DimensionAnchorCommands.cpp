#include "DimensionAnchorCommands.hpp"
#include "MigrationHelper.hpp"
#include "WallReferenceCommands.hpp"
#include <cmath>
#include <string>

namespace {
GS::UniString Stamp (const API_Elem_Head& h) { return GS::UniString (std::to_string (h.modiStamp).c_str ()); }
// The SDK API_Base documentation and Automatic Dimensioning FAQ define these
// conversions. Section nodes require different descriptors and are excluded.
const char* Kind (const API_Neig& n, bool& line, char& special) {
    line = false; special = 0;
    switch (n.neigID) {
        case APINeig_Wall: return "WallPoint";
        case APINeig_WallOn: line = true; return "WallEdge";
        case APINeig_WallPl: special = 1; return "WallPolygonPoint";
        case APINeig_WallPlOn: special = 1; line = true; return "WallPolygonEdge";
        case APINeig_Wind: return "WindowPoint";
        case APINeig_WindHole: special = 1; return "WindowHolePoint";
        case APINeig_Door: return "DoorPoint";
        case APINeig_DoorHole: special = 1; return "DoorHolePoint";
        case APINeig_Ceil: return "SlabVertex";
        case APINeig_CeilOn: line = true; return "SlabEdge";
        case APINeig_Beam: return "BeamPoint";
        case APINeig_BeamOn: line = true; return "BeamEdge";
        case APINeig_Line: return "LineEndpoint";
        case APINeig_LineOn: line = true; return "LineEdge";
        case APINeig_Roof: return "RoofVertex";
        case APINeig_RoofOn: line = true; return "RoofEdge";
        case APINeig_Mesh: return "MeshVertex";
        case APINeig_MeshOn: line = true; return "MeshEdge";
        default: return nullptr;
    }
}
GSErrCode FloorPlan (API_DatabaseInfo& db) {
    const GSErrCode err = ACAPI_Database_GetCurrentDatabase (&db);
    return err != NoError ? err : db.typeID == APIWind_FloorPlanID ? NoError : APIERR_BADDATABASE;
}
GS::ObjectState Selection (const API_Elem_Head& h, const API_DatabaseInfo& db, UInt32 index, const API_Coord3D& c) {
    return GS::ObjectState ("hotspotIndex",index,"expectedModificationStamp",Stamp (h),
        "expectedDatabaseId",CreateGuidObjectState (DatabaseIdResolver::Instance ().GetIdOfDatabase (db)),"expectedCoordinate",Create3DCoordinateObjectState (c));
}
}

GS::Optional<GS::UniString> GetDimensionAnchorsCommand::GetInputParametersSchema () const {
    return R"({"type":"object","properties":{"elementId":{"$ref":"#/ElementId"},"offset":{"type":"integer","minimum":0},"limit":{"type":"integer","minimum":1,"maximum":100},"includeWallBoundaryContext":{"type":"boolean","default":false,"description":"For supported walls, label native hotspots coincident with untrimmed outside/inside/core boundary lines within 1e-7 metres. Coordinate coincidence does not establish a semantic core/finish association and does not include junction/SEO/opening-cut faces."}},"required":["elementId"],"additionalProperties":false})";
}
GS::Optional<GS::UniString> GetDimensionAnchorsCommand::GetRawResponseSchema () const {
    return R"({"type":"object","properties":{"elementId":{"$ref":"#/ElementId"},"anchors":{"type":"array","items":{"type":"object","properties":{"kind":{"type":"string"},"coordinate":{"$ref":"#/Coordinate3D"},"witnessPoint":{"type":"object"},"coincidentUntrimmedWallBoundaries":{"type":"array","items":{"enum":["outsideFace","insideFace","coreOutsideFace","coreInsideFace"]}}},"required":["kind","coordinate","witnessPoint"],"additionalProperties":false}},"coordinateSystem":{"const":"FloorPlanNativeHotspots"},"units":{"const":"metres"},"nextOffset":{"type":"integer"},"hasMore":{"type":"boolean"},"nativeHotspotCount":{"type":"integer"},"wallBoundaryContext":{"type":"object"}},"required":["elementId","anchors","coordinateSystem","units","nextOffset","hasMore","nativeHotspotCount"],"additionalProperties":false})";
}
GS::ObjectState GetDimensionAnchorsCommand::Execute (const GS::ObjectState& p, GS::ProcessControl& control) const {
    API_DatabaseInfo db = {}; GSErrCode err = FloorPlan (db);
    if (err != NoError) return CreateErrorResponse (err,"Dimension anchor discovery requires the floor-plan database.");
    API_Elem_Head h = {}; h.guid = GetGuidFromArrayItem ("elementId",p);
    err = ACAPI_Element_GetHeader (&h);
    if (err != NoError) return CreateErrorResponse (err,"Cannot read dimension anchor owner.");
    Int32 offset=0,limit=50; p.Get ("offset",offset); p.Get ("limit",limit);
    if (offset < 0 || limit < 1 || limit > 100) return CreateErrorResponse (APIERR_BADPARS,"Invalid anchor page.");
    GS::Array<API_ElementHotspot> hotspots; err = ACAPI_Element_GetHotspots (h.guid,&hotspots);
    if (err != NoError) return CreateErrorResponse (err,"Cannot read native anchor hotspots.");
    bool wallContext=false; p.Get("includeWallBoundaryContext",wallContext);
    GS::ObjectState boundaries;
    if (wallContext) {
        GS::Array<GS::ObjectState> requested; requested.Push(CreateElementIdObjectState(h.guid));
        const auto query=GetWallReferenceGeometryCommand().Execute(GS::ObjectState("elements",requested,"includeLayerGeometry",true),control);
        GS::Array<GS::ObjectState> walls;
        if (query.Get("walls",walls) && walls.GetSize()==1) {
            if (const auto* geometry=walls[0].Get("layerGeometry")) boundaries=*geometry;
            else if (const auto* failure=walls[0].Get("error")) boundaries.Add("error",*failure);
        } else boundaries=CreateErrorResponse(APIERR_GENERAL,"Cannot obtain wall boundary context.");
    }
    GS::ObjectState result = CreateElementIdObjectState (h.guid);
    result.Add ("coordinateSystem","FloorPlanNativeHotspots"); result.Add ("units","metres");
    const auto& add = result.AddList<GS::ObjectState> ("anchors");
    UInt32 cursor = static_cast<UInt32> (offset); Int32 found=0;
    while (cursor < hotspots.GetSize () && found < limit) {
        const UInt32 index = cursor++; const auto& hotspot = hotspots[index];
        bool line; char special;
        const char* kind = Kind (hotspot.first,line,special);
        if (kind == nullptr || hotspot.first.guid != h.guid) continue;
        GS::ObjectState anchor("kind",kind,"coordinate",Create3DCoordinateObjectState(hotspot.second),
            "witnessPoint",GS::ObjectState("elementId",CreateGuidObjectState(h.guid),"hotspot",Selection(h,db,index,hotspot.second)));
        if (wallContext && !boundaries.Contains("error")) {
            GS::Array<GS::UniString> matches;
            for (const char* name:{"outsideFace","insideFace","coreOutsideFace","coreInsideFace"}) {
                const auto* boundary=boundaries.Get(name);
                if (boundary==nullptr || boundary->Get("start")==nullptr || boundary->Get("end")==nullptr) continue;
                const auto start=Get2DCoordinateFromObjectState(*boundary->Get("start")),end=Get2DCoordinateFromObjectState(*boundary->Get("end"));
                const double dx=end.x-start.x,dy=end.y-start.y,length=std::hypot(dx,dy);
                if (!std::isfinite(length) || length<=1e-8 || !std::isfinite(hotspot.second.x) || !std::isfinite(hotspot.second.y)) continue;
                const double x=hotspot.second.x-start.x,y=hotspot.second.y-start.y;
                const double station=(x*dx+y*dy)/length,distance=std::abs(x*dy-y*dx)/length;
                if (distance<=1e-7 && station>=-1e-7 && station<=length+1e-7) matches.Push(GS::UniString(name));
            }
            anchor.Add("coincidentUntrimmedWallBoundaries",matches);
        }
        add(anchor);
        ++found;
    }
    result.Add ("nextOffset",cursor); result.Add ("hasMore",cursor < hotspots.GetSize ()); result.Add ("nativeHotspotCount",hotspots.GetSize ());
    if (wallContext) result.Add("wallBoundaryContext",boundaries);
    return result;
}

GSErrCode ResolveNativeDimensionHotspot (const GS::ObjectState& p, API_Base& base, GS::UniString& message) {
    message = "Native dimension anchor changed, is unsupported, or has an invalid descriptor; inspect GetDimensionAnchors again.";
    const auto* selection = p.Get ("hotspot");
    if (selection == nullptr) return APIERR_BADPARS;
    for (const char* field : {"line","inIndex","special","nodeType","nodeStatus","nodeId"}) if (p.Contains (field)) return APIERR_BADPARS;
    API_DatabaseInfo db = {}; GSErrCode err = FloorPlan (db);
    if (err != NoError) return err;
    if (GetGuidFromArrayItem ("expectedDatabaseId",*selection) != DatabaseIdResolver::Instance ().GetIdOfDatabase (db)) return APIERR_BADPARS;
    API_Elem_Head h = {}; h.guid = GetGuidFromArrayItem ("elementId",p);
    err = ACAPI_Element_GetHeader (&h); if (err != NoError) return err;
    GS::UniString expected; selection->Get ("expectedModificationStamp",expected);
    if (expected != Stamp (h)) return APIERR_BADPARS;
    Int32 index=-1; selection->Get ("hotspotIndex",index);
    const auto* expectedPoint = selection->Get ("expectedCoordinate");
    if (index < 0 || expectedPoint == nullptr) return APIERR_BADPARS;
    const API_Coord3D point = Get3DCoordinateFromObjectState (*expectedPoint);
    GS::Array<API_ElementHotspot> hotspots; err = ACAPI_Element_GetHotspots (h.guid,&hotspots);
    if (err != NoError) return err;
    if (static_cast<UInt32> (index) >= hotspots.GetSize ()) return APIERR_BADPARS;
    const auto& native = hotspots[index];
    auto coordinateMatches = [] (double a,double b) { return std::isfinite (a) && std::isfinite (b) && std::abs (a-b) <= 1e-8; };
    if (native.first.guid != h.guid || !coordinateMatches (point.x,native.second.x) || !coordinateMatches (point.y,native.second.y) || !coordinateMatches (point.z,native.second.z)) return APIERR_BADPARS;
    base = {};
    if (Kind (native.first,base.line,base.special) == nullptr) return APIERR_BADPARS;
#ifdef ServerMainVers_2600
    base.type = h.type;
#else
    base.typeID = h.typeID;
#endif
    base.guid = h.guid; base.inIndex = native.first.inIndex; base.node_id = native.first.supplUnId;
    return NoError;
}

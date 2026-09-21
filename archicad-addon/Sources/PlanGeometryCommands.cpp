#include "PlanGeometryCommands.hpp"
#include "MigrationHelper.hpp"
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include <cmath>

namespace {
GS::UniString Stamp (const API_Element& e) { return GS::UniString (std::to_string (e.header.modiStamp).c_str ()); }
GSErrCode Polygon (GS::ObjectState& out,const API_Polygon& poly,API_Coord** coords,Int32** pends,API_PolyArc** arcs,Int32 limit) {
    if (poly.nCoords < 0 || poly.nCoords>limit || poly.nSubPolys<0 || poly.nSubPolys>poly.nCoords || poly.nArcs<0 || poly.nArcs>limit) return APIERR_BADPARS;
    if (poly.nCoords>0 && (coords==nullptr || BMhGetSize (reinterpret_cast<GSHandle> (coords))<static_cast<GSSize> ((poly.nCoords+1)*sizeof (API_Coord)))) return APIERR_GENERAL;
    if (poly.nSubPolys>0 && (pends==nullptr || BMhGetSize (reinterpret_cast<GSHandle> (pends))<static_cast<GSSize> ((poly.nSubPolys+1)*sizeof (Int32)))) return APIERR_GENERAL;
    if (poly.nArcs>0 && (arcs==nullptr || BMhGetSize (reinterpret_cast<GSHandle> (arcs))<static_cast<GSSize> (poly.nArcs*sizeof (API_PolyArc)))) return APIERR_GENERAL;
    const auto& add=out.AddList<GS::ObjectState> ("coordinates");
    for (Int32 i=1;i<=poly.nCoords;++i) {
        if (!std::isfinite ((*coords)[i].x) || !std::isfinite ((*coords)[i].y)) return APIERR_BADPARS;
        add (GS::ObjectState ("nativeIndex",i,"coordinate",Create2DCoordinateObjectState ((*coords)[i])));
    }
    const auto& ends=out.AddList<Int32> ("contourEndIndices");
    Int32 previousEnd=0;
    for (Int32 i=1;i<=poly.nSubPolys;++i) {
        const Int32 end=(*pends)[i];
        if (end<=previousEnd || end>poly.nCoords) return APIERR_GENERAL;
        ends (end); previousEnd=end;
    }
    if (poly.nSubPolys>0 && previousEnd!=poly.nCoords) return APIERR_GENERAL;
    const auto& addArc=out.AddList<GS::ObjectState> ("arcs");
    for (Int32 i=0;i<poly.nArcs;++i) {
        const auto& arc=(*arcs)[i];
        if (arc.begIndex<1 || arc.begIndex>poly.nCoords || arc.endIndex<1 || arc.endIndex>poly.nCoords || !std::isfinite (arc.arcAngle)) return APIERR_GENERAL;
        addArc (CreatePolyArcObjectState (arc));
    }
    return NoError;
}
}
GS::Optional<GS::UniString> GetPlanPrimitivesCommand::GetInputParametersSchema () const {
    return R"({"type":"object","properties":{"expectedDatabaseId":{"$ref":"#/DatabaseId"},"types":{"type":"array","minItems":1,"maxItems":5,"uniqueItems":true,"items":{"enum":["Line","Polyline","Arc","Circle","Text"]}},"offset":{"type":"integer","minimum":0},"limit":{"type":"integer","minimum":1,"maximum":100},"maxCoordinatesPerElement":{"type":"integer","minimum":2,"maximum":10000},"floorIndex":{"type":"integer","minimum":-32768,"maximum":32767}},"required":["expectedDatabaseId","types"],"additionalProperties":false})";
}
GS::Optional<GS::UniString> GetPlanPrimitivesCommand::GetRawResponseSchema () const {
    return R"({"type":"object","properties":{"databaseId":{"$ref":"#/DatabaseId"},"coordinateSystem":{"const":"CurrentDatabaseXY"},"lengthUnit":{"const":"metres"},"angleUnit":{"const":"radians"},"pagination":{"const":"SortedGuidOffsetRestartAfterEdits"},"primitives":{"type":"array","items":{"type":"object"}},"nextOffset":{"type":"integer"},"hasMore":{"type":"boolean"},"candidateCount":{"type":"integer"}},"required":["databaseId","coordinateSystem","lengthUnit","angleUnit","pagination","primitives","nextOffset","hasMore","candidateCount"],"additionalProperties":false})";
}
GS::ObjectState GetPlanPrimitivesCommand::Execute (const GS::ObjectState& p,GS::ProcessControl&) const {
    API_DatabaseInfo db = {}; GSErrCode err=ACAPI_Database_GetCurrentDatabase (&db);
    if (err!=NoError) return CreateErrorResponse (err,"Cannot identify plan database.");
    const auto databaseId=DatabaseIdResolver::Instance ().GetIdOfDatabase (db);
    if (databaseId!=GetGuidFromArrayItem ("expectedDatabaseId",p)) return CreateErrorResponse (APIERR_BADDATABASE,"Plan database changed.");
    GS::Array<GS::UniString> types; p.Get ("types",types);
    if (types.IsEmpty () || types.GetSize ()>5) return CreateErrorResponse (APIERR_BADPARS,"Supply one to five primitive types.");
    const std::map<std::string,API_ElemTypeID> known {{"Line",API_LineID},{"Polyline",API_PolyLineID},{"Arc",API_ArcID},{"Circle",API_CircleID},{"Text",API_TextID}};
    std::vector<API_Guid> ids; GS::HashSet<API_Guid> seen;
    for (const auto& name:types) {
        const auto found=known.find (std::string (name.ToCStr (CC_UTF8).Get ()));
        if (found==known.end ()) return CreateErrorResponse (APIERR_BADPARS,"Unknown plan primitive type.");
        GS::Array<API_Guid> native; err=ACAPI_Element_GetElemList (found->second,&native);
        if (err!=NoError) return CreateErrorResponse (err,"Cannot enumerate native plan primitives.");
        for (const auto& id:native) if (!seen.Contains (id)) { seen.Add (id); ids.push_back (id); }
    }
    std::sort (ids.begin (),ids.end (),[] (const API_Guid& a,const API_Guid& b) { return APIGuidToString (a)<APIGuidToString (b); });
    Int32 offset=0,limit=50,maxCoords=1000,floor=0;
    p.Get ("offset",offset); p.Get ("limit",limit); p.Get ("maxCoordinatesPerElement",maxCoords); const bool byFloor=p.Get ("floorIndex",floor);
    if (offset<0 || limit<1 || limit>100 || maxCoords<2 || maxCoords>10000) return CreateErrorResponse (APIERR_BADPARS,"Invalid geometry page limits.");
    GS::ObjectState result ("databaseId",CreateGuidObjectState (databaseId),"coordinateSystem","CurrentDatabaseXY","lengthUnit","metres","angleUnit","radians","pagination","SortedGuidOffsetRestartAfterEdits");
    const auto& add=result.AddList<GS::ObjectState> ("primitives");
    size_t cursor=static_cast<size_t> (offset); Int32 scanned=0,returned=0;
    while (cursor<ids.size () && returned<limit && scanned<1000) {
        const auto id=ids[cursor++]; ++scanned;
        API_Element e = {}; e.header.guid=id; err=ACAPI_Element_Get (&e);
        if (err==NoError && byFloor && e.header.floorInd!=floor) continue;
        auto row=CreateElementIdObjectState (id);
        if (err==NoError) {
            const auto type=GetElemTypeId (e.header);
            row.Add ("type",GetElementTypeNonLocalizedName (type)); row.Add ("modificationStamp",Stamp (e)); row.Add ("floorIndex",e.header.floorInd);
            row.Add ("layerIndex",GetAttributeIndex (e.header.layer));
            GS::ObjectState geometry;
            if (type==API_LineID) { geometry.Add ("begin",Create2DCoordinateObjectState (e.line.begC)); geometry.Add ("end",Create2DCoordinateObjectState (e.line.endC)); }
            else if (type==API_ArcID || type==API_CircleID) {
                geometry.Add ("center",Create2DCoordinateObjectState (e.arc.origC)); geometry.Add ("radius",e.arc.r); geometry.Add ("ratio",e.arc.ratio);
                geometry.Add ("rotation",e.arc.angle); geometry.Add ("reflected",e.arc.reflected); geometry.Add ("whole",type==API_CircleID);
                if (type==API_ArcID) { geometry.Add ("beginAngle",e.arc.begAng); geometry.Add ("endAngle",e.arc.endAng); }
            } else {
                API_ElementMemo memo = {}; const GS::OnExit dispose ([&] { ACAPI_DisposeElemMemoHdls (&memo); });
                err=ACAPI_Element_GetMemo (id,&memo,type==API_TextID?APIMemoMask_TextContent:APIMemoMask_Polygon);
                if (err==NoError && type==API_PolyLineID) err=Polygon (geometry,e.polyLine.poly,memo.coords,memo.pends,memo.parcs,maxCoords);
                else if (err==NoError && type==API_TextID) {
#ifdef ServerMainVers_2800
                    const GS::UniString text=memo.textContent!=nullptr?*memo.textContent:GS::UniString ();
#else
                    const GS::UniString text=memo.textContent!=nullptr?GS::UniString (reinterpret_cast<GS::uchar_t*> (*memo.textContent)):GS::UniString ();
#endif
                    if (text.GetLength ()>10000) err=APIERR_BADPARS;
                    else { geometry.Add ("text",text); geometry.Add ("position",Create2DCoordinateObjectState (e.text.loc)); geometry.Add ("angle",e.text.angle); geometry.Add ("textSizeMillimetres",e.text.size); }
                }
            }
            if (err==NoError) row.Add ("geometry",geometry);
        }
        if (err!=NoError) row.Add ("error",*CreateErrorResponse (err,"Cannot read this primitive within requested payload limits.").Get ("error"));
        add (row); ++returned;
    }
    result.Add ("candidateCount",static_cast<Int32> (ids.size ())); result.Add ("nextOffset",static_cast<Int32> (cursor)); result.Add ("hasMore",cursor<ids.size ());
    return result;
}
GS::Optional<GS::UniString> GetRoofGeometryCommand::GetInputParametersSchema () const {
    return R"({"type":"object","properties":{"elementId":{"$ref":"#/ElementId"}},"required":["elementId"],"additionalProperties":false})";
}
GS::Optional<GS::UniString> GetRoofGeometryCommand::GetRawResponseSchema () const {
    return R"({"type":"object","properties":{"elementId":{"$ref":"#/ElementId"},"modificationStamp":{"type":"string"},"roofClass":{"enum":["SinglePlane","MultiPlane"]},"floorIndex":{"type":"integer"},"levelOffset":{"type":"number"},"thickness":{"type":"number"},"structureType":{"type":"string"},"buildingMaterialId":{"$ref":"#/AttributeId"},"compositeId":{"$ref":"#/AttributeId"},"coordinateSystem":{"const":"ProjectXY"},"lengthUnit":{"const":"metres"},"angleUnit":{"const":"radians"},"polygonRole":{"enum":["Contour","Pivot"]},"polygon":{"type":"object"},"pivotLine":{"type":"object"},"angle":{"type":"number"},"positiveSide":{"type":"boolean"},"eavesOverhang":{"type":"number"}},"required":["elementId","modificationStamp","roofClass","floorIndex","levelOffset","thickness","structureType","coordinateSystem","lengthUnit","angleUnit","polygonRole","polygon"],"additionalProperties":false})";
}
GS::ObjectState GetRoofGeometryCommand::Execute (const GS::ObjectState& p,GS::ProcessControl&) const {
    API_Element e = {}; e.header.guid=GetGuidFromArrayItem ("elementId",p);
    GSErrCode err=ACAPI_Element_Get (&e);
    if (err!=NoError) return CreateErrorResponse (err,"Cannot read roof.");
    if (GetElemTypeId (e.header)!=API_RoofID) return CreateErrorResponse (APIERR_BADID,"Expected a native roof.");
    const bool plane=e.roof.roofClass==API_PlaneRoofID;
    API_ElementMemo memo = {}; const GS::OnExit dispose ([&] { ACAPI_DisposeElemMemoHdls (&memo); });
    err=ACAPI_Element_GetMemo (e.header.guid,&memo,plane?APIMemoMask_Polygon:APIMemoMask_AdditionalPolygon);
    if (err!=NoError) return CreateErrorResponse (err,"Cannot read roof polygon.");
    GS::ObjectState polygon;
    err=plane?Polygon (polygon,e.roof.u.planeRoof.poly,memo.coords,memo.pends,memo.parcs,10000):Polygon (polygon,e.roof.u.polyRoof.pivotPolygon,memo.additionalPolyCoords,memo.additionalPolyPends,memo.additionalPolyParcs,10000);
    if (err!=NoError) return CreateErrorResponse (err,"Roof polygon exceeds the bounded query or has invalid native arrays.");
    auto result=CreateElementIdObjectState (e.header.guid);
    result.Add ("modificationStamp",Stamp (e)); result.Add ("roofClass",plane?"SinglePlane":"MultiPlane"); result.Add ("floorIndex",e.header.floorInd);
    result.Add ("levelOffset",e.roof.shellBase.level); result.Add ("thickness",e.roof.shellBase.thickness);
    const bool composite=e.roof.shellBase.modelElemStructureType==API_CompositeStructure;
    result.Add ("structureType",composite?"Composite":"Basic");
    result.Add (composite?"compositeId":"buildingMaterialId",CreateGuidObjectState (GetAttributeGuidFromIndex (composite?API_CompWallID:API_BuildingMaterialID,composite?e.roof.shellBase.composite:e.roof.shellBase.buildingMaterial)));
    result.Add ("coordinateSystem","ProjectXY"); result.Add ("lengthUnit","metres"); result.Add ("angleUnit","radians"); result.Add ("polygonRole",plane?"Contour":"Pivot"); result.Add ("polygon",polygon);
    if (plane) {
        result.Add ("pivotLine",GS::ObjectState ("begCoordinate",Create2DCoordinateObjectState (e.roof.u.planeRoof.baseLine.c1),"endCoordinate",Create2DCoordinateObjectState (e.roof.u.planeRoof.baseLine.c2)));
        result.Add ("angle",e.roof.u.planeRoof.angle); result.Add ("positiveSide",e.roof.u.planeRoof.posSign);
    } else result.Add ("eavesOverhang",e.roof.u.polyRoof.eavesOverHang);
    return result;
}

#include "PolygonalWallCommands.hpp"
#include "MigrationHelper.hpp"
#include <cmath>
#include <string>

GS::Optional<GS::UniString> CreatePolygonalWallsCommand::GetInputParametersSchema () const
{
    return R"({"type":"object","properties":{"wallsData":{"type":"array","minItems":1,"maxItems":100,"items":{
        "type":"object","properties":{
            "polygonOutline":{"type":"array","minItems":3,"maxItems":1000,"items":{"$ref":"#/Coordinate2D"},"description":"Straight-edged wall footprint in project XY metres, without closing duplicate. No holes."},
            "height":{"type":"number","exclusiveMinimum":0},"zCoordinate":{"type":"number","description":"Absolute base elevation above project zero, metres; default 0 even with explicit floorIndex."},
            "floorIndex":{"type":"integer","minimum":-32768,"maximum":32767},
            "buildingMaterialId":{"$ref":"#/AttributeId"},"layerId":{"$ref":"#/AttributeId"},
            "referenceEdgeIndex":{"type":"integer","minimum":0,"description":"Zero-based footprint edge used as reference; default 0."},
            "cornersCanChange":{"type":"boolean","description":"Allow native L/T junctions to change polygon corners; default false."}
        },"required":["polygonOutline","height","buildingMaterialId"],"additionalProperties":false
    }}},"required":["wallsData"],"additionalProperties":false})";
}
GS::Optional<GS::ObjectState> CreatePolygonalWallsCommand::SetTypeSpecificParameters (
    API_Element& element, API_ElementMemo& memo, const Stories& stories, const GS::ObjectState& parameters) const
{
    GS::Array<GS::ObjectState> outline;
    parameters.Get ("polygonOutline", outline);
    if (outline.GetSize () < 3 || outline.GetSize () > 1000) return CreateErrorResponse (APIERR_BADPARS,"Supply 3 to 1000 unique footprint vertices.");
    double height=0,z=0; parameters.Get("height",height); parameters.Get("zCoordinate",z);
    if (!std::isfinite(height) || height<=0 || !std::isfinite(z)) return CreateErrorResponse(APIERR_BADPARS,"Height must be positive; elevations must be finite.");
    Int32 reference=0; parameters.Get("referenceEdgeIndex",reference);
    const Int32 count=static_cast<Int32>(outline.GetSize());
    if (reference<0 || reference>=count) return CreateErrorResponse(APIERR_BADPARS,"Reference edge is outside the footprint.");
    const auto* material=parameters.Get("buildingMaterialId");
    if (material==nullptr) return CreateErrorResponse(APIERR_BADPARS,"Building material is required.");
    const auto materialIndex=GetAttributeIndexFromGuid(API_BuildingMaterialID,GetGuidFromObjectState(*material));
    if (materialIndex==APIInvalidAttributeIndex) return CreateErrorResponse(APIERR_BADPARS,"Building material was not found in this project.");
    if (const auto* layer=parameters.Get("layerId")) {
        const auto layerIndex=GetAttributeIndexFromGuid(API_LayerID,GetGuidFromObjectState(*layer));
        if (layerIndex==APIInvalidAttributeIndex) return CreateErrorResponse(APIERR_BADPARS,"Layer was not found in this project.");
        element.header.layer=layerIndex;
    }
    if (memo.coords!=nullptr || memo.pends!=nullptr || memo.parcs!=nullptr)
        return CreateErrorResponse(APIERR_BADPARS,"Unexpected polygon data in wall defaults; no wall was created.");
    memo.coords=reinterpret_cast<API_Coord**>(BMAllocateHandle((count+2)*sizeof(API_Coord),ALLOCATE_CLEAR,0));
    memo.pends=reinterpret_cast<Int32**>(BMAllocateHandle(2*sizeof(Int32),ALLOCATE_CLEAR,0));
    memo.parcs=reinterpret_cast<API_PolyArc**>(BMAllocateHandle(0,ALLOCATE_CLEAR,0));
    if (memo.coords==nullptr || memo.pends==nullptr || memo.parcs==nullptr) return CreateErrorResponse(APIERR_MEMFULL,"Cannot allocate polygon wall footprint.");
    for (Int32 i=0;i<count;++i) {
        const auto point=Get2DCoordinateFromObjectState(outline[i]);
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) return CreateErrorResponse(APIERR_BADPARS,"Footprint coordinates must be finite.");
        (*memo.coords)[i+1]=point;
    }
    (*memo.coords)[count+1]=(*memo.coords)[1]; (*memo.pends)[1]=count+1;
    double twiceArea=0;
    for (Int32 i=1;i<=count;++i) {
        const auto& a=(*memo.coords)[i]; const auto& b=(*memo.coords)[i+1];
        if (std::hypot(a.x-b.x,a.y-b.y)<1e-8) return CreateErrorResponse(APIERR_BADPARS,"Footprint has a zero-length edge or closing duplicate.");
        twiceArea+=a.x*b.y-b.x*a.y;
    }
    if (std::abs(twiceArea)<1e-10) return CreateErrorResponse(APIERR_BADPOLY,"Footprint has no area.");
    const auto floor=ResolveFloorIndexAndOffset(parameters,"floorIndex",z,stories);
    element.header.floorInd=floor.first; element.wall.bottomOffset=floor.second;
    element.wall.height=height; element.wall.relativeTopStory=0;
    element.wall.type=APIWtyp_Poly; element.wall.modelElemStructureType=API_BasicStructure;
    element.wall.buildingMaterial=materialIndex; element.wall.composite=APIInvalidAttributeIndex; element.wall.profileAttr=APIInvalidAttributeIndex;
    element.wall.profileType=APISect_Normal; element.wall.referenceLineLocation=APIWallRefLine_Outside;
    element.wall.angle=0; element.wall.offset=0; element.wall.flipped=false;
    element.wall.polyCanChange=false; parameters.Get("cornersCanChange",element.wall.polyCanChange);
    element.wall.poly.nCoords=count+1; element.wall.poly.nSubPolys=1; element.wall.poly.nArcs=0;
    element.wall.rLinInd=reference+1; element.wall.rLinEndInd=reference+1;
    element.wall.refInd=reference+1; element.wall.refEndInd=reference+1;
    element.wall.oppInd=0; element.wall.oppEndInd=0;
    element.wall.begC=(*memo.coords)[reference+1]; element.wall.endC=(*memo.coords)[reference+2];
    return {};
}

GS::Optional<GS::UniString> GetPolygonalWallGeometryCommand::GetInputParametersSchema () const
{
    return R"({"type":"object","properties":{"elementId":{"$ref":"#/ElementId"}},"required":["elementId"],"additionalProperties":false})";
}
GS::Optional<GS::UniString> GetPolygonalWallGeometryCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{
        "elementId":{"$ref":"#/ElementId"},"modificationStamp":{"type":"string","pattern":"^[0-9]+$"},
        "polygonOutline":{"type":"array","items":{"$ref":"#/Coordinate2D"}},"polygonArcs":{"type":"array","items":{"type":"object"}},
        "referenceEdgeIndex":{"type":"integer"},"cornersCanChange":{"type":"boolean"},
        "units":{"const":"metres"},"coordinateSystem":{"const":"ProjectXY"}
    },"required":["elementId","modificationStamp","polygonOutline","polygonArcs","referenceEdgeIndex","cornersCanChange","units","coordinateSystem"],"additionalProperties":false})";
}
GS::ObjectState GetPolygonalWallGeometryCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl&) const
{
    API_Element element = {};
    element.header.guid = GetGuidFromArrayItem ("elementId", parameters);
    GSErrCode err = ACAPI_Element_Get (&element);
    if (err != NoError) return CreateErrorResponse (err, "Cannot read polygonal wall.");
    if (GetElemTypeId (element.header) != API_WallID || element.wall.type != APIWtyp_Poly)
        return CreateErrorResponse (APIERR_BADPARS, "The requested element is not a polygonal wall.");
    API_ElementMemo memo = {};
    const GS::OnExit dispose ([&] () { ACAPI_DisposeElemMemoHdls (&memo); });
    err = ACAPI_Element_GetMemo (element.header.guid, &memo, APIMemoMask_Polygon);
    if (err != NoError) return CreateErrorResponse (err, "Cannot read wall polygon memo.");
    if (element.wall.poly.nSubPolys != 1 || element.wall.poly.nCoords < 4 || element.wall.poly.nCoords > 10001 || memo.coords == nullptr)
        return CreateErrorResponse (APIERR_BADPOLY, "Unsupported or incomplete native wall polygon.");
    if (BMhGetSize (reinterpret_cast<GSHandle> (memo.coords)) < static_cast<GSSize> ((element.wall.poly.nCoords + 1) * sizeof (API_Coord)) ||
        element.wall.poly.nArcs < 0 || element.wall.poly.nArcs > element.wall.poly.nCoords ||
        (element.wall.poly.nArcs > 0 && (memo.parcs == nullptr ||
         BMhGetSize (reinterpret_cast<GSHandle> (memo.parcs)) < static_cast<GSSize> (element.wall.poly.nArcs * sizeof (API_PolyArc)))))
        return CreateErrorResponse (APIERR_BADPOLY, "Native polygon counts exceed the available geometry memo.");
    GS::ObjectState result = CreateElementIdObjectState (element.header.guid);
    result.Add ("modificationStamp", GS::UniString (std::to_string (element.header.modiStamp).c_str ()));
    result.Add ("units", "metres"); result.Add ("coordinateSystem", "ProjectXY");
    result.Add ("referenceEdgeIndex", element.wall.rLinInd - 1);
    result.Add ("cornersCanChange", element.wall.polyCanChange);
    const auto& add = result.AddList<GS::ObjectState> ("polygonOutline");
    for (Int32 i=1; i<element.wall.poly.nCoords; ++i) add (Create2DCoordinateObjectState ((*memo.coords)[i]));
    const auto& addArc = result.AddList<GS::ObjectState> ("polygonArcs");
    if (element.wall.poly.nArcs > 0 && memo.parcs == nullptr) return CreateErrorResponse (APIERR_BADPOLY, "Missing native wall arcs.");
    for (Int32 i=0; i<element.wall.poly.nArcs; ++i) addArc (CreatePolyArcObjectState ((*memo.parcs)[i]));
    return result;
}
GS::Optional<GS::UniString> ModifyPolygonalWallGeometryCommand::GetInputParametersSchema () const
{
    return R"({"type":"object","properties":{
        "elementId":{"$ref":"#/ElementId"},"expectedModificationStamp":{"type":"string","pattern":"^[0-9]+$"},
        "polygonOutline":{"type":"array","minItems":3,"maxItems":1000,"items":{"$ref":"#/Coordinate2D"},"description":"New straight-edged footprint in project XY metres, without closing duplicate; no holes."},
        "referenceEdgeIndex":{"type":"integer","minimum":0},"cornersCanChange":{"type":"boolean"}
    },"required":["elementId","expectedModificationStamp","polygonOutline","referenceEdgeIndex"],"additionalProperties":false})";
}
GS::Optional<GS::UniString> ModifyPolygonalWallGeometryCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{
        "elementId":{"$ref":"#/ElementId"},"status":{"const":"executionAccepted"},
        "verification":{"const":"nativeGeometryReadBack"},"geometry":{"type":"object"}
    },"required":["elementId","status","verification","geometry"],"additionalProperties":false})";
}
GS::ObjectState ModifyPolygonalWallGeometryCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& processControl) const
{
    API_Element element = {};
    element.header.guid = GetGuidFromArrayItem ("elementId", parameters);
    GSErrCode err = ACAPI_Element_Get (&element);
    if (err != NoError) return CreateErrorResponse (err, "Cannot read target wall.");
    if (GetElemTypeId (element.header) != API_WallID || element.wall.type != APIWtyp_Poly || element.wall.modelElemStructureType != API_BasicStructure)
        return CreateErrorResponse (APIERR_BADPARS, "Only Basic polygonal walls support this footprint edit.");
    GS::UniString expected;
    if (!parameters.Get ("expectedModificationStamp", expected) || expected != GS::UniString (std::to_string (element.header.modiStamp).c_str ()))
        return CreateErrorResponse (APIERR_BADPARS, "Wall changed since inspection; read its geometry again.");
    if (!ACAPI_Element_Filter (element.header.guid, APIFilt_IsEditable | APIFilt_InMyWorkspace | APIFilt_HasAccessRight))
        return CreateErrorResponse (APIERR_BADPARS, "Wall is not editable in the current project context.");
    GS::Array<GS::ObjectState> outline; Int32 reference = -1;
    if (!parameters.Get ("polygonOutline", outline) || !parameters.Get ("referenceEdgeIndex", reference))
        return CreateErrorResponse (APIERR_BADPARS, "Footprint and reference edge are required.");
    GS::ObjectState createParameters ("height", element.wall.height, "referenceEdgeIndex", reference,
        "buildingMaterialId", CreateGuidObjectState (GetAttributeGuidFromIndex (API_BuildingMaterialID, element.wall.buildingMaterial)),
        "cornersCanChange", element.wall.polyCanChange);
    const auto& add = createParameters.AddList<GS::ObjectState> ("polygonOutline");
    for (const auto& point : outline) add (point);
    API_Element candidate = element;
    API_ElementMemo memo = {};
    const GS::OnExit dispose ([&] () { ACAPI_DisposeElemMemoHdls (&memo); });
    const auto failure = CreatePolygonalWallsCommand ().SetTypeSpecificParameters (candidate, memo, GetStories (), createParameters);
    if (failure.HasValue ()) return failure.Get ();
    candidate.wall.polyCanChange = element.wall.polyCanChange;
    parameters.Get ("cornersCanChange", candidate.wall.polyCanChange);
    // Only geometry fields are masked: material, height, story, classification and GUID remain native.
    API_Element mask = {};
    ACAPI_ELEMENT_MASK_CLEAR (mask);
    ACAPI_ELEMENT_MASK_SET (mask, API_WallType, poly);
    ACAPI_ELEMENT_MASK_SET (mask, API_WallType, rLinInd);
    ACAPI_ELEMENT_MASK_SET (mask, API_WallType, rLinEndInd);
    ACAPI_ELEMENT_MASK_SET (mask, API_WallType, refInd);
    ACAPI_ELEMENT_MASK_SET (mask, API_WallType, refEndInd);
    ACAPI_ELEMENT_MASK_SET (mask, API_WallType, oppInd);
    ACAPI_ELEMENT_MASK_SET (mask, API_WallType, oppEndInd);
    ACAPI_ELEMENT_MASK_SET (mask, API_WallType, begC);
    ACAPI_ELEMENT_MASK_SET (mask, API_WallType, endC);
    ACAPI_ELEMENT_MASK_SET (mask, API_WallType, polyCanChange);
    err = ACAPI_CallUndoableCommand ("Modify polygonal wall footprint", [&] () -> GSErrCode {
        return ACAPI_Element_Change (&candidate, &mask, &memo, APIMemoMask_Polygon, true);
    });
    if (err != NoError) return CreateErrorResponse (err, "Native polygonal wall edit failed; committed changes are not confirmed.");
    GS::ObjectState geometry = GetPolygonalWallGeometryCommand ().Execute (parameters, processControl);
    if (geometry.Contains ("error")) return CreateErrorResponse (APIERR_GENERAL, "Wall edit was accepted but geometry readback failed; inspect before retrying.");
    GS::ObjectState result = CreateElementIdObjectState (element.header.guid);
    result.Add ("status", "executionAccepted"); result.Add ("verification", "nativeGeometryReadBack");
    result.Add ("geometry", geometry);
    return result;
}

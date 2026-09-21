#include "WallReferenceCommands.hpp"
#include "WallPlacementGeometry.hpp"
#include "MigrationHelper.hpp"
#include <string>
#include <algorithm>
#include <vector>

namespace {
GS::Optional<GS::UniString> LoadStraightWall (const API_Guid& guid, API_Element& wall,
                                             WallPlacementGeometry::Frame& frame)
{
    wall = {};
    wall.header.guid = guid;
    if (ACAPI_Element_Get (&wall) != NoError)
        return "Cannot load the requested wall.";
    if (GetElemTypeId (wall.header) != API_WallID)
        return "The element is not a wall.";
    if (wall.wall.type != APIWtyp_Normal || std::abs (wall.wall.angle) > 1e-9)
        return "Reference-line placement currently supports straight non-polygonal, non-trapezoid walls only.";
    const char* error = WallPlacementGeometry::MakeFrame (
        {wall.wall.begC.x, wall.wall.begC.y}, {wall.wall.endC.x, wall.wall.endC.y}, frame);
    if (error != nullptr)
        return GS::UniString (error);
    return {};
}

GS::ObjectState PointState (WallPlacementGeometry::Point point)
{
    return GS::ObjectState ("x", point.x, "y", point.y);
}
// 19 September 2026, 19:42 CEST. SDK wall-reference documentation defines the
// outside as left of an unflipped start-to-end wall. These are untrimmed lines,
// not intersection/SEO/opening-cut faces or native dimension references.
GS::ObjectState WallLayerGeometry (const API_Element& element,const WallPlacementGeometry::Frame& frame)
{
    const auto& wall=element.wall;
    if (wall.profileType!=APISect_Normal || (wall.modelElemStructureType!=API_BasicStructure && wall.modelElemStructureType!=API_CompositeStructure))
        return CreateErrorResponse(APIERR_BADPARS,"Layer lines require a vertical basic or composite wall; profiled and slanted faces require body geometry.");
    if (!std::isfinite(wall.thickness) || wall.thickness<=0 || !std::isfinite(wall.offsetFromOutside)) return CreateErrorResponse(APIERR_BADPARS,"Wall thickness or reference offset is invalid.");
    const double direction=wall.flipped ? -1.0 : 1.0;
    const auto line=[&] (double depth) {
        const double offset=direction*(wall.offsetFromOutside-depth);
        return GS::ObjectState("start",PointState({frame.start.x+offset*frame.leftNormal.x,frame.start.y+offset*frame.leftNormal.y}),
            "end",PointState({frame.end.x+offset*frame.leftNormal.x,frame.end.y+offset*frame.leftNormal.y}),"depthFromOutside",depth);
    };
    GS::ObjectState result("geometryBasis","UntrimmedVerticalWallCrossSection","includesJunctionsOpeningsAndSolidOperations",false,
        "nativeDimensionReference",false,"outsideFace",line(0),"insideFace",line(wall.thickness));
    GS::Array<GS::ObjectState> layers;
    if (wall.modelElemStructureType==API_BasicStructure) {
        const auto material=GetAttributeGuidFromIndex(API_BuildingMaterialID,wall.buildingMaterial);
        if (material==APINULLGuid) return CreateErrorResponse(APIERR_BADID,"Cannot resolve the wall's building material.");
        layers.Push(GS::ObjectState("index",0,"classification","Basic","thickness",wall.thickness,"buildingMaterialId",CreateGuidObjectState(material),"outsideFace",line(0),"insideFace",line(wall.thickness)));
    } else {
        API_Attribute composite={}; composite.header.typeID=API_CompWallID; composite.header.index=wall.composite;
        auto error=ACAPI_Attribute_Get(&composite);
        if (error!=NoError) return CreateErrorResponse(error,"Cannot read the wall's composite.");
        API_AttributeDefExt definition={};
        const GS::OnExit dispose([&] { ACAPI_DisposeAttrDefsHdlsExt(&definition); });
        error=ACAPI_Attribute_GetDefExt(API_CompWallID,wall.composite,&definition);
        if (error!=NoError) return CreateErrorResponse(error,"Cannot read the composite layers.");
        const Int32 count=composite.compWall.nComps;
        if (count<1 || count>1000 || definition.cwall_compItems==nullptr ||
            BMhGetSize(reinterpret_cast<GSHandle>(definition.cwall_compItems))<static_cast<GSSize>(count*sizeof(API_CWallComponent)))
            return CreateErrorResponse(APIERR_BADPARS,"Composite layer data is incomplete or exceeds 1000 layers.");
        double depth=0,coreStart=0,coreEnd=0; bool coreSeen=false,coreEnded=false,multipleCoreRegions=false;
        for (Int32 i=0;i<count;++i) {
            const auto& layer=(*definition.cwall_compItems)[i];
            if (!std::isfinite(layer.fillThick) || layer.fillThick<=0) return CreateErrorResponse(APIERR_BADPARS,"Composite layer thickness must be finite and positive.");
            const auto material=GetAttributeGuidFromIndex(API_BuildingMaterialID,layer.buildingMaterial);
            if (material==APINULLGuid) return CreateErrorResponse(APIERR_BADID,"Cannot resolve a composite building material.");
            const bool core=(layer.flagBits&APICWallComp_Core)!=0,finish=(layer.flagBits&APICWallComp_Finish)!=0;
            if (core && finish) return CreateErrorResponse(APIERR_BADPARS,"Composite layer is marked both core and finish.");
            if (core) { if (!coreSeen) coreStart=depth; if (coreEnded) multipleCoreRegions=true; coreSeen=true; coreEnd=depth+layer.fillThick; }
            else if (coreSeen) coreEnded=true;
            layers.Push(GS::ObjectState("index",i,"classification",core ? "Core" : finish ? "Finish" : "Other",
                "thickness",layer.fillThick,"buildingMaterialId",CreateGuidObjectState(material),"outsideFace",line(depth),"insideFace",line(depth+layer.fillThick)));
            depth+=layer.fillThick;
        }
        if (!std::isfinite(depth) || std::abs(depth-wall.thickness)>1e-8) return CreateErrorResponse(APIERR_BADPARS,"Composite layer sum differs from the wall thickness; no face lines are inferred.");
        result.Add("compositeId",CreateGuidObjectState(composite.header.guid));
        result.Add("compositeModificationTime",GS::UniString(std::to_string(composite.header.modiTime).c_str()));
        result.Add("coreRegionsContiguous",!multipleCoreRegions);
        if (coreSeen && !multipleCoreRegions) { result.Add("coreOutsideFace",line(coreStart)); result.Add("coreInsideFace",line(coreEnd)); result.Add("coreThickness",coreEnd-coreStart); }
    }
    result.Add("layers",layers);
    return result;
}
void AddOpeningContext (const API_Guid& wallGuid,const WallPlacementGeometry::Frame& frame,Int32 offset,Int32 limit,GS::ObjectState& result)
{
    std::vector<API_Guid> ids;
    for (const auto type : {API_WindowID,API_DoorID}) {
        GS::Array<API_Guid> connected;
        const GSErrCode err=ACAPI_Grouping_GetConnectedElements (wallGuid,type,&connected);
        if (err!=NoError) { result.Add ("openingReadError",*CreateErrorResponse (err,"Cannot enumerate hosted doors/windows.").Get ("error")); return; }
        for (const auto& id:connected) ids.push_back (id);
    }
    std::sort (ids.begin (),ids.end (),[] (const API_Guid& a,const API_Guid& b) {
        return std::string (APIGuidToString (a).ToCStr ().Get ())<std::string (APIGuidToString (b).ToCStr ().Get ());
    });
    const size_t begin=std::min (static_cast<size_t> (offset),ids.size ());
    const size_t end=std::min (begin+static_cast<size_t> (limit),ids.size ());
    result.Add ("openingCount",static_cast<UInt32> (ids.size ()));
    result.Add ("nextOpeningOffset",static_cast<UInt32> (end));
    result.Add ("hasMoreOpenings",end<ids.size ());
    const auto& add=result.AddList<GS::ObjectState> ("openings");
    for (size_t i=begin;i<end;++i) {
        API_Element e={}; e.header.guid=ids[i];
        GS::ObjectState row=CreateElementIdObjectState (ids[i]);
        GSErrCode err=ACAPI_Element_Get (&e);
        if (err==NoError && ((GetElemTypeId (e.header)!=API_WindowID && GetElemTypeId (e.header)!=API_DoorID) || e.window.owner!=wallGuid)) err=APIERR_BADID;
        if (err==NoError && (!std::isfinite (e.window.objLoc) || !std::isfinite (e.window.openingBase.width) || !std::isfinite(e.window.openingBase.height) || e.window.openingBase.width<=0 || e.window.openingBase.height<=0)) err=APIERR_BADPARS;
        if (err!=NoError) { row.Add ("error",*CreateErrorResponse (err,"Cannot read a hosted opening's nominal placement.").Get ("error")); add (row); continue; }
        const auto& w=e.window;
        const double start=w.objLoc-w.openingBase.width/2,endStation=w.objLoc+w.openingBase.width/2;
        row.Add ("elementType",GetElemTypeId (e.header)==API_WindowID ? "Window" : "Door");
        row.Add ("modificationStamp",GS::UniString (std::to_string (e.header.modiStamp).c_str ()));
        row.Add ("width",w.openingBase.width); row.Add ("height",w.openingBase.height);
        row.Add ("centerOffset",w.objLoc);
        row.Add ("distanceFromWallStartToNearJamb",start);
        row.Add ("distanceFromWallEndToNearJamb",frame.length-endStation);
        row.Add ("nominalCenter",PointState (WallPlacementGeometry::AtStation (frame,w.objLoc)));
        row.Add ("nominalStartJamb",PointState (WallPlacementGeometry::AtStation (frame,start)));
        row.Add ("nominalEndJamb",PointState (WallPlacementGeometry::AtStation (frame,endStation)));
        row.Add ("withinReferenceLineEnds",start>=-1e-9 && endStation<=frame.length+1e-9);
        row.Add ("reflected",w.openingBase.reflected); row.Add ("refSide",w.openingBase.refSide); row.Add ("oSide",w.openingBase.oSide);
        row.Add ("libraryPartIndex",w.openingBase.libInd);
        row.Add ("geometryBasis","NominalWidthProjectedOnWallReferenceLine;NotClearPassageOrTrim");
        add (row);
    }
}
}

GS::Optional<GS::UniString> ResolveWallOpeningPlacement (
    const GS::ObjectState& data, const API_Guid& wallGuid, double width, double& centerOffset)
{
    const GS::ObjectState* placement = data.Get ("placement");
    if (placement == nullptr)
        return {};
    if (data.Contains ("centerOffset"))
        return "Use either placement or centerOffset, not both.";
    GS::UniString from, anchor;
    double distance = 0.0;
    if (!placement->Get ("from", from) || !placement->Get ("anchor", anchor) ||
        !placement->Get ("distance", distance))
        return "placement requires from, anchor and distance.";
    if ((from != "Start" && from != "End") || (anchor != "Center" && anchor != "NearestJamb"))
        return "placement.from must be Start/End; placement.anchor must be Center/NearestJamb.";
    API_Element wall = {};
    WallPlacementGeometry::Frame frame = {};
    auto error = LoadStraightWall (wallGuid, wall, frame);
    if (error.HasValue ())
        return error;
    const char* geometryError = WallPlacementGeometry::ResolveOpening (
        frame.length, width, distance, from == "End", anchor == "NearestJamb", centerOffset);
    if (geometryError != nullptr)
        return GS::UniString (geometryError);
    return {};
}

GS::String GetWallReferenceGeometryCommand::GetName () const
{
    return "GetWallReferenceGeometry";
}

GS::Optional<GS::UniString> GetWallReferenceGeometryCommand::GetInputParametersSchema () const
{
    return R"({
        "type":"object","properties":{
            "elements":{"type":"array","minItems":1,"maxItems":100,"items":{"type":"object","properties":{"elementId":{"$ref":"#/ElementId"}},"required":["elementId"],"additionalProperties":false}},
            "includeOpenings":{"type":"boolean","description":"Default false. Include hosted doors/windows and nominal jamb stations on each straight wall's reference line; these are not clear-passage dimensions."},
            "includeLayerGeometry":{"type":"boolean","default":false,"description":"For at most 20 walls per request, read vertical basic/composite wall layer lines, including contiguous composite core boundaries, in ProjectXY metres. These are untrimmed cross-sections; they exclude junctions, openings and solid operations and are not native associative dimension references."},
            "openingOffset":{"type":"integer","minimum":0,"description":"Per-wall sorted GUID page offset, default zero."},
            "openingLimit":{"type":"integer","minimum":1,"maximum":100,"description":"Per-wall opening page size, default 20."}
        },"required":["elements"],"additionalProperties":false
    })";
}

GS::Optional<GS::UniString> GetWallReferenceGeometryCommand::GetRawResponseSchema () const
{
    return R"({
        "type":"object","properties":{
            "walls":{"type":"array","items":{"type":"object","properties":{
                "elementId":{"$ref":"#/ElementId"},
                "layerGeometry":{"type":"object"},
                "openings":{"type":"array","items":{"type":"object"}},"openingCount":{"type":"integer"},"nextOpeningOffset":{"type":"integer"},"hasMoreOpenings":{"type":"boolean"},"openingReadError":{"$ref":"#/Error"},
                "geometry":{"type":"object","properties":{
                    "start":{"$ref":"#/Coordinate2D"},"end":{"$ref":"#/Coordinate2D"},
                    "tangent":{"$ref":"#/Coordinate2D"},"leftNormal":{"$ref":"#/Coordinate2D"},
                    "length":{"type":"number"},"thickness":{"type":"number"},
                    "height":{"type":"number"},"flipped":{"type":"boolean"},
                    "referenceLineLocation":{"type":"string"},"offset":{"type":"number"},
                    "offsetFromOutside":{"type":"number","description":"Native output-only distance from the reference line to the outside face, in metres. This is not a trimmed face polygon."},
                    "profileType":{"type":"string"},"modificationStamp":{"type":"string"},
                    "source":{"const":"API_WallType and derived reference frame"}
                },"required":["start","end","tangent","leftNormal","length","thickness","height","flipped"],"additionalProperties":false},
                "error":{"type":"object","properties":{"code":{"type":"integer"},"message":{"type":"string"}},"required":["code","message"],"additionalProperties":false}
            },"required":["elementId"],"additionalProperties":false}},
            "units":{"type":"string"},"coordinateSystem":{"type":"string"}
        },"required":["walls","units","coordinateSystem"],"additionalProperties":false
    })";
}

GS::ObjectState GetWallReferenceGeometryCommand::Execute (
    const GS::ObjectState& parameters, GS::ProcessControl&) const
{
    GS::Array<GS::ObjectState> elements;
    if (!parameters.Get ("elements", elements) || elements.IsEmpty () || elements.GetSize () > 100)
        return CreateErrorResponse (APIERR_BADPARS, "Supply 1 to 100 walls.");
    bool openings=false,layers=false; Int32 offset=0,limit=20;
    parameters.Get("includeLayerGeometry",layers);
    if (layers && elements.GetSize()>20) return CreateErrorResponse(APIERR_BADPARS,"Inspect layer geometry for at most 20 walls per request.");
    parameters.Get ("includeOpenings",openings); parameters.Get ("openingOffset",offset); parameters.Get ("openingLimit",limit);
    if (offset<0 || limit<1 || limit>100) return CreateErrorResponse (APIERR_BADPARS,"Invalid opening page.");
    GS::ObjectState response ("units", "metres", "coordinateSystem", "ProjectXY");
    const auto& results = response.AddList<GS::ObjectState> ("walls");
    for (const auto& item : elements) {
        const auto* id = item.Get ("elementId");
        if (id == nullptr)
            return CreateErrorResponse (APIERR_BADPARS, "Each item requires elementId.");
        const API_Guid guid = GetGuidFromObjectState (*id);
        GS::ObjectState result = CreateElementIdObjectState (guid);
        API_Element wall = {};
        WallPlacementGeometry::Frame frame = {};
        auto error = LoadStraightWall (guid, wall, frame);
        if (error.HasValue ()) {
            result.Add ("error", GS::ObjectState ("code", static_cast<Int32> (APIERR_BADPARS), "message", error.Get ()));
        } else {
            GS::ObjectState geometry (
                "start", PointState (frame.start), "end", PointState (frame.end),
                "tangent", PointState (frame.tangent), "leftNormal", PointState (frame.leftNormal),
                "length", frame.length, "thickness", wall.wall.thickness,
                "height", wall.wall.height, "flipped", wall.wall.flipped);
            geometry.Add ("referenceLineLocation", WallReferenceLineLocationToString (wall.wall.referenceLineLocation));
            geometry.Add ("offset", wall.wall.offset);
            geometry.Add ("offsetFromOutside", wall.wall.offsetFromOutside);
            geometry.Add ("profileType", wall.wall.profileType == APISect_Normal ? "Normal" : wall.wall.profileType == APISect_Slanted ? "Slanted" : wall.wall.profileType == APISect_Trapez ? "Trapez" : "Profile");
            geometry.Add ("modificationStamp", GS::UniString (std::to_string (wall.header.modiStamp).c_str ()));
            geometry.Add ("source", "API_WallType and derived reference frame");
            result.Add ("geometry", geometry);
            if (openings) AddOpeningContext (guid,frame,offset,limit,result);
            if (layers) result.Add("layerGeometry",WallLayerGeometry(wall,frame));
        }
        results (result);
    }
    return response;
}


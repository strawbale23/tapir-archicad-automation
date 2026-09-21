#include "SlabTopologyCommands.hpp"
#include "MigrationHelper.hpp"
#include "GSProcessControl.hpp"
#include <cmath>
#include <vector>
#include <string>
#include <utility>
#include <algorithm>
#include <tuple>

namespace {
struct SlabArcShape { API_Coord begin,end; double angle; };
bool ReadArcShapes (const API_ElementMemo& memo,Int32 count,std::vector<SlabArcShape>& arcs) {
    arcs.clear ();
    if (count==0) return true;
    if (count<0 || count>10000 || memo.coords==nullptr || memo.parcs==nullptr ||
        BMhGetSize (reinterpret_cast<GSHandle> (memo.parcs))<static_cast<GSSize> (count*sizeof(API_PolyArc))) return false;
    const GSSize coordinates=BMhGetSize (reinterpret_cast<GSHandle> (memo.coords))/sizeof(API_Coord);
    for (Int32 i=0;i<count;++i) {
        const auto& arc=(*memo.parcs)[i];
        if (arc.begIndex<1 || arc.endIndex<1 || arc.begIndex>=coordinates || arc.endIndex>=coordinates || !std::isfinite (arc.arcAngle)) return false;
        const auto a=(*memo.coords)[arc.begIndex],b=(*memo.coords)[arc.endIndex];
        if (!std::isfinite (a.x) || !std::isfinite (a.y) || !std::isfinite (b.x) || !std::isfinite (b.y)) return false;
        arcs.push_back ({a,b,arc.arcAngle});
    }
    std::sort (arcs.begin (),arcs.end (),[] (const SlabArcShape& a,const SlabArcShape& b) {
        return std::tie (a.begin.x,a.begin.y,a.end.x,a.end.y,a.angle)<std::tie (b.begin.x,b.begin.y,b.end.x,b.end.y,b.angle);
    });
    return true;
}
bool RetainsArcs (const API_ElementMemo& memo,Int32 count,const std::vector<SlabArcShape>& expected) {
    std::vector<SlabArcShape> actual;
    if (!ReadArcShapes (memo,count,actual) || actual.size ()!=expected.size ()) return false;
    for (size_t i=0;i<actual.size ();++i) {
        const auto& a=actual[i]; const auto& b=expected[i];
        if (std::hypot (a.begin.x-b.begin.x,a.begin.y-b.begin.y)>1e-8 ||
            std::hypot (a.end.x-b.end.x,a.end.y-b.end.y)>1e-8 || std::abs (a.angle-b.angle)>1e-8) return false;
    }
    return true;
}
}

GS::Optional<GS::UniString> EditSlabTopologyCommand::GetInputParametersSchema () const
{
    return R"({"type":"object","properties":{
        "elementId":{"$ref":"#/ElementId"},
        "operation":{"type":"string","enum":["InsertVertex","SplitEdge","MergeEdges","DeleteVertex","AddHole","DeleteHole"]},
        "expectedModificationStamp":{"type":"string","description":"Observed slab revision; mandatory for SplitEdge and MergeEdges because endpoint coordinates alone do not identify the curve."},
        "fraction":{"type":"number","exclusiveMinimum":0,"exclusiveMaximum":1,"description":"SplitEdge only: fraction of the straight length or signed circular sweep, measured from vertexIndex toward its successor. The point is calculated on the existing edge; both resulting edges inherit its trim and surface."},
        "contourIndex":{"type":"integer","minimum":0},
        "expectedContourCount":{"type":"integer","minimum":1},
        "expectedContour":{"type":"array","minItems":3,"maxItems":10000,"items":{"$ref":"#/Coordinate2D"},"description":"Current unique contour vertices in native order; omit closing duplicate. For AddHole select contour 0."},
        "vertexIndex":{"type":"integer","minimum":0,"description":"Zero-based vertex to delete or insert/split after. MergeEdges removes this shared vertex only for collinear straight edges or cocircular arcs with matching trim/surface settings; the boundary is retained."},
        "coordinate":{"$ref":"#/Coordinate2D"},
        "hole":{"type":"array","minItems":3,"maxItems":1000,"items":{"$ref":"#/Coordinate2D"},"description":"New straight-edged hole; unique vertices, no closing duplicate."}
    },"required":["elementId","operation","contourIndex","expectedContourCount","expectedContour"],"additionalProperties":false,
    "oneOf":[
        {"properties":{"operation":{"const":"InsertVertex"}},"required":["vertexIndex","coordinate"],"not":{"required":["hole"]}},
        {"properties":{"operation":{"const":"SplitEdge"}},"required":["vertexIndex","fraction","expectedModificationStamp"],"not":{"anyOf":[{"required":["coordinate"]},{"required":["hole"]}]}},
        {"properties":{"operation":{"const":"MergeEdges"}},"required":["vertexIndex","expectedModificationStamp"],"not":{"anyOf":[{"required":["coordinate"]},{"required":["hole"]},{"required":["fraction"]}]}},
        {"properties":{"operation":{"const":"DeleteVertex"}},"required":["vertexIndex"],"not":{"anyOf":[{"required":["coordinate"]},{"required":["hole"]}]}},
        {"properties":{"operation":{"const":"AddHole"},"contourIndex":{"const":0}},"required":["hole"],"not":{"anyOf":[{"required":["vertexIndex"]},{"required":["coordinate"]}]}},
        {"properties":{"operation":{"const":"DeleteHole"},"contourIndex":{"minimum":1}},"not":{"anyOf":[{"required":["vertexIndex"]},{"required":["coordinate"]},{"required":["hole"]}]}}
    ]})";
}
GS::Optional<GS::UniString> EditSlabTopologyCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{"elementId":{"$ref":"#/ElementId"},"success":{"type":"boolean"},"status":{"type":"string"},"nativeCoordinateCount":{"type":"integer"},"nativeContourCount":{"type":"integer"}},"required":["elementId","success","status","nativeCoordinateCount","nativeContourCount"],"additionalProperties":false})";
}

static GSErrCode LoadSlab (const API_Guid& guid, API_Element& element, API_ElementMemo& memo)
{
    element = {};
    element.header.guid = guid;
    GSErrCode err = ACAPI_Element_Get (&element);
    if (err != NoError) return err;
    if (GetElemTypeId (element.header) != API_SlabID) return APIERR_BADPARS;
    err = ACAPI_Element_GetMemo (guid, &memo, APIMemoMask_Polygon | APIMemoMask_SideMaterials | APIMemoMask_EdgeTrims);
    if (err!=NoError) return err;
    const auto& poly=element.slab.poly;
    if (poly.nCoords<4 || poly.nCoords>10000 || poly.nSubPolys<1 || poly.nSubPolys>poly.nCoords/4 || poly.nArcs<0 || poly.nArcs>poly.nCoords) return APIERR_BADPOLY;
    if (memo.coords==nullptr || memo.pends==nullptr ||
        BMhGetSize (reinterpret_cast<GSHandle> (memo.coords)) < static_cast<GSSize> ((poly.nCoords+1)*sizeof(API_Coord)) ||
        BMhGetSize (reinterpret_cast<GSHandle> (memo.pends)) < static_cast<GSSize> ((poly.nSubPolys+1)*sizeof(Int32))) return APIERR_BADPOLY;
    if (memo.vertexIDs!=nullptr && BMhGetSize (reinterpret_cast<GSHandle> (memo.vertexIDs)) < static_cast<GSSize> ((poly.nCoords+1)*sizeof(UInt32))) return APIERR_BADPOLY;
    if (memo.edgeTrims!=nullptr && BMhGetSize (reinterpret_cast<GSHandle> (memo.edgeTrims)) < static_cast<GSSize> ((poly.nCoords+1)*sizeof(API_EdgeTrim))) return APIERR_BADPOLY;
    if (memo.sideMaterials!=nullptr && BMGetPtrSize (reinterpret_cast<GSPtr> (memo.sideMaterials)) < static_cast<GSSize> ((poly.nCoords+1)*sizeof(API_OverriddenAttribute))) return APIERR_BADPOLY;
    if (poly.nArcs>0 && (memo.parcs==nullptr || BMhGetSize (reinterpret_cast<GSHandle> (memo.parcs)) < static_cast<GSSize> (poly.nArcs*sizeof(API_PolyArc)))) return APIERR_BADPOLY;
    Int32 previous=0;
    for (Int32 i=1;i<=poly.nSubPolys;++i) {
        const Int32 end=(*memo.pends)[i];
        if (end-previous<4 || end>poly.nCoords) return APIERR_BADPOLY;
        const auto a=(*memo.coords)[previous+1], b=(*memo.coords)[end];
        if (std::hypot (a.x-b.x,a.y-b.y)>1e-8) return APIERR_BADPOLY;
        previous=end;
    }
    if (previous!=poly.nCoords) return APIERR_BADPOLY;
    for (Int32 i=1;i<=poly.nCoords;++i) if (!std::isfinite ((*memo.coords)[i].x) || !std::isfinite ((*memo.coords)[i].y)) return APIERR_BADPOLY;
    return err;
}

// 19 September 2026, 16:00 CEST. Edit individual slab edges without rebuilding its polygon.
static bool SameEdgeSurface (const API_OverriddenAttribute& a,const API_OverriddenAttribute& b)
{
#ifdef ServerMainVers_2700
    return a.hasValue==b.hasValue && (!a.hasValue || a.value==b.value);
#else
    return a.overridden==b.overridden && (!a.overridden || a.attributeIndex==b.attributeIndex);
#endif
}
GS::Optional<GS::UniString> SetSlabEdgeSettingsCommand::GetInputParametersSchema () const
{
    return R"({"type":"object","properties":{
        "elementId":{"$ref":"#/ElementId"},"expectedModificationStamp":{"type":"string"},
        "edges":{"type":"array","minItems":1,"maxItems":1000,"items":{"type":"object","properties":{
            "contourIndex":{"type":"integer","minimum":0},"edgeIndex":{"type":"integer","minimum":0},
            "trim":{"type":"object","properties":{"type":{"enum":["Vertical","CustomAngle"]},"angle":{"type":"number","exclusiveMinimum":0,"exclusiveMaximum":3.141592653589793,"description":"Native side-trim angle in radians; required for CustomAngle, forbidden for Vertical."}},"required":["type"],"additionalProperties":false},
            "sideMaterial":{"$ref":"#/OverriddenMaterial"}
        },"required":["contourIndex","edgeIndex"],"anyOf":[{"required":["trim"]},{"required":["sideMaterial"]}],"additionalProperties":false}}
    },"required":["elementId","expectedModificationStamp","edges"],"additionalProperties":false})";
}
GS::Optional<GS::UniString> SetSlabEdgeSettingsCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{"elementId":{"$ref":"#/ElementId"},"success":{"type":"boolean"},"status":{"enum":["applied","alreadySatisfied"]},"edgeCount":{"type":"integer"}},"required":["elementId","success","status","edgeCount"],"additionalProperties":false})";
}
GS::ObjectState SetSlabEdgeSettingsCommand::Execute (const GS::ObjectState& p,GS::ProcessControl& control) const
{
    API_Element element={}; API_ElementMemo memo={};
    const GS::OnExit cleanup([&] { ACAPI_DisposeElemMemoHdls(&memo); });
    auto error=LoadSlab(GetGuidFromArrayItem("elementId",p),element,memo);
    if (error!=NoError) return CreateErrorResponse(error,"Cannot read slab edge settings.");
    GS::UniString stamp; p.Get("expectedModificationStamp",stamp);
    if (stamp!=GS::UniString(std::to_string(element.header.modiStamp).c_str())) return CreateErrorResponse(APIERR_BADPARS,"Slab changed; read its topology again.");
    if (!ACAPI_Element_Filter(element.header.guid,APIFilt_IsEditable|APIFilt_InMyWorkspace|APIFilt_HasAccessRight)) return CreateErrorResponse(APIERR_NOTEDITABLE,"Slab is not editable.");
    GS::Array<GS::ObjectState> edges;
    if (!p.Get("edges",edges) || edges.IsEmpty() || edges.GetSize()>1000) return CreateErrorResponse(APIERR_BADPARS,"Supply 1..1000 edge edits.");
    const Int32 coordinates=element.slab.poly.nCoords;
    std::vector<SlabArcShape> originalArcs;
    if (!ReadArcShapes(memo,element.slab.poly.nArcs,originalArcs)) return CreateErrorResponse(APIERR_BADPOLY,"Cannot read the original slab arcs.");
    if (memo.edgeTrims==nullptr) {
        memo.edgeTrims=reinterpret_cast<API_EdgeTrim**>(BMAllocateHandle((coordinates+1)*sizeof(API_EdgeTrim),ALLOCATE_CLEAR,0));
        if (memo.edgeTrims==nullptr) return CreateErrorResponse(APIERR_MEMFULL,"Cannot allocate edge trims.");
        for (Int32 i=0;i<=coordinates;++i) (*memo.edgeTrims)[i].sideType=APIEdgeTrim_Vertical;
    }
    if (memo.sideMaterials==nullptr) {
        memo.sideMaterials=reinterpret_cast<API_OverriddenAttribute*>(BMAllocatePtr((coordinates+1)*sizeof(API_OverriddenAttribute),ALLOCATE_CLEAR,0));
        if (memo.sideMaterials==nullptr) return CreateErrorResponse(APIERR_MEMFULL,"Cannot allocate edge surfaces.");
        for (Int32 i=0;i<=coordinates;++i) memo.sideMaterials[i]=element.slab.sideMat;
    }
    std::vector<bool> selected(static_cast<size_t>(coordinates+1),false);
    bool changed=false;
    for (const auto& input:edges) {
        Int32 contour=-1,edge=-1; input.Get("contourIndex",contour); input.Get("edgeIndex",edge);
        if (contour<0 || contour>=element.slab.poly.nSubPolys) return CreateErrorResponse(APIERR_BADPARS,"Invalid contour index.");
        const Int32 first=contour==0 ? 1 : (*memo.pends)[contour]+1,last=(*memo.pends)[contour+1];
        if (edge<0 || edge>=last-first) return CreateErrorResponse(APIERR_BADPARS,"Invalid edge index.");
        const Int32 index=first+edge;
        if (selected[index] || (!input.Contains("trim") && !input.Contains("sideMaterial"))) return CreateErrorResponse(APIERR_BADPARS,"Each edge must be unique and contain a setting.");
        selected[index]=true;
        auto trim=(*memo.edgeTrims)[index]; auto material=memo.sideMaterials[index];
        if (const auto* spec=input.Get("trim")) {
            GS::UniString type; spec->Get("type",type);
            if (type=="Vertical") { if (spec->Contains("angle")) return CreateErrorResponse(APIERR_BADPARS,"Vertical trim has no custom angle."); trim.sideType=APIEdgeTrim_Vertical; trim.sideAngle=0; }
            else if (type=="CustomAngle") {
                if (!spec->Get("angle",trim.sideAngle) || !std::isfinite(trim.sideAngle) || trim.sideAngle<=0 || trim.sideAngle>=3.141592653589793)
                    return CreateErrorResponse(APIERR_BADPARS,"Custom trim needs a finite angle strictly between zero and pi radians.");
                trim.sideType=APIEdgeTrim_CustomAngle;
            } else return CreateErrorResponse(APIERR_BADPARS,"Unsupported slab trim type.");
        }
        if (const auto* spec=input.Get("sideMaterial")) {
            bool overridden=false;
            if (!spec->Get("overridden",overridden)) return CreateErrorResponse(APIERR_BADPARS,"Surface override flag is missing.");
            if (overridden) {
                API_Attribute surface={}; surface.header.typeID=API_MaterialID; surface.header.guid=GetGuidFromAttributesArrayItem(*spec);
                if (surface.header.guid==APINULLGuid) return CreateErrorResponse(APIERR_BADPARS,"Surface identifier is missing or invalid.");
                error=ACAPI_Attribute_Get(&surface);
                if (error!=NoError) return CreateErrorResponse(error,"Cannot read the requested surface.");
#ifdef ServerMainVers_2700
                material.hasValue=true; material.value=surface.header.index;
#else
                material.overridden=true; material.attributeIndex=surface.header.index;
#endif
            } else {
                if (spec->Contains("attributeId")) return CreateErrorResponse(APIERR_BADPARS,"A disabled override cannot specify a surface.");
#ifdef ServerMainVers_2700
                material.hasValue=false;
#else
                material.overridden=false;
#endif
            }
        }
        const auto& previous=(*memo.edgeTrims)[index];
        if (previous.sideType!=trim.sideType || (trim.sideType==APIEdgeTrim_CustomAngle && std::abs(previous.sideAngle-trim.sideAngle)>1e-10) || !SameEdgeSurface(material,memo.sideMaterials[index])) changed=true;
        (*memo.edgeTrims)[index]=trim; memo.sideMaterials[index]=material;
        if (index==first) { (*memo.edgeTrims)[last]=trim; memo.sideMaterials[last]=material; }
    }
    auto result=CreateElementIdObjectState(element.header.guid); result.Add("edgeCount",edges.GetSize()); result.Add("success",true);
    if (!changed) { result.Add("status","alreadySatisfied"); return result; }
    if (control.TestBreak()) return CreateErrorResponse(APIERR_CANCEL,"Cancelled before editing slab edge settings.");
    error=ACAPI_CallUndoableCommand("Set Slab Edge Settings",[&] () -> GSErrCode { return ACAPI_Element_ChangeMemo(element.header.guid,APIMemoMask_EdgeTrims|APIMemoMask_SideMaterials,&memo); });
    if (error!=NoError) return CreateErrorResponse(error,"Slab edge settings transaction failed.");
    API_Element observed={}; API_ElementMemo actual={};
    const GS::OnExit dispose([&] { ACAPI_DisposeElemMemoHdls(&actual); });
    error=LoadSlab(element.header.guid,observed,actual);
    if (error!=NoError || observed.slab.poly.nCoords!=coordinates || observed.slab.poly.nSubPolys!=element.slab.poly.nSubPolys)
        return CreateErrorResponse(error==NoError ? APIERR_GENERAL : error,"Edge settings write returned but the slab shape could not be confirmed. Inspect before retrying.");
    if (!RetainsArcs(actual,observed.slab.poly.nArcs,originalArcs))
        return CreateErrorResponse(APIERR_GENERAL,"Edge settings returned but preservation of slab arcs is not confirmed. Inspect before retrying.");
    for (Int32 contour=1;contour<=element.slab.poly.nSubPolys;++contour)
        if ((*memo.pends)[contour]!=(*actual.pends)[contour]) return CreateErrorResponse(APIERR_GENERAL,"Edge settings returned but contour boundaries changed. Inspect before retrying.");
    for (Int32 i=1;i<=coordinates;++i) {
        if (memo.vertexIDs!=nullptr && (actual.vertexIDs==nullptr || (*memo.vertexIDs)[i]!=(*actual.vertexIDs)[i]))
            return CreateErrorResponse(APIERR_GENERAL,"Edge settings returned but native vertex identities changed. Inspect before retrying.");
        API_EdgeTrim trim={}; trim.sideType=APIEdgeTrim_Vertical;
        if (actual.edgeTrims!=nullptr) trim=(*actual.edgeTrims)[i];
        const auto material=actual.sideMaterials!=nullptr ? actual.sideMaterials[i] : observed.slab.sideMat;
        const auto& expected=(*memo.edgeTrims)[i];
        if (trim.sideType!=expected.sideType || (expected.sideType==APIEdgeTrim_CustomAngle && (!std::isfinite(trim.sideAngle) || std::abs(trim.sideAngle-expected.sideAngle)>1e-10)) ||
            !SameEdgeSurface(material,memo.sideMaterials[i]) || std::hypot((*actual.coords)[i].x-(*memo.coords)[i].x,(*actual.coords)[i].y-(*memo.coords)[i].y)>1e-8)
            return CreateErrorResponse(APIERR_GENERAL,"Edge settings returned but preservation/readback differs. Inspect before retrying.");
    }
    result.Add("status","applied"); return result;
}

GS::Optional<GS::UniString> SetSlabEdgeArcCommand::GetInputParametersSchema () const
{
    return R"({"type":"object","properties":{
        "elementId":{"$ref":"#/ElementId"},"expectedModificationStamp":{"type":"string","pattern":"^[0-9]+$"},
        "contourIndex":{"type":"integer","minimum":0},"edgeIndex":{"type":"integer","minimum":0},
        "arcAngle":{"type":"number","exclusiveMinimum":-6.283185307179586,"exclusiveMaximum":6.283185307179586,"description":"Signed native arc sweep in radians from this edge's vertex to the next in GetSlabTopology order. Positive is counterclockwise. Zero straightens the edge. Endpoints, holes and slab identity are retained. This does not split or merge vertices."}
    },"required":["elementId","expectedModificationStamp","contourIndex","edgeIndex","arcAngle"],"additionalProperties":false})";
}
GS::Optional<GS::UniString> SetSlabEdgeArcCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{"elementId":{"$ref":"#/ElementId"},"success":{"type":"boolean"},"status":{"enum":["applied","alreadySatisfied"]},"nativeArcCount":{"type":"integer"}},"required":["elementId","success","status","nativeArcCount"],"additionalProperties":false})";
}
GS::ObjectState SetSlabEdgeArcCommand::Execute (const GS::ObjectState& p,GS::ProcessControl& control) const
{
    API_Element element={}; API_ElementMemo memo={};
    const GS::OnExit cleanup([&] { ACAPI_DisposeElemMemoHdls(&memo); });
    auto err=LoadSlab(GetGuidFromArrayItem("elementId",p),element,memo);
    if (err!=NoError) return CreateErrorResponse(err,"Cannot read slab edge geometry.");
    GS::UniString stamp; p.Get("expectedModificationStamp",stamp);
    if (stamp!=GS::UniString(std::to_string(element.header.modiStamp).c_str())) return CreateErrorResponse(APIERR_BADPARS,"Slab changed; read its topology again.");
    if (!ACAPI_Element_Filter(element.header.guid,APIFilt_IsEditable | APIFilt_InMyWorkspace | APIFilt_HasAccessRight)) return CreateErrorResponse(APIERR_NOTEDITABLE,"Slab is not editable.");
    Int32 contour=-1,edge=-1; double angle=0;
    if (!p.Get("contourIndex",contour) || !p.Get("edgeIndex",edge) || !p.Get("arcAngle",angle) || contour<0 || contour>=element.slab.poly.nSubPolys ||
        !std::isfinite(angle) || std::abs(angle)>=6.283185307179586-1e-9) return CreateErrorResponse(APIERR_BADPARS,"Invalid contour or arc sweep; a complete circle is not a single slab edge.");
    const Int32 first=contour==0 ? 1 : (*memo.pends)[contour]+1;
    const Int32 last=(*memo.pends)[contour+1];
    if (edge<0 || edge>=last-first) return CreateErrorResponse(APIERR_BADPARS,"Edge is outside the selected contour.");
    const Int32 begin=first+edge,end=begin+1;
    const auto a=(*memo.coords)[begin],b=(*memo.coords)[end];
    if (std::hypot(a.x-b.x,a.y-b.y)<=1e-8) return CreateErrorResponse(APIERR_BADPOLY,"A zero-length edge cannot be curved.");
    const auto canonical=[&] (Int32 index) { return index==last ? first : index; };
    std::vector<API_PolyArc> arcs;
    std::vector<SlabArcShape> original;
    if (!ReadArcShapes(memo,element.slab.poly.nArcs,original)) return CreateErrorResponse(APIERR_BADPOLY,"Invalid native arc data.");
    Int32 matching=0; double currentAngle=0;
    for (Int32 i=0;i<element.slab.poly.nArcs;++i) {
        const auto& arc=(*memo.parcs)[i];
        const bool forward=canonical(arc.begIndex)==canonical(begin) && canonical(arc.endIndex)==canonical(end);
        const bool reverse=canonical(arc.begIndex)==canonical(end) && canonical(arc.endIndex)==canonical(begin);
        if (forward || reverse) { ++matching; currentAngle=forward ? arc.arcAngle : -arc.arcAngle; }
        else arcs.push_back(arc);
    }
    if (matching>1) return CreateErrorResponse(APIERR_BADPOLY,"Multiple arcs address the same edge.");
    if (std::abs(currentAngle-angle)<=1e-10) {
        auto result=CreateElementIdObjectState(element.header.guid);
        result.Add("success",true); result.Add("status","alreadySatisfied"); result.Add("nativeArcCount",element.slab.poly.nArcs); return result;
    }
    if (std::abs(angle)>1e-10) { API_PolyArc arc={}; arc.begIndex=begin; arc.endIndex=end; arc.arcAngle=angle; arcs.push_back(arc); }
    auto** replacement=reinterpret_cast<API_PolyArc**>(BMAllocateHandle(static_cast<GSSize>(arcs.size()*sizeof(API_PolyArc)),ALLOCATE_CLEAR,0));
    if (replacement==nullptr) return CreateErrorResponse(APIERR_MEMFULL,"Cannot allocate slab arc data.");
    for (size_t i=0;i<arcs.size();++i) (*replacement)[i]=arcs[i];
    BMKillHandle(reinterpret_cast<GSHandle*>(&memo.parcs)); memo.parcs=replacement;
    std::vector<SlabArcShape> expected;
    if (!ReadArcShapes(memo,static_cast<Int32>(arcs.size()),expected)) return CreateErrorResponse(APIERR_BADPOLY,"Cannot represent the requested slab arc.");
    if (control.TestBreak()) return CreateErrorResponse(APIERR_CANCEL,"Cancelled before slab mutation.");
    err=ACAPI_CallUndoableCommand("Set Slab Edge Arc",[&] () -> GSErrCode { return ACAPI_Element_ChangeMemo(element.header.guid,APIMemoMask_Polygon,&memo); });
    if (err!=NoError) return CreateErrorResponse(err,"Native slab arc edit failed.");
    API_Element observed={}; API_ElementMemo observedMemo={};
    const GS::OnExit disposeObserved([&] { ACAPI_DisposeElemMemoHdls(&observedMemo); });
    err=LoadSlab(element.header.guid,observed,observedMemo);
    if (err!=NoError || observed.slab.poly.nCoords!=element.slab.poly.nCoords || observed.slab.poly.nSubPolys!=element.slab.poly.nSubPolys ||
        !RetainsArcs(observedMemo,observed.slab.poly.nArcs,expected)) return CreateErrorResponse(err==NoError ? APIERR_GENERAL : err,"Arc edit returned, but the requested polygon was not confirmed. Inspect before retrying.");
    for (Int32 i=1;i<=element.slab.poly.nCoords;++i) {
        if (std::hypot((*memo.coords)[i].x-(*observedMemo.coords)[i].x,(*memo.coords)[i].y-(*observedMemo.coords)[i].y)>1e-8 ||
            (memo.vertexIDs!=nullptr && ((*memo.vertexIDs)[i]!=0) && (observedMemo.vertexIDs==nullptr || (*memo.vertexIDs)[i]!=(*observedMemo.vertexIDs)[i])))
            return CreateErrorResponse(APIERR_GENERAL,"Arc edit returned, but endpoint positions or native vertex identities changed. Inspect before retrying.");
    }
    auto result=CreateElementIdObjectState(element.header.guid);
    result.Add("success",true); result.Add("status","applied"); result.Add("nativeArcCount",observed.slab.poly.nArcs); return result;
}

GS::Optional<GS::UniString> OffsetSlabEdgeCommand::GetInputParametersSchema () const
{
    return R"({"type":"object","properties":{
        "elementId":{"$ref":"#/ElementId"},"expectedModificationStamp":{"type":"string","pattern":"^[0-9]+$"},
        "contourIndex":{"type":"integer","minimum":0},"edgeIndex":{"type":"integer","minimum":0},
        "distance":{"type":"number","description":"Metres perpendicular to the selected edge. Positive is left when looking from its vertex to the next vertex in GetSlabTopology order; negative is right. This does not mean automatically inward or outward."},
        "maximumEndpointMovement":{"type":"number","exclusiveMinimum":0,"description":"Metres. Explicit bound on how far either endpoint may move along its neighbouring edge."}
    },"required":["elementId","expectedModificationStamp","contourIndex","edgeIndex","distance","maximumEndpointMovement"],"additionalProperties":false})";
}
GS::Optional<GS::UniString> OffsetSlabEdgeCommand::GetRawResponseSchema () const
{
    return MoveSlabVerticesCommand ().GetRawResponseSchema ();
}
GS::ObjectState OffsetSlabEdgeCommand::Execute (const GS::ObjectState& p,GS::ProcessControl& control) const
{
    API_Element element={}; API_ElementMemo memo={};
    const GS::OnExit cleanup ([&] { ACAPI_DisposeElemMemoHdls (&memo); });
    const GSErrCode err=LoadSlab (GetGuidFromArrayItem ("elementId",p),element,memo);
    if (err!=NoError) return CreateErrorResponse (err,"Cannot read slab for edge offset.");
    GS::UniString stamp; p.Get ("expectedModificationStamp",stamp);
    if (stamp!=GS::UniString (std::to_string (element.header.modiStamp).c_str ())) return CreateErrorResponse (APIERR_BADPARS,"Slab changed; read its topology again.");
    if (!ACAPI_Element_Filter (element.header.guid,APIFilt_IsEditable | APIFilt_InMyWorkspace | APIFilt_HasAccessRight)) return CreateErrorResponse (APIERR_NOTEDITABLE,"Slab is not editable.");
    Int32 contour=-1,edge=-1; double distance=0,maximum=0;
    p.Get ("contourIndex",contour); p.Get ("edgeIndex",edge); p.Get ("distance",distance); p.Get ("maximumEndpointMovement",maximum);
    if (contour<0 || contour>=element.slab.poly.nSubPolys || !std::isfinite (distance) || !std::isfinite (maximum) || maximum<=0)
        return CreateErrorResponse (APIERR_BADPARS,"Invalid contour, distance or endpoint movement bound.");
    const Int32 first=contour==0 ? 1 : (*memo.pends)[contour]+1;
    const Int32 count=(*memo.pends)[contour+1]-first;
    if (edge<0 || edge>=count) return CreateErrorResponse (APIERR_BADPARS,"Edge index is outside the selected contour.");
    const Int32 next=(edge+1)%count;
    const API_Coord a=(*memo.coords)[first+edge],b=(*memo.coords)[first+next];
    std::vector<SlabArcShape> originalArcs;
    if (!ReadArcShapes (memo,element.slab.poly.nArcs,originalArcs)) return CreateErrorResponse (APIERR_BADPOLY,"Cannot read slab arc geometry.");
    const auto canonical=[&] (Int32 index) { return index==first+count ? first : index; };
    for (Int32 i=0;i<element.slab.poly.nArcs;++i) {
        const auto& arc=(*memo.parcs)[i];
        const Int32 begin=canonical (arc.begIndex),end=canonical (arc.endIndex);
        if (begin==first+edge || end==first+edge || begin==first+next || end==first+next)
            return CreateErrorResponse (APIERR_BADPARS,"Selected edge or its adjacent edges are curved. Offset requires three straight edges; other curves are retained.");
    }
    const API_Coord previous=(*memo.coords)[first+(edge+count-1)%count],following=(*memo.coords)[first+(edge+2)%count];
    const double dx=b.x-a.x,dy=b.y-a.y,length=std::hypot (dx,dy);
    if (!std::isfinite (length) || length<1e-8) return CreateErrorResponse (APIERR_BADPOLY,"Selected edge has no usable length.");
    const API_Coord origin={a.x-dy/length*distance,a.y+dx/length*distance};
    const auto intersection=[&] (const API_Coord& point,const API_Coord& other,API_Coord& result) {
        const double vx=other.x-point.x,vy=other.y-point.y,vlen=std::hypot (vx,vy);
        const double denominator=dx*vy-dy*vx;
        if (vlen<1e-8 || !std::isfinite (denominator) || std::abs (denominator)<=1e-10*length*vlen) return false;
        const double t=((point.x-origin.x)*vy-(point.y-origin.y)*vx)/denominator;
        result={origin.x+t*dx,origin.y+t*dy};
        return std::isfinite (result.x) && std::isfinite (result.y);
    };
    API_Coord movedA={},movedB={};
    if (!intersection (previous,a,movedA) || !intersection (b,following,movedB))
        return CreateErrorResponse (APIERR_BADPOLY,"Adjacent parallel or degenerate edges cannot form bounded offset corners.");
    if (std::hypot (movedA.x-a.x,movedA.y-a.y)>maximum || std::hypot (movedB.x-b.x,movedB.y-b.y)>maximum)
        return CreateErrorResponse (APIERR_BADPARS,"Offset exceeds the supplied endpoint movement bound.");
    if ((movedB.x-movedA.x)*dx+(movedB.y-movedA.y)*dy<=1e-12 ||
        (movedA.x-previous.x)*(a.x-previous.x)+(movedA.y-previous.y)*(a.y-previous.y)<=1e-12 ||
        (following.x-movedB.x)*(following.x-b.x)+(following.y-movedB.y)*(following.y-b.y)<=1e-12)
        return CreateErrorResponse (APIERR_BADPOLY,"Offset would collapse or reverse a neighbouring edge; no changes applied.");
    if (control.TestBreak ()) return CreateErrorResponse (APIERR_CANCEL,"Cancelled before moving the slab edge.");
    GS::ObjectState request=CreateElementIdObjectState (element.header.guid);
    const auto& add=request.AddList<GS::ObjectState> ("moves");
    for (const auto& target : {std::pair<Int32,API_Coord> (edge,movedA),{next,movedB}}) {
        GS::ObjectState move ("contourIndex",contour,"vertexIndex",target.first,
            "expectedCoordinate",Create2DCoordinateObjectState ((*memo.coords)[first+target.first]),
            "coordinate",Create2DCoordinateObjectState (target.second));
        if (memo.vertexIDs!=nullptr) move.Add ("expectedNativeVertexId",(*memo.vertexIDs)[first+target.first]);
        add (move);
    }
    // Reuse the native in-place vertex edit, including its Undo scope and readback.
    // No slab deletion/recreation and no replacement of materials or other contours.
    auto result=MoveSlabVerticesCommand ().Execute (request,control);
    if (result.Contains ("error")) return result;
    API_Element actual={}; API_ElementMemo actualMemo={};
    const GS::OnExit disposeActual ([&] { ACAPI_DisposeElemMemoHdls (&actualMemo); });
    const GSErrCode readError=LoadSlab (element.header.guid,actual,actualMemo);
    if (readError!=NoError || !RetainsArcs (actualMemo,actual.slab.poly.nArcs,originalArcs))
        return CreateErrorResponse (readError==NoError ? APIERR_GENERAL : readError,"Edge edit executed but retention of unrelated curved edges is not confirmed. Inspect before retrying.");
    return result;
}

GS::Optional<GS::UniString> GetSlabTopologyCommand::GetInputParametersSchema () const
{
    return R"({"type":"object","properties":{"elementId":{"$ref":"#/ElementId"}},"required":["elementId"],"additionalProperties":false})";
}
GS::Optional<GS::UniString> GetSlabTopologyCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{
        "elementId":{"$ref":"#/ElementId"},"coordinateSystem":{"type":"string"},"units":{"type":"string"},"modificationStamp":{"type":"string"},
        "contours":{"type":"array","items":{"type":"object","properties":{
            "contourIndex":{"type":"integer"},"hole":{"type":"boolean"},"vertices":{"type":"array","items":{"type":"object","properties":{
                "vertexIndex":{"type":"integer"},"nativeCoordinateIndex":{"type":"integer"},"nativeVertexId":{"type":"integer"},"coordinate":{"$ref":"#/Coordinate2D"},"edgeTrimType":{"type":"integer","description":"Native API_EdgeTrimID for the outgoing edge."},"edgeTrimAngle":{"type":"number","description":"Native trim angle in radians; meaningful for custom-angle trim."},"sideMaterial":{"$ref":"#/OverriddenMaterial"}
            },"required":["vertexIndex","nativeCoordinateIndex","nativeVertexId","coordinate"],"additionalProperties":false}}
        },"required":["contourIndex","hole","vertices"],"additionalProperties":false}},
        "arcs":{"type":"array","items":{"type":"object","properties":{
            "beginNativeCoordinateIndex":{"type":"integer"},"endNativeCoordinateIndex":{"type":"integer"},"angle":{"type":"number","description":"Arc sweep in radians."}
        },"required":["beginNativeCoordinateIndex","endNativeCoordinateIndex","angle"],"additionalProperties":false}}
    },"required":["elementId","coordinateSystem","units","contours","arcs"],"additionalProperties":false})";
}
GS::ObjectState GetSlabTopologyCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl&) const
{
    API_Element element = {};
    API_ElementMemo memo = {};
    const GS::OnExit dispose ([&] () { ACAPI_DisposeElemMemoHdls (&memo); });
    const GSErrCode err = LoadSlab (GetGuidFromArrayItem ("elementId", parameters), element, memo);
    if (err != NoError) return CreateErrorResponse (err, "Cannot read the requested slab polygon.");
    const GS::UniString modificationStamp (std::to_string (element.header.modiStamp).c_str ());
    if (element.slab.poly.nCoords > 10000) return CreateErrorResponse (APIERR_BADPARS, "Slab topology exceeds this command's 10000-coordinate response limit.");
    GS::ObjectState result = CreateElementIdObjectState (element.header.guid);
    result.Add ("coordinateSystem", "ProjectXY");
    result.Add ("modificationStamp",modificationStamp);
    result.Add ("units", "metres");
    const auto& addContour = result.AddList<GS::ObjectState> ("contours");
    Int32 first = 1;
    for (Int32 contour = 0; contour < element.slab.poly.nSubPolys; ++contour) {
        const Int32 last = (*memo.pends)[contour + 1];
        GS::ObjectState entry ("contourIndex", contour, "hole", contour != 0);
        const auto& addVertex = entry.AddList<GS::ObjectState> ("vertices");
        // The final coordinate duplicates the first; expose each vertex once.
        for (Int32 index = first; index < last; ++index) {
            addVertex (GS::ObjectState ("vertexIndex", index - first, "nativeCoordinateIndex", index,
                "nativeVertexId", memo.vertexIDs != nullptr ? (*memo.vertexIDs)[index] : 0,
                "coordinate", Create2DCoordinateObjectState ((*memo.coords)[index]),
                "edgeTrimType",static_cast<Int32>(memo.edgeTrims!=nullptr ? (*memo.edgeTrims)[index].sideType : APIEdgeTrim_Vertical),
                "edgeTrimAngle",memo.edgeTrims!=nullptr && (*memo.edgeTrims)[index].sideType==APIEdgeTrim_CustomAngle ? (*memo.edgeTrims)[index].sideAngle : 0.0,
                "sideMaterial",CreateOverriddenMaterialObjectState(memo.sideMaterials!=nullptr ? memo.sideMaterials[index] : element.slab.sideMat)));
        }
        addContour (entry);
        first = last + 1;
    }
    const auto& addArc = result.AddList<GS::ObjectState> ("arcs");
    for (Int32 index = 0; memo.parcs != nullptr && index < element.slab.poly.nArcs; ++index) {
        const auto& arc = (*memo.parcs)[index];
        addArc (GS::ObjectState ("beginNativeCoordinateIndex", arc.begIndex, "endNativeCoordinateIndex", arc.endIndex, "angle", arc.arcAngle));
    }
    return result;
}

GS::Optional<GS::UniString> MoveSlabVerticesCommand::GetInputParametersSchema () const
{
    return R"({"type":"object","properties":{
        "elementId":{"$ref":"#/ElementId"},
        "moves":{"type":"array","minItems":1,"maxItems":1000,"items":{"type":"object","properties":{
            "contourIndex":{"type":"integer","minimum":0},"vertexIndex":{"type":"integer","minimum":0},
            "expectedCoordinate":{"$ref":"#/Coordinate2D"},"coordinate":{"$ref":"#/Coordinate2D"},
            "expectedNativeVertexId":{"type":"integer","minimum":0,"maximum":4294967295}
        },"required":["contourIndex","vertexIndex","expectedCoordinate","coordinate"],"additionalProperties":false}}
    },"required":["elementId","moves"],"additionalProperties":false})";
}
GS::Optional<GS::UniString> MoveSlabVerticesCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{
        "success":{"type":"boolean"},"elementId":{"$ref":"#/ElementId"},"status":{"type":"string"},"requestedVertexCount":{"type":"integer"}
    },"required":["success","elementId","status","requestedVertexCount"],"additionalProperties":false})";
}
GS::ObjectState MoveSlabVerticesCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl&) const
{
    GS::Array<GS::ObjectState> moves;
    parameters.Get ("moves", moves);
    if (moves.IsEmpty () || moves.GetSize () > 1000) return CreateErrorResponse (APIERR_BADPARS, "Supply 1 to 1000 vertex moves.");
    API_Element element = {};
    API_ElementMemo memo = {};
    const GS::OnExit dispose ([&] () { ACAPI_DisposeElemMemoHdls (&memo); });
    const GSErrCode err = LoadSlab (GetGuidFromArrayItem ("elementId", parameters), element, memo);
    if (err != NoError) return CreateErrorResponse (err, "Cannot read the requested slab polygon.");
    struct Target { Int32 index; UInt32 nativeId; API_Coord coordinate; };
    GS::Array<Target> targets;
    GS::HashSet<Int32> visited;
    for (const auto& move : moves) {
        Int32 contour = -1, vertex = -1;
        move.Get ("contourIndex", contour);
        move.Get ("vertexIndex", vertex);
        const auto* expected = move.Get ("expectedCoordinate");
        const auto* desired = move.Get ("coordinate");
        if (contour < 0 || contour >= element.slab.poly.nSubPolys || vertex < 0 || expected == nullptr || desired == nullptr)
            return CreateErrorResponse (APIERR_BADPARS, "Invalid contour or vertex specification.");
        const Int32 first = contour == 0 ? 1 : (*memo.pends)[contour] + 1;
        const Int32 last = (*memo.pends)[contour + 1];
        if (vertex >= last - first) return CreateErrorResponse (APIERR_BADPARS, "Vertex index is outside the contour's unique vertices.");
        const Int32 index = first + vertex;
        if (visited.Contains (index)) return CreateErrorResponse (APIERR_BADPARS, "A vertex can appear only once per request.");
        visited.Add (index);
        const API_Coord before = Get2DCoordinateFromObjectState (*expected), after = Get2DCoordinateFromObjectState (*desired);
        if (!std::isfinite (before.x) || !std::isfinite (before.y) || !std::isfinite (after.x) || !std::isfinite (after.y))
            return CreateErrorResponse (APIERR_BADPARS, "Coordinates must be finite.");
        const auto current = (*memo.coords)[index];
        if (std::hypot (current.x - before.x, current.y - before.y) > 1e-8)
            return CreateErrorResponse (APIERR_BADPARS, "Slab geometry changed: expected vertex coordinate does not match.");
        UInt32 expectedId = 0;
        if (move.Get ("expectedNativeVertexId", expectedId) && expectedId != (memo.vertexIDs != nullptr ? (*memo.vertexIDs)[index] : 0))
            return CreateErrorResponse (APIERR_BADPARS, "Slab topology changed: expected native vertex ID does not match.");
        targets.Push ({index, memo.vertexIDs != nullptr ? (*memo.vertexIDs)[index] : 0, after});
        (*memo.coords)[index] = after;
        if (index == first) (*memo.coords)[last] = after;
    }
    const GSErrCode transaction = ACAPI_CallUndoableCommand ("Move Slab Vertices", [&] () -> GSErrCode {
        return ACAPI_Element_ChangeMemo (element.header.guid, APIMemoMask_Polygon, &memo);
    });
    if (transaction != NoError) return CreateErrorResponse (transaction, "Native slab vertex transaction failed.");
    API_Element actual = {};
    API_ElementMemo actualMemo = {};
    const GS::OnExit disposeActual ([&] () { ACAPI_DisposeElemMemoHdls (&actualMemo); });
    const GSErrCode readError = LoadSlab (element.header.guid, actual, actualMemo);
    if (readError != NoError) return CreateErrorResponse (readError, "Edit returned without error, but slab readback failed; inspect the element before retrying.");
    if (actual.slab.poly.nCoords != element.slab.poly.nCoords || actual.slab.poly.nSubPolys != element.slab.poly.nSubPolys)
        return CreateErrorResponse (APIERR_GENERAL, "Native edit changed topology unexpectedly; inspect the slab before retrying.");
    for (const auto& target : targets) {
        Int32 actualIndex = target.index;
        if (target.nativeId != 0) {
            actualIndex = 0;
            for (Int32 candidate = 1; actualMemo.vertexIDs != nullptr && candidate <= actual.slab.poly.nCoords; ++candidate) {
                if ((*actualMemo.vertexIDs)[candidate] == target.nativeId) { actualIndex = candidate; break; }
            }
        }
        if (actualIndex < 1 || actualIndex > actual.slab.poly.nCoords ||
            std::hypot ((*actualMemo.coords)[actualIndex].x - target.coordinate.x, (*actualMemo.coords)[actualIndex].y - target.coordinate.y) > 1e-8)
            return CreateErrorResponse (APIERR_GENERAL, "Requested native slab vertex position was not confirmed; inspect the slab before retrying.");
    }
    GS::ObjectState result = CreateElementIdObjectState (element.header.guid);
    result.Add ("success", true);
    result.Add ("status", "applied");
    result.Add ("requestedVertexCount", static_cast<Int32> (moves.GetSize ()));
    return result;
}

GS::ObjectState EditSlabTopologyCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& control) const
{
    API_Element element = {};
    API_ElementMemo memo = {}, insertion = {};
    const GS::OnExit dispose ([&] () { ACAPI_DisposeElemMemoHdls (&memo); ACAPI_DisposeElemMemoHdls (&insertion); });
    GSErrCode err = LoadSlab (GetGuidFromArrayItem ("elementId", parameters), element, memo);
    if (err != NoError) return CreateErrorResponse (err, "Cannot load slab topology.");
    GS::UniString operation;
    Int32 contour = -1, count = 0, vertex = -1;
    GS::Array<GS::ObjectState> expected, hole;
    parameters.Get ("operation", operation); parameters.Get ("contourIndex", contour);
    parameters.Get ("expectedContourCount", count); parameters.Get ("expectedContour", expected);
    parameters.Get ("vertexIndex", vertex); parameters.Get ("hole", hole);
    GS::UniString stamp;
    if ((parameters.Get("expectedModificationStamp",stamp) || operation=="SplitEdge" || operation=="MergeEdges") && stamp!=GS::UniString(std::to_string(element.header.modiStamp).c_str()))
        return CreateErrorResponse(APIERR_BADPARS,"Slab changed; read its topology again before this edit.");
    if (parameters.Contains("fraction") && operation!="SplitEdge") return CreateErrorResponse(APIERR_BADPARS,"fraction is only applicable to SplitEdge.");
    if (count != element.slab.poly.nSubPolys || contour < 0 || contour >= count || element.slab.poly.nCoords > 10000)
        return CreateErrorResponse (APIERR_BADPARS, "Contour count/index changed or slab exceeds 10000 coordinates.");
    const Int32 first = contour == 0 ? 1 : (*memo.pends)[contour] + 1;
    const Int32 last = (*memo.pends)[contour + 1];
    const auto same = [] (const API_Coord& a, const API_Coord& b) {
        return std::isfinite (a.x) && std::isfinite (a.y) && std::isfinite (b.x) && std::isfinite (b.y) && std::hypot (a.x-b.x, a.y-b.y) <= 1e-8;
    };
    if (expected.GetSize () != static_cast<USize> (last-first)) return CreateErrorResponse (APIERR_BADPARS, "Expected contour vertex count does not match.");
    for (Int32 index = first; index < last; ++index)
        if (!same ((*memo.coords)[index], Get2DCoordinateFromObjectState (expected[index-first])))
            return CreateErrorResponse (APIERR_BADPARS, "Expected contour geometry is stale.");
    if (!ACAPI_Element_Filter (element.header.guid, APIFilt_IsEditable | APIFilt_InMyWorkspace | APIFilt_HasAccessRight))
        return CreateErrorResponse (APIERR_NOACCESSRIGHT, "Slab is not editable.");
    // A remote curved edge must not block a straight-edge edit. Arc endpoints
    // involved in the edit still need separate splitting/merging semantics.
    const bool preserveArcs=operation=="InsertVertex" || operation=="DeleteVertex" || operation=="SplitEdge" || operation=="MergeEdges";
    std::vector<SlabArcShape> originalArcs;
    std::vector<API_PolyArc> splitArcs;
    Int32 expectedArcCount=element.slab.poly.nArcs;
    API_Coord splitPoint={};
    if (preserveArcs) {
        if (vertex<0 || vertex>=last-first) return CreateErrorResponse (APIERR_BADPARS,"Invalid selected vertex.");
        if (!ReadArcShapes (memo,element.slab.poly.nArcs,originalArcs)) return CreateErrorResponse (APIERR_BADPOLY,"Cannot read existing arc geometry.");
        const Int32 selected=first+vertex,next=selected+1;
        const auto canonical=[&] (Int32 index) { return index==last ? first : index; };
        Int32 affectedCount=0; double selectedAngle=0;
        for (Int32 i=0;i<element.slab.poly.nArcs;++i) {
            const auto& arc=(*memo.parcs)[i];
            const Int32 begin=canonical (arc.begIndex),end=canonical (arc.endIndex);
            const bool affected=operation=="DeleteVertex" ? begin==selected || end==selected :
                ((begin==selected && end==canonical (next)) || (end==selected && begin==canonical (next)));
            if (affected) {
                if (operation!="SplitEdge" && operation!="MergeEdges") return CreateErrorResponse (APIERR_BADPARS,"Selected edge/corner touches an arc. Use SplitEdge to split along its existing curve; curved corner deletion requires explicit merging semantics.");
                ++affectedCount; selectedAngle=begin==selected ? arc.arcAngle : -arc.arcAngle;
            } else if (operation=="SplitEdge") {
                auto shifted=arc;
                if (shifted.begIndex>selected) ++shifted.begIndex;
                if (shifted.endIndex>selected) ++shifted.endIndex;
                splitArcs.push_back(shifted);
            }
        }
        if (operation=="SplitEdge") {
            double fraction=0;
            if (!parameters.Get("fraction",fraction) || !std::isfinite(fraction) || fraction<=0 || fraction>=1 || affectedCount>1)
                return CreateErrorResponse(APIERR_BADPARS,"Split fraction must be strictly between zero and one, with at most one arc on the selected edge.");
            const auto a=(*memo.coords)[selected],b=(*memo.coords)[next];
            const double dx=b.x-a.x,dy=b.y-a.y;
            if (std::hypot(dx,dy)<=1e-8) return CreateErrorResponse(APIERR_BADPOLY,"Cannot split a zero-length edge.");
            if (affectedCount==0) splitPoint={a.x+fraction*dx,a.y+fraction*dy};
            else {
                if (!std::isfinite(selectedAngle) || std::abs(selectedAngle)<1e-8 || std::abs(selectedAngle)>=6.283185307179586-1e-8)
                    return CreateErrorResponse(APIERR_BADPOLY,"Arc sweep is too close to zero or a complete circle for stable splitting.");
                const double tangent=std::tan(selectedAngle/2);
                const API_Coord center={(a.x+b.x)/2-dy/(2*tangent),(a.y+b.y)/2+dx/(2*tangent)};
                const double rotation=selectedAngle*fraction,c=std::cos(rotation),s=std::sin(rotation);
                splitPoint={center.x+(a.x-center.x)*c-(a.y-center.y)*s,center.y+(a.x-center.x)*s+(a.y-center.y)*c};
                API_PolyArc left={}; left.begIndex=selected; left.endIndex=selected+1; left.arcAngle=rotation;
                API_PolyArc right={}; right.begIndex=selected+1; right.endIndex=selected+2; right.arcAngle=selectedAngle-rotation;
                splitArcs.push_back(left); splitArcs.push_back(right);
            }
            if (!std::isfinite(splitPoint.x) || !std::isfinite(splitPoint.y) || same(splitPoint,a) || same(splitPoint,b))
                return CreateErrorResponse(APIERR_BADPARS,"Split point is not finite or is indistinguishable from an endpoint.");
            expectedArcCount=static_cast<Int32>(splitArcs.size());
        }
    }
    if (operation=="MergeEdges") {
        const Int32 selected=first+vertex,previous=selected==first ? last-1 : selected-1,next=selected+1;
        if (last-first<=3) return CreateErrorResponse(APIERR_BADPARS,"Merging must leave at least three contour vertices.");
        const auto canonical=[&] (Int32 index) { return index==last ? first : index; };
        double incoming=0,outgoing=0; Int32 inCount=0,outCount=0;
        splitArcs.clear();
        for (Int32 i=0;i<element.slab.poly.nArcs;++i) {
            const auto& arc=(*memo.parcs)[i];
            const Int32 begin=canonical(arc.begIndex),end=canonical(arc.endIndex);
            if ((begin==previous && end==selected) || (end==previous && begin==selected)) { ++inCount; incoming=begin==previous ? arc.arcAngle : -arc.arcAngle; }
            else if ((begin==selected && end==canonical(next)) || (end==selected && begin==canonical(next))) { ++outCount; outgoing=begin==selected ? arc.arcAngle : -arc.arcAngle; }
            else {
                if (begin==selected || end==selected) return CreateErrorResponse(APIERR_BADPOLY,"Nonadjacent arc references the selected vertex.");
                auto shifted=arc;
                if (shifted.begIndex>selected) --shifted.begIndex;
                if (shifted.endIndex>selected) --shifted.endIndex;
                splitArcs.push_back(shifted);
            }
        }
        const auto a=(*memo.coords)[previous],middle=(*memo.coords)[selected],b=(*memo.coords)[next];
        const double dx1=middle.x-a.x,dy1=middle.y-a.y,dx2=b.x-middle.x,dy2=b.y-middle.y;
        const double length1=std::hypot(dx1,dy1),length2=std::hypot(dx2,dy2);
        if (length1<=1e-8 || length2<=1e-8 || inCount>1 || outCount>1 || inCount!=outCount)
            return CreateErrorResponse(APIERR_BADPOLY,"Merge needs two nonzero straight edges or exactly two circular arcs.");
        if (inCount==0) {
            if (std::abs(dx1*dy2-dy1*dx2)>1e-8*(length1+length2) || dx1*dx2+dy1*dy2<=0)
                return CreateErrorResponse(APIERR_BADPARS,"Straight edges are not collinear in the same direction.");
        } else {
            const double combined=incoming+outgoing;
            if (!std::isfinite(combined) || incoming*outgoing<=0 || std::abs(incoming)<1e-8 || std::abs(outgoing)<1e-8 || std::abs(combined)>=6.283185307179586-1e-8)
                return CreateErrorResponse(APIERR_BADPARS,"Arc directions differ or their combined sweep cannot form a single edge.");
            const auto center=[] (const API_Coord& start,const API_Coord& end,double angle) -> API_Coord {
                const double tangent=std::tan(angle/2);
                return {(start.x+end.x)/2-(end.y-start.y)/(2*tangent),(start.y+end.y)/2+(end.x-start.x)/(2*tangent)};
            };
            const auto c1=center(a,middle,incoming),c2=center(middle,b,outgoing);
            if (!std::isfinite(c1.x) || !std::isfinite(c1.y) || !std::isfinite(c2.x) || !std::isfinite(c2.y) || std::hypot(c1.x-c2.x,c1.y-c2.y)>1e-7)
                return CreateErrorResponse(APIERR_BADPARS,"Arcs are not on the same circle within 0.0000001 metres.");
            API_PolyArc merged={}; merged.begIndex=previous>selected ? previous-1 : previous; merged.endIndex=selected==first ? last-1 : next-1; merged.arcAngle=combined;
            splitArcs.push_back(merged);
        }
        expectedArcCount=static_cast<Int32>(splitArcs.size());
    }
    struct Edge { API_Coord coordinate; API_EdgeTrim trim; API_OverriddenAttribute material; };
    std::vector<Edge> target;
    for (Int32 index = 0; index <= element.slab.poly.nCoords; ++index) {
        Edge entry = {}; entry.coordinate = (*memo.coords)[index];
        entry.trim.sideType = APIEdgeTrim_Vertical; entry.material = element.slab.sideMat;
        if (memo.edgeTrims != nullptr) entry.trim = (*memo.edgeTrims)[index];
        if (memo.sideMaterials != nullptr) entry.material = memo.sideMaterials[index];
        target.push_back (entry);
    }
    if (operation=="MergeEdges") {
        const Int32 selected=first+vertex,previous=selected==first ? last-1 : selected-1;
        const auto& a=target[previous]; const auto& b=target[selected];
#ifdef ServerMainVers_2700
        const bool sameMaterial=a.material.hasValue==b.material.hasValue && (!a.material.hasValue || a.material.value==b.material.value);
#else
        const bool sameMaterial=a.material.overridden==b.material.overridden && (!a.material.overridden || a.material.attributeIndex==b.material.attributeIndex);
#endif
        if (a.trim.sideType!=b.trim.sideType || !std::isfinite(a.trim.sideAngle) || !std::isfinite(b.trim.sideAngle) ||
            std::abs(a.trim.sideAngle-b.trim.sideAngle)>1e-10 || !sameMaterial)
            return CreateErrorResponse(APIERR_BADPARS,"Adjacent edge trims or surface overrides differ; merging would discard a setting.");
    }
    if (memo.vertexIDs == nullptr)
        memo.vertexIDs = reinterpret_cast<UInt32**> (BMAllocateHandle (static_cast<GSSize> (target.size () * sizeof (UInt32)), ALLOCATE_CLEAR, 0));
    if (memo.parcs == nullptr) memo.parcs = reinterpret_cast<API_PolyArc**> (BMAllocateHandle (0, ALLOCATE_CLEAR, 0));
    if (memo.vertexIDs == nullptr || memo.parcs == nullptr) return CreateErrorResponse (APIERR_MEMFULL, "Cannot initialise native polygon editing handles.");
    Int32 nativeIndex = first + vertex;
    Int32 targetContours = count;
    if (operation == "InsertVertex" || operation == "SplitEdge") {
        const auto* coordinate = parameters.Get ("coordinate");
        if (vertex < 0 || nativeIndex >= last || (operation=="InsertVertex" && coordinate == nullptr)) return CreateErrorResponse (APIERR_BADPARS, "Invalid insertion vertex.");
        API_Coord point = operation=="SplitEdge" ? splitPoint : Get2DCoordinateFromObjectState (*coordinate);
        if (!same (point, point)) return CreateErrorResponse (APIERR_BADPARS, "Coordinate must be finite.");
        Edge entry = target[nativeIndex]; entry.coordinate = point;
        target.insert (target.begin () + nativeIndex + 1, entry);
#ifdef ServerMainVers_2700
        err = ACAPI_Polygon_InsertPolyNode (&memo, &nativeIndex, &point);
#else
        err = ACAPI_Goodies (APIAny_InsertPolyNodeID, &memo, &nativeIndex, &point);
#endif
    } else if (operation == "DeleteVertex" || operation == "MergeEdges") {
        if (vertex < 0 || nativeIndex >= last || last-first <= 3) return CreateErrorResponse (APIERR_BADPARS, "Deletion must leave at least three contour vertices.");
        target.erase (target.begin () + nativeIndex);
        target[last-1] = target[first];
#ifdef ServerMainVers_2700
        err = ACAPI_Polygon_DeletePolyNode (&memo, &nativeIndex);
#else
        err = ACAPI_Goodies (APIAny_DeletePolyNodeID, &memo, &nativeIndex);
#endif
    } else if (operation == "DeleteHole") {
        if (contour == 0) return CreateErrorResponse (APIERR_BADPARS, "The outer contour cannot be deleted.");
        target.erase (target.begin () + first, target.begin () + last + 1);
        --targetContours;
        Int32 nativeContour = contour + 1;
#ifdef ServerMainVers_2700
        err = ACAPI_Polygon_DeleteSubPoly (&memo, &nativeContour);
#else
        err = ACAPI_Goodies (APIAny_DeleteSubPolyID, &memo, &nativeContour);
#endif
    } else if (operation == "AddHole") {
        if (contour != 0 || hole.GetSize () < 3 || hole.GetSize () > 1000) return CreateErrorResponse (APIERR_BADPARS, "Specify outer contour and 3 to 1000 unique hole vertices.");
        const Int32 size = static_cast<Int32> (hole.GetSize ());
        insertion.coords = reinterpret_cast<API_Coord**> (BMAllocateHandle ((size+2)*sizeof(API_Coord), ALLOCATE_CLEAR, 0));
        insertion.pends = reinterpret_cast<Int32**> (BMAllocateHandle (2*sizeof(Int32), ALLOCATE_CLEAR, 0));
        if (insertion.coords == nullptr || insertion.pends == nullptr) return CreateErrorResponse (APIERR_MEMFULL, "Cannot allocate hole.");
        for (Int32 index = 0; index < size; ++index) {
            Edge entry = {}; entry.coordinate = Get2DCoordinateFromObjectState (hole[index]);
            if (!same (entry.coordinate, entry.coordinate)) return CreateErrorResponse (APIERR_BADPARS, "Hole coordinates must be finite.");
            entry.trim.sideType = APIEdgeTrim_Vertical; entry.material = element.slab.sideMat;
            (*insertion.coords)[index+1] = entry.coordinate; target.push_back (entry);
        }
        if (same ((*insertion.coords)[1], (*insertion.coords)[size])) return CreateErrorResponse (APIERR_BADPARS, "Omit the hole closing duplicate.");
        target.push_back (target[target.size ()-size]);
        (*insertion.coords)[size+1] = (*insertion.coords)[1]; (*insertion.pends)[1] = size+1;
        ++targetContours;
#ifdef ServerMainVers_2700
        err = ACAPI_Polygon_InsertSubPoly (&memo, &insertion);
#else
        err = ACAPI_Goodies (APIAny_InsertSubPolyID, &memo, &insertion);
#endif
    } else return CreateErrorResponse (APIERR_BADPARS, "Unknown slab topology operation.");
    if (err != NoError) return CreateErrorResponse (err, "Native polygon edit failed before model mutation.");
    if (operation=="SplitEdge" || operation=="MergeEdges") {
        auto** arcs=reinterpret_cast<API_PolyArc**>(BMAllocateHandle(static_cast<GSSize>(splitArcs.size()*sizeof(API_PolyArc)),ALLOCATE_CLEAR,0));
        if (arcs==nullptr) return CreateErrorResponse(APIERR_MEMFULL,"Cannot allocate revised arc data.");
        for (size_t i=0;i<splitArcs.size();++i) (*arcs)[i]=splitArcs[i];
        BMKillHandle(reinterpret_cast<GSHandle*>(&memo.parcs)); memo.parcs=arcs;
        if (!ReadArcShapes(memo,expectedArcCount,originalArcs)) return CreateErrorResponse(APIERR_BADPOLY,"Cannot represent the revised arcs; no model change.");
    }
    if (preserveArcs && !RetainsArcs (memo,expectedArcCount,originalArcs))
        return CreateErrorResponse (APIERR_BADPOLY,"Polygon operation changed an unrelated arc; no model changes applied.");
    const Int32 newCount = static_cast<Int32> (BMGetHandleSize (reinterpret_cast<GSHandle> (memo.coords))/sizeof(API_Coord))-1;
    if (newCount>10000) return CreateErrorResponse(APIERR_BADPARS,"Edited slab would exceed 10000 coordinates; no model change.");
    if (newCount+1 != static_cast<Int32> (target.size ())) return CreateErrorResponse (APIERR_BADPOLY, "Unexpected native polygon topology; model was not modified.");
    for (Int32 index = 1; index <= newCount; ++index)
        if (!same ((*memo.coords)[index], target[index].coordinate)) return CreateErrorResponse (APIERR_BADPOLY, "Native polygon reordered vertices; edge settings cannot be safely mapped. Model was not modified.");
    if (memo.edgeTrims != nullptr) BMKillHandle (reinterpret_cast<GSHandle*> (&memo.edgeTrims));
    if (memo.sideMaterials != nullptr) BMKillPtr (reinterpret_cast<GSPtr*> (&memo.sideMaterials));
    memo.edgeTrims = reinterpret_cast<API_EdgeTrim**> (BMAllocateHandle (static_cast<GSSize> (target.size()*sizeof(API_EdgeTrim)), ALLOCATE_CLEAR, 0));
    memo.sideMaterials = reinterpret_cast<API_OverriddenAttribute*> (BMAllocatePtr (static_cast<GSSize> (target.size()*sizeof(API_OverriddenAttribute)), ALLOCATE_CLEAR, 0));
    if (memo.edgeTrims == nullptr || memo.sideMaterials == nullptr) return CreateErrorResponse (APIERR_MEMFULL, "Cannot preserve slab edge settings.");
    for (Int32 index = 1; index <= newCount; ++index) {
        (*memo.edgeTrims)[index] = target[index].trim; memo.sideMaterials[index] = target[index].material;
    }
    if (control.TestBreak()) return CreateErrorResponse(APIERR_CANCEL,"Cancelled before changing the slab.");
    err = ACAPI_CallUndoableCommand ("Edit Slab Topology", [&] () -> GSErrCode {
        return ACAPI_Element_ChangeMemo (element.header.guid, APIMemoMask_Polygon | APIMemoMask_EdgeTrims | APIMemoMask_SideMaterials, &memo);
    });
    if (err != NoError) return CreateErrorResponse (err, "Slab topology transaction failed.");
    API_Element actual = {}; API_ElementMemo actualMemo = {};
    const GS::OnExit cleanup ([&] () { ACAPI_DisposeElemMemoHdls (&actualMemo); });
    err = LoadSlab (element.header.guid, actual, actualMemo);
    if (err != NoError || actual.slab.poly.nCoords != newCount || actual.slab.poly.nSubPolys != targetContours)
        return CreateErrorResponse (err == NoError ? APIERR_GENERAL : err, "Topology edit committed but readback is not confirmed. Inspect before retrying.");
    if (preserveArcs && !RetainsArcs (actualMemo,actual.slab.poly.nArcs,originalArcs))
        return CreateErrorResponse (APIERR_GENERAL,"Topology edit committed but retention of existing arcs is not confirmed. Inspect before retrying.");
    for (Int32 index = 1; index <= newCount; ++index)
        if (!same ((*actualMemo.coords)[index], target[index].coordinate)) return CreateErrorResponse (APIERR_GENERAL, "Topology edit committed but coordinates differ. Inspect before retrying.");
    GS::ObjectState result = CreateElementIdObjectState (element.header.guid);
    result.Add ("success", true); result.Add ("status", "applied");
    result.Add ("nativeCoordinateCount", newCount); result.Add ("nativeContourCount", targetContours);
    return result;
}

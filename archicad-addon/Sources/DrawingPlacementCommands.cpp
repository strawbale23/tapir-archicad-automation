#include "DrawingPlacementCommands.hpp"
#include "DrawingPlacementGeometry.hpp"
#include "MigrationHelper.hpp"
#include "GSProcessControl.hpp"
#include <algorithm>
#include <string>
#include <vector>

namespace {
using DrawingPlacementGeometry::Box;
using DrawingPlacementGeometry::Point;
GS::UniString Stamp (const API_Element& e) { return GS::UniString (std::to_string (e.header.modiStamp).c_str ()); }
GSErrCode CheckLayout (const GS::ObjectState& p,API_DatabaseInfo& db) {
    GSErrCode err=ACAPI_Database_GetCurrentDatabase (&db);
    if (err != NoError) return err;
    if (db.typeID != APIWind_LayoutID && db.typeID != APIWind_MasterLayoutID) return APIERR_BADDATABASE;
    if (DatabaseIdResolver::Instance ().GetIdOfDatabase (db) != GetGuidFromArrayItem ("expectedDatabaseId",p)) return APIERR_BADDATABASE;
    return NoError;
}
GSErrCode Frame (const API_Guid& id,API_Element& e,Box& box) {
    e.header.guid=id;
    GSErrCode err=ACAPI_Element_Get (&e);
    if (err != NoError) return err;
    if (GetElemTypeId (e.header) != API_DrawingID || e.drawing.isMultiPageDrawing) return APIERR_BADID;
    API_ElementMemo memo = {};
    const GS::OnExit dispose ([&] { ACAPI_DisposeElemMemoHdls (&memo); });
    err=ACAPI_Element_GetMemo (id,&memo,APIMemoMask_Polygon);
    if (err != NoError) return err;
    const auto& poly=e.drawing.poly;
    // 19 September 2026, 19:38 CEST. Include true circular extrema, not just vertices.
    if (poly.nCoords < 4 || poly.nCoords > 10000 || poly.nArcs<0 || poly.nArcs>poly.nCoords || memo.coords == nullptr ||
        BMhGetSize (reinterpret_cast<GSHandle> (memo.coords)) < static_cast<GSSize> ((poly.nCoords+1)*sizeof(API_Coord))) return APIERR_BADPARS;
    const auto first=(*memo.coords)[1]; box={first.x,first.y,first.x,first.y};
    for (Int32 i=1;i<=poly.nCoords;++i) {
        const auto& c=(*memo.coords)[i];
        if (!std::isfinite (c.x) || !std::isfinite (c.y)) return APIERR_BADPARS;
        box.left=std::min (box.left,c.x); box.right=std::max (box.right,c.x); box.bottom=std::min (box.bottom,c.y); box.top=std::max (box.top,c.y);
    }
    if (poly.nArcs>0) {
        if (memo.parcs==nullptr || BMhGetSize(reinterpret_cast<GSHandle>(memo.parcs))<static_cast<GSSize>(poly.nArcs*sizeof(API_PolyArc))) return APIERR_BADPOLY;
        constexpr double pi=3.141592653589793,tau=2*pi;
        const auto normalized=[] (double angle) { constexpr double circle=6.283185307179586; double value=std::fmod(angle,circle); return value<0 ? value+circle : value; };
        for (Int32 i=0;i<poly.nArcs;++i) {
            const auto& arc=(*memo.parcs)[i];
            if (arc.begIndex<1 || arc.begIndex>poly.nCoords || arc.endIndex<1 || arc.endIndex>poly.nCoords ||
                !std::isfinite(arc.arcAngle) || std::abs(arc.arcAngle)<1e-8 || std::abs(arc.arcAngle)>=tau-1e-8) return APIERR_BADPOLY;
            const auto a=(*memo.coords)[arc.begIndex],b=(*memo.coords)[arc.endIndex];
            const double dx=b.x-a.x,dy=b.y-a.y,tangent=std::tan(arc.arcAngle/2);
            const API_Coord center={(a.x+b.x)/2-dy/(2*tangent),(a.y+b.y)/2+dx/(2*tangent)};
            const double radius=std::hypot(a.x-center.x,a.y-center.y),start=std::atan2(a.y-center.y,a.x-center.x);
            if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(radius) || radius<=1e-10) return APIERR_BADPOLY;
            for (int quadrant=0;quadrant<4;++quadrant) {
                const double angle=quadrant*pi/2;
                const double travelled=arc.arcAngle>0 ? normalized(angle-start) : normalized(start-angle);
                if (travelled>std::abs(arc.arcAngle)+1e-10) continue;
                const double x=center.x+radius*std::cos(angle),y=center.y+radius*std::sin(angle);
                if (!std::isfinite(x) || !std::isfinite(y)) return APIERR_BADPOLY;
                box.left=std::min(box.left,x); box.right=std::max(box.right,x); box.bottom=std::min(box.bottom,y); box.top=std::max(box.top,y);
            }
        }
    }
    return NoError;
}
GS::ObjectState Describe (const API_Element& e,const Box& b) {
    GS::ObjectState row=CreateElementIdObjectState (e.header.guid);
    row.Add ("modificationStamp",Stamp (e));
    row.Add ("frameBoundsMillimetres",GS::ObjectState ("left",b.left*1000,"bottom",b.bottom*1000,"right",b.right*1000,"top",b.top*1000));
    row.Add ("drawingOriginMillimetres",GS::ObjectState ("x",e.drawing.pos.x*1000,"y",e.drawing.pos.y*1000));
    row.Add ("angleRadians",e.drawing.angle); row.Add ("ratio",e.drawing.ratio); row.Add ("clipped",e.drawing.isCutWithFrame);
    row.Add ("titleIncludedInBounds",false);
    GS::ObjectState title ("libraryPartIndex",e.drawing.title.libInd,
        "useUniformTextFormat",e.drawing.title.useUniformTextFormat,"fontIndex",e.drawing.title.font,
        "textSizeMillimetres",e.drawing.title.textSize,"textPen",e.drawing.title.textPen,
        "flipped",e.drawing.title.flipped,"useUniformSymbolPens",e.drawing.title.useUniformSymbolPens,"pen",e.drawing.title.pen);
    GS::Array<API_ElementHotspot> hotspots;
    const auto hotspotError=ACAPI_Element_GetHotspots(e.header.guid,&hotspots);
    if (hotspotError==NoError && hotspots.GetSize()<=10000) {
        const auto& add=title.AddList<GS::ObjectState>("placementHotspots");
        for (UIndex i=0;i<hotspots.GetSize();++i) {
            const auto& point=hotspots[i];
            if (point.first.guid==e.header.guid && point.first.neigID==APINeig_DrawingTitle) {
                if (!std::isfinite(point.second.x*1000) || !std::isfinite(point.second.y*1000)) {
                    title.Add("hotspotError",*CreateErrorResponse(APIERR_BADPARS,"Title hotspot coordinate is not finite.").Get("error")); break;
                }
                add(GS::ObjectState("hotspotIndex",i,"nativeSubIndex",point.first.inIndex,
                    "nativePartType",static_cast<Int32>(point.first.elemPartType),"nativePartIndex",point.first.elemPartIndex,
                    "positionMillimetres",GS::ObjectState("x",point.second.x*1000,"y",point.second.y*1000)));
            }
        }
    } else title.Add("hotspotError",*CreateErrorResponse(hotspotError==NoError ? APIERR_BADPARS : hotspotError,"Cannot enumerate title placement hotspots.").Get("error"));
    if (e.drawing.title.guid!=APINULLGuid) {
        title.Add ("elementId",CreateGuidObjectState (e.drawing.title.guid));
        API_Element nativeTitle = {}; nativeTitle.header.guid=e.drawing.title.guid;
        API_Box3D bounds = {};
        GSErrCode err=ACAPI_Element_Get (&nativeTitle);
        if (err==NoError) err=ACAPI_Element_CalcBounds (&nativeTitle.header,&bounds);
        if (err==NoError && (!std::isfinite (bounds.xMin) || !std::isfinite (bounds.xMax) || !std::isfinite (bounds.yMin) || !std::isfinite (bounds.yMax) || bounds.xMin>bounds.xMax || bounds.yMin>bounds.yMax)) err=APIERR_GENERAL;
        if (err==NoError) title.Add ("nativeBoundsMillimetres",GS::ObjectState ("left",bounds.xMin*1000,"bottom",bounds.yMin*1000,"right",bounds.xMax*1000,"top",bounds.yMax*1000));
        else title.Add ("boundsError",*CreateErrorResponse (err,"Native title bounds unavailable; drawing frame excludes its title.").Get ("error"));
    }
    row.Add ("title",title);
    return row;
}
}
GS::Optional<GS::UniString> GetDrawingFramesCommand::GetInputParametersSchema () const {
    return R"({"type":"object","properties":{"expectedDatabaseId":{"$ref":"#/DatabaseId"},"elements":{"type":"array","minItems":1,"maxItems":100,"items":{"type":"object","properties":{"elementId":{"$ref":"#/ElementId"}},"required":["elementId"],"additionalProperties":false}}},"required":["expectedDatabaseId","elements"],"additionalProperties":false})";
}
GS::Optional<GS::UniString> GetDrawingFramesCommand::GetRawResponseSchema () const {
    return R"({"type":"object","properties":{"databaseId":{"$ref":"#/DatabaseId"},"coordinateSystem":{"const":"LayoutPaperMillimetres"},"drawings":{"type":"array","items":{"type":"object"}}},"required":["databaseId","coordinateSystem","drawings"],"additionalProperties":false})";
}
GS::ObjectState GetDrawingFramesCommand::Execute (const GS::ObjectState& p, GS::ProcessControl&) const {
    API_DatabaseInfo db = {}; GSErrCode err=CheckLayout (p,db);
    if (err != NoError) return CreateErrorResponse (err,"Activate the expected layout/master database before reading drawings.");
    GS::Array<GS::ObjectState> rows; p.Get ("elements",rows);
    if (rows.IsEmpty () || rows.GetSize ()>100) return CreateErrorResponse (APIERR_BADPARS,"Supply 1..100 drawings.");
    GS::ObjectState result ("databaseId",CreateGuidObjectState (DatabaseIdResolver::Instance ().GetIdOfDatabase (db)),"coordinateSystem","LayoutPaperMillimetres");
    const auto& add=result.AddList<GS::ObjectState> ("drawings");
    for (const auto& input:rows) {
        API_Element e = {}; Box box = {}; const auto id=GetGuidFromArrayItem ("elementId",input);
        err=Frame (id,e,box);
        if (err == NoError) add (Describe (e,box));
        else { auto row=CreateElementIdObjectState (id); row.Add ("error",*CreateErrorResponse (err,"Cannot read drawing frame; invalid or numerically unstable arcs and multipage drawings are unsupported.").Get ("error")); add (row); }
    }
    return result;
}
GS::Optional<GS::UniString> PositionDrawingsCommand::GetInputParametersSchema () const {
    return R"({"type":"object","properties":{"expectedDatabaseId":{"$ref":"#/DatabaseId"},"drawings":{"type":"array","minItems":1,"maxItems":100,"items":{"type":"object","properties":{
        "elementId":{"$ref":"#/ElementId"},"expectedModificationStamp":{"type":"string","pattern":"^[0-9]+$"},
        "horizontal":{"enum":["Left","Center","Right"]},"vertical":{"enum":["Bottom","Center","Top"]},"positionMillimetres":{"$ref":"#/Coordinate2D"}
    },"required":["elementId","expectedModificationStamp","horizontal","vertical","positionMillimetres"],"additionalProperties":false}},"dryRun":{"type":"boolean","description":"Return planned translations without modifying drawings. Default false."}},"required":["expectedDatabaseId","drawings"],"additionalProperties":false})";
}
GS::Optional<GS::UniString> PositionDrawingsCommand::GetRawResponseSchema () const {
    return R"({"type":"object","properties":{"dryRun":{"type":"boolean"},"transactionStatus":{"enum":["notStarted","committed","failed"]},"results":{"type":"array","items":{"type":"object"}},"error":{"$ref":"#/Error"}},"required":["dryRun","transactionStatus","results"],"additionalProperties":false})";
}
GS::ObjectState PositionDrawingsCommand::Execute (const GS::ObjectState& p, GS::ProcessControl&) const {
    API_DatabaseInfo db = {}; GSErrCode err=CheckLayout (p,db);
    if (err != NoError) return CreateErrorResponse (err,"Expected layout is not active; no drawings moved.");
    GS::Array<GS::ObjectState> inputs; p.Get ("drawings",inputs); bool dryRun=false; p.Get ("dryRun",dryRun);
    if (inputs.IsEmpty () || inputs.GetSize ()>100) return CreateErrorResponse (APIERR_BADPARS,"Supply 1..100 drawings.");
    struct Target { API_Element element; Box box; Point delta; GSErrCode editError=NoError; bool attempted=false; };
    std::vector<Target> targets; GS::HashSet<API_Guid> seen;
    for (const auto& input:inputs) {
        Target target = {}; const auto id=GetGuidFromArrayItem ("elementId",input);
        if (seen.Contains (id)) return CreateErrorResponse (APIERR_BADPARS,"Duplicate drawing target.");
        seen.Add (id); err=Frame (id,target.element,target.box);
        if (err != NoError) return CreateErrorResponse (err,"Cannot inspect drawing frame; no drawings moved.");
        GS::UniString stamp,horizontal,vertical; input.Get ("expectedModificationStamp",stamp); input.Get ("horizontal",horizontal); input.Get ("vertical",vertical);
        if (stamp != Stamp (target.element)) return CreateErrorResponse (APIERR_BADPARS,"Drawing changed since inspection; no drawings moved.");
        if (!ACAPI_Element_Filter (id,APIFilt_IsEditable | APIFilt_InMyWorkspace | APIFilt_HasAccessRight)) return CreateErrorResponse (APIERR_NOTEDITABLE,"Drawing is not editable.");
        API_Guid group=APINULLGuid;
        err=ACAPI_Grouping_GetGroup (id,&group);
        if (err == NoError && group != APINULLGuid) return CreateErrorResponse (APIERR_BADPARS,"Ungroup drawings before independent frame placement.");
        const auto* position=input.Get ("positionMillimetres");
        if (position==nullptr || (horizontal!="Left" && horizontal!="Center" && horizontal!="Right") || (vertical!="Bottom" && vertical!="Center" && vertical!="Top")) return CreateErrorResponse (APIERR_BADPARS,"Invalid frame anchor.");
        const auto point=Get2DCoordinateFromObjectState (*position);
        if (!DrawingPlacementGeometry::Translation (target.box,horizontal=="Left"?0:horizontal=="Right"?1:.5,vertical=="Bottom"?0:vertical=="Top"?1:.5,{point.x,point.y},target.delta)) return CreateErrorResponse (APIERR_BADPARS,"Invalid finite drawing placement.");
        targets.push_back (target);
    }
    GSErrCode transaction=NoError;
    if (!dryRun) transaction=ACAPI_CallUndoableCommand ("Position drawings",[&] () -> GSErrCode {
        for (auto& target:targets) {
            API_EditPars edit = {}; edit.typeID=APIEdit_Drag; edit.withDelete=true;
            edit.endC={target.delta.x,target.delta.y,0};
            GS::Array<API_Neig> neigs; neigs.Push (API_Neig (target.element.header.guid));
            target.attempted=true; target.editError=ACAPI_Element_Edit (&neigs,edit);
            if (target.editError!=NoError) return target.editError;
            if (neigs.GetSize ()!=1 || neigs[0].guid!=target.element.header.guid) { target.editError=APIERR_GENERAL; return target.editError; }
        }
        return NoError;
    });
    GS::ObjectState result ("dryRun",dryRun,"transactionStatus",dryRun?"notStarted":transaction==NoError?"committed":"failed");
    const auto& add=result.AddList<GS::ObjectState> ("results");
    for (const auto& target:targets) {
        auto row=CreateElementIdObjectState (target.element.header.guid);
        row.Add ("translationMillimetres",GS::ObjectState ("x",target.delta.x*1000,"y",target.delta.y*1000));
        if (!dryRun) {
            API_Element actual = {}; Box box = {}; err=Frame (target.element.header.guid,actual,box);
            const auto close=[] (double a,double b) { return std::isfinite (a) && std::isfinite (b) && std::abs (a-b)<1e-8; };
            const bool matches=transaction==NoError && err==NoError && close (box.left,target.box.left+target.delta.x) && close (box.right,target.box.right+target.delta.x) && close (box.bottom,target.box.bottom+target.delta.y) && close (box.top,target.box.top+target.delta.y);
            row.Add ("success",matches); row.Add ("status",matches?"applied":"notConfirmed"); row.Add ("attempted",target.attempted);
            if (err==NoError) row.Add ("actual",Describe (actual,box));
            else row.Add ("readbackError",*CreateErrorResponse (err,"Cannot read actual drawing frame.").Get ("error"));
        }
        add (row);
    }
    if (transaction!=NoError) result.Add ("error",*CreateErrorResponse (transaction,"Drawing placement transaction failed; inspect actual frames before retrying.").Get ("error"));
    return result;
}


// 19 September 2026, 19:39 CEST. A separate operation on existing drawings;
// do not edit a just-created replacement inside its creation transaction.
GS::Optional<GS::UniString> PositionDrawingTitlesCommand::GetInputParametersSchema () const {
    return R"({"type":"object","properties":{
        "expectedDatabaseId":{"$ref":"#/DatabaseId"},"dryRun":{"type":"boolean","default":false},
        "titles":{"type":"array","minItems":1,"maxItems":100,"items":{"type":"object","properties":{
            "elementId":{"$ref":"#/ElementId","description":"Owning drawing, not its title object's GUID."},
            "expectedModificationStamp":{"type":"string"},"hotspotIndex":{"type":"integer","minimum":0},
            "expectedPositionMillimetres":{"$ref":"#/Coordinate2D"},"positionMillimetres":{"$ref":"#/Coordinate2D"}
        },"required":["elementId","expectedModificationStamp","hotspotIndex","expectedPositionMillimetres","positionMillimetres"],"additionalProperties":false}}
    },"required":["expectedDatabaseId","titles"],"additionalProperties":false})";
}
GS::Optional<GS::UniString> PositionDrawingTitlesCommand::GetRawResponseSchema () const {
    return R"({"type":"object","properties":{"dryRun":{"type":"boolean"},"transactionStatus":{"enum":["notStarted","committed","failed"]},"results":{"type":"array","items":{"type":"object"}},"error":{"$ref":"#/Error"}},"required":["dryRun","transactionStatus","results"],"additionalProperties":false})";
}
GS::ObjectState PositionDrawingTitlesCommand::Execute (const GS::ObjectState& p,GS::ProcessControl& control) const {
    API_DatabaseInfo database={}; auto error=CheckLayout(p,database);
    if (error!=NoError) return CreateErrorResponse(error,"Activate the expected layout before positioning drawing titles.");
    GS::Array<GS::ObjectState> inputs;
    if (!p.Get("titles",inputs) || inputs.IsEmpty() || inputs.GetSize()>100) return CreateErrorResponse(APIERR_BADPARS,"Supply 1..100 title placements.");
    bool dryRun=false; p.Get("dryRun",dryRun);
    const auto close=[] (double a,double b) { return std::isfinite(a) && std::isfinite(b) && std::abs(a-b)<=1e-8; };
    struct Target { API_Element element; Box box; API_Neig neig; API_Coord3D before,after; bool attempted=false; };
    std::vector<Target> targets;
    GS::HashSet<API_Guid> seen;
    for (const auto& input:inputs) {
        if (control.TestBreak()) return CreateErrorResponse(APIERR_CANCEL,"Cancelled before title placement.");
        Target target={}; const auto id=GetGuidFromArrayItem("elementId",input);
        if (seen.Contains(id)) return CreateErrorResponse(APIERR_BADPARS,"Specify each drawing only once.");
        seen.Add(id);
        error=Frame(id,target.element,target.box);
        if (error!=NoError) return CreateErrorResponse(error,"Cannot inspect the drawing frame before title placement.");
        GS::UniString stamp; input.Get("expectedModificationStamp",stamp);
        if (stamp!=Stamp(target.element)) return CreateErrorResponse(APIERR_BADPARS,"Drawing changed; inspect its title hotspots again.");
        if (!ACAPI_Element_Filter(id,APIFilt_IsEditable|APIFilt_InMyWorkspace|APIFilt_HasAccessRight)) return CreateErrorResponse(APIERR_NOTEDITABLE,"Drawing is not editable.");
        API_Guid group=APINULLGuid;
        if (ACAPI_Grouping_GetGroup(id,&group)==NoError && group!=APINULLGuid) return CreateErrorResponse(APIERR_BADPARS,"Ungroup the drawing before independent title placement.");
        GS::Array<API_ElementHotspot> hotspots;
        error=ACAPI_Element_GetHotspots(id,&hotspots);
        if (error!=NoError) return CreateErrorResponse(error,"Cannot resolve the drawing title hotspot.");
        Int32 index=-1; input.Get("hotspotIndex",index);
        const auto* expected=input.Get("expectedPositionMillimetres"); const auto* destination=input.Get("positionMillimetres");
        if (index<0 || hotspots.GetSize()>10000 || index>=static_cast<Int32>(hotspots.GetSize()) || expected==nullptr || destination==nullptr)
            return CreateErrorResponse(APIERR_BADPARS,"Invalid title hotspot or placement coordinates.");
        const auto& hotspot=hotspots[index];
        if (hotspot.first.guid!=id || hotspot.first.neigID!=APINeig_DrawingTitle) return CreateErrorResponse(APIERR_BADPARS,"Selected hotspot is not an owning drawing's title point.");
        const auto previous=Get2DCoordinateFromObjectState(*expected),next=Get2DCoordinateFromObjectState(*destination);
        if (!close(previous.x/1000,hotspot.second.x) || !close(previous.y/1000,hotspot.second.y) || !std::isfinite(next.x) || !std::isfinite(next.y) || !std::isfinite(hotspot.second.z))
            return CreateErrorResponse(APIERR_BADPARS,"Title position changed or coordinates are not finite.");
        target.neig=hotspot.first; target.before=hotspot.second; target.after={next.x/1000,next.y/1000,hotspot.second.z};
        targets.push_back(target);
    }
    GSErrCode transaction=NoError;
    if (!dryRun) transaction=ACAPI_CallUndoableCommand("Position drawing titles",[&] () -> GSErrCode {
        for (auto& target:targets) {
            if (control.TestBreak()) return APIERR_CANCEL;
            if (close(target.before.x,target.after.x) && close(target.before.y,target.after.y)) continue;
            API_EditPars edit={}; edit.typeID=APIEdit_Drag; edit.withDelete=true;
            edit.begC=target.before; edit.endC=target.after;
            GS::Array<API_Neig> neigs; neigs.Push(target.neig); target.attempted=true;
            auto err=ACAPI_Element_Edit(&neigs,edit);
            if (err!=NoError) return err;
            if (neigs.GetSize()!=1 || neigs[0].guid!=target.element.header.guid || neigs[0].neigID!=APINeig_DrawingTitle) return APIERR_GENERAL;
            target.neig=neigs[0];
            API_Coord3D actual={};
#ifdef ServerMainVers_2700
            err=ACAPI_Element_NeigToCoord(&target.neig,&actual);
#else
            err=ACAPI_Goodies(APIAny_NeigToCoordID,&target.neig,&actual);
#endif
            if (err!=NoError) return err;
            if (!close(actual.x,target.after.x) || !close(actual.y,target.after.y) || !close(actual.z,target.after.z)) return APIERR_GENERAL;
            API_Element drawing={}; Box box={}; err=Frame(target.element.header.guid,drawing,box);
            if (err!=NoError) return err;
            if (!close(box.left,target.box.left) || !close(box.right,target.box.right) || !close(box.bottom,target.box.bottom) || !close(box.top,target.box.top) ||
                !close(drawing.drawing.pos.x,target.element.drawing.pos.x) || !close(drawing.drawing.pos.y,target.element.drawing.pos.y) ||
                !close(drawing.drawing.angle,target.element.drawing.angle) || !close(drawing.drawing.ratio,target.element.drawing.ratio)) return APIERR_GENERAL;
        }
        return NoError;
    });
    GS::ObjectState result("dryRun",dryRun,"transactionStatus",dryRun ? "notStarted" : transaction==NoError ? "committed" : "failed");
    const auto& add=result.AddList<GS::ObjectState>("results");
    for (auto& target:targets) {
        auto row=CreateElementIdObjectState(target.element.header.guid);
        row.Add("requestedPositionMillimetres",GS::ObjectState("x",target.after.x*1000,"y",target.after.y*1000));
        if (!dryRun) {
            API_Coord3D actual={};
#ifdef ServerMainVers_2700
            error=ACAPI_Element_NeigToCoord(&target.neig,&actual);
#else
            error=ACAPI_Goodies(APIAny_NeigToCoordID,&target.neig,&actual);
#endif
            const bool matches=transaction==NoError && error==NoError && close(actual.x,target.after.x) && close(actual.y,target.after.y) && close(actual.z,target.after.z);
            row.Add("success",matches); row.Add("status",matches ? (target.attempted ? "applied" : "alreadySatisfied") : "notConfirmed"); row.Add("attempted",target.attempted);
            if (error==NoError) row.Add("actualPositionMillimetres",GS::ObjectState("x",actual.x*1000,"y",actual.y*1000));
            else row.Add("readbackError",*CreateErrorResponse(error,"Cannot read title position after transaction.").Get("error"));
        }
        add(row);
    }
    if (transaction!=NoError) result.Add("error",*CreateErrorResponse(transaction,"Title placement failed or affected the drawing frame. Inspect current state before retrying; no placement is confirmed.").Get("error"));
    return result;
}

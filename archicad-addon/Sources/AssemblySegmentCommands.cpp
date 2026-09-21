#include "AssemblySegmentCommands.hpp"
#include "MigrationHelper.hpp"
#include <cmath>
#include <string>
#include <set>

namespace {
constexpr UInt32 MaximumSegments = 100;
const UInt64 SegmentMask = APIMemoMask_BeamSegment | APIMemoMask_ColumnSegment |
    APIMemoMask_AssemblySegmentCut | APIMemoMask_AssemblySegmentScheme;

GS::UniString Stamp (const API_Element& e) { return GS::UniString (std::to_string (e.header.modiStamp).c_str ()); }
bool IsBeam (const API_Element& e) { return GetElemTypeId (e.header) == API_BeamID; }
UInt32 Count (const API_Element& e) { return IsBeam (e) ? e.beam.nSegments : e.column.nSegments; }
UInt32 CutCount (const API_Element& e) { return IsBeam (e) ? e.beam.nCuts : e.column.nCuts; }
UInt32 SchemeCount (const API_Element& e) { return IsBeam (e) ? e.beam.nSchemes : e.column.nSchemes; }
template<class T> bool Has (T* p, UInt32 n) {
    return p != nullptr && BMGetPtrSize (reinterpret_cast<GSPtr> (p)) >= static_cast<GSSize> (sizeof (T) * n);
}
template<class T> T* Allocate (UInt32 n) { return reinterpret_cast<T*> (BMAllocatePtr (n * sizeof (T), ALLOCATE_CLEAR, 0)); }
API_AssemblySegmentData& Data (const API_Element& e, API_ElementMemo& m, UInt32 i) {
    return IsBeam (e) ? m.beamSegments[i].assemblySegmentData : m.columnSegments[i].assemblySegmentData;
}
GSErrCode Read (const GS::ObjectState& p, API_Element& e, API_ElementMemo& m) {
    e.header.guid = GetGuidFromArrayItem ("elementId", p);
    GSErrCode err = ACAPI_Element_Get (&e);
    if (err != NoError) return err;
    if (GetElemTypeId (e.header) != API_BeamID && GetElemTypeId (e.header) != API_ColumnID) return APIERR_BADID;
    if (Count (e) < 1 || Count (e) > MaximumSegments || CutCount (e) > MaximumSegments+1 || SchemeCount (e) > MaximumSegments) return APIERR_BADPARS;
    err = ACAPI_Element_GetMemo (e.header.guid, &m, SegmentMask);
    if (err != NoError) return err;
    if (!(IsBeam (e) ? Has (m.beamSegments, Count (e)) : Has (m.columnSegments, Count (e))) ||
        !Has (m.assemblySegmentCuts, CutCount (e)) || !Has (m.assemblySegmentSchemes, SchemeCount (e))) return APIERR_GENERAL;
    return NoError;
}
const char* CutName (API_AssemblySegmentCutTypeID t) {
    return t == APIAssemblySegmentCut_Horizontal ? "Horizontal" : t == APIAssemblySegmentCut_Vertical ? "Vertical" : "Custom";
}
GS::ObjectState Describe (const API_Element& e, API_ElementMemo& m) {
    GS::ObjectState result = CreateElementIdObjectState (e.header.guid);
    result.Add ("elementType", IsBeam (e) ? "Beam" : "Column");
    result.Add ("modificationStamp", Stamp (e)); result.Add ("lengthUnit", "metres"); result.Add ("angleUnit", "radians");
    const auto& add = result.AddList<GS::ObjectState> ("segments");
    for (UInt32 i=0; i<Count (e); ++i) {
        const auto& s = Data (e, m, i);
        GS::ObjectState row ("index", i, "structure", s.modelElemStructureType == API_ProfileStructure ? "Profile" : "Basic",
            "width", s.nominalWidth, "height", s.nominalHeight, "circleBased", s.circleBased,
            "homogeneous", s.isHomogeneous, "endWidth", s.endWidth, "endHeight", s.endHeight,
            "linkedDimensions", s.isWidthAndHeightLinked, "linkedEndDimensions", s.isEndWidthAndHeightLinked);
        const API_AttrTypeID type = s.modelElemStructureType == API_ProfileStructure ? API_ProfileID : API_BuildingMaterialID;
        row.Add (s.modelElemStructureType == API_ProfileStructure ? "profileId" : "buildingMaterialId",
            CreateGuidObjectState (GetAttributeGuidFromIndex (type, s.modelElemStructureType == API_ProfileStructure ? s.profileAttr : s.buildingMaterial)));
        row.Add ("segmentId", CreateGuidObjectState (IsBeam (e) ? m.beamSegments[i].head.guid : m.columnSegments[i].head.guid));
        add (row);
    }
    const auto& addScheme = result.AddList<GS::ObjectState> ("schemes");
    for (UInt32 i=0; i<SchemeCount (e); ++i) {
        const auto& s = m.assemblySegmentSchemes[i];
        addScheme (GS::ObjectState ("lengthType", s.lengthType == APIAssemblySegment_Fixed ? "Fixed" : "Proportional",
            "value", s.lengthType == APIAssemblySegment_Fixed ? s.fixedLength : s.lengthProportion));
    }
    const auto& addCut = result.AddList<GS::ObjectState> ("cuts");
    for (UInt32 i=0; i<CutCount (e); ++i) {
        const auto& c = m.assemblySegmentCuts[i];
        GS::ObjectState row ("type", CutName (c.cutType));
        if (c.cutType == APIAssemblySegmentCut_Custom) row.Add ("angle", c.customAngle);
        addCut (row);
    }
    return result;
}
GSErrCode Configure (API_AssemblySegmentData& s, const GS::ObjectState& p, bool beam) {
    for (const char* name : {"width", "height", "endWidth", "endHeight"}) {
        double v = 0;
        if (p.Contains (name) && (!p.Get (name, v) || !std::isfinite (v) || v <= 0)) return APIERR_BADPARS;
    }
    if (p.Contains ("buildingMaterialId") && p.Contains ("profileId")) return APIERR_BADPARS;
    if (p.Contains ("buildingMaterialId") || p.Contains ("profileId")) {
        const bool profile = p.Contains ("profileId");
        API_Attribute a = {}; a.header.typeID = profile ? API_ProfileID : API_BuildingMaterialID;
        a.header.guid = GetGuidFromArrayItem (profile ? "profileId" : "buildingMaterialId", p);
        if (a.header.guid == APINULLGuid) return APIERR_BADPARS;
        const GSErrCode err = ACAPI_Attribute_Get (&a);
        if (err != NoError) return err;
        s.modelElemStructureType = profile ? API_ProfileStructure : API_BasicStructure;
        if (profile) { s.profileAttr = a.header.index; s.circleBased = false; }
        else s.buildingMaterial = a.header.index;
    }
    p.Get ("width", s.nominalWidth); p.Get ("height", s.nominalHeight);
    p.Get ("homogeneous", s.isHomogeneous); p.Get ("endWidth", s.endWidth); p.Get ("endHeight", s.endHeight);
    p.Get ("linkedDimensions", s.isWidthAndHeightLinked); p.Get ("linkedEndDimensions", s.isEndWidthAndHeightLinked);
    if (p.Contains ("circleBased") && (beam || s.modelElemStructureType == API_ProfileStructure)) return APIERR_BADPARS;
    p.Get ("circleBased", s.circleBased);
    if (s.isHomogeneous && (p.Contains ("endWidth") || p.Contains ("endHeight") || p.Contains ("linkedEndDimensions"))) return APIERR_BADPARS;
    if (s.modelElemStructureType == API_BasicStructure) {
        if ((s.isWidthAndHeightLinked || s.circleBased) && std::abs (s.nominalWidth-s.nominalHeight) > 1e-9) return APIERR_BADPARS;
        if (!s.isHomogeneous && (s.isEndWidthAndHeightLinked || s.circleBased) && std::abs (s.endWidth-s.endHeight) > 1e-9) return APIERR_BADPARS;
    }
    return NoError;
}
bool Near (double a, double b) { return std::isfinite (a) && std::isfinite (b) && std::abs (a-b) <= 1e-8; }
bool Same (const API_AssemblySegmentData& a, const API_AssemblySegmentData& b) {
    return a.modelElemStructureType == b.modelElemStructureType && a.circleBased == b.circleBased &&
        a.isHomogeneous == b.isHomogeneous && a.isWidthAndHeightLinked == b.isWidthAndHeightLinked &&
        Near (a.nominalWidth,b.nominalWidth) && Near (a.nominalHeight,b.nominalHeight) &&
        (a.modelElemStructureType == API_ProfileStructure ? a.profileAttr == b.profileAttr : a.buildingMaterial == b.buildingMaterial) &&
        (a.isHomogeneous || (Near (a.endWidth,b.endWidth) && Near (a.endHeight,b.endHeight) && a.isEndWidthAndHeightLinked == b.isEndWidthAndHeightLinked));
}
}

GS::Optional<GS::UniString> GetAssemblySegmentsCommand::GetInputParametersSchema () const {
    return R"({"type":"object","properties":{"elementId":{"$ref":"#/ElementId"}},"required":["elementId"],"additionalProperties":false})";
}
GS::Optional<GS::UniString> GetAssemblySegmentsCommand::GetRawResponseSchema () const {
    return R"({"type":"object","properties":{"elementId":{"$ref":"#/ElementId"},"elementType":{"enum":["Beam","Column"]},"modificationStamp":{"type":"string"},"lengthUnit":{"const":"metres"},"angleUnit":{"const":"radians"},"segments":{"type":"array","items":{"type":"object"}},"schemes":{"type":"array","items":{"type":"object"}},"cuts":{"type":"array","items":{"type":"object"}}},"required":["elementId","elementType","modificationStamp","segments","schemes","cuts","lengthUnit","angleUnit"],"additionalProperties":false})";
}
GS::ObjectState GetAssemblySegmentsCommand::Execute (const GS::ObjectState& p, GS::ProcessControl&) const {
    API_Element e = {}; API_ElementMemo m = {};
    const GS::OnExit dispose ([&] { ACAPI_DisposeElemMemoHdls (&m); });
    const GSErrCode err = Read (p,e,m);
    if (err != NoError) return CreateErrorResponse (err,"Cannot read native beam/column segments (maximum 100). ");
    return Describe (e,m);
}

GS::Optional<GS::UniString> SetAssemblySegmentsCommand::GetInputParametersSchema () const {
    return R"({"type":"object","properties":{
        "elementId":{"$ref":"#/ElementId"},"expectedModificationStamp":{"type":"string","pattern":"^[0-9]+$"},
        "segments":{"type":"array","minItems":1,"maxItems":100,"description":"Complete ordered replacement. Unspecified settings inherit from sourceIndex. First use of an existing segment retains its identity; repeated sourceIndex clones it. Omitted segments are removed. Custom profile topology cannot be reordered.","items":{"type":"object","properties":{
            "sourceIndex":{"type":"integer","minimum":0},"width":{"type":"number","exclusiveMinimum":0},"height":{"type":"number","exclusiveMinimum":0},
            "endWidth":{"type":"number","exclusiveMinimum":0},"endHeight":{"type":"number","exclusiveMinimum":0},"homogeneous":{"type":"boolean"},
            "circleBased":{"type":"boolean"},"linkedDimensions":{"type":"boolean"},"linkedEndDimensions":{"type":"boolean"},
            "buildingMaterialId":{"$ref":"#/AttributeId"},"profileId":{"$ref":"#/AttributeId"}
        },"required":["sourceIndex"],"not":{"required":["buildingMaterialId","profileId"]},"additionalProperties":false}},
        "schemes":{"type":"array","minItems":1,"maxItems":100,"description":"One length scheme per output segment; proportional values must sum to one.","items":{"type":"object","properties":{"lengthType":{"enum":["Fixed","Proportional"]},"value":{"type":"number","exclusiveMinimum":0}},"required":["lengthType","value"],"additionalProperties":false}},
        "cuts":{"type":"array","minItems":2,"maxItems":101,"description":"One more cut than segments. Angles in radians.","items":{"type":"object","properties":{"type":{"enum":["Horizontal","Vertical","Custom"]},"angle":{"type":"number"}},"required":["type"],"allOf":[{"if":{"properties":{"type":{"const":"Custom"}}},"then":{"required":["angle"]},"else":{"not":{"required":["angle"]}}}],"additionalProperties":false}}
    },"required":["elementId","expectedModificationStamp","segments","schemes","cuts"],"additionalProperties":false})";
}
GS::Optional<GS::UniString> SetAssemblySegmentsCommand::GetRawResponseSchema () const {
    return R"({"type":"object","properties":{"elementId":{"$ref":"#/ElementId"},"success":{"type":"boolean"},"status":{"enum":["applied","notConfirmed"]},"verification":{"const":"nativeSegmentSettingsReadBack"},"actual":{"type":"object"},"error":{"$ref":"#/Error"}},"required":["elementId","success","status","verification"],"additionalProperties":false})";
}
GS::ObjectState SetAssemblySegmentsCommand::Execute (const GS::ObjectState& p, GS::ProcessControl&) const {
    API_Element e = {}; API_ElementMemo old = {}, replacement = {}, actualMemo = {};
    const GS::OnExit dispose ([&] { ACAPI_DisposeElemMemoHdls (&old); ACAPI_DisposeElemMemoHdls (&replacement); ACAPI_DisposeElemMemoHdls (&actualMemo); });
    GSErrCode err = Read (p,e,old);
    if (err != NoError) return CreateErrorResponse (err,"Cannot read assembly before editing.");
    GS::UniString expected; p.Get ("expectedModificationStamp", expected);
    if (expected != Stamp (e)) return CreateErrorResponse (APIERR_BADPARS,"Assembly changed; inspect it again before editing.");
    if (!ACAPI_Element_Filter (e.header.guid, APIFilt_IsEditable | APIFilt_InMyWorkspace | APIFilt_HasAccessRight)) return CreateErrorResponse (APIERR_NOTEDITABLE,"Assembly is not editable.");
    GS::Array<GS::ObjectState> segments, schemes, cuts;
    p.Get ("segments", segments); p.Get ("schemes", schemes); p.Get ("cuts", cuts);
    const UInt32 n = segments.GetSize ();
    if (n < 1 || n > MaximumSegments || schemes.GetSize () != n || cuts.GetSize () != n+1) return CreateErrorResponse (APIERR_BADPARS,"Supply 1..100 segments, one scheme per segment and one additional cut.");
    const bool beam = IsBeam (e);
    if (beam) replacement.beamSegments = Allocate<API_BeamSegmentType> (n);
    else replacement.columnSegments = Allocate<API_ColumnSegmentType> (n);
    replacement.assemblySegmentSchemes = Allocate<API_AssemblySegmentSchemeData> (n);
    replacement.assemblySegmentCuts = Allocate<API_AssemblySegmentCutData> (n+1);
    if ((beam ? replacement.beamSegments == nullptr : replacement.columnSegments == nullptr) || replacement.assemblySegmentSchemes == nullptr || replacement.assemblySegmentCuts == nullptr) return CreateErrorResponse (APIERR_MEMFULL,"Cannot allocate replacement assembly.");
    // Custom per-instance profile memos contain segment indices. Keep their indexing stable.
    const bool hasCustomProfiles = beam ? e.beam.nProfiles > 0 : e.column.nProfiles > 0;
    std::set<Int32> retained;
    double proportion = 0; bool hasProportion = false;
    for (UInt32 i=0; i<n; ++i) {
        Int32 source = -1; segments[i].Get ("sourceIndex", source);
        if (source < 0 || static_cast<UInt32> (source) >= Count (e)) return CreateErrorResponse (APIERR_BADPARS,"sourceIndex does not identify an existing segment.");
        if (hasCustomProfiles && (n != Count (e) || source != static_cast<Int32> (i))) return CreateErrorResponse (APIERR_BADPARS,"Reordering custom-profile segment memos is unsupported; retain their count/order.");
        if (hasCustomProfiles && (segments[i].Contains ("profileId") || segments[i].Contains ("buildingMaterialId")))
            return CreateErrorResponse (APIERR_BADPARS,"Changing construction with custom per-instance profile memos is unsupported; retain the current profile.");
        const bool clone = !retained.insert (source).second;
        if (beam) {
            replacement.beamSegments[i] = old.beamSegments[source];
            if (clone) replacement.beamSegments[i].head.guid = APINULLGuid;
        } else {
            replacement.columnSegments[i] = old.columnSegments[source];
            if (clone) replacement.columnSegments[i].head.guid = APINULLGuid;
        }
        err = Configure (Data (e,replacement,i), segments[i], beam);
        if (err != NoError) return CreateErrorResponse (err,"Invalid segment settings or attribute reference. No changes applied.");
        GS::UniString lengthType; double value = 0;
        schemes[i].Get ("lengthType", lengthType); schemes[i].Get ("value", value);
        if (!std::isfinite (value) || value <= 0 || (lengthType != "Fixed" && lengthType != "Proportional")) return CreateErrorResponse (APIERR_BADPARS,"Invalid segment length scheme.");
        auto& scheme = replacement.assemblySegmentSchemes[i];
        if (lengthType == "Fixed") { scheme.lengthType = APIAssemblySegment_Fixed; scheme.fixedLength = value; }
        else { scheme.lengthType = APIAssemblySegment_Proportional; scheme.lengthProportion = value; proportion += value; hasProportion = true; }
    }
    if (hasProportion && !Near (proportion,1)) return CreateErrorResponse (APIERR_BADPARS,"Proportional segment lengths must sum to one.");
    for (UInt32 i=0; i<n+1; ++i) {
        GS::UniString type; cuts[i].Get ("type", type);
        auto& cut = replacement.assemblySegmentCuts[i];
        if (type == "Horizontal") cut.cutType = APIAssemblySegmentCut_Horizontal;
        else if (type == "Vertical") cut.cutType = APIAssemblySegmentCut_Vertical;
        else if (type == "Custom") {
            cut.cutType = APIAssemblySegmentCut_Custom;
            if (!cuts[i].Get ("angle", cut.customAngle) || !std::isfinite (cut.customAngle)) return CreateErrorResponse (APIERR_BADPARS,"Custom cut requires a finite angle in radians.");
        } else return CreateErrorResponse (APIERR_BADPARS,"Unknown cut type.");
        if (type != "Custom" && cuts[i].Contains ("angle")) return CreateErrorResponse (APIERR_BADPARS,"Angle is applicable only to custom cuts.");
    }
    API_Element mask = {}; ACAPI_ELEMENT_MASK_CLEAR (mask);
    if (beam) {
        e.beam.nSegments=n; e.beam.nSchemes=n; e.beam.nCuts=n+1;
        ACAPI_ELEMENT_MASK_SET (mask, API_BeamType, nSegments); ACAPI_ELEMENT_MASK_SET (mask, API_BeamType, nSchemes); ACAPI_ELEMENT_MASK_SET (mask, API_BeamType, nCuts);
    } else {
        e.column.nSegments=n; e.column.nSchemes=n; e.column.nCuts=n+1;
        ACAPI_ELEMENT_MASK_SET (mask, API_ColumnType, nSegments); ACAPI_ELEMENT_MASK_SET (mask, API_ColumnType, nSchemes); ACAPI_ELEMENT_MASK_SET (mask, API_ColumnType, nCuts);
    }
    const UInt64 editMask = (beam ? APIMemoMask_BeamSegment : APIMemoMask_ColumnSegment) | APIMemoMask_AssemblySegmentCut | APIMemoMask_AssemblySegmentScheme;
    err = ACAPI_CallUndoableCommand ("Set assembly segments", [&] () -> GSErrCode { return ACAPI_Element_Change (&e,&mask,&replacement,editMask,true); });
    if (err != NoError) return CreateErrorResponse (err,"Native assembly edit failed; inspect the target before retrying.");
    API_Element actual = {};
    err = Read (p,actual,actualMemo);
    bool matches = err == NoError && Count (actual) == n && SchemeCount (actual) == n && CutCount (actual) == n+1;
    if (matches) {
        for (UInt32 i=0; i<n; ++i) {
            matches = matches && Same (Data (e,replacement,i),Data (actual,actualMemo,i));
            const auto& a = replacement.assemblySegmentSchemes[i]; const auto& b = actualMemo.assemblySegmentSchemes[i];
            matches = matches && a.lengthType == b.lengthType && Near (a.lengthType == APIAssemblySegment_Fixed ? a.fixedLength : a.lengthProportion, b.lengthType == APIAssemblySegment_Fixed ? b.fixedLength : b.lengthProportion);
        }
        for (UInt32 i=0; i<n+1; ++i) {
            const auto& a = replacement.assemblySegmentCuts[i]; const auto& b = actualMemo.assemblySegmentCuts[i];
            matches = matches && a.cutType == b.cutType && (a.cutType != APIAssemblySegmentCut_Custom || Near (a.customAngle,b.customAngle));
        }
    }
    GS::ObjectState result = CreateElementIdObjectState (e.header.guid);
    result.Add ("success", matches); result.Add ("status", matches ? "applied" : "notConfirmed"); result.Add ("verification","nativeSegmentSettingsReadBack");
    if (err == NoError) result.Add ("actual",Describe (actual,actualMemo));
    else result.Add ("error",GS::ObjectState ("code",err,"message","Edit executed, but native readback failed. Do not blindly repeat."));
    return result;
}

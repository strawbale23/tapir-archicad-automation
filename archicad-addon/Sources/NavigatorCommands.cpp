#include "NavigatorCommands.hpp"
#include "MigrationHelper.hpp"
#include "Transformation2D.hpp"
#include "Matrix2.hpp"
#include "TM.h"
#include <cmath>
#include "GSProcessControl.hpp"
#include "ViewGDLParameters.hpp"

// Exact sibling resolution keeps repeatable setup scoped to its intended folder.
// Duplicate names and a same-name item of the wrong kind are never picked arbitrarily.
static GSErrCode FindViewMapSibling (API_Guid parentGuid, const GS::UniString& name, GS::Optional<API_NavigatorItem>& match)
{
    API_NavigatorItem parent = {};
    GSErrCode err = ACAPI_Navigator_GetNavigatorItem (&parentGuid, &parent);
    if (err != NoError) return err;
    if (parent.mapId != API_PublicViewMap) return APIERR_BADPARS;
    GS::Array<API_NavigatorItem> children;
    err = ACAPI_Navigator_GetNavigatorChildrenItems (&parent, &children);
    if (err != NoError) return err;
    for (const auto& child : children) {
        if (GS::UniString (child.uName) != name) continue;
        if (match.HasValue ()) return APIERR_BADPARS;
        match = child;
    }
    return NoError;
}

// Ownership follows the SDK Navigator_Test example. Keep a shallow allocation
// snapshot when a setter clears custom settings in favour of a named resource.
static void DisposeNavigatorViewData (API_NavigatorView& view)
{
    if (view.modelViewOpt != nullptr) {
        ACAPI_FreeGDLModelViewOptionsPtr (&view.modelViewOpt->gdlOptions);
        BMKillPtr (reinterpret_cast<GSPtr*> (&view.modelViewOpt));
    }
    delete view.layerStats;
    view.layerStats = nullptr;
    BMKillPtr (reinterpret_cast<GSPtr*> (&view.dimPrefs));
    BMKillPtr (reinterpret_cast<GSPtr*> (&view.pens));
}

#ifdef ServerMainVers_2900
// 19 September 2026, 15:48 CEST. Dimension preferences belong to this saved view;
// never borrow the active project's possibly unrelated current preferences.
template<class T> struct DimensionChoice { const char* name; T value; };
static const DimensionChoice<API_LengthTypeID> lengthUnits[] = {
    {"Meter",API_LengthTypeID::Meter},{"Decimeter",API_LengthTypeID::Decimeter},{"Centimeter",API_LengthTypeID::Centimeter},
    {"Millimeter",API_LengthTypeID::Millimeter},{"FootFracInch",API_LengthTypeID::FootFracInch},{"FootDecInch",API_LengthTypeID::FootDecInch},
    {"DecFoot",API_LengthTypeID::DecFoot},{"FracInch",API_LengthTypeID::FracInch},{"DecInch",API_LengthTypeID::DecInch},
    {"KiloMeter",API_LengthTypeID::KiloMeter},{"Yard",API_LengthTypeID::Yard}
};
static const DimensionChoice<API_AreaTypeID> areaUnits[] = {
    {"SquareMeter",API_AreaTypeID::SquareMeter},{"SquareKiloMeter",API_AreaTypeID::SquareKiloMeter},{"SquareDeciMeter",API_AreaTypeID::SquareDeciMeter},
    {"SquareCentimeter",API_AreaTypeID::SquareCentimeter},{"SquareMillimeter",API_AreaTypeID::SquareMillimeter},
    {"SquareFoot",API_AreaTypeID::SquareFoot},{"SquareInch",API_AreaTypeID::SquareInch},{"SquareYard",API_AreaTypeID::SquareYard}
};
static const DimensionChoice<API_AngleTypeID> angleUnits[] = {
    {"DecimalDegree",API_AngleTypeID::DecimalDegree},{"DegreeMinSec",API_AngleTypeID::DegreeMinSec},
    {"Grad",API_AngleTypeID::Grad},{"Radian",API_AngleTypeID::Radian},{"Surveyors",API_AngleTypeID::Surveyors}
};
static const DimensionChoice<API_ExtraAccuracyID> accuracies[] = {
    {"Off",APIExtAc_Off},{"Small5",APIExtAc_Small5},{"Small25",APIExtAc_Small25},
    {"Small1",APIExtAc_Small1},{"Small01",APIExtAc_Small01},{"Fractions",APIExtAc_Fractions}
};
template<class T, size_t N> static bool PatchDimensionChoice (const GS::ObjectState& input,const char* key,T& value,const DimensionChoice<T> (&choices)[N])
{
    GS::UniString name;
    if (!input.Get(key,name)) return !input.Contains(key);
    for (const auto& choice:choices) if (name==choice.name) { value=choice.value; return true; }
    return false;
}
template<class T, size_t N> static GS::UniString DimensionChoiceName (T value,const DimensionChoice<T> (&choices)[N])
{
    for (const auto& choice:choices) if (value==choice.value) return choice.name;
    return "Unknown";
}
static bool PatchDimensionDecimals (const GS::ObjectState& input,const char* key,short& value)
{
    Int32 number=0;
    if (!input.Get(key,number)) return !input.Contains(key);
    if (number<0 || number>4) return false;
    value=static_cast<short>(number); return true;
}
static bool PatchLengthDimension (const GS::ObjectState& input,API_LengthDimFormat& format)
{
    if (!PatchDimensionChoice(input,"unit",format.unit,lengthUnits) || !PatchDimensionChoice(input,"extraAccuracy",format.showSmall5,accuracies) ||
        !PatchDimensionDecimals(input,"decimals",format.lenDecimals)) return false;
    Int32 fraction=0;
    if (input.Get("roundInch",fraction)) {
        if (fraction!=1 && fraction!=2 && fraction!=4 && fraction!=8 && fraction!=16 && fraction!=32 && fraction!=64) return false;
        format.roundInch=static_cast<short>(fraction);
    }
    input.Get("showLeadingZero",format.show0Whole); input.Get("hideTrailingZeros",format.hide0Dec);
    bool inch=false; if (input.Get("showZeroInches",inch)) format.show0Inch=inch ? 1 : 0;
    return true;
}
static GS::ObjectState ReadLengthDimension (const API_LengthDimFormat& format)
{
    return GS::ObjectState("unit",DimensionChoiceName(format.unit,lengthUnits),"decimals",static_cast<Int32>(format.lenDecimals),
        "roundInch",static_cast<Int32>(format.roundInch),"extraAccuracy",DimensionChoiceName(format.showSmall5,accuracies),
        "showLeadingZero",format.show0Whole,"hideTrailingZeros",format.hide0Dec,"showZeroInches",format.show0Inch!=0);
}
static GS::ObjectState ReadDimensionPreferences (const API_DimensionPrefs& dimensions)
{
    return GS::ObjectState("linear",ReadLengthDimension(dimensions.linear),"radial",ReadLengthDimension(dimensions.radial),
        "level",ReadLengthDimension(dimensions.level),"elevation",ReadLengthDimension(dimensions.elevation),
        "doorWindow",ReadLengthDimension(dimensions.doorwindow),"sillHeight",ReadLengthDimension(dimensions.parapet),
        "angle",GS::ObjectState("unit",DimensionChoiceName(dimensions.angle.unit,angleUnits),"decimals",static_cast<Int32>(dimensions.angle.angleDecimals),"hideTrailingZeros",dimensions.angle.hide0Dec),
        "area",GS::ObjectState("unit",DimensionChoiceName(dimensions.area.unit,areaUnits),"decimals",static_cast<Int32>(dimensions.area.lenDecimals),"hideTrailingZeros",dimensions.area.hide0Dec));
}
static GSErrCode PrepareViewDimensions (const GS::ObjectState& settings,const API_NavigatorView& original,API_DimensionPrefs& dimensions)
{
    const auto* custom=settings.Get("customDimensions");
    if (custom==nullptr) return NoError;
    GS::UniString named; settings.Get("dimensionStyle",named);
    bool save=true; settings.Get("saveDim",save);
    if (!named.IsEmpty() || !save) return APIERR_BADPARS;
    GS::UniString source;
    const bool explicitSource=custom->Get("sourceStandardName",source);
    if (explicitSource && source.IsEmpty()) return APIERR_BADPARS;
    if (!explicitSource && original.saveDim && original.dimName[0]==0 && original.dimPrefs!=nullptr) dimensions=*original.dimPrefs;
    else {
        if (!explicitSource) { if (!original.saveDim || original.dimName[0]==0) return APIERR_BADPARS; source=GS::UniString(original.dimName,CC_UTF8); }
        API_DimensionStandardsType standard={}; standard.head.uniStringNamePtr=&source;
        const auto error=ACAPI_Navigator_DimStand_Get(standard);
        if (error!=NoError) return error;
        dimensions=standard.dim;
    }
    struct LengthMember { const char* name; API_LengthDimFormat API_DimensionPrefs::*member; };
    const LengthMember lengthMembers[]={{"linear",&API_DimensionPrefs::linear},{"radial",&API_DimensionPrefs::radial},{"level",&API_DimensionPrefs::level},
        {"elevation",&API_DimensionPrefs::elevation},{"doorWindow",&API_DimensionPrefs::doorwindow},{"sillHeight",&API_DimensionPrefs::parapet}};
    for (const auto& field:lengthMembers) if (const auto* patch=custom->Get(field.name)) if (!PatchLengthDimension(*patch,dimensions.*field.member)) return APIERR_BADPARS;
    if (const auto* patch=custom->Get("angle")) {
        if (!PatchDimensionChoice(*patch,"unit",dimensions.angle.unit,angleUnits) || !PatchDimensionDecimals(*patch,"decimals",dimensions.angle.angleDecimals)) return APIERR_BADPARS;
        patch->Get("hideTrailingZeros",dimensions.angle.hide0Dec);
    }
    if (const auto* patch=custom->Get("area")) {
        if (!PatchDimensionChoice(*patch,"unit",dimensions.area.unit,areaUnits) || !PatchDimensionDecimals(*patch,"decimals",dimensions.area.lenDecimals)) return APIERR_BADPARS;
        patch->Get("hideTrailingZeros",dimensions.area.hide0Dec);
    }
    dimensions.index=0;
    return NoError;
}

static const DimensionChoice<char> openingDisplays[]={{"ShowWithDimensions",API_Hole_ShowWithDim},{"ShowOnPlan",API_Hole_ShowOnPlan},
    {"HideOnPlan",API_Hole_HideOnPlan},{"ReflectedCeiling",API_Hole_ReflCeiling},{"HideOpening",API_Hole_HideHole}};
static const DimensionChoice<char> sectionDisplays[]={{"Normal",API_Section_Marker_Normal},{"KeyPlan",API_Section_Marker_KeyPlan},{"AsInSettings",API_Section_Marker_AsInSettings}};
static const DimensionChoice<char> beamDisplays[]={{"Full",API_Beam_Drawing_Full},{"ReferenceLine",API_Beam_Drawing_RefLine},{"Contour",API_Beam_Drawing_Contour}};
static const DimensionChoice<char> roofDisplays[]={{"AllDetails",API_RoofShellShow_AllDetails},{"TopSurface",API_RoofShellShow_TopSurface},{"Contour",API_RoofShellShow_ContourDrawing}};
static const DimensionChoice<char> slabDisplays[]={{"Show",API_Slab_ShowCommonEdges},{"Eliminate",API_Slab_EliminateCommonEdges},{"Hidden",API_Slab_HiddenCommonEdges}};
static GS::ObjectState ReadCustomModelOptions (const API_ModelViewOptions& options)
{
    GS::ObjectState result("doors",DimensionChoiceName(options.doorMode,openingDisplays),"windows",DimensionChoiceName(options.windowMode,openingDisplays),
        "skylights",DimensionChoiceName(options.skylightMode,openingDisplays),"sectionMarkers",DimensionChoiceName(options.sectionMarker,sectionDisplays),
        "beams",DimensionChoiceName(options.beamMethod,beamDisplays),"roofs",DimensionChoiceName(options.roofShellMode,roofDisplays),
        "slabCommonEdges",DimensionChoiceName(options.slabMethod,slabDisplays),"showColumnSymbols",options.showColumnSymbol==API_Column_Symbol_Show,
        "columnHiddenLinesUnderSlabs",options.coluHiddenlineUnderSlabs,"beamHiddenLinesUnderSlabs",options.beamHiddenlineUnderSlabs,"hideZoneStamps",options.zoneHideZoneStamp);
    result.Add("gdlParameterDetails",ViewGDL::Read(options.gdlOptions));
    return result;
}
static GSErrCode PrepareViewModelOptions (const GS::ObjectState& settings,const API_NavigatorView& original,API_ModelViewOptions& options,bool& ownsGDL)
{
    const auto* custom=settings.Get("customModelViewOptions");
    if (custom==nullptr) return NoError;
    GS::UniString named; settings.Get("modelViewOptions",named);
    bool save=true; settings.Get("saveDispOpt",save);
    if (!named.IsEmpty() || !save) return APIERR_BADPARS;
    GS::UniString source;
    const bool explicitSource=custom->Get("sourceName",source);
    if (explicitSource && source.IsEmpty()) return APIERR_BADPARS;
    if (!explicitSource && original.saveDispOpt && original.modelViewOptName[0]==0 && original.modelViewOpt!=nullptr) options=*original.modelViewOpt;
    else {
        if (!explicitSource) { if (!original.saveDispOpt || original.modelViewOptName[0]==0) return APIERR_BADPARS; source=GS::UniString(original.modelViewOptName,CC_UTF8); }
        API_ModelViewOptionsType stored={}; stored.head.uniStringNamePtr=&source;
        const auto error=ACAPI_Navigator_ModelViewOptions_Get(&stored);
        options=stored.modelViewOpt; ownsGDL=true;
        if (error!=NoError) return error;
    }
    if (!PatchDimensionChoice(*custom,"doors",options.doorMode,openingDisplays) || !PatchDimensionChoice(*custom,"windows",options.windowMode,openingDisplays) ||
        !PatchDimensionChoice(*custom,"skylights",options.skylightMode,openingDisplays) || !PatchDimensionChoice(*custom,"sectionMarkers",options.sectionMarker,sectionDisplays) ||
        !PatchDimensionChoice(*custom,"beams",options.beamMethod,beamDisplays) || !PatchDimensionChoice(*custom,"roofs",options.roofShellMode,roofDisplays) ||
        !PatchDimensionChoice(*custom,"slabCommonEdges",options.slabMethod,slabDisplays)) return APIERR_BADPARS;
    bool show=false; if (custom->Get("showColumnSymbols",show)) options.showColumnSymbol=show ? API_Column_Symbol_Show : API_Column_Symbol_Hide;
    custom->Get("columnHiddenLinesUnderSlabs",options.coluHiddenlineUnderSlabs); custom->Get("beamHiddenLinesUnderSlabs",options.beamHiddenlineUnderSlabs);
    custom->Get("hideZoneStamps",options.zoneHideZoneStamp);
    return ViewGDL::Patch(*custom,options.gdlOptions);
}
#endif

// 19 September 2026, 15:17 CEST. Custom view data is borrowed from request-local
// containers for the synchronous SDK write; the original view keeps its allocations.
#ifdef ServerMainVers_2700
static GSErrCode ResolveViewResource (const GS::ObjectState* explicitId,const char* name,API_AttrTypeID type,API_Attribute& attribute)
{
    attribute={}; attribute.header.typeID=type;
    GS::UniString sourceName(name);
    if (explicitId!=nullptr) {
        attribute.header.guid=GetGuidFromAttributesArrayItem(*explicitId);
        if (attribute.header.guid==APINULLGuid) return APIERR_BADID;
    } else {
        if (sourceName.IsEmpty()) return APIERR_BADPARS;
        attribute.header.uniStringNamePtr=&sourceName;
    }
    const auto err=ACAPI_Attribute_Get(&attribute);
    attribute.header.uniStringNamePtr=nullptr;
    return err;
}
static GSErrCode PrepareCustomViewSettings (const GS::ObjectState& settings,API_NavigatorView& view,
    GS::HashTable<API_AttributeIndex,API_LayerStat>& layers,GS::Array<API_Pen>& pens,GS::UniString& message)
{
    if (const auto* custom=settings.Get("customLayers")) {
        message="Cannot prepare custom view layers. Use a valid source combination or readable saved view settings; layer identities must be unique.";
        GS::UniString named; settings.Get("layerCombination",named);
        bool save=true; settings.Get("saveLaySet",save);
        if (!named.IsEmpty() || !save) return APIERR_BADPARS;
        const auto* source=custom->Get("sourceAttributeId");
        if (source==nullptr && view.saveLaySet && view.layerCombination[0]==0 && view.layerStats!=nullptr) layers=*view.layerStats;
        else {
            if (source==nullptr && !view.saveLaySet) return APIERR_BADPARS;
            API_Attribute attribute={};
            auto err=ResolveViewResource(source,view.layerCombination,API_LayerCombID,attribute);
            if (err!=NoError) return err;
            API_AttributeDef definition={};
            const GS::OnExit dispose([&] { ACAPI_DisposeAttrDefsHdls(&definition); });
            err=ACAPI_Attribute_GetDef(API_LayerCombID,attribute.header.index,&definition);
            if (err!=NoError || definition.layer_statItems==nullptr) return err==NoError ? APIERR_GENERAL : err;
            layers=*definition.layer_statItems;
        }
        if (layers.GetSize()>10000) return APIERR_BADPARS;
        GS::Array<GS::ObjectState> patches;
        if (!custom->Get("layers",patches) || patches.GetSize()>10000) return APIERR_BADPARS;
        GS::HashSet<API_AttributeIndex> seen;
        for (const auto& patch:patches) {
            API_Attribute layer={}; layer.header.typeID=API_LayerID; layer.header.guid=GetGuidFromAttributesArrayItem(patch);
            if (layer.header.guid==APINULLGuid) return APIERR_BADID;
            auto err=ACAPI_Attribute_Get(&layer);
            if (err!=NoError) return err;
            const auto index=layer.header.index;
            if (seen.Contains(index)) return APIERR_BADPARS;
            seen.Add(index);
            API_LayerStat state={};
            if (layers.ContainsKey(index)) state=layers.Get(index);
            else { state.lFlags=layer.header.flags; state.conClassId=layer.layer.conClassId; }
            bool value=false;
            if (patch.Get("isHidden",value)) { if (value) state.lFlags|=APILay_Hidden; else state.lFlags&=~APILay_Hidden; }
            if (patch.Get("isLocked",value)) { if (value) state.lFlags|=APILay_Locked; else state.lFlags&=~APILay_Locked; }
            if (patch.Get("isWireframe",value)) { if (value) state.lFlags|=APILay_ForceToWire; else state.lFlags&=~APILay_ForceToWire; }
            Int32 group=0;
            if (patch.Get("intersectionGroupNr",group)) { if (group<0 || group>32767) return APIERR_BADPARS; state.conClassId=group; }
            layers.Put(index,state);
        }
        view.layerCombination[0]=0; view.layerStats=&layers; view.saveLaySet=true;
    }
    if (const auto* custom=settings.Get("customPens")) {
        message="Cannot prepare custom view pens. Use a valid source pen table or readable saved pens; pen indices must be unique and values representable.";
        GS::UniString named; settings.Get("penSetName",named);
        bool save=true; settings.Get("savePenSet",save);
        if (!named.IsEmpty() || !save) return APIERR_BADPARS;
        const auto* source=custom->Get("sourceAttributeId");
        if (source==nullptr && view.savePenSet && view.penSetName[0]==0 && view.pens!=nullptr) pens=*view.pens;
        else {
            if (source==nullptr && !view.savePenSet) return APIERR_BADPARS;
            API_Attribute attribute={};
            auto err=ResolveViewResource(source,view.penSetName,API_PenTableID,attribute);
            if (err!=NoError) return err;
            API_AttributeDefExt definition={};
            const GS::OnExit dispose([&] { ACAPI_DisposeAttrDefsHdlsExt(&definition); });
            err=ACAPI_Attribute_GetDefExt(API_PenTableID,attribute.header.index,&definition);
            if (err!=NoError || definition.penTable_Items==nullptr) return err==NoError ? APIERR_GENERAL : err;
            pens=*definition.penTable_Items;
        }
        if (pens.GetSize()!=255) return APIERR_GENERAL;
        for (short i=0;i<255;++i) if (pens[i].index!=i+1) return APIERR_GENERAL;
        GS::Array<GS::ObjectState> patches;
        if (!custom->Get("pens",patches) || patches.GetSize()>255) return APIERR_BADPARS;
        bool seen[256]={};
        for (const auto& patch:patches) {
            Int32 index=0;
            if (!patch.Get("index",index) || index<1 || index>255 || seen[index]) return APIERR_BADPARS;
            seen[index]=true;
            auto& pen=pens[index-1];
            GetColor(patch,"color",pen.rgb); patch.Get("width",pen.width);
            if (!std::isfinite(pen.width) || pen.width<0 || !std::isfinite(pen.rgb.f_red) || !std::isfinite(pen.rgb.f_green) || !std::isfinite(pen.rgb.f_blue) ||
                pen.rgb.f_red<0 || pen.rgb.f_red>1 || pen.rgb.f_green<0 || pen.rgb.f_green>1 || pen.rgb.f_blue<0 || pen.rgb.f_blue>1) return APIERR_BADPARS;
            GS::UniString description;
            if (patch.Get("description",description)) {
                SetCharProperty(&patch,"description",pen.description);
                if (GS::UniString(pen.description)!=description) return APIERR_BADPARS;
            }
        }
        view.penSetName[0]=0; view.pens=&pens; view.savePenSet=true;
    }
    return NoError;
}
static GSErrCode ReadCustomViewSettings (const API_NavigatorView& view,GS::ObjectState& result)
{
#ifdef ServerMainVers_2900
    if (view.saveDispOpt && view.modelViewOptName[0]==0) {
        if (view.modelViewOpt==nullptr) return APIERR_GENERAL;
        result.Add("customModelViewOptions",ReadCustomModelOptions(*view.modelViewOpt));
    }
    if (view.saveDispOpt && view.modelViewOptName[0]!=0) {
        API_ModelViewOptionsType stored={}; GS::UniString name(view.modelViewOptName,CC_UTF8);
        stored.head.uniStringNamePtr=&name;
        const GS::OnExit dispose([&] { ACAPI_FreeGDLModelViewOptionsPtr(&stored.modelViewOpt.gdlOptions); });
        const auto err=ACAPI_Navigator_ModelViewOptions_Get(&stored);
        if (err!=NoError) return err;
        auto resolved=ReadCustomModelOptions(stored.modelViewOpt);
        resolved.Add("sourceName",name);
        result.Add("resolvedModelViewOptions",resolved);
    }
    if (view.saveDim && view.dimName[0]==0) {
        if (view.dimPrefs==nullptr) return APIERR_GENERAL;
        result.Add("customDimensions",ReadDimensionPreferences(*view.dimPrefs));
    }
#endif
    if (view.saveLaySet && view.layerCombination[0]==0) {
        if (view.layerStats==nullptr || view.layerStats->GetSize()>10000) return APIERR_GENERAL;
        GS::ObjectState custom;
        GS::Array<GS::ObjectState> rows;
        for (const auto& entry:*view.layerStats) {
#ifdef ServerMainVers_2800
            const auto index=entry.key; const auto& state=entry.value;
#else
            const auto index=*entry.key; const auto& state=*entry.value;
#endif
            API_Attribute layer={}; layer.header.typeID=API_LayerID; layer.header.index=index;
            const auto err=ACAPI_Attribute_Get(&layer);
            if (err!=NoError) return err;
            auto row=CreateAttributeIdObjectState(layer.header.guid);
            row.Add("isHidden",(state.lFlags&APILay_Hidden)!=0);
            row.Add("isLocked",(state.lFlags&APILay_Locked)!=0);
            row.Add("isWireframe",(state.lFlags&APILay_ForceToWire)!=0);
            row.Add("intersectionGroupNr",state.conClassId);
            rows.Push(row);
        }
        custom.Add("layers",rows); result.Add("customLayers",custom);
    }
    if (view.savePenSet && view.penSetName[0]==0) {
        if (view.pens==nullptr || view.pens->GetSize()!=255) return APIERR_GENERAL;
        GS::ObjectState custom;
        const auto& add=custom.AddList<GS::ObjectState>("pens");
        for (const auto& pen:*view.pens) add(GS::ObjectState("index",static_cast<Int32>(pen.index),"width",pen.width,"description",GS::UniString(pen.description),
            "color",GS::ObjectState("red",pen.rgb.f_red,"green",pen.rgb.f_green,"blue",pen.rgb.f_blue)));
        result.Add("customPens",custom);
    }
    return NoError;
}
#endif

static GS::HashTable<GS::UniString, API_Guid> GetPublisherSetNameGuidTable()
{
    GS::HashTable<GS::UniString, API_Guid> table;

    Int32 numberOfPublisherSets = 0;
    ACAPI_Navigator_GetNavigatorSetNum(&numberOfPublisherSets);

    API_NavigatorSet set = {};
    for (Int32 ii = 0; ii < numberOfPublisherSets; ++ii) {
        set.mapId = API_PublisherSets;
        GSErrCode err = ACAPI_Navigator_GetNavigatorSet(&set, &ii);
        if (err == NoError) {
            table.Add(set.name, set.rootGuid);
        }
    }

    return table;
}

PublishPublisherSetCommand::PublishPublisherSetCommand() :
    CommandBase(CommonSchema::Used)
{
}

GS::String PublishPublisherSetCommand::GetName() const
{
    return "PublishPublisherSet";
}

GS::Optional<GS::UniString> PublishPublisherSetCommand::GetInputParametersSchema() const
{
    return R"({
        "type": "object",
        "properties": {
            "publisherSetName": {
                "type": "string",
                "description": "The name of the publisher set.",
                "minLength": 1
            },
            "outputPath": {
                "type": "string",
                "description": "Full local or LAN path for publishing. Optional, by default the path set in the settings of the publisher set will be used.",
                "minLength": 1
            },
            "selectedNavigatorItemIds": {
                "$ref": "#/NavigatorItemIds",
                "description": "Optional publisher-tree navigator items to publish instead of the whole publisher set."
            }
        },
        "additionalProperties": false,
        "required": [
            "publisherSetName"
        ]
    })";
}

GS::ObjectState PublishPublisherSetCommand::Execute(const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::UniString publisherSetName;
    parameters.Get("publisherSetName", publisherSetName);

    const auto publisherSetNameGuidTable = GetPublisherSetNameGuidTable();
    if (!publisherSetNameGuidTable.ContainsKey(publisherSetName)) {
        return CreateErrorResponse(Error, "Not valid publisher set name.");
    }

    API_PublishPars publishPars = {};
    publishPars.guid = publisherSetNameGuidTable.Get(publisherSetName);

    if (parameters.Contains("outputPath")) {
        GS::UniString outputPath;
        parameters.Get("outputPath", outputPath);
        publishPars.path = new IO::Location(outputPath);
    }

    GS::Array<API_Guid> selectedLinks;
    const GS::Array<API_Guid>* selectedLinksPtr = nullptr;
    if (parameters.Contains("selectedNavigatorItemIds")) {
        GS::Array<GS::ObjectState> selectedNavigatorItemIds;
        parameters.Get("selectedNavigatorItemIds", selectedNavigatorItemIds);

        for (const GS::ObjectState& navigatorItemIdArrayItem : selectedNavigatorItemIds) {
            const API_Guid selectedGuid = GetGuidFromNavigatorItemIdArrayItem(navigatorItemIdArrayItem);
            if (selectedGuid == APINULLGuid) {
                delete publishPars.path;
                return CreateErrorResponse(APIERR_BADPARS, "selectedNavigatorItemId is corrupt or missing.");
            }
            selectedLinks.Push(selectedGuid);
        }

        if (!selectedLinks.IsEmpty()) {
            selectedLinksPtr = &selectedLinks;
        }
    }

    GSErrCode err = ACAPI_ProjectOperation_Publish(&publishPars, selectedLinksPtr);
    delete publishPars.path;

    if (err != NoError) {
        return CreateErrorResponse(err, "Publishing failed. Check output path!");
    }

    return {};
}

UpdateDrawingsCommand::UpdateDrawingsCommand() :
    CommandBase(CommonSchema::Used)
{
}

GS::String UpdateDrawingsCommand::GetName() const
{
    return "UpdateDrawings";
}

GS::Optional<GS::UniString> UpdateDrawingsCommand::GetInputParametersSchema() const
{
    return R"({
    "type": "object",
    "properties": {
        "elements": {
            "$ref": "#/Elements"
        }
    },
    "additionalProperties": false,
    "required": [
        "elements"
    ]
})";
}

GS::Optional<GS::UniString> UpdateDrawingsCommand::GetRawResponseSchema() const
{
    return R"({
        "$ref": "#/ExecutionResult"
    })";
}

GS::ObjectState UpdateDrawingsCommand::Execute(const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
#ifdef ServerMainVers_2700
    GS::Array<GS::ObjectState> elements;
    parameters.Get("elements", elements);

    const GS::Array<API_Guid> elemIds = elements.Transform<API_Guid>(GetGuidFromElementsArrayItem);
    GSErrCode err = ACAPI_Drawing_Update_Drawings(elemIds);

    return err == NoError
        ? CreateSuccessfulExecutionResult()
        : CreateFailedExecutionResult(err, "Failed to update drawings.");
#else
    (void) parameters;
    return CreateFailedExecutionResult (APIERR_NOTSUPPORTED, "DrawingUpdateCommand is not supported in Archicad versions earlier than 27.");
#endif
}

GetDatabaseIdFromNavigatorItemIdCommand::GetDatabaseIdFromNavigatorItemIdCommand () :
    CommandBase (CommonSchema::Used)
{}

GS::String GetDatabaseIdFromNavigatorItemIdCommand::GetName () const
{
    return "GetDatabaseIdFromNavigatorItemId";
}

GS::Optional<GS::UniString> GetDatabaseIdFromNavigatorItemIdCommand::GetInputParametersSchema () const
{
    return R"({
    "type": "object",
    "properties": {
        "navigatorItemIds": {
            "$ref": "#/NavigatorItemIds"
        }
    },
    "additionalProperties": false,
    "required": [
        "navigatorItemIds"
    ]
})";
}

GS::Optional<GS::UniString> GetDatabaseIdFromNavigatorItemIdCommand::GetRawResponseSchema () const
{
    return R"({
    "type": "object",
    "properties": {
        "databases": {
            "$ref": "#/Databases"
        }
    },
    "additionalProperties": false,
    "required": [
        "databases"
    ]
})";
}

GS::ObjectState GetDatabaseIdFromNavigatorItemIdCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<GS::ObjectState> navigatorItemIds;
    parameters.Get ("navigatorItemIds", navigatorItemIds);

    GS::ObjectState response;
    const auto& databases = response.AddList<GS::ObjectState> ("databases");

    for (const GS::ObjectState& navigatorItemIdArrayItem : navigatorItemIds) {
        API_Guid navGuid = GetGuidFromNavigatorItemIdArrayItem (navigatorItemIdArrayItem);
        if (navGuid == APINULLGuid) {
            databases (CreateErrorResponse (APIERR_BADPARS, "navigatorItemId is corrupt or missing"));
            continue;
        }

        API_NavigatorItem navigatorItem = {};
        GSErrCode err = ACAPI_Navigator_GetNavigatorItem (&navGuid, &navigatorItem);

        if (err != NoError) {
            databases (CreateErrorResponse (err, "Failed to get navigator item from guid"));
            continue;
        }

        const API_Guid databaseGuid = DatabaseIdResolver::Instance ().GetIdOfDatabase (navigatorItem.db);

        if (databaseGuid == APINULLGuid) {
            databases (CreateErrorResponse (APIERR_BADPARS, "Navigator item {navigatorItem.itemType} has no associated database"));
            continue;
        }

        databases (CreateDatabaseIdObjectState (databaseGuid));
    }
    return response;
}

GetModelViewOptionsCommand::GetModelViewOptionsCommand () :
    CommandBase (CommonSchema::Used)
{}

GS::String GetModelViewOptionsCommand::GetName () const
{
    return "GetModelViewOptions";
}

GS::Optional<GS::UniString> GetModelViewOptionsCommand::GetRawResponseSchema () const
{
    return R"({
    "type": "object",
    "properties": {
        "modelViewOptions": {
            "type": "array",
            "items": {
                "type": "object",
                "description": "Represents the model view options.",
                "properties": {
                    "name": {
                        "type": "string"
                    }
                },
                "additionalProperties": false,
                "required": [
                    "name"
                ]
            }
        }
    },
    "additionalProperties": false,
    "required": [
        "modelViewOptions"
    ]
})";
}

GS::ObjectState GetModelViewOptionsCommand::Execute (const GS::ObjectState& /*parameters*/, GS::ProcessControl& /*processControl*/) const
{
    GS::ObjectState response;
    const auto& modelViewOptions = response.AddList<GS::ObjectState> ("modelViewOptions");

#ifdef ServerMainVers_2700
    UInt32 count = 0;
    ACAPI_Navigator_ModelViewOptions_GetNum (count);

    for (UInt32 i = 1; i <= count; ++i) {
        GS::UniString name;
        API_ModelViewOptionsType modelViewOption = {};
        modelViewOption.head.index = i;
        modelViewOption.head.uniStringNamePtr = &name;

        const GS::OnExit disposeOptions([&] { ACAPI_FreeGDLModelViewOptionsPtr(&modelViewOption.modelViewOpt.gdlOptions); });
        if (ACAPI_Navigator_ModelViewOptions_Get (&modelViewOption) == NoError) {
            modelViewOptions (GS::ObjectState ("name", name));
        }
    }
#else
    API_AttributeIndex count = 0;
    ACAPI_Attribute_GetNum (API_ModelViewOptionsID, &count);

    for (API_AttributeIndex i = 1; i <= count; ++i) {
        GS::UniString name;
        API_Attribute attr = {};
        attr.header.typeID = API_ModelViewOptionsID;
        attr.header.index = i;
        attr.header.uniStringNamePtr = &name;

        if (ACAPI_Attribute_Get (&attr) == NoError) {
            modelViewOptions (GS::ObjectState ("name", name));
        }
    }
#endif

    return response;
}

GetViewSettingsCommand::GetViewSettingsCommand () :
    CommandBase (CommonSchema::Used)
{}

GS::String GetViewSettingsCommand::GetName () const
{
    return "GetViewSettings";
}

GS::Optional<GS::UniString> GetViewSettingsCommand::GetInputParametersSchema () const
{
    return R"({
    "type": "object",
    "properties": {
        "navigatorItemIds": {
            "$ref": "#/NavigatorItemIds"
        },
        "includeCustomSettings": {
            "type": "boolean",
            "description": "Default false. Archicad 27+: include stored custom layer states and all 255 custom pens. Up to 20 views per such request; named resources remain identified by name."
        }
    },
    "additionalProperties": false,
    "required": [
        "navigatorItemIds"
    ]
})";
}

GS::Optional<GS::UniString> GetViewSettingsCommand::GetRawResponseSchema () const
{
    return R"({
    "type": "object",
    "properties": {
        "viewSettings": {
            "type": "array",
            "items": {
                "$ref": "#/ViewSettingsOrError"
            }
        }
    },
    "additionalProperties": false,
    "required": [
        "viewSettings"
    ]
})";
}

GS::ObjectState GetViewSettingsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<GS::ObjectState> navigatorItemIds;
    parameters.Get ("navigatorItemIds", navigatorItemIds);
    bool includeCustom=false; parameters.Get("includeCustomSettings",includeCustom);
    if (includeCustom && navigatorItemIds.GetSize()>20) return CreateErrorResponse(APIERR_BADPARS,"Read custom settings for at most 20 views at a time.");
#ifndef ServerMainVers_2700
    if (includeCustom) return CreateErrorResponse(APIERR_BADPARS,"Custom settings inspection requires Archicad 27 or newer.");
#endif

    GS::ObjectState response;
    const auto& viewSettings = response.AddList<GS::ObjectState> ("viewSettings");

    for (const GS::ObjectState& navigatorItemIdArrayItem : navigatorItemIds) {
        API_NavigatorItem navigatorItem = {};
        navigatorItem.guid = GetGuidFromNavigatorItemIdArrayItem (navigatorItemIdArrayItem);
        if (navigatorItem.guid == APINULLGuid) {
            viewSettings (CreateErrorResponse (APIERR_BADPARS, "navigatorItemId is corrupt or missing"));
            continue;
        }

        navigatorItem.mapId = API_PublicViewMap;

        API_NavigatorView navigatorView = {};
        GSErrCode err = ACAPI_Navigator_GetNavigatorView (&navigatorItem, &navigatorView);
        API_NavigatorView allocatedViewData = navigatorView;
        const GS::OnExit viewGuard ([&] () { DisposeNavigatorViewData (allocatedViewData); });
        if (err != NoError) {
            viewSettings (CreateErrorResponse (err, "Failed to get view settings from the navigator item, probably it's not a view"));
            continue;
        }

        GS::UniString graphicOverrideCombinationName (navigatorView.overrideCombination);
        GS::ObjectState viewSetting (
            "modelViewOptions", navigatorView.modelViewOptName,
            "layerCombination", navigatorView.layerCombination,
            "dimensionStyle", navigatorView.dimName,
            "penSetName", navigatorView.penSetName,
            "graphicOverrideCombination", graphicOverrideCombinationName,
            "drawingScale", navigatorView.drawingScale,
            "saveZoom", navigatorView.saveZoom,
            "ignoreSavedZoom", navigatorView.ignoreSavedZoom);

#ifdef ServerMainVers_2700
        if (includeCustom) {
            const auto customError=ReadCustomViewSettings(navigatorView,viewSetting);
            if (customError!=NoError) { viewSettings(CreateErrorResponse(customError,"Cannot read the saved custom settings or resolve this view's named model view options.")); continue; }
        }
#endif

        viewSetting.Add ("saveDispOpt", navigatorView.saveDispOpt);
        viewSetting.Add ("saveLaySet", navigatorView.saveLaySet);
        viewSetting.Add ("saveDScale", navigatorView.saveDScale);
        viewSetting.Add ("saveDim", navigatorView.saveDim);
        viewSetting.Add ("savePenSet", navigatorView.savePenSet);
        viewSetting.Add ("saveStructureDisplay", navigatorView.saveStructureDisplay);

        if (navigatorView.saveZoom) {
            viewSetting.Add ("zoom", GS::ObjectState (
                "xMin", navigatorView.zoom.xMin,
                "yMin", navigatorView.zoom.yMin,
                "xMax", navigatorView.zoom.xMax,
                "yMax", navigatorView.zoom.yMax));
        }

        {
            Vector2D vec2D1 (navigatorView.tr.tmx[0], navigatorView.tr.tmx[4]);
            Vector2D vec2D2 (navigatorView.tr.tmx[1], navigatorView.tr.tmx[5]);
            Vector2D offset (navigatorView.tr.tmx[3], navigatorView.tr.tmx[7]);
            Geometry::Matrix22 rotMatrix;
            Geometry::Matrix22::ColVectorsMatrix (vec2D1, vec2D2, rotMatrix);
            Geometry::Transformation2D trafo;
            trafo.SetMatrix (rotMatrix);
            trafo.SetOffset (offset);
            double rotAngle = 0.0;
            Geometry::TranAngle (trafo, &rotAngle);
            viewSetting.Add ("rotation", rotAngle);
        }

        const char* structureDisplayStr = "EntireStructure";
        switch (navigatorView.structureDisplay) {
            case API_CoreOnly:        structureDisplayStr = "CoreOnly";        break;
            case API_WithoutFinishes: structureDisplayStr = "WithoutFinishes"; break;
            case API_StructureOnly:   structureDisplayStr = "StructureOnly";   break;
            default: break;
        }
        viewSetting.Add ("structureDisplay", GS::UniString (structureDisplayStr));

        if (navigatorView.renovationFilterGuid != APINULLGuid) {
            viewSetting.Add ("renovationFilterGuid", CreateGuidObjectState (navigatorView.renovationFilterGuid));
        }

        GS::UniString d3styleName (navigatorView.d3styleName);
        viewSetting.Add ("d3styleName", d3styleName);

        GS::UniString renderingSceneName (navigatorView.renderingSceneName);
        viewSetting.Add ("renderingSceneName", renderingSceneName);

        viewSetting.Add ("usePhotoRendering", navigatorView.usePhotoRendering);

        viewSettings (viewSetting);
    }

    return response;
}

SetViewSettingsCommand::SetViewSettingsCommand () :
    CommandBase (CommonSchema::Used)
{}

GS::String SetViewSettingsCommand::GetName () const
{
    return "SetViewSettings";
}

GS::Optional<GS::UniString> SetViewSettingsCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "navigatorItemIdsWithViewSettings": {
                "type": "array",
                "items": {
                    "type": "object",
                    "properties": {
                        "navigatorItemId": {
                            "$ref": "#/NavigatorItemId"
                        },
                        "viewSettings": {
                            "$ref": "#/ViewSettings"
                        }
                    },
                    "additionalProperties": false,
                    "required": [
                        "navigatorItemId",
                        "viewSettings"
                    ]
                }
            }
        },
        "additionalProperties": false,
        "required": [
            "navigatorItemIdsWithViewSettings"
        ]
    })";
}

GS::Optional<GS::UniString> SetViewSettingsCommand::GetRawResponseSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "executionResults": {
                "$ref": "#/ExecutionResults"
            }
        },
        "additionalProperties": false,
        "required": [
            "executionResults"
        ]
    })";
}

GS::ObjectState SetViewSettingsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<GS::ObjectState> navigatorItemIdsWithViewSettings;
    parameters.Get ("navigatorItemIdsWithViewSettings", navigatorItemIdsWithViewSettings);

    GS::ObjectState response;
    const auto& executionResults = response.AddList<GS::ObjectState> ("executionResults");

    for (const GS::ObjectState& navigatorItemIdWithViewSetting : navigatorItemIdsWithViewSettings) {
        const GS::ObjectState* viewSettingsOS = navigatorItemIdWithViewSetting.Get ("viewSettings");
        if (viewSettingsOS == nullptr) {
            executionResults (CreateFailedExecutionResult (APIERR_BADPARS, "viewSettings input is missing"));
            continue;
        }
    
        API_Guid guid = GetGuidFromNavigatorItemIdArrayItem (navigatorItemIdWithViewSetting);
        if (guid == APINULLGuid) {
            executionResults (CreateFailedExecutionResult (APIERR_BADPARS, "navigatorItemId is corrupt or missing"));
            continue;
        }

        API_NavigatorItem navigatorItem = {};
        navigatorItem.mapId = API_PublicViewMap;
        GSErrCode err = ACAPI_Navigator_GetNavigatorItem (&guid, &navigatorItem);
        if (err != NoError) {
            executionResults (CreateFailedExecutionResult (err, "Failed to get navigator item from guid"));
            continue;
        }


        API_NavigatorView navigatorView = {};
        err = ACAPI_Navigator_GetNavigatorView (&navigatorItem, &navigatorView);
        API_NavigatorView allocatedViewData = navigatorView;
        const GS::OnExit viewGuard ([&] () { DisposeNavigatorViewData (allocatedViewData); });
        if (err != NoError) {
            executionResults (CreateFailedExecutionResult (err, "Failed to get navigator view settings from the navigator item"));
            continue;
        }

        if (SetCharProperty (viewSettingsOS, "modelViewOptions", navigatorView.modelViewOptName)) {
            navigatorView.saveDispOpt = true;
            if (navigatorView.modelViewOptName[0] != 0) navigatorView.modelViewOpt = nullptr;
        }
        if (SetCharProperty (viewSettingsOS, "layerCombination", navigatorView.layerCombination)) {
            navigatorView.saveLaySet = true;
            if (navigatorView.layerCombination[0] != 0) navigatorView.layerStats = nullptr;
        }
        if (SetCharProperty (viewSettingsOS, "dimensionStyle", navigatorView.dimName)) {
            navigatorView.saveDim = true;
            if (navigatorView.dimName[0] != 0) navigatorView.dimPrefs = nullptr;
        }
        if (SetCharProperty (viewSettingsOS, "penSetName", navigatorView.penSetName)) {
            navigatorView.savePenSet = true;
            if (navigatorView.penSetName[0] != 0) navigatorView.pens = nullptr;
        }
        SetUCharProperty (viewSettingsOS, "graphicOverrideCombination", navigatorView.overrideCombination);
        if (viewSettingsOS->Get ("drawingScale", navigatorView.drawingScale)) {
            navigatorView.saveDScale = true;
        }
        viewSettingsOS->Get ("saveZoom", navigatorView.saveZoom);
        viewSettingsOS->Get ("ignoreSavedZoom", navigatorView.ignoreSavedZoom);
        const GS::ObjectState* zoomOS = viewSettingsOS->Get ("zoom");
        if (zoomOS != nullptr &&
            zoomOS->Get ("xMin", navigatorView.zoom.xMin) &&
            zoomOS->Get ("yMin", navigatorView.zoom.yMin) &&
            zoomOS->Get ("xMax", navigatorView.zoom.xMax) &&
            zoomOS->Get ("yMax", navigatorView.zoom.yMax)) {
            navigatorView.saveZoom = true;
        }
        // navigatorView starts from the view's already-stored settings (see
        // ACAPI_Navigator_GetNavigatorView above), so an untouched field can carry
        // over pre-existing, possibly invalid data (e.g. a corrupt stored zoom
        // rectangle from before this call). Only validate scale/zoom below if the
        // caller actually asked to set them this call - otherwise an unrelated field
        // change (e.g. drawingScale alone) would be rejected for a problem the caller
        // never touched and isn't trying to fix.
        const bool callerTouchedScale = viewSettingsOS->Contains ("drawingScale") || viewSettingsOS->Contains ("saveDScale");
        const bool callerTouchedZoom   = viewSettingsOS->Contains ("zoom") || viewSettingsOS->Contains ("saveZoom");

        double rotation = 0.0;
        if (viewSettingsOS->Get ("rotation", rotation)) {
            const double c = cos (rotation);
            const double s = sin (rotation);
            navigatorView.tr.tmx[0]  = c;   navigatorView.tr.tmx[1]  = -s;  navigatorView.tr.tmx[2]  = 0.0;
            navigatorView.tr.tmx[4]  = s;   navigatorView.tr.tmx[5]  = c;   navigatorView.tr.tmx[6]  = 0.0;
            navigatorView.tr.tmx[8]  = 0.0; navigatorView.tr.tmx[9]  = 0.0; navigatorView.tr.tmx[10] = 1.0;
            navigatorView.tr.tmx[11] = 0.0;
        }

        GS::UniString structureDisplayStr;
        if (viewSettingsOS->Get ("structureDisplay", structureDisplayStr)) {
            if      (structureDisplayStr == "CoreOnly")         navigatorView.structureDisplay = API_CoreOnly;
            else if (structureDisplayStr == "WithoutFinishes")  navigatorView.structureDisplay = API_WithoutFinishes;
            else if (structureDisplayStr == "StructureOnly")    navigatorView.structureDisplay = API_StructureOnly;
            else                                                navigatorView.structureDisplay = API_EntireStructure;
            navigatorView.saveStructureDisplay = true;
        }

        const GS::ObjectState* renovFilterOS = viewSettingsOS->Get ("renovationFilterGuid");
        if (renovFilterOS != nullptr) {
            navigatorView.renovationFilterGuid = GetGuidFromObjectState (*renovFilterOS);
        } else {
            GS::UniString filterGuid;
            if (viewSettingsOS->Get ("renovationFilterGuid", filterGuid))
                navigatorView.renovationFilterGuid = APIGuidFromString (filterGuid.ToCStr ());
        }

        SetUCharProperty (viewSettingsOS, "d3styleName", navigatorView.d3styleName);
        SetUCharProperty (viewSettingsOS, "renderingSceneName", navigatorView.renderingSceneName);
        viewSettingsOS->Get ("usePhotoRendering", navigatorView.usePhotoRendering);

        // Explicit save flags override automatic enabling by supplied settings.
        viewSettingsOS->Get ("saveDispOpt", navigatorView.saveDispOpt);
        viewSettingsOS->Get ("saveLaySet", navigatorView.saveLaySet);
        viewSettingsOS->Get ("saveDScale", navigatorView.saveDScale);
        viewSettingsOS->Get ("saveDim", navigatorView.saveDim);
        viewSettingsOS->Get ("savePenSet", navigatorView.savePenSet);
        viewSettingsOS->Get ("saveStructureDisplay", navigatorView.saveStructureDisplay);
        viewSettingsOS->Get ("saveZoom", navigatorView.saveZoom);
        const bool scaleInvalid = navigatorView.saveDScale && navigatorView.drawingScale <= 0;
        const bool zoomInvalid  = navigatorView.saveZoom && (!std::isfinite (navigatorView.zoom.xMin) || !std::isfinite (navigatorView.zoom.xMax) ||
                !std::isfinite (navigatorView.zoom.yMin) || !std::isfinite (navigatorView.zoom.yMax) ||
                navigatorView.zoom.xMin >= navigatorView.zoom.xMax || navigatorView.zoom.yMin >= navigatorView.zoom.yMax);
        if ((callerTouchedScale && scaleInvalid) || (callerTouchedZoom && zoomInvalid)) {
            executionResults (CreateFailedExecutionResult (APIERR_BADPARS, "Stored scale must be positive and stored zoom must be a finite nonempty rectangle."));
            continue;
        }
        // The caller didn't ask to change scale/zoom this call, but the view's
        // already-stored value is invalid (can happen on views corrupted before this
        // add-on ever touched them). ACAPI_Navigator_ChangeNavigatorView writes back
        // the whole struct, so carrying the stale invalid value forward would make
        // any unrelated field change on this view permanently fail. Drop the invalid
        // stored value instead of forcing a doomed write-back for data the caller
        // never asked about.
        if (!callerTouchedScale && scaleInvalid) navigatorView.saveDScale = false;
        if (!callerTouchedZoom && zoomInvalid) navigatorView.saveZoom = false;

#ifdef ServerMainVers_2700
        GS::HashTable<API_AttributeIndex,API_LayerStat> customLayers;
        GS::Array<API_Pen> customPens;
        GS::UniString customMessage;
        API_NavigatorView customView=allocatedViewData;
        err=PrepareCustomViewSettings(*viewSettingsOS,customView,customLayers,customPens,customMessage);
        if (err!=NoError) { executionResults(CreateFailedExecutionResult(err,customMessage)); continue; }
        if (viewSettingsOS->Contains("customLayers")) { navigatorView.layerCombination[0]=0; navigatorView.layerStats=customView.layerStats; navigatorView.saveLaySet=true; }
        if (viewSettingsOS->Contains("customPens")) { navigatorView.penSetName[0]=0; navigatorView.pens=customView.pens; navigatorView.savePenSet=true; }
#else
        if (viewSettingsOS->Contains("customLayers") || viewSettingsOS->Contains("customPens")) {
            executionResults(CreateFailedExecutionResult(APIERR_BADPARS,"Custom layer/pen authoring requires Archicad 27 or newer.")); continue;
        }
#endif

#ifdef ServerMainVers_2900
        API_ModelViewOptions customModelOptions={};
        bool ownsCustomGDL=false;
        const GS::OnExit freeCustomGDL([&] { if (ownsCustomGDL) ACAPI_FreeGDLModelViewOptionsPtr(&customModelOptions.gdlOptions); });
        err=PrepareViewModelOptions(*viewSettingsOS,allocatedViewData,customModelOptions,ownsCustomGDL);
        if (err!=NoError) { executionResults(CreateFailedExecutionResult(err,"Cannot prepare custom model view options. Use readable saved options or sourceName, supported modes, and no conflicting modelViewOptions/saveDispOpt.")); continue; }
        if (viewSettingsOS->Contains("customModelViewOptions")) { navigatorView.modelViewOptName[0]=0; navigatorView.modelViewOpt=&customModelOptions; navigatorView.saveDispOpt=true; }
        API_DimensionPrefs customDimensions={};
        err=PrepareViewDimensions(*viewSettingsOS,allocatedViewData,customDimensions);
        if (err!=NoError) { executionResults(CreateFailedExecutionResult(err,"Cannot prepare view dimensions. Use readable saved dimensions or sourceStandardName, valid formats, and no conflicting dimensionStyle/saveDim.")); continue; }
        if (viewSettingsOS->Contains("customDimensions")) { navigatorView.dimName[0]=0; navigatorView.dimPrefs=&customDimensions; navigatorView.saveDim=true; }
#else
        if (viewSettingsOS->Contains("customModelViewOptions")) { executionResults(CreateFailedExecutionResult(APIERR_BADPARS,"Custom model view option authoring requires Archicad 29.")); continue; }
        if (viewSettingsOS->Contains("customDimensions")) { executionResults(CreateFailedExecutionResult(APIERR_BADPARS,"Custom dimension authoring requires Archicad 29.")); continue; }
#endif
        err = ACAPI_Navigator_ChangeNavigatorView (&navigatorItem, &navigatorView);
        if (err != NoError) {
            executionResults (CreateFailedExecutionResult (err, "Failed to set navigator item view settings"));
            continue;
        }

#ifdef ServerMainVers_2900
        if (const auto* custom=viewSettingsOS->Get("customModelViewOptions")) {
            if (custom->Contains("gdlOptions")) {
                API_NavigatorView observed={};
                const GS::OnExit disposeObserved([&] { DisposeNavigatorViewData(observed); });
                err=ACAPI_Navigator_GetNavigatorView(&navigatorItem,&observed);
                if (err==NoError && (!observed.saveDispOpt || observed.modelViewOptName[0]!=0 || observed.modelViewOpt==nullptr)) err=APIERR_GENERAL;
                if (err==NoError) err=ViewGDL::Patch(*custom,observed.modelViewOpt->gdlOptions,true);
                if (err!=NoError) { executionResults(CreateFailedExecutionResult(err,"View edit was accepted, but requested GDL display parameters were not confirmed. Inspect before retrying.")); continue; }
            }
        }
#endif
        executionResults (CreateSuccessfulExecutionResult ());
    }

    return response;
}

GetView2DTransformationsCommand::GetView2DTransformationsCommand () :
    CommandBase (CommonSchema::Used)
{}

GS::String GetView2DTransformationsCommand::GetName () const
{
    return "GetView2DTransformations";
}

GS::Optional<GS::UniString> GetView2DTransformationsCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "navigatorItemIds": {
                "$ref": "#/NavigatorItemIds"
            },
            "databases": {
                 "$ref": "#/Databases"
            }
        },
        "additionalProperties": false,
        "required": []
    })";
}

GS::Optional<GS::UniString> GetView2DTransformationsCommand::GetRawResponseSchema () const
{
    return R"({
    "type": "object",
    "properties": {
        "transformations": {
            "type": "array",
            "items": {
                "$ref": "#/ViewTransformationsOrError"
            }
        }
    },
    "additionalProperties": false,
    "required": [
        "transformations"
    ]
})";
}

template <typename ListProxyType>
static GSErrCode GetTransformationFromCurrentDatabase (ListProxyType& transformationsListProxy)
{
    API_Box     zoomBox = {};
    API_Tranmat tranmat = {};
    GSErrCode err = ACAPI_View_GetZoom (&zoomBox, &tranmat);
    if (err != NoError) {
        return err;
    }

	Vector2D vec2D1 (tranmat.tmx[0], tranmat.tmx[4]);
	Vector2D vec2D2 (tranmat.tmx[1], tranmat.tmx[5]);
	Vector2D offset (tranmat.tmx[3], tranmat.tmx[7]);
	Geometry::Matrix22 matrix;
	Geometry::Matrix22::ColVectorsMatrix (vec2D1, vec2D2, matrix);
	Geometry::Transformation2D trafo;
	trafo.SetMatrix (matrix);
	trafo.SetOffset (offset);

    double tranAngle = 0;
    Geometry::TranAngle (trafo, &tranAngle);

    transformationsListProxy (
        GS::ObjectState (
            "zoom", GS::ObjectState (
                "xMin", zoomBox.xMin,
                "yMin", zoomBox.yMin,
                "xMax", zoomBox.xMax,
                "yMax", zoomBox.yMax),
            "rotation", tranAngle));

    return err;
}

GS::ObjectState GetView2DTransformationsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::ObjectState response;
    const auto& transformations = response.AddList<GS::ObjectState> ("transformations");

    auto runForDatabaseIds = [&](const GS::Array<API_Guid>& databaseIds) -> GSErrCode {
        auto action = [&]() -> GSErrCode {
            return GetTransformationFromCurrentDatabase (transformations);
        };
        auto actionSuccess = [&]() -> void {};
        auto actionFailure = [&](GSErrCode err, const GS::UniString& errMsg) -> void {
            transformations (CreateErrorResponse (err, errMsg));
        };
        return ExecuteActionForEachDatabase (databaseIds, action, actionSuccess, actionFailure);
    };

    GS::Array<GS::ObjectState> navigatorItemIds;
    GS::Array<GS::ObjectState> databases;

    if (parameters.Get ("navigatorItemIds", navigatorItemIds)) {
        GS::Array<API_Guid> databaseIds;
        for (const GS::ObjectState& navItemOS : navigatorItemIds) {
            API_Guid navGuid = GetGuidFromNavigatorItemIdArrayItem (navItemOS);
            if (navGuid == APINULLGuid) {
                transformations (CreateErrorResponse (APIERR_BADPARS, "navigatorItemId is corrupt or missing"));
                continue;
            }
            API_NavigatorItem navigatorItem = {};
            if (ACAPI_Navigator_GetNavigatorItem (&navGuid, &navigatorItem) != NoError) {
                transformations (CreateErrorResponse (APIERR_BADPARS, "Failed to get navigator item"));
                continue;
            }
            API_DatabaseInfo navDb = navigatorItem.db;
            GSErrCode dbErr = ACAPI_Window_GetDatabaseInfo (&navDb);
            if (dbErr != NoError) {
                transformations (CreateErrorResponse (dbErr, "Navigator item has no associated database"));
                continue;
            }
            const API_Guid dbGuid = DatabaseIdResolver::Instance ().GetIdOfDatabase (navDb);
            if (dbGuid == APINULLGuid) {
                transformations (CreateErrorResponse (APIERR_BADPARS, "Could not resolve database id for navigator item"));
                continue;
            }
            databaseIds.Push (dbGuid);
        }
        if (!databaseIds.IsEmpty ()) {
            GSErrCode err = runForDatabaseIds (databaseIds);
            if (err != NoError) {
                return CreateErrorResponse (err, "Failed to switch database.");
            }
        }
    } else if (parameters.Get ("databases", databases)) {
        const GS::Array<API_Guid> databaseIds = databases.Transform<API_Guid> (GetGuidFromDatabaseArrayItem);
        GSErrCode err = runForDatabaseIds (databaseIds);
        if (err != NoError) {
            return CreateErrorResponse (err, "Failed to retrieve the starting database or to switch back to it after execution.");
        }
    } else {
        GetTransformationFromCurrentDatabase (transformations);
    }

    return response;
}

SetViewRotationCommand::SetViewRotationCommand () :
    CommandBase (CommonSchema::Used)
{}

GS::String SetViewRotationCommand::GetName () const
{
    return "SetViewRotation";
}

GS::Optional<GS::UniString> SetViewRotationCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "navigatorItemIdsWithRotation": {
                "type": "array",
                "items": {
                    "type": "object",
                    "properties": {
                        "navigatorItemId": {
                            "$ref": "#/NavigatorItemId"
                        },
                        "rotation": {
                            "type": "number",
                            "description": "View rotation angle in radians."
                        }
                    },
                    "additionalProperties": false,
                    "required": ["navigatorItemId", "rotation"]
                }
            }
        },
        "additionalProperties": false,
        "required": ["navigatorItemIdsWithRotation"]
    })";
}

GS::Optional<GS::UniString> SetViewRotationCommand::GetRawResponseSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "executionResults": {
                "$ref": "#/ExecutionResults"
            }
        },
        "additionalProperties": false,
        "required": ["executionResults"]
    })";
}

GS::ObjectState SetViewRotationCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<GS::ObjectState> itemsWithRotation;
    parameters.Get ("navigatorItemIdsWithRotation", itemsWithRotation);

    GS::ObjectState response;
    const auto& executionResults = response.AddList<GS::ObjectState> ("executionResults");

    for (const GS::ObjectState& item : itemsWithRotation) {
        double rotation = 0.0;
        if (!item.Get ("rotation", rotation)) {
            executionResults (CreateFailedExecutionResult (APIERR_BADPARS, "rotation is missing"));
            continue;
        }

        API_Guid navGuid = GetGuidFromNavigatorItemIdArrayItem (item);
        if (navGuid == APINULLGuid) {
            executionResults (CreateFailedExecutionResult (APIERR_BADPARS, "navigatorItemId is corrupt or missing"));
            continue;
        }

        API_NavigatorItem navigatorItem = {};
        GSErrCode err = ACAPI_Navigator_GetNavigatorItem (&navGuid, &navigatorItem);
        if (err != NoError) {
            executionResults (CreateFailedExecutionResult (err, "Failed to get navigator item"));
            continue;
        }
        navigatorItem.mapId = API_PublicViewMap;

        API_NavigatorView navigatorView = {};
        err = ACAPI_Navigator_GetNavigatorView (&navigatorItem, &navigatorView);
        if (err != NoError) {
            executionResults (CreateFailedExecutionResult (err, "Failed to get navigator view"));
            continue;
        }

        // Obtain the axis-aligned model extent for this database without
        // modifying the database's live zoom state.
        // - ACAPI_Element_GetExtent returns the fit-in-window bounding box of
        //   all elements, always axis-aligned (independent of current rotation).
        // - We never call SetZoom/GetZoom, so the shared database zoom is
        //   untouched. Views without their own saveZoom keep using whatever
        //   the database currently shows.
        // ArchiCAD only applies tr (rotation) when navigating to a view whose
        // saveZoom flag is true.
        if (!navigatorView.saveZoom) {
            API_DatabaseInfo startDb  = {};
            API_DatabaseInfo targetDb = navigatorItem.db;
            if (ACAPI_Database_GetCurrentDatabase (&startDb) == NoError &&
                ACAPI_Window_GetDatabaseInfo      (&targetDb) == NoError &&
                ACAPI_Database_ChangeCurrentDatabase (&targetDb) == NoError)
            {
                API_Box extent = {};
                if (ACAPI_Element_GetExtent (&extent) == NoError &&
                    extent.xMax > extent.xMin)
                {
                    navigatorView.zoom     = extent;
                    navigatorView.saveZoom = true;
                }
                ACAPI_Database_ChangeCurrentDatabase (&startDb);
            }
        }

        const double c = cos (rotation);
        const double s = sin (rotation);
        navigatorView.tr.tmx[0]  = c;   navigatorView.tr.tmx[1]  = -s;  navigatorView.tr.tmx[2]  = 0.0;
        navigatorView.tr.tmx[4]  = s;   navigatorView.tr.tmx[5]  =  c;  navigatorView.tr.tmx[6]  = 0.0;
        navigatorView.tr.tmx[8]  = 0.0; navigatorView.tr.tmx[9]  = 0.0; navigatorView.tr.tmx[10] = 1.0;
        navigatorView.tr.tmx[11] = 0.0;

        err = ACAPI_Navigator_ChangeNavigatorView (&navigatorItem, &navigatorView);
        if (err != NoError) {
            executionResults (CreateFailedExecutionResult (err, "Failed to change navigator view rotation"));
            continue;
        }

        executionResults (CreateSuccessfulExecutionResult ());
    }

    return response;
}

FitInWindowCommand::FitInWindowCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String FitInWindowCommand::GetName () const
{
    return "FitInWindow";
}

GS::Optional<GS::UniString> FitInWindowCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "elements": {
                "$ref": "#/Elements"
            }
        },
        "additionalProperties": false,
        "required": []
    })";
}

GS::Optional<GS::UniString> FitInWindowCommand::GetRawResponseSchema () const
{
    return R"({
        "$ref": "#/ExecutionResult"
    })";
}

GS::ObjectState FitInWindowCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<GS::ObjectState> elements;
    GSErrCode err = NoError;

    if (parameters.Get ("elements", elements) && !elements.IsEmpty ()) {
        const GS::Array<API_Guid> elemIds = elements.Transform<API_Guid> (GetGuidFromElementsArrayItem);
        err = ACAPI_View_ZoomToElements (&elemIds);
        if (err != NoError) {
            return CreateFailedExecutionResult (err, "Failed to zoom to elements.");
        }
    } else {
        err = ACAPI_View_Zoom ();
        if (err != NoError) {
            return CreateFailedExecutionResult (err, "Failed to fit in window. There might be no project open.");
        }
    }

    return CreateSuccessfulExecutionResult ();
}

CloneProjectMapItemToViewMapCommand::CloneProjectMapItemToViewMapCommand () :
    CommandBase (CommonSchema::Used)
{}

GS::String CloneProjectMapItemToViewMapCommand::GetName () const
{
    return "CloneProjectMapItemToViewMap";
}

GS::Optional<GS::UniString> CloneProjectMapItemToViewMapCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "viewsData": {
                "type": "array",
                "description": "Array of views to clone from the Project Map to the View Map.",
                "items": {
                    "type": "object",
                    "properties": {
                        "navigatorItemId": {
                            "$ref": "#/NavigatorItemId",
                            "description": "Navigator item ID of the Project Map viewpoint to clone."
                        },
                        "parentNavigatorItemId": {
                            "$ref": "#/NavigatorItemId",
                            "description": "Navigator item ID of the View Map folder to place the clone in. Optional; defaults to the View Map root."
                        }
                    },
                    "additionalProperties": false,
                    "required": ["navigatorItemId"]
                }
            }
        },
        "additionalProperties": false,
        "required": ["viewsData"]
    })";
}

GS::Optional<GS::UniString> CloneProjectMapItemToViewMapCommand::GetRawResponseSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "navigatorItems": {
                "type": "array",
                "items": {
                    "$ref": "#/NavigatorItemIdOrError"
                }
            }
        },
        "additionalProperties": false,
        "required": ["navigatorItems"]
    })";
}

GS::ObjectState CloneProjectMapItemToViewMapCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<GS::ObjectState> viewsData;
    parameters.Get ("viewsData", viewsData);

    GS::ObjectState response;
    const auto& navigatorItems = response.AddList<GS::ObjectState> ("navigatorItems");

    for (const GS::ObjectState& item : viewsData) {
        const GS::ObjectState* navigatorItemIdOS = item.Get ("navigatorItemId");
        if (navigatorItemIdOS == nullptr) {
            navigatorItems (CreateErrorResponse (APIERR_BADPARS, "navigatorItemId is missing."));
            continue;
        }

        API_Guid sourceGuid = GetGuidFromObjectState (*navigatorItemIdOS);
        if (sourceGuid == APINULLGuid) {
            navigatorItems (CreateErrorResponse (APIERR_BADPARS, "navigatorItemId is corrupt or missing."));
            continue;
        }

        API_Guid parentGuid = APINULLGuid;
        const GS::ObjectState* parentOS = item.Get ("parentNavigatorItemId");
        if (parentOS != nullptr) {
            parentGuid = GetGuidFromObjectState (*parentOS);
        }

        if (parentGuid == APINULLGuid) {
            API_NavigatorSet viewMapSet = {};
            viewMapSet.mapId = API_PublicViewMap;
            Int32 idx = 0;
            if (ACAPI_Navigator_GetNavigatorSet (&viewMapSet, &idx) == NoError) {
                parentGuid = viewMapSet.rootGuid;
            }
        }

        API_Guid createdGuid = APINULLGuid;
        const GSErrCode err = ACAPI_Navigator_CloneProjectMapItemToViewMap (&sourceGuid, &parentGuid, &createdGuid);
        if (err != NoError) {
            navigatorItems (CreateErrorResponse (err, "Failed to clone navigator item to view map."));
            continue;
        }

        navigatorItems (CreateIdObjectState ("navigatorItemId", createdGuid));
    }

    return response;
}

CreateViewsInViewMapCommand::CreateViewsInViewMapCommand () :
    CommandBase (CommonSchema::Used)
{}

GS::String CreateViewsInViewMapCommand::GetName () const
{
    return "CreateViewsInViewMap";
}

GS::Optional<GS::UniString> CreateViewsInViewMapCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "viewsData": {
                "type": "array",
                "description": "Array of views to create as independent (non-clone) items in the View Map.",
                "minItems": 1, "maxItems": 100,
                "items": {
                    "type": "object",
                    "properties": {
                        "navigatorItemId": {
                            "$ref": "#/NavigatorItemId",
                            "description": "Source Project Map item cloned into an independent view. Native clone restrictions apply."
                        },
                        "parentNavigatorItemId": {
                            "$ref": "#/NavigatorItemId",
                            "description": "View Map folder to place the new view in. Optional; defaults to View Map root."
                        },
                        "name": {
                            "type": "string",
                            "description": "Name for the new view. Optional; defaults to the source item name.",
                            "maxLength": 255
                        },
                        "viewSettings": {"$ref":"#/ViewSettings","description":"Optional settings applied after creation or explicit matching-source update. Failure returns the retained ID in incompleteNavigatorItems; do not blindly retry creation."},
                        "ifExists": {"type":"string","enum":["Create", "Error", "UpdateMatchingSource"],
                            "description":"Default Create preserves legacy behaviour. Error rejects a same-name sibling. UpdateMatchingSource updates one same-name sibling only when its native sourceGuid equals navigatorItemId; otherwise fails without creating a duplicate. Requires explicit nonempty name. Existing IDs are separately listed in updatedNavigatorItems."}
                    },
                    "additionalProperties": false,
                    "required": ["navigatorItemId"],
                    "allOf":[{"if":{"properties":{"ifExists":{"enum":["Error","UpdateMatchingSource"]}},"required":["ifExists"]},
                              "then":{"required":["name"],"properties":{"name":{"minLength":1}}}}]
                }
            }
        },
        "additionalProperties": false,
        "required": ["viewsData"]
    })";
}

GS::Optional<GS::UniString> CreateViewsInViewMapCommand::GetRawResponseSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "updatedNavigatorItems": {"type":"array","description":"Pre-existing IDs selected for update, including those with a later settings failure. These are not newly created items.",
                "items":{"type":"object","properties":{"navigatorItemId":{"$ref":"#/NavigatorItemId"},"inputIndex":{"type":"integer"}},
                "required":["navigatorItemId","inputIndex"],"additionalProperties":false}},
            "incompleteNavigatorItems": {
                "type":"array","items":{"type":"object","properties":{
                    "navigatorItemId":{"$ref":"#/NavigatorItemId"},"inputIndex":{"type":"integer"},
                    "failurePhase":{"type":"string"}
                },"required":["navigatorItemId","inputIndex","failurePhase"],"additionalProperties":false}
            },
            "navigatorItems": {
                "type": "array",
                "items": {
                    "$ref": "#/NavigatorItemIdOrError"
                }
            }
        },
        "additionalProperties": false,
        "required": ["navigatorItems"]
    })";
}

GS::ObjectState CreateViewsInViewMapCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& processControl) const
{
    GS::Array<GS::ObjectState> viewsData;
    parameters.Get ("viewsData", viewsData);
    if (viewsData.IsEmpty () || viewsData.GetSize () > 100) return CreateErrorResponse (APIERR_BADPARS, "Supply 1 to 100 view specifications.");

    GS::ObjectState response;
    const auto& navigatorItems = response.AddList<GS::ObjectState> ("navigatorItems");
    const auto& incompleteItems = response.AddList<GS::ObjectState> ("incompleteNavigatorItems");
    const auto& updatedItems = response.AddList<GS::ObjectState> ("updatedNavigatorItems");
    Int32 inputIndex = -1;

    for (const GS::ObjectState& item : viewsData) {
        ++inputIndex;
        GS::UniString newName;
        item.Get ("name", newName);
        GS::UniString collisionPolicy = "Create";
        item.Get ("ifExists", collisionPolicy);
        if ((collisionPolicy != "Create" && collisionPolicy != "Error" && collisionPolicy != "UpdateMatchingSource") ||
            (collisionPolicy != "Create" && newName.IsEmpty ())) {
            navigatorItems (CreateErrorResponse (APIERR_BADPARS, "Non-Create view collision policies require an explicit nonempty name.")); continue;
        }
        if (newName.GetLength () >= API_UniLongNameLen) {
            navigatorItems (CreateErrorResponse (APIERR_BADPARS, "View name exceeds the native UTF-16 capacity.")); continue;
        }
        const GS::ObjectState* navigatorItemIdOS = item.Get ("navigatorItemId");
        if (navigatorItemIdOS == nullptr) {
            navigatorItems (CreateErrorResponse (APIERR_BADPARS, "navigatorItemId is missing."));
            continue;
        }

        API_Guid sourceGuid = GetGuidFromObjectState (*navigatorItemIdOS);
        if (sourceGuid == APINULLGuid) {
            navigatorItems (CreateErrorResponse (APIERR_BADPARS, "navigatorItemId is corrupt or missing."));
            continue;
        }

        API_Guid parentGuid = APINULLGuid;
        const GS::ObjectState* parentOS = item.Get ("parentNavigatorItemId");
        if (parentOS != nullptr) {
            parentGuid = GetGuidFromObjectState (*parentOS);
        }
        if (parentGuid == APINULLGuid) {
            API_NavigatorSet viewMapSet = {};
            viewMapSet.mapId = API_PublicViewMap;
            Int32 idx = 0;
            const GSErrCode rootError = ACAPI_Navigator_GetNavigatorSet (&viewMapSet, &idx);
            if (rootError != NoError) { navigatorItems (CreateErrorResponse (rootError, "Cannot resolve View Map root.")); continue; }
            parentGuid = viewMapSet.rootGuid;
        }

        API_Guid createdGuid = APINULLGuid;
        if (collisionPolicy != "Create") {
            GS::Optional<API_NavigatorItem> existing;
            const GSErrCode lookupError = FindViewMapSibling (parentGuid, newName, existing);
            if (lookupError != NoError) {
                navigatorItems (CreateErrorResponse (lookupError, "Cannot resolve an unambiguous same-name View Map sibling.")); continue;
            }
            if (existing.HasValue ()) {
                if (collisionPolicy == "Error" || existing->itemType == API_FolderNavItem || existing->sourceGuid != sourceGuid) {
                    navigatorItems (CreateErrorResponse (APIERR_BADPARS, "A same-name sibling exists but collision policy or native source identity prevents reuse.")); continue;
                }
                createdGuid = existing->guid;
                updatedItems (GS::ObjectState ("navigatorItemId", CreateGuidObjectState (createdGuid), "inputIndex", inputIndex));
            }
        }
        // Clone only when no existing native source match was selected.
        GSErrCode err = createdGuid == APINULLGuid ? ACAPI_Navigator_CloneProjectMapItemToViewMap (&sourceGuid, &parentGuid, &createdGuid) : NoError;
        if (err != NoError) {
            navigatorItems (CreateErrorResponse (err, "Failed to clone navigator item to view map."));
            continue;
        }

        // Step 2: read the newly created navigator item
        API_NavigatorItem createdItem = {};
        err = ACAPI_Navigator_GetNavigatorItem (&createdGuid, &createdItem);
        if (err != NoError) {
            navigatorItems (CreateErrorResponse (err, "Failed to read created navigator item."));
            incompleteItems (GS::ObjectState ("navigatorItemId", CreateGuidObjectState (createdGuid), "inputIndex", inputIndex, "failurePhase", "readCreatedItem"));
            continue;
        }

        // Step 3: make it independent (break the link to the Project Map) and
        // apply optional custom name
        createdItem.mapId         = API_PublicViewMap;
        createdItem.isIndependent = true;

        if (!newName.IsEmpty ()) {
            GS::ucscpy (createdItem.uName, newName.ToUStr ());
            createdItem.customName = true;
        }

        err = ACAPI_Navigator_ChangeNavigatorItem (&createdItem);
        if (err != NoError) {
            navigatorItems (CreateErrorResponse (err, "View was created but independence/name update failed. Inspect incompleteNavigatorItems before retrying."));
            incompleteItems (GS::ObjectState ("navigatorItemId", CreateGuidObjectState (createdGuid), "inputIndex", inputIndex, "failurePhase", "nameAndIndependence"));
            continue;
        }
        if (const auto* settings = item.Get ("viewSettings")) {
            GS::Array<GS::ObjectState> updates;
            updates.Push (GS::ObjectState ("navigatorItemId", CreateGuidObjectState (createdGuid), "viewSettings", *settings));
            const auto applied = SetViewSettingsCommand ().Execute (GS::ObjectState ("navigatorItemIdsWithViewSettings", updates), processControl);
            GS::Array<GS::ObjectState> rows;
            applied.Get ("executionResults", rows);
            bool succeeded = false;
            if (rows.GetSize () == 1) rows[0].Get ("success", succeeded);
            if (!succeeded) {
                if (rows.GetSize () == 1 && rows[0].Get ("error") != nullptr)
                    navigatorItems (GS::ObjectState ("error", *rows[0].Get ("error")));
                else navigatorItems (CreateErrorResponse (APIERR_GENERAL, "Created view settings were not confirmed."));
                incompleteItems (GS::ObjectState ("navigatorItemId", CreateGuidObjectState (createdGuid), "inputIndex", inputIndex, "failurePhase", "viewSettings"));
                continue;
            }
        }

        navigatorItems (CreateIdObjectState ("navigatorItemId", createdGuid));
    }

    return response;
}

CreateViewMapFolderCommand::CreateViewMapFolderCommand () :
    CommandBase (CommonSchema::Used)
{}

GS::String CreateViewMapFolderCommand::GetName () const
{
    return "CreateViewMapFolder";
}

GS::Optional<GS::UniString> CreateViewMapFolderCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "ifExists": {"type":"string","enum":["Create","Error","Reuse"],
                "description":"Default Create preserves legacy duplicate creation. Reuse returns the unique same-name folder under the requested parent without changing it; ambiguous or wrong-kind matches fail."},
            "folderName": {
                "type": "string",
                "description": "Name of the new folder to create in the View Map."
            },
            "parentNavigatorItemId": {
                "$ref": "#/NavigatorItemId",
                "description": "Navigator item ID of the parent View Map folder. Optional; defaults to the View Map root."
            }
        },
        "additionalProperties": false,
        "required": ["folderName"]
    })";
}

GS::Optional<GS::UniString> CreateViewMapFolderCommand::GetRawResponseSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "navigatorItemId": {
                "$ref": "#/NavigatorItemId"
            }
        },
        "additionalProperties": false,
        "required": ["navigatorItemId"]
    })";
}

GS::ObjectState CreateViewMapFolderCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::UniString folderName;
    if (!parameters.Get ("folderName", folderName) || folderName.IsEmpty () || folderName.GetLength () >= API_UniLongNameLen) {
        return CreateErrorResponse (APIERR_BADPARS, "folderName must be nonempty and fit the native UTF-16 capacity.");
    }

    API_Guid parentGuid = APINULLGuid;

    const GS::ObjectState* parentOS = parameters.Get ("parentNavigatorItemId");
    if (parentOS != nullptr) {
        parentGuid = GetGuidFromObjectState (*parentOS);
        if (parentGuid == APINULLGuid) return CreateErrorResponse (APIERR_BADPARS, "Invalid parentNavigatorItemId.");
    }
    if (parentGuid == APINULLGuid) {
        API_NavigatorSet set = {}; set.mapId = API_PublicViewMap;
        Int32 index = 0;
        const GSErrCode rootError = ACAPI_Navigator_GetNavigatorSet (&set, &index);
        if (rootError != NoError) return CreateErrorResponse (rootError, "Cannot resolve View Map root.");
        parentGuid = set.rootGuid;
    }
    GS::UniString collisionPolicy = "Create"; parameters.Get ("ifExists", collisionPolicy);
    if (collisionPolicy != "Create" && collisionPolicy != "Error" && collisionPolicy != "Reuse")
        return CreateErrorResponse (APIERR_BADPARS, "Unknown folder collision policy.");
    if (collisionPolicy != "Create") {
        GS::Optional<API_NavigatorItem> existing;
        const GSErrCode lookupError = FindViewMapSibling (parentGuid, folderName, existing);
        if (lookupError != NoError) return CreateErrorResponse (lookupError, "Cannot resolve an unambiguous same-name folder.");
        if (existing.HasValue ()) {
            if (collisionPolicy == "Error" || existing->itemType != API_FolderNavItem)
                return CreateErrorResponse (APIERR_BADPARS, "A same-name sibling exists and cannot be reused as a folder.");
            return GS::ObjectState ("navigatorItemId", CreateGuidObjectState (existing->guid));
        }
    }
    GS::Guid parentGuidStorage = APIGuid2GSGuid (parentGuid);

    API_NavigatorItem folderItem = {};
    folderItem.itemType   = API_FolderNavItem;
    folderItem.mapId      = API_PublicViewMap;
    folderItem.customName = true;
    GS::ucscpy (folderItem.uName, folderName.ToUStr ());

    const GSErrCode err = ACAPI_Navigator_NewNavigatorView (&folderItem, nullptr, &parentGuidStorage, nullptr);
    if (err != NoError) {
        return CreateErrorResponse (err, GS::UniString::Printf ("Failed to create View Map folder (code %d).", (int) err));
    }

    GS::ObjectState response;
    response.Add ("navigatorItemId", CreateGuidObjectState (folderItem.guid));
    return response;
}

MoveNavigatorItemCommand::MoveNavigatorItemCommand () :
    CommandBase (CommonSchema::Used)
{}

GS::String MoveNavigatorItemCommand::GetName () const
{
    return "MoveNavigatorItem";
}

GS::Optional<GS::UniString> MoveNavigatorItemCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "navigatorItemIdToMove": { "$ref": "#/NavigatorItemId" },
            "parentNavigatorItemId": { "$ref": "#/NavigatorItemId" },
            "previousNavigatorItemId": { "$ref": "#/NavigatorItemId" }
        },
        "additionalProperties": false,
        "required": ["navigatorItemIdToMove", "parentNavigatorItemId"]
    })";
}

GS::Optional<GS::UniString> MoveNavigatorItemCommand::GetRawResponseSchema () const
{
    return R"({
        "$ref": "#/ExecutionResult"
    })";
}

GS::ObjectState MoveNavigatorItemCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    const GS::ObjectState* itemToMoveOS = parameters.Get ("navigatorItemIdToMove");
    const GS::ObjectState* parentOS     = parameters.Get ("parentNavigatorItemId");

    if (itemToMoveOS == nullptr || parentOS == nullptr) {
        return CreateFailedExecutionResult (APIERR_BADPARS, "navigatorItemIdToMove and parentNavigatorItemId are required.");
    }

    const GS::Guid sourceGuid = APIGuid2GSGuid (GetGuidFromObjectState (*itemToMoveOS));
    const GS::Guid parentGuid = APIGuid2GSGuid (GetGuidFromObjectState (*parentOS));

    const GS::ObjectState* prevOS = parameters.Get ("previousNavigatorItemId");
    GS::Guid  prevGuidStorage;
    GS::Guid* prevGuidPtr = nullptr;
    if (prevOS != nullptr) {
        prevGuidStorage = APIGuid2GSGuid (GetGuidFromObjectState (*prevOS));
        prevGuidPtr = &prevGuidStorage;
    }

    GSErrCode err = NoError;
    const GSErrCode transactionError = ACAPI_CallUndoableCommand ("MoveNavigatorItemCommand", [&]() -> GSErrCode {
        err = ACAPI_Navigator_SetNavigatorItemPosition (&sourceGuid, &parentGuid, prevGuidPtr);
        return err;
    });
    if (transactionError != NoError) return CreateErrorResponse (transactionError, "Native transaction failed; committed changes are not confirmed.");

    if (err != NoError) {
        return CreateFailedExecutionResult (err, "Failed to move navigator item.");
    }

    return CreateSuccessfulExecutionResult ();
}

RenameNavigatorItemCommand::RenameNavigatorItemCommand () :
    CommandBase (CommonSchema::Used)
{}

GS::String RenameNavigatorItemCommand::GetName () const
{
    return "RenameNavigatorItem";
}

GS::Optional<GS::UniString> RenameNavigatorItemCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "navigatorItemId": { "$ref": "#/NavigatorItemId" },
            "newName":         { "type": "string" },
            "newId":           { "type": "string" }
        },
        "additionalProperties": false,
        "required": ["navigatorItemId"]
    })";
}

GS::Optional<GS::UniString> RenameNavigatorItemCommand::GetRawResponseSchema () const
{
    return R"({
        "$ref": "#/ExecutionResult"
    })";
}

GS::ObjectState RenameNavigatorItemCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    const GS::ObjectState* navIdOS = parameters.Get ("navigatorItemId");
    if (navIdOS == nullptr) {
        return CreateFailedExecutionResult (APIERR_BADPARS, "navigatorItemId is required.");
    }

    const API_Guid guid = GetGuidFromObjectState (*navIdOS);

    API_NavigatorItem navItem = {};
    GSErrCode err = ACAPI_Navigator_GetNavigatorItem (&guid, &navItem);
    if (err != NoError) {
        return CreateFailedExecutionResult (err, "Failed to get navigator item.");
    }

    GS::UniString newName;
    if (parameters.Get ("newName", newName) && !newName.IsEmpty ()) {
        GS::ucsncpy (navItem.uName, newName.ToUStr (), GS::ArraySize (navItem.uName));
        navItem.uName[GS::ArraySize (navItem.uName) - 1] = 0;
        navItem.customName = true;
    }

    err = ACAPI_Navigator_ChangeNavigatorItem (&navItem);
    if (err != NoError) {
        return CreateFailedExecutionResult (err, "Failed to rename navigator item.");
    }

    // For layouts: set custom layout number (ID) via ChangeLayoutSets
    GS::UniString newId;
    if (parameters.Get ("newId", newId)) {
        API_LayoutInfo layoutInfo = {};
        BNZeroMemory (&layoutInfo, sizeof (layoutInfo));
        err = ACAPI_Navigator_GetLayoutSets (&layoutInfo, &navItem.db.databaseUnId);
        if (err != NoError) {
            return CreateFailedExecutionResult (err, "Item was renamed, but its layout number could not be read to set 'newId'.");
        }
        CHTruncate (newId.ToCStr ().Get (), layoutInfo.customLayoutNumber,
                    GS::ArraySize (layoutInfo.customLayoutNumber));
        layoutInfo.customLayoutNumbering = true;
        err = ACAPI_Navigator_ChangeLayoutSets (&layoutInfo, &navItem.db.databaseUnId);
        delete layoutInfo.customData;
        layoutInfo.customData = nullptr;
        if (err != NoError) {
            return CreateFailedExecutionResult (err, "Item was renamed, but 'newId' (custom layout number) could not be set.");
        }

        API_LayoutInfo verify = {};
        BNZeroMemory (&verify, sizeof (verify));
        err = ACAPI_Navigator_GetLayoutSets (&verify, &navItem.db.databaseUnId);
        const bool verified = err == NoError && GS::UniString (verify.customLayoutNumber) == GS::UniString (layoutInfo.customLayoutNumber);
        delete verify.customData;
        verify.customData = nullptr;
        if (!verified) {
            return CreateFailedExecutionResult (APIERR_GENERAL, "Item was renamed, but 'newId' (custom layout number) was not confirmed after being set.");
        }
    }

    return CreateSuccessfulExecutionResult ();
}

DeleteNavigatorItemsCommand::DeleteNavigatorItemsCommand () :
    CommandBase (CommonSchema::Used)
{}

GS::String DeleteNavigatorItemsCommand::GetName () const
{
    return "DeleteNavigatorItems";
}

GS::Optional<GS::UniString> DeleteNavigatorItemsCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "navigatorItemIds": { "$ref": "#/NavigatorItemIds" }
        },
        "additionalProperties": false,
        "required": ["navigatorItemIds"]
    })";
}

GS::Optional<GS::UniString> DeleteNavigatorItemsCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{"executionResults":{"$ref":"#/ExecutionResults"}},"additionalProperties":false,"required":["executionResults"]})";
}

GS::ObjectState DeleteNavigatorItemsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<GS::ObjectState> navigatorItemIds;
    parameters.Get ("navigatorItemIds", navigatorItemIds);

    GS::ObjectState response;
    const auto& executionResults = response.AddList<GS::ObjectState> ("executionResults");

    for (const GS::ObjectState& navItemIdOS : navigatorItemIds) {
        const API_Guid guid = GetGuidFromNavigatorItemIdArrayItem (navItemIdOS);
        if (guid == APINULLGuid) {
            executionResults (CreateFailedExecutionResult (APIERR_BADPARS, "navigatorItemId is corrupt or missing."));
            continue;
        }

        API_NavigatorItem navItem = {};
        const GSErrCode getErr = ACAPI_Navigator_GetNavigatorItem (&guid, &navItem);
        if (getErr != NoError) {
            executionResults (CreateFailedExecutionResult (getErr, "Failed to get navigator item."));
            continue;
        }

        GSErrCode err = NoError;
        if (navItem.itemType == API_LayoutNavItem || navItem.itemType == API_MasterLayoutNavItem) {
            API_DatabaseInfo dbInfo = navItem.db;
            err = ACAPI_Database_DeleteDatabase (&dbInfo);
        } else if (navItem.itemType == API_SubSetNavItem) {
            err = APIERR_NOTSUPPORTED;
        } else {
            bool silentMode = true;
            err = ACAPI_Navigator_DeleteNavigatorView (&guid, &silentMode);
        }

        if (err == APIERR_NOTSUPPORTED) {
            executionResults (CreateFailedExecutionResult (err, "Deleting layout subsets is not supported by the ArchiCAD API."));
        } else if (err != NoError) {
            executionResults (CreateFailedExecutionResult (err, "Failed to delete navigator item."));
        } else {
            executionResults (CreateSuccessfulExecutionResult ());
        }
    }

    return response;
}

// ─── GetNavigatorItemTree ─────────────────────────────────────────────────────

static GS::UniString NavigatorItemTypeToString (API_NavigatorItemTypeID t)
{
    switch (t) {
        case API_UndefinedNavItem:           return "UndefinedItem";
        case API_ProjectNavItem:             return "ProjectItem";
        case API_StoryNavItem:               return "StoryItem";
        case API_SectionNavItem:             return "SectionItem";
        case API_DetailDrawingNavItem:       return "DetailDrawingItem";
        case API_PerspectiveNavItem:         return "PerspectiveItem";
        case API_AxonometryNavItem:          return "AxonometryItem";
        case API_ListNavItem:                return "ListItem";
        case API_ScheduleNavItem:            return "ScheduleItem";
        case API_TocNavItem:                 return "TocItem";
        case API_CameraNavItem:              return "CameraItem";
        case API_CameraSetNavItem:           return "CameraSetItem";
        case API_InfoNavItem:                return "InfoItem";
        case API_HelpNavItem:                return "HelpItem";
        case API_LayoutNavItem:              return "LayoutItem";
        case API_MasterLayoutNavItem:        return "MasterLayoutItem";
        case API_BookNavItem:                return "BookItem";
        case API_MasterFolderNavItem:        return "MasterFolderItem";
        case API_SubSetNavItem:              return "SubSetItem";
        case API_TextListNavItem:            return "TextListItem";
        case API_ElevationNavItem:           return "ElevationItem";
        case API_InteriorElevationNavItem:   return "InteriorElevationItem";
        case API_WorksheetDrawingNavItem:    return "WorksheetDrawingItem";
        case API_DocumentFrom3DNavItem:      return "DocumentFrom3DItem";
        case API_FolderNavItem:              return "FolderItem";
        case API_DrawingNavItem:             return "DrawingItem";
        default:                             return "UnknownItem";
    }
}

static GS::ObjectState NavigatorItemToObjectState (API_NavigatorItem item, API_NavigatorMapID mapId)
{
    GS::ObjectState itemOS;
    itemOS.Add ("type",            NavigatorItemTypeToString (item.itemType));
    itemOS.Add ("name",            GS::UniString (item.uName));
    itemOS.Add ("navigatorItemId", CreateGuidObjectState (item.guid));

    GS::UniString prefix;
    if (item.itemType == API_StoryNavItem) {
        prefix = GS::UniString::Printf ("%d", (int) item.floorNum);
    }
    itemOS.Add ("prefix", prefix);

    // The ID shown next to the name on the navigator, and the two flags telling
    // whether the item's ID and name are hand-written or inherited from the
    // Project Map source - the View Settings "Custom" vs "By Project Map"
    // radio buttons. Only the flags can tell the two apart: a custom ID typed
    // identical to the source's is indistinguishable by comparing the strings.
    itemOS.Add ("uiId",          GS::UniString (item.uiId));
    itemOS.Add ("customUiId",    item.customUiId);
    itemOS.Add ("customName",    item.customName);
    itemOS.Add ("isIndependent", item.isIndependent);

    item.mapId = mapId;
    GS::Array<API_NavigatorItem> children;
    if (ACAPI_Navigator_GetNavigatorChildrenItems (&item, &children) == NoError && !children.IsEmpty ()) {
        const auto& childList = itemOS.AddList<GS::ObjectState> ("children");
        for (const API_NavigatorItem& child : children) {
            GS::ObjectState wrapper;
            wrapper.Add ("navigatorItem", NavigatorItemToObjectState (child, mapId));
            childList (wrapper);
        }
    }

    return itemOS;
}

GetNavigatorItemTreeCommand::GetNavigatorItemTreeCommand () :
    CommandBase (CommonSchema::Used)
{}

GS::String GetNavigatorItemTreeCommand::GetName () const
{
    return "GetNavigatorItemTree";
}

GS::Optional<GS::UniString> GetNavigatorItemTreeCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "navigatorMapId": {
                "type": "string",
                "enum": ["PublicViewMap", "ProjectMap", "LayoutBook", "PublisherSets"],
                "description": "The navigator map to retrieve."
            }
        },
        "additionalProperties": false,
        "required": ["navigatorMapId"]
    })";
}

GS::ObjectState GetNavigatorItemTreeCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::UniString navigatorMapIdStr;
    parameters.Get ("navigatorMapId", navigatorMapIdStr);

    API_NavigatorMapID mapId = API_PublicViewMap;
    if (navigatorMapIdStr == "ProjectMap")         mapId = API_ProjectMap;
    else if (navigatorMapIdStr == "LayoutBook")    mapId = API_LayoutMap;
    else if (navigatorMapIdStr == "PublisherSets") mapId = API_PublisherSets;

    API_NavigatorSet navSet = {};
    navSet.mapId = mapId;
    Int32 idx = 0;
    GSErrCode err = ACAPI_Navigator_GetNavigatorSet (&navSet, &idx);
    if (err != NoError) {
        return CreateErrorResponse (err, "Failed to get navigator set.");
    }

    API_NavigatorItem rootItem = {};
    err = ACAPI_Navigator_GetNavigatorItem (&navSet.rootGuid, &rootItem);
    if (err != NoError) {
        return CreateErrorResponse (err, "Failed to get root navigator item.");
    }

    GS::ObjectState response;
    response.Add ("navigatorItemTree", NavigatorItemToObjectState (rootItem, mapId));
    return response;
}

// Updated 19 September 2026, 13:14 CEST. Borrow source settings only for the
// synchronous SDK call; its original view owns the custom-data allocations.
GS::Optional<GS::UniString> CopyViewSettingsCommand::GetInputParametersSchema () const
{
    return R"({"type":"object","properties":{
        "sourceViewId":{"$ref":"#/NavigatorItemId"},
        "targetViewIds":{"type":"array","minItems":1,"maxItems":100,"uniqueItems":true,"items":{"$ref":"#/NavigatorItemId"}},
        "settingGroups":{"type":"array","minItems":1,"maxItems":10,"uniqueItems":true,"items":{"enum":["ModelViewOptions","Layers","Scale","Dimensions","Pens","StructureDisplay","ZoomAndRotation","Renovation","GraphicOverrides","Rendering"]}}
    },"required":["sourceViewId","targetViewIds","settingGroups"],"additionalProperties":false})";
}
GS::Optional<GS::UniString> CopyViewSettingsCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{"sourceViewId":{"$ref":"#/NavigatorItemId"},"transactionSemantics":{"const":"PerTargetNonAtomic"},"results":{"type":"array","items":{"type":"object"}}},"required":["sourceViewId","transactionSemantics","results"],"additionalProperties":false})";
}
GS::ObjectState CopyViewSettingsCommand::Execute (const GS::ObjectState& parameters,GS::ProcessControl& processControl) const
{
    const API_Guid sourceId=GetGuidFromArrayItem ("sourceViewId",parameters);
    API_NavigatorItem sourceItem = {}; sourceItem.mapId=API_PublicViewMap;
    GSErrCode err=ACAPI_Navigator_GetNavigatorItem (&sourceId,&sourceItem);
    if (err!=NoError) return CreateErrorResponse (err,"Cannot read source view.");
    if (sourceItem.mapId!=API_PublicViewMap || sourceItem.itemType==API_FolderNavItem) return CreateErrorResponse (APIERR_BADPARS,"Source must be a saved public view.");
    API_NavigatorView source = {};
    const GS::OnExit freeSource ([&] { DisposeNavigatorViewData (source); });
    err=ACAPI_Navigator_GetNavigatorView (&sourceItem,&source);
    if (err!=NoError) return CreateErrorResponse (err,"Cannot read source saved settings.");
    GS::Array<GS::ObjectState> ids; parameters.Get ("targetViewIds",ids);
    GS::Array<GS::UniString> groups; parameters.Get ("settingGroups",groups);
    if (ids.IsEmpty () || ids.GetSize ()>100 || groups.IsEmpty () || groups.GetSize ()>10) return CreateErrorResponse (APIERR_BADPARS,"Invalid target or settings count.");
    GS::HashSet<GS::UniString> selected;
    for (const auto& group:groups) {
        if (group!="ModelViewOptions" && group!="Layers" && group!="Scale" && group!="Dimensions" && group!="Pens" && group!="StructureDisplay" && group!="ZoomAndRotation" && group!="Renovation" && group!="GraphicOverrides" && group!="Rendering") return CreateErrorResponse (APIERR_BADPARS,"Unknown settings group.");
        if (selected.Contains (group)) return CreateErrorResponse (APIERR_BADPARS,"Repeated settings group.");
        selected.Add (group);
    }
    if ((selected.Contains ("ModelViewOptions") && source.saveDispOpt && source.modelViewOptName[0]==0 && source.modelViewOpt==nullptr) ||
        (selected.Contains ("Layers") && source.saveLaySet && source.layerCombination[0]==0 && source.layerStats==nullptr) ||
        (selected.Contains ("Dimensions") && source.saveDim && source.dimName[0]==0 && source.dimPrefs==nullptr) ||
        (selected.Contains ("Pens") && source.savePenSet && source.penSetName[0]==0 && source.pens==nullptr))
        return CreateErrorResponse (APIERR_GENERAL,"Source custom settings could not be read; nothing copied.");
    GS::Array<API_NavigatorItem> targets; GS::HashSet<API_Guid> seen;
    for (const auto& id:ids) {
        const API_Guid guid=GetGuidFromObjectState (id);
        if (guid==APINULLGuid || guid==sourceId || seen.Contains (guid)) return CreateErrorResponse (APIERR_BADPARS,"Targets must be distinct views other than the source.");
        seen.Add (guid); API_NavigatorItem target = {}; target.mapId=API_PublicViewMap;
        err=ACAPI_Navigator_GetNavigatorItem (&guid,&target);
        if (err!=NoError) return CreateErrorResponse (err,"Cannot inspect a target view; nothing copied.");
        if (target.mapId!=API_PublicViewMap || target.itemType!=sourceItem.itemType) return CreateErrorResponse (APIERR_BADPARS,"Copy settings between saved views of the same native kind.");
        targets.Push (target);
    }
    GS::ObjectState result ("sourceViewId",CreateGuidObjectState (sourceId),"transactionSemantics","PerTargetNonAtomic");
    const auto& add=result.AddList<GS::ObjectState> ("results");
    for (auto& target:targets) {
        API_NavigatorView view = {};
        err=ACAPI_Navigator_GetNavigatorView (&target,&view);
        API_NavigatorView owned=view;
        const GS::OnExit freeTarget ([&] { DisposeNavigatorViewData (owned); });
        if (err==NoError) {
            if (selected.Contains ("ModelViewOptions")) { CHCopyC (source.modelViewOptName,view.modelViewOptName); view.modelViewOpt=source.modelViewOpt; view.saveDispOpt=source.saveDispOpt; }
            if (selected.Contains ("Layers")) { CHCopyC (source.layerCombination,view.layerCombination); view.layerStats=source.layerStats; view.saveLaySet=source.saveLaySet; }
            if (selected.Contains ("Scale")) { view.drawingScale=source.drawingScale; view.saveDScale=source.saveDScale; }
            if (selected.Contains ("Dimensions")) { CHCopyC (source.dimName,view.dimName); view.dimPrefs=source.dimPrefs; view.saveDim=source.saveDim; }
            if (selected.Contains ("Pens")) { CHCopyC (source.penSetName,view.penSetName); view.pens=source.pens; view.savePenSet=source.savePenSet; }
            if (selected.Contains ("StructureDisplay")) { view.structureDisplay=source.structureDisplay; view.saveStructureDisplay=source.saveStructureDisplay; }
            if (selected.Contains ("ZoomAndRotation")) { view.zoom=source.zoom; view.saveZoom=source.saveZoom; view.ignoreSavedZoom=source.ignoreSavedZoom; view.tr=source.tr; }
            if (selected.Contains ("Renovation")) view.renovationFilterGuid=source.renovationFilterGuid;
            if (selected.Contains ("GraphicOverrides")) GS::ucscpy (view.overrideCombination,source.overrideCombination);
            if (selected.Contains ("Rendering")) { GS::ucscpy (view.d3styleName,source.d3styleName); GS::ucscpy (view.renderingSceneName,source.renderingSceneName); view.usePhotoRendering=source.usePhotoRendering; }
            err=processControl.TestBreak ()?APIERR_CANCEL:ACAPI_Navigator_ChangeNavigatorView (&target,&view);
        }
        GS::ObjectState row ("navigatorItemId",CreateGuidObjectState (target.guid),"status",err==NoError?"executionAccepted":"failed","success",err==NoError,"customSettingsVerified",false);
        if (err!=NoError) row.Add ("error",*CreateErrorResponse (err,"Could not copy view settings; earlier targets may have changed.").Get ("error"));
        else {
            GS::Array<GS::ObjectState> readIds; readIds.Push (GS::ObjectState ("navigatorItemId",CreateGuidObjectState (target.guid)));
            row.Add ("readback",GetViewSettingsCommand ().Execute (GS::ObjectState ("navigatorItemIds",readIds),processControl));
        }
        add (row);
    }
    return result;
}

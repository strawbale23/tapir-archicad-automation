#include "AnnotationCommands.hpp"
#include "DimensionAnchorCommands.hpp"
#include "MigrationHelper.hpp"
#include <cmath>
#include <string>
#include <utility>

GS::Optional<GS::UniString> EditDimensionChainCommand::GetInputParametersSchema () const
{
    return R"({"type":"object","properties":{
        "elementId":{"$ref":"#/ElementId"},"expectedModificationStamp":{"type":"string","pattern":"^[0-9]+$"},
        "witnesses":{"type":"array","minItems":2,"maxItems":1000,"description":"Retain witnesses by zero-based index from GetDimensionData, or supply a new native association. Omitted witnesses are removed. Native geometric order applies.","items":{"oneOf":[
            {"type":"object","properties":{"anchor":{"type":"object","properties":{"elementId":{"$ref":"#/ElementId"},"hotspot":{"$ref":"#/NativeDimensionHotspot"}},"required":["elementId","hotspot"],"additionalProperties":false}},"required":["anchor"],"additionalProperties":false},
            {"type":"object","properties":{"existingIndex":{"type":"integer","minimum":0}},"required":["existingIndex"],"additionalProperties":false},
            {"type":"object","properties":{"anchor":{"type":"object","properties":{"elementId":{"$ref":"#/ElementId"},"line":{"type":"boolean"},"inIndex":{"type":"integer"},"special":{"type":"integer","minimum":-128,"maximum":127},"nodeType":{"type":"integer","minimum":-32768,"maximum":32767},"nodeStatus":{"type":"integer","minimum":-32768,"maximum":32767},"nodeId":{"type":"integer","minimum":0,"maximum":4294967295}},"required":["elementId","line","inIndex"],"additionalProperties":false}},"required":["anchor"],"additionalProperties":false}
        ]}}
    },"required":["elementId","expectedModificationStamp","witnesses"],"additionalProperties":false})";
}
GS::Optional<GS::UniString> EditDimensionChainCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{"elementId":{"$ref":"#/ElementId"},"success":{"type":"boolean"},"status":{"const":"executionAccepted"},"verification":{"const":"nativeWitnessCountReadBack"},"witnessCount":{"type":"integer"}},"required":["elementId","success","status","verification","witnessCount"],"additionalProperties":false})";
}
GS::ObjectState EditDimensionChainCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl&) const
{
    API_Element element = {};
    element.header.guid = GetGuidFromArrayItem ("elementId",parameters);
    GSErrCode err = ACAPI_Element_Get (&element);
    if (err != NoError) return CreateErrorResponse(err,"Cannot read dimension.");
    if (GetElemTypeId(element.header)!=API_DimensionID || element.dimension.usedIn3D)
        return CreateErrorResponse(APIERR_BADPARS,"Only 2D linear dimension chains are supported.");
    GS::UniString expected;
    parameters.Get("expectedModificationStamp",expected);
    if (expected!=GS::UniString(std::to_string(element.header.modiStamp).c_str())) return CreateErrorResponse(APIERR_BADPARS,"Dimension has changed; read GetDimensionData before editing.");
    GS::Array<GS::ObjectState> witnesses; parameters.Get("witnesses",witnesses);
    if (witnesses.GetSize()<2 || witnesses.GetSize()>1000) return CreateErrorResponse(APIERR_BADPARS,"Supply 2 to 1000 witnesses.");
    API_ElementMemo memo = {}, replacement = {};
    const GS::OnExit dispose([&](){ACAPI_DisposeElemMemoHdls(&memo);ACAPI_DisposeElemMemoHdls(&replacement);});
    err=ACAPI_Element_GetMemo(element.header.guid,&memo);
    if (err!=NoError || memo.dimElems==nullptr) return CreateErrorResponse(err==NoError?APIERR_GENERAL:err,"Cannot read witness memo.");
    if (element.dimension.nDimElem<0 || element.dimension.nDimElem>10000 ||
        BMhGetSize (reinterpret_cast<GSHandle> (memo.dimElems))<static_cast<GSSize> (element.dimension.nDimElem*sizeof(API_DimElem)))
        return CreateErrorResponse (APIERR_GENERAL,"Native witness chain has invalid or excessive size.");
    replacement.dimElems=reinterpret_cast<API_DimElem**>(BMhAllClear(witnesses.GetSize()*sizeof(API_DimElem)));
    if (replacement.dimElems==nullptr) return CreateErrorResponse(APIERR_MEMFULL,"Cannot allocate witness chain.");
    GS::HashSet<Int32> retained;
    for (UIndex index=0;index<witnesses.GetSize();++index) {
        auto& target=(*replacement.dimElems)[index];
        Int32 existing=-1;
        if (witnesses[index].Get("existingIndex",existing)) {
            if (existing<0 || existing>=element.dimension.nDimElem || retained.Contains(existing)) return CreateErrorResponse(APIERR_BADPARS,"Existing witness index is invalid or repeated.");
            retained.Add(existing); target=(*memo.dimElems)[existing];
            if (target.note.contentUStr!=nullptr) target.note.contentUStr=new GS::UniString(*target.note.contentUStr);
        } else {
            const auto* anchor=witnesses[index].Get("anchor");
            if (anchor==nullptr) return CreateErrorResponse(APIERR_BADPARS,"Witness must retain an index or specify an anchor.");
            if (anchor->Contains ("hotspot")) {
                GS::UniString message;
                err = ResolveNativeDimensionHotspot (*anchor,target.base.base,message);
                if (err != NoError) return CreateErrorResponse (err,message);
            } else {
            API_Element owner = {}; owner.header.guid=GetGuidFromArrayItem("elementId",*anchor);
            err=ACAPI_Element_Get(&owner);
            if (err!=NoError) return CreateErrorResponse(err,"Cannot read anchor element.");
#ifdef ServerMainVers_2600
            target.base.base.type=owner.header.type;
#else
            target.base.base.typeID=owner.header.typeID;
#endif
            target.base.base.guid=owner.header.guid;
            anchor->Get("line",target.base.base.line); anchor->Get("inIndex",target.base.base.inIndex);
            Int32 special=0,nodeType=0,nodeStatus=0;
            anchor->Get("special",special); anchor->Get("nodeType",nodeType); anchor->Get("nodeStatus",nodeStatus);
            if (special<-128 || special>127 || nodeType<-32768 || nodeType>32767 || nodeStatus<-32768 || nodeStatus>32767) return CreateErrorResponse(APIERR_BADPARS,"Native anchor descriptor is out of range.");
            target.base.base.special=static_cast<char>(special);target.base.base.node_typ=static_cast<short>(nodeType);target.base.base.node_status=static_cast<short>(nodeStatus);
            anchor->Get("nodeId",target.base.base.node_id);
            }
            target.note=element.dimension.defNote;
            if(target.note.contentUStr!=nullptr) target.note.contentUStr=new GS::UniString(*target.note.contentUStr);
            target.witnessVal=element.dimension.defWitnessVal;target.witnessForm=element.dimension.defWitnessForm;
        }
    }
    std::swap(memo.dimElems,replacement.dimElems);
    element.dimension.nDimElem=static_cast<Int32>(witnesses.GetSize());
    API_Element mask = {}; ACAPI_ELEMENT_MASK_CLEAR(mask);ACAPI_ELEMENT_MASK_SET(mask,API_DimensionType,nDimElem);
    err=ACAPI_CallUndoableCommand("Edit Dimension Chain",[&]()->GSErrCode {return ACAPI_Element_Change(&element,&mask,&memo,APIMemoMask_All,true);});
    if(err!=NoError)return CreateErrorResponse(err,"Dimension chain transaction failed.");
    API_Element actual = {};actual.header.guid=element.header.guid;
    err=ACAPI_Element_Get(&actual);
    if(err!=NoError || actual.dimension.nDimElem!=element.dimension.nDimElem)return CreateErrorResponse(err==NoError?APIERR_GENERAL:err,"Dimension edit committed but witness count is not confirmed; inspect before retrying.");
    GS::ObjectState response=CreateElementIdObjectState(element.header.guid);
    response.Add("success",true);response.Add("status","executionAccepted");response.Add("verification","nativeWitnessCountReadBack");response.Add("witnessCount",actual.dimension.nDimElem);
    return response;
}

GS::Optional<GS::UniString> GetAnnotationDetailsCommand::GetInputParametersSchema () const
{
    return R"({"type":"object","properties":{"elements":{"type":"array","minItems":1,"maxItems":100,"items":{
        "type":"object","properties":{"elementId":{"$ref":"#/ElementId"}},"required":["elementId"],"additionalProperties":false
    }}},"required":["elements"],"additionalProperties":false})";
}
GS::Optional<GS::UniString> GetAnnotationDetailsCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{"annotations":{"type":"array","items":{
        "type":"object","properties":{
            "elementId":{"$ref":"#/ElementId"},"type":{"type":"string"},"text":{"type":"string"},
            "baseTextStyle":{"type":"object","description":"Native base/default style. Individual rich-text runs may override these fields."},
            "coordinate":{"$ref":"#/Coordinate2D"},"angle":{"type":"number"},"widthMillimetres":{"type":"number"},
            "heightMillimetres":{"type":"number"},"nonBreaking":{"type":"boolean"},
            "begCoordinate":{"$ref":"#/Coordinate2D"},"midCoordinate":{"$ref":"#/Coordinate2D"},"endCoordinate":{"$ref":"#/Coordinate2D"},
            "parentElementId":{"$ref":"#/ElementId"},"symbolic":{"type":"boolean"},"hasLeaderLine":{"type":"boolean"},
            "error":{"type":"object"}
        },"required":["elementId"],"additionalProperties":false
    }},"coordinateSystem":{"type":"string"},"coordinateUnit":{"type":"string"}},
    "required":["annotations","coordinateSystem","coordinateUnit"],"additionalProperties":false})";
}
GS::ObjectState GetAnnotationDetailsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl&) const
{
    GS::Array<GS::ObjectState> elements;
    parameters.Get ("elements", elements);
    if (elements.IsEmpty () || elements.GetSize () > 100)
        return CreateErrorResponse (APIERR_BADPARS, "Supply 1 to 100 annotations.");
    GS::ObjectState response ("coordinateSystem", "CurrentDatabaseXY", "coordinateUnit", "metres");
    const auto& add = response.AddList<GS::ObjectState> ("annotations");
    for (const auto& item : elements) {
        API_Element element = {};
        element.header.guid = GetGuidFromArrayItem ("elementId", item);
        GS::ObjectState row = CreateElementIdObjectState (element.header.guid);
        GSErrCode err = ACAPI_Element_Get (&element);
        if (err != NoError) {
            row.Add ("error", *CreateErrorResponse (err, "Cannot read annotation.").Get ("error")); add (row); continue;
        }
        const API_ElemTypeID type = GetElemTypeId (element.header);
        if (type != API_TextID && type != API_LabelID) {
            row.Add ("error", *CreateErrorResponse (APIERR_BADPARS, "Only Text and Label elements are supported.").Get ("error")); add (row); continue;
        }
        row.Add ("type", type == API_TextID ? "Text" : "Label");
        const bool symbolic = type == API_LabelID && element.label.labelClass != APILblClass_Text;
        if (!symbolic) {
            API_ElementMemo memo = {};
            const GS::OnExit dispose ([&] () { ACAPI_DisposeElemMemoHdls (&memo); });
            err = ACAPI_Element_GetMemo (element.header.guid, &memo, APIMemoMask_TextContent);
            if (err != NoError) row.Add ("error", *CreateErrorResponse (err, "Cannot read annotation text content.").Get ("error"));
            else {
#ifdef ServerMainVers_2800
                row.Add ("text", memo.textContent != nullptr ? *memo.textContent : GS::UniString ());
#else
                row.Add ("text", memo.textContent != nullptr ? GS::UniString (reinterpret_cast<GS::uchar_t*> (*memo.textContent)) : GS::UniString ());
#endif
            }
        }
        if (!symbolic) {
            const API_TextType& text = type == API_TextID ? element.text : element.label.u.text;
            GS::ObjectState baseStyle ("fontIndex", text.font, "sizeMillimetres", text.size, "pen", text.pen,
                "bold", (text.faceBits & APIFace_Bold) != 0, "italic", (text.faceBits & APIFace_Italic) != 0,
                "underline", (text.faceBits & APIFace_Underline) != 0,
                "alignment", text.just == APIJust_Center ? "Center" : text.just == APIJust_Right ? "Right" : text.just == APIJust_Full ? "Justified" : "Left",
                "lineSpacingFactor", text.spacing, "fixedSize", text.fixedSize, "fixedAngle", text.fixedAngle,
                "widthFactor", text.widthFactor, "characterSpacingFactor", text.charSpaceFactor);
#ifdef ServerMainVers_2700
            API_FontType font = {}; font.head.index = text.font;
            GS::UniString fontName; font.head.uniStringNamePtr = &fontName;
            if (ACAPI_Font_GetFont (font) == NoError) baseStyle.Add ("fontName", fontName);
#endif
            row.Add ("baseTextStyle", baseStyle);
        }
        if (type == API_TextID) {
            row.Add ("coordinate", Create2DCoordinateObjectState (element.text.loc));
            row.Add ("angle", element.text.angle);
            row.Add ("widthMillimetres", element.text.width);
            row.Add ("heightMillimetres", element.text.height);
            row.Add ("nonBreaking", element.text.nonBreaking);
        } else {
            row.Add ("symbolic", symbolic);
            row.Add ("begCoordinate", Create2DCoordinateObjectState (element.label.begC));
            row.Add ("midCoordinate", Create2DCoordinateObjectState (element.label.midC));
            row.Add ("endCoordinate", Create2DCoordinateObjectState (element.label.endC));
            row.Add ("hasLeaderLine", element.label.hasLeaderLine);
            if (element.label.parent != APINULLGuid) row.Add ("parentElementId", CreateGuidObjectState (element.label.parent));
        }
        add (row);
    }
    return response;
}

GS::Optional<GS::UniString> ModifyDimensionSettingsCommand::GetInputParametersSchema () const
{
    return R"({"type":"object","properties":{"dimensionsWithSettings":{"type":"array","minItems":1,"maxItems":100,"items":{
        "type":"object","properties":{
            "elementId":{"$ref":"#/ElementId"},"referencePoint":{"$ref":"#/Coordinate2D"},
            "direction":{"$ref":"#/Coordinate2D"},"linePen":{"type":"integer","minimum":1,"maximum":255},
            "horizontalText":{"type":"boolean"}
        },"required":["elementId"],"minProperties":2,"additionalProperties":false
    }}},"required":["dimensionsWithSettings"],"additionalProperties":false})";
}
GS::Optional<GS::UniString> ModifyDimensionSettingsCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{"executionResults":{"$ref":"#/ExecutionResults"}},"required":["executionResults"],"additionalProperties":false})";
}
GS::ObjectState ModifyDimensionSettingsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl&) const
{
    GS::Array<GS::ObjectState> items;
    parameters.Get ("dimensionsWithSettings", items);
    if (items.IsEmpty () || items.GetSize () > 100) return CreateErrorResponse (APIERR_BADPARS, "Supply 1 to 100 dimension settings.");
    GS::ObjectState response;
    const auto& add = response.AddList<GS::ObjectState> ("executionResults");
    const GSErrCode transaction = ACAPI_CallUndoableCommand ("Modify Dimension Settings", [&] () -> GSErrCode {
        for (const auto& item : items) {
            API_Element element = {}, mask;
            element.header.guid = GetGuidFromArrayItem ("elementId", item);
            GSErrCode err = ACAPI_Element_Get (&element);
            if (err != NoError) { add (CreateFailedExecutionResult (err, "Cannot read dimension.")); continue; }
            if (GetElemTypeId (element.header) != API_DimensionID || element.dimension.usedIn3D) {
                add (CreateFailedExecutionResult (APIERR_BADPARS, "Only native 2D linear dimensions are supported by this command.")); continue;
            }
            ACAPI_ELEMENT_MASK_CLEAR (mask);
            bool changed = false;
            if (const auto* point = item.Get ("referencePoint")) {
                element.dimension.refC = Get2DCoordinateFromObjectState (*point);
                ACAPI_ELEMENT_MASK_SET (mask, API_DimensionType, refC); changed = true;
            }
            if (const auto* direction = item.Get ("direction")) {
                const auto vector = Get2DCoordinateFromObjectState (*direction);
                const double length = std::hypot (vector.x, vector.y);
                if (!std::isfinite (length) || length < 1e-9) { add (CreateFailedExecutionResult (APIERR_BADPARS, "Dimension direction must be finite and nonzero.")); continue; }
                element.dimension.direction = {vector.x / length, vector.y / length};
                ACAPI_ELEMENT_MASK_SET (mask, API_DimensionType, direction); changed = true;
            }
            if (item.Get ("linePen", element.dimension.linPen)) { ACAPI_ELEMENT_MASK_SET (mask, API_DimensionType, linPen); changed = true; }
            if (item.Get ("horizontalText", element.dimension.horizontalText)) { ACAPI_ELEMENT_MASK_SET (mask, API_DimensionType, horizontalText); changed = true; }
            if (!changed) { add (CreateFailedExecutionResult (APIERR_BADPARS, "No dimension settings supplied.")); continue; }
            // Preserve the dimension memo and its existing witness associations.
            err = ACAPI_Element_Change (&element, &mask, nullptr, 0, true);
            add (err == NoError ? CreateSuccessfulExecutionResult () : CreateFailedExecutionResult (err, "Native dimension settings edit failed."));
        }
        return NoError;
    });
    if (transaction != NoError) return CreateErrorResponse (transaction, "Dimension transaction failed; committed state is not confirmed.");
    return response;
}

GS::Optional<GS::UniString> SetAnnotationTextStyleCommand::GetInputParametersSchema () const
{
    return R"({"type":"object","properties":{
        "elements":{"type":"array","minItems":1,"maxItems":100,"items":{"type":"object","properties":{"elementId":{"$ref":"#/ElementId"}},"required":["elementId"],"additionalProperties":false}},
        "style":{"type":"object","minProperties":1,"properties":{
            "fontName":{"type":"string","minLength":1,"maxLength":255},
            "sizeMillimetres":{"type":"number","exclusiveMinimum":0},"pen":{"type":"integer","minimum":1,"maximum":255},
            "bold":{"type":"boolean"},"italic":{"type":"boolean"},"underline":{"type":"boolean"},
            "alignment":{"type":"string","enum":["Left","Center","Right","Justified"]},
            "lineSpacingFactor":{"type":"number","minimum":-10,"maximum":-1},
            "fixedSize":{"type":"boolean"},"fixedAngle":{"type":"boolean"},
            "widthFactor":{"type":"number","minimum":0.75,"maximum":10},
            "characterSpacingFactor":{"type":"number","minimum":0.75,"maximum":10}
        },"additionalProperties":false,"description":"Supplied fields are applied to every existing paragraph/run. Content, other style fields and run ranges are retained. Text and text labels only."}
    },"required":["elements","style"],"additionalProperties":false})";
}
GS::Optional<GS::UniString> SetAnnotationTextStyleCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{"executionResults":{"$ref":"#/ExecutionResults"}},"required":["executionResults"],"additionalProperties":false})";
}
GS::ObjectState SetAnnotationTextStyleCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl&) const
{
    GS::Array<GS::ObjectState> elements; parameters.Get ("elements", elements);
    const auto* style = parameters.Get ("style");
    if (style == nullptr || elements.IsEmpty () || elements.GetSize () > 100)
        return CreateErrorResponse (APIERR_BADPARS, "Supply a style and 1 to 100 text/text-label elements.");
    short fontIndex = 0;
    GS::UniString fontName;
    if (style->Get ("fontName", fontName)) {
#ifdef ServerMainVers_2700
        API_FontType font = {}; font.head.uniStringNamePtr = &fontName;
        const GSErrCode err = ACAPI_Font_GetFont (font);
        if (err != NoError || font.head.index < 1 || font.head.index > 32767)
            return CreateErrorResponse (err != NoError ? err : APIERR_BADINDEX, "Requested font is unavailable or outside the native text font range.");
        fontIndex = static_cast<short> (font.head.index);
#else
        return CreateErrorResponse (APIERR_NOTSUPPORTED, "Font-name resolution in this command requires Archicad 27 or newer.");
#endif
    }
    const auto face = [&] (unsigned short original) {
        bool value;
        for (const auto& flag : {std::pair<const char*, unsigned short> ("bold", APIFace_Bold),
             {"italic", APIFace_Italic}, {"underline", APIFace_Underline}}) {
            if (style->Get (flag.first, value)) original = value ? original | flag.second : original & ~flag.second;
        }
        return original;
    };
    GS::UniString alignment;
    const bool setAlignment = style->Get ("alignment", alignment);
    const API_JustID just = alignment == "Center" ? APIJust_Center : alignment == "Right" ? APIJust_Right : alignment == "Justified" ? APIJust_Full : APIJust_Left;
    GS::ObjectState response;
    const auto& add = response.AddList<GS::ObjectState> ("executionResults");
    const GSErrCode transaction = ACAPI_CallUndoableCommand ("Set annotation text style", [&] () -> GSErrCode {
        for (const auto& item : elements) {
            API_Element element = {}, mask = {};
            element.header.guid = GetGuidFromArrayItem ("elementId", item);
            GSErrCode err = ACAPI_Element_Get (&element);
            if (err != NoError) { add (CreateFailedExecutionResult (err, "Cannot read annotation.")); continue; }
            const auto type = GetElemTypeId (element.header);
            const bool label = type == API_LabelID;
            if ((type != API_TextID && !label) || (label && element.label.labelClass != APILblClass_Text)) {
                add (CreateFailedExecutionResult (APIERR_BADPARS, "Style applies to Text and textual Label elements; symbolic labels use their own settings/parameters.")); continue;
            }
            API_TextType& text = label ? element.label.u.text : element.text;
            ACAPI_ELEMENT_MASK_CLEAR (mask);
            API_TextType& textMask = label ? mask.label.u.text : mask.text;
            // API_LabelType.u.text shares the native text structure layout.
#define TAPIR_TEXT_STYLE(field, key) if (style->Get (key, text.field)) { memset (&textMask.field, 0xFF, sizeof (textMask.field)); }
            TAPIR_TEXT_STYLE (size, "sizeMillimetres")
            TAPIR_TEXT_STYLE (pen, "pen")
            TAPIR_TEXT_STYLE (spacing, "lineSpacingFactor")
            TAPIR_TEXT_STYLE (fixedSize, "fixedSize")
            TAPIR_TEXT_STYLE (fixedAngle, "fixedAngle")
            TAPIR_TEXT_STYLE (widthFactor, "widthFactor")
            TAPIR_TEXT_STYLE (charSpaceFactor, "characterSpacingFactor")
#undef TAPIR_TEXT_STYLE
            if (fontIndex != 0) { text.font = fontIndex; memset (&textMask.font, 0xFF, sizeof (textMask.font)); }
            if (setAlignment) { text.just = just; memset (&textMask.just, 0xFF, sizeof (textMask.just)); }
            if (style->Contains ("bold") || style->Contains ("italic") || style->Contains ("underline")) {
                text.faceBits = face (text.faceBits); memset (&textMask.faceBits, 0xFF, sizeof (textMask.faceBits));
            }
            if ((style->Contains ("sizeMillimetres") && (!std::isfinite (text.size) || text.size <= 0)) ||
                (style->Contains ("lineSpacingFactor") && (!std::isfinite (text.spacing) || text.spacing < -10 || text.spacing > -1)) ||
                (style->Contains ("widthFactor") && (!std::isfinite (text.widthFactor) || text.widthFactor < 0.75 || text.widthFactor > 10)) ||
                (style->Contains ("characterSpacingFactor") && (!std::isfinite (text.charSpaceFactor) || text.charSpaceFactor < 0.75 || text.charSpaceFactor > 10)) ||
                (style->Contains ("pen") && (text.pen < 1 || text.pen > 255))) {
                add (CreateFailedExecutionResult (APIERR_BADPARS, "Text size, pen or spacing is outside the supported native range.")); continue;
            }
            API_ElementMemo memo = {};
            const GS::OnExit dispose ([&] () { ACAPI_DisposeElemMemoHdls (&memo); });
            err = ACAPI_Element_GetMemo (element.header.guid, &memo, APIMemoMask_Paragraph);
            if (err != NoError) { add (CreateFailedExecutionResult (err, "Cannot read existing paragraph formatting.")); continue; }
            if (memo.paragraphs != nullptr) {
                const GSSize count = BMhGetSize (reinterpret_cast<GSHandle> (memo.paragraphs)) / sizeof (API_ParagraphType);
                for (GSSize i=0; i<count; ++i) {
                    auto& paragraph = (*memo.paragraphs)[i];
                    if (setAlignment) paragraph.just = just;
                    style->Get ("lineSpacingFactor", paragraph.spacing);
                    const GSSize runs = paragraph.run == nullptr ? 0 : BMGetPtrSize (reinterpret_cast<GSPtr> (paragraph.run)) / sizeof (API_RunType);
                    for (GSSize j=0; j<runs; ++j) {
                        auto& run = paragraph.run[j];
                        if (fontIndex != 0) run.font = fontIndex;
                        style->Get ("sizeMillimetres", run.size); style->Get ("pen", run.pen);
                        run.faceBits = face (run.faceBits);
                    }
                }
            }
            err = ACAPI_Element_Change (&element, &mask, &memo, APIMemoMask_Paragraph, true);
            add (err == NoError ? CreateSuccessfulExecutionResult () : CreateFailedExecutionResult (err, "Native text style edit failed."));
        }
        return NoError;
    });
    if (transaction != NoError) return CreateErrorResponse (transaction, "Text style transaction failed; committed changes are not confirmed.");
    return response;
}

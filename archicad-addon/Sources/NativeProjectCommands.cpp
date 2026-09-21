#include "NativeProjectCommands.hpp"
#include "MigrationHelper.hpp"
#include <cmath>
#include "NativeQuantityDefinitions.hpp"

GS::Optional<GS::UniString> GetNativeQuantitiesCommand::GetInputParametersSchema () const
{
    return R"({"type":"object","properties":{
        "elements":{"type":"array","minItems":1,"maxItems":100,"items":{"type":"object","properties":{"elementId":{"$ref":"#/ElementId"}},"required":["elementId"],"additionalProperties":false}},
        "minimumWallOpeningArea":{"type":"number","minimum":0,"description":"Square metres. Openings below this area are not subtracted from wall surface calculations. Default 0."}
    },"required":["elements"],"additionalProperties":false})";
}

GS::Optional<GS::UniString> GetNativeQuantitiesCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{
        "source":{"type":"string"},"minimumWallOpeningArea":{"type":"number"},
        "quantities":{"type":"array","items":{"type":"object","properties":{
            "elementId":{"$ref":"#/ElementId"},"type":{"type":"string"},
            "values":{"type":"array","items":{"type":"object","properties":{
                "name":{"type":"string"},"value":{"type":"number"},"unit":{"type":"string","enum":["m","m2","m3"]}
            },"required":["name","value","unit"],"additionalProperties":false}},
            "error":{"type":"object"}
        },"required":["elementId"],"additionalProperties":false}}
    },"required":["source","minimumWallOpeningArea","quantities"],"additionalProperties":false})";
}

GS::ObjectState GetNativeQuantitiesCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl&) const
{
    GS::Array<GS::ObjectState> elements;
    parameters.Get ("elements", elements);
    double minimum = 0;
    parameters.Get ("minimumWallOpeningArea", minimum);
    if (elements.IsEmpty () || elements.GetSize () > 100 || !std::isfinite (minimum) || minimum < 0)
        return CreateErrorResponse (APIERR_BADPARS, "Supply 1 to 100 elements and a finite nonnegative minimum wall opening area.");
    GS::ObjectState response ("source", "ACAPI_Element_GetQuantities", "minimumWallOpeningArea", minimum);
    const auto& add = response.AddList<GS::ObjectState> ("quantities");
    for (const auto& item : elements) {
        API_Element element = {};
        element.header.guid = GetGuidFromArrayItem ("elementId", item);
        GS::ObjectState row = CreateElementIdObjectState (element.header.guid);
        GSErrCode err = ACAPI_Element_Get (&element);
        if (err != NoError) {
            row.Add ("error", *CreateErrorResponse (err, "Cannot read requested element.").Get ("error"));
            add (row); continue;
        }
        const API_ElemTypeID type = GetElemTypeId (element.header);
        row.Add ("type", GetElementTypeNonLocalizedName (type));
        bool supported = false;
        for (const auto& definition : NativeQuantityDefinitions ()) supported |= definition.type == type;
        if (!supported) {
            row.Add ("error", *CreateErrorResponse (APIERR_BADPARS, "This quantity projection does not support this element family.").Get ("error"));
            add (row); continue;
        }
        API_ElementQuantity quantity = {};
        API_Quantities quantities = {};
        quantities.elements = &quantity;
        API_QuantitiesMask mask;
        ACAPI_ELEMENT_QUANTITY_MASK_SETFULL (mask);
        API_QuantityPar rules = {};
        rules.minOpeningSize = minimum;
        err = ACAPI_Element_GetQuantities (element.header.guid, &rules, &quantities, &mask);
        if (err != NoError) {
            row.Add ("error", *CreateErrorResponse (err, "Native quantity calculation failed.").Get ("error"));
            add (row); continue;
        }
        bool finite = true;
        for (const auto& definition : NativeQuantityDefinitions ())
            if (definition.type == type && !std::isfinite (definition.read (quantity))) finite = false;
        if (!finite) {
            row.Add ("error", *CreateErrorResponse (APIERR_GENERAL, "Native quantity calculation returned a non-finite value.").Get ("error"));
            add (row); continue;
        }
        const auto& addValue = row.AddList<GS::ObjectState> ("values");
        const auto value = [&] (const char* name, double number, const char* unit) {
            addValue (GS::ObjectState ("name", name, "value", number, "unit", unit));
        };
        for (const auto& definition : NativeQuantityDefinitions ()) {
            if (definition.type == type) value (definition.name, definition.read (quantity), definition.unit);
        }
        add (row);
    }
    return response;
}

GS::Optional<GS::UniString> SetElementRenovationStatusCommand::GetInputParametersSchema () const
{
    return R"({"type":"object","properties":{
        "elements":{"type":"array","minItems":1,"maxItems":100,"uniqueItems":true,"items":{"type":"object","properties":{"elementId":{"$ref":"#/ElementId"}},"required":["elementId"],"additionalProperties":false}},
        "status":{"type":"string","enum":["Existing","New","ToBeDemolished"]}
    },"required":["elements","status"],"additionalProperties":false})";
}

GS::Optional<GS::UniString> SetElementRenovationStatusCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{
        "executionResults":{"type":"array","items":{"type":"object","properties":{
            "elementId":{"$ref":"#/ElementId"},"success":{"type":"boolean"},"status":{"type":"string"},
            "renovationStatus":{"type":"string"},"error":{"type":"object"}
        },"required":["elementId","success"],"additionalProperties":false}}
    },"required":["executionResults"],"additionalProperties":false})";
}

GS::ObjectState SetElementRenovationStatusCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl&) const
{
    GS::Array<GS::ObjectState> elements;
    parameters.Get ("elements", elements);
    GS::UniString name;
    parameters.Get ("status", name);
    if (elements.IsEmpty () || elements.GetSize () > 100 || (name != "Existing" && name != "New" && name != "ToBeDemolished"))
        return CreateErrorResponse (APIERR_BADPARS, "Supply 1 to 100 elements and an explicit renovation status.");
    const API_RenovationStatusType desired = name == "Existing" ? API_ExistingStatus : name == "New" ? API_NewStatus : API_DemolishedStatus;
    GS::ObjectState response;
    const auto& add = response.AddList<GS::ObjectState> ("executionResults");
    const GSErrCode transaction = ACAPI_CallUndoableCommand ("Set Element Renovation Status", [&] () -> GSErrCode {
        for (const auto& item : elements) {
            API_Element element = {};
            element.header.guid = GetGuidFromArrayItem ("elementId", item);
            GS::ObjectState row = CreateElementIdObjectState (element.header.guid);
            GSErrCode err = ACAPI_Element_Get (&element);
            const bool unchanged = err == NoError && element.header.renovationStatus == desired;
            if (err == NoError && !unchanged) {
                API_Element mask;
                ACAPI_ELEMENT_MASK_CLEAR (mask);
                element.header.renovationStatus = desired;
                ACAPI_ELEMENT_MASK_SET (mask, API_Elem_Head, renovationStatus);
                err = ACAPI_Element_Change (&element, &mask, nullptr, 0, true);
            }
            if (err == NoError) err = ACAPI_Element_Get (&element);
            if (err == NoError && element.header.renovationStatus != desired) err = APIERR_GENERAL;
            row.Add ("success", err == NoError);
            if (err != NoError) row.Add ("error", *CreateErrorResponse (err, "Renovation status was not confirmed on native readback.").Get ("error"));
            else {
                row.Add ("status", unchanged ? "alreadySatisfied" : "applied");
                row.Add ("renovationStatus", name);
            }
            add (row);
        }
        return NoError;
    });
    if (transaction != NoError) return CreateErrorResponse (transaction, "Renovation transaction failed; committed state is not confirmed.");
    return response;
}

GS::Optional<GS::UniString> GetRenovationFiltersCommand::GetInputParametersSchema () const
{
    return R"({"type":"object","properties":{},"additionalProperties":false})";
}

GS::Optional<GS::UniString> GetRenovationFiltersCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{
        "renovationFilters":{"type":"array","items":{"type":"object","properties":{
            "renovationFilterId":{"$ref":"#/AttributeId"},"name":{"type":"string"}
        },"required":["renovationFilterId","name"],"additionalProperties":false}},
        "activeRenovationFilterId":{"$ref":"#/AttributeId"}
    },"required":["renovationFilters"],"additionalProperties":false})";
}

GS::ObjectState GetRenovationFiltersCommand::Execute (const GS::ObjectState&, GS::ProcessControl&) const
{
    GS::Array<API_Guid> filterGuids;
    GSErrCode err = ACAPI_Renovation_GetRenovationFilters (&filterGuids);
    if (err != NoError) return CreateErrorResponse (err, "Failed to list renovation filters.");

    GS::ObjectState response;
    const auto& add = response.AddList<GS::ObjectState> ("renovationFilters");
    for (const API_Guid& guid : filterGuids) {
        GS::UniString name;
        if (ACAPI_Renovation_GetRenovationFilterName (&guid, &name) != NoError) continue;
        add (GS::ObjectState ("renovationFilterId", CreateGuidObjectState (guid), "name", name));
    }

    API_Guid activeGuid = APINULLGuid;
    if (ACAPI_Renovation_GetActualRenovationFilter (&activeGuid) == NoError && activeGuid != APINULLGuid) {
        response.Add ("activeRenovationFilterId", CreateGuidObjectState (activeGuid));
    }
    return response;
}

GS::Optional<GS::UniString> SetActiveRenovationFilterCommand::GetInputParametersSchema () const
{
    return R"({"type":"object","properties":{
        "renovationFilterId":{"$ref":"#/AttributeId"}
    },"required":["renovationFilterId"],"additionalProperties":false})";
}

GS::Optional<GS::UniString> SetActiveRenovationFilterCommand::GetRawResponseSchema () const
{
    return R"({"$ref":"#/ExecutionResult"})";
}

GS::ObjectState SetActiveRenovationFilterCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl&) const
{
    const GS::ObjectState* filterIdOS = parameters.Get ("renovationFilterId");
    if (filterIdOS == nullptr) return CreateErrorResponse (APIERR_BADPARS, "renovationFilterId is required.");
    const API_Guid requested = GetGuidFromObjectState (*filterIdOS);

    GSErrCode err = ACAPI_Renovation_SetActualRenovationFilter (&requested);
    if (err != NoError) return CreateFailedExecutionResult (err, "Failed to set the active renovation filter.");

    API_Guid confirmedGuid = APINULLGuid;
    if (ACAPI_Renovation_GetActualRenovationFilter (&confirmedGuid) != NoError || confirmedGuid != requested) {
        return CreateFailedExecutionResult (APIERR_GENERAL, "Active renovation filter was not confirmed on readback.");
    }
    return CreateSuccessfulExecutionResult ();
}

GS::Optional<GS::UniString> GetNativeQuantityDefinitionsCommand::GetInputParametersSchema () const
{
    return R"({"type":"object","properties":{"elementType":{"type":"string","description":"Optional native element family, for example Wall or Roof. Omit to list the supported catalogue."}},"additionalProperties":false})";
}
GS::Optional<GS::UniString> GetNativeQuantityDefinitionsCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{
        "source":{"type":"string"},
        "definitions":{"type":"array","items":{"type":"object","properties":{
            "elementType":{"type":"string"},"name":{"type":"string"},"nativeField":{"type":"string"},
            "unit":{"type":"string","enum":["m","m2","m3"]}
        },"required":["elementType","name","nativeField","unit"],"additionalProperties":false}}
    },"required":["source","definitions"],"additionalProperties":false})";
}
GS::ObjectState GetNativeQuantityDefinitionsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl&) const
{
    GS::UniString requested;
    const bool filtered = parameters.Get ("elementType", requested);
    const API_ElemTypeID type = filtered ? GetElementTypeFromNonLocalizedName (requested) : API_ZombieElemID;
    GS::ObjectState response ("source", "API_ElementQuantity");
    const auto& add = response.AddList<GS::ObjectState> ("definitions");
    bool found = false;
    for (const auto& definition : NativeQuantityDefinitions ()) {
        if (filtered && definition.type != type) continue;
        found = true;
        add (GS::ObjectState ("elementType", GetElementTypeNonLocalizedName (definition.type),
            "name", definition.name, "nativeField", definition.nativeField, "unit", definition.unit));
    }
    if (!found) return CreateErrorResponse (APIERR_BADPARS, "No native quantity projection is available for the requested element type.");
    return response;
}

GS::Optional<GS::UniString> GetNativeComponentQuantitiesCommand::GetInputParametersSchema () const
{
    return R"({"type":"object","properties":{
        "elements":{"type":"array","minItems":1,"maxItems":100,"items":{"type":"object","properties":{"elementId":{"$ref":"#/ElementId"}},"required":["elementId"],"additionalProperties":false}},
        "minimumWallOpeningArea":{"type":"number","minimum":0,"default":0,"description":"Square metres; native wall opening subtraction threshold."}
    },"required":["elements"],"additionalProperties":false})";
}

GS::Optional<GS::UniString> GetNativeComponentQuantitiesCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{
        "source":{"type":"string"},"minimumWallOpeningArea":{"type":"number"},
        "projectedAreaCaveat":{"type":"string"},"units":{"type":"object","properties":{"volume":{"const":"m3"},"projectedArea":{"const":"m2"}},"required":["volume","projectedArea"],"additionalProperties":false},
        "elements":{"type":"array","items":{"type":"object","properties":{
            "elementId":{"$ref":"#/ElementId"},"error":{"type":"object"},
            "components":{"type":"array","items":{"type":"object","properties":{
                "nativeIndex":{"type":"integer"},"subElementId":{"$ref":"#/ElementId"},
                "volume":{"type":"number"},"projectedArea":{"type":"number"},"nativeFlags":{"type":"integer"},
                "buildingMaterialIndex":{"type":"integer"},"buildingMaterialId":{"$ref":"#/AttributeId"},"materialLookupError":{"type":"object"}
            },"required":["nativeIndex","volume","projectedArea","nativeFlags","buildingMaterialIndex"],"additionalProperties":false}}
        },"required":["elementId"],"additionalProperties":false}}
    },"required":["source","minimumWallOpeningArea","projectedAreaCaveat","units","elements"],"additionalProperties":false})";
}

GS::ObjectState GetNativeComponentQuantitiesCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl&) const
{
    GS::Array<GS::ObjectState> requested;
    parameters.Get ("elements", requested);
    double minimum = 0;
    parameters.Get ("minimumWallOpeningArea", minimum);
    if (requested.IsEmpty () || requested.GetSize () > 100 || !std::isfinite (minimum) || minimum < 0)
        return CreateErrorResponse (APIERR_BADPARS, "Supply 1 to 100 elements and a finite nonnegative opening area threshold.");
    GS::ObjectState response ("source", "ACAPI_Element_GetQuantities/API_CompositeQuantity",
        "minimumWallOpeningArea", minimum, "units", GS::ObjectState ("volume", "m3", "projectedArea", "m2"),
        "projectedAreaCaveat", "Native projectedArea is zero when legacy intersection/surface methods are enabled. Native component order is not a stable component identity.");
    const auto& add = response.AddList<GS::ObjectState> ("elements");
    for (const auto& item : requested) {
        const API_Guid guid = GetGuidFromArrayItem ("elementId", item);
        GS::ObjectState row = CreateElementIdObjectState (guid);
        API_Elem_Head head = {};
        head.guid = guid;
        GSErrCode err = ACAPI_Element_GetHeader (&head);
        GS::Array<API_CompositeQuantity> components;
        API_Quantities quantities;
        quantities.composites = &components;
        API_QuantitiesMask mask;
        ACAPI_ELEMENT_COMPOSITES_QUANTITY_MASK_SETFULL (mask);
        API_QuantityPar rules = {};
        rules.minOpeningSize = minimum;
        if (err == NoError) err = ACAPI_Element_GetQuantities (guid, &rules, &quantities, &mask);
        if (err == NoError) {
            for (const auto& component : components) {
                if (!std::isfinite (component.volumes) || !std::isfinite (component.projectedArea)) {
                    err = APIERR_BADPARS;
                    break;
                }
            }
        }
        if (err != NoError) {
            row.Add ("error", *CreateErrorResponse (err, "Cannot read finite native component quantities for this element.").Get ("error"));
            add (row);
            continue;
        }
        const auto& addComponent = row.AddList<GS::ObjectState> ("components");
        Int32 index = 0;
        for (const auto& component : components) {
            GS::ObjectState value ("nativeIndex", index++, "volume", component.volumes,
                "projectedArea", component.projectedArea, "nativeFlags", component.flags,
                "buildingMaterialIndex", GetAttributeIndex (component.buildMatIndices));
            if (component.compositeId.subElementGuid != APINULLGuid)
                value.Add ("subElementId", CreateGuidObjectState (component.compositeId.subElementGuid));
            API_Attribute material = {};
            material.header.typeID = API_BuildingMaterialID;
            material.header.index = component.buildMatIndices;
            const GSErrCode materialError = ACAPI_Attribute_Get (&material);
            if (materialError == NoError) value.Add ("buildingMaterialId", CreateGuidObjectState (material.header.guid));
            else value.Add ("materialLookupError", *CreateErrorResponse (materialError, "Cannot resolve component building material.").Get ("error"));
            addComponent (value);
        }
        add (row);
    }
    return response;
}

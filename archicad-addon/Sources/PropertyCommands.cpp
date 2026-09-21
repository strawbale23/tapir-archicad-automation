#include "PropertyCommands.hpp"
#include "MigrationHelper.hpp"
#include "HashTable.hpp"
#include "GSProcessControl.hpp"
#include <tuple>
#include <cmath>
#include <limits>

using PropertyTypeTuple = std::tuple<API_PropertyCollectionType, API_VariantType, API_PropertyMeasureType>;

static GS::HashTable<GS::UniString, PropertyTypeTuple> PropertyTypeDictionary = {
    { "number",  PropertyTypeTuple (API_PropertySingleCollectionType, API_PropertyRealValueType,    API_PropertyDefaultMeasureType) },
    { "integer", PropertyTypeTuple (API_PropertySingleCollectionType, API_PropertyIntegerValueType, API_PropertyDefaultMeasureType) },
    { "string",  PropertyTypeTuple (API_PropertySingleCollectionType, API_PropertyStringValueType,  API_PropertyDefaultMeasureType) },
    { "boolean", PropertyTypeTuple (API_PropertySingleCollectionType, API_PropertyBooleanValueType, API_PropertyDefaultMeasureType) },
    { "guid",    PropertyTypeTuple (API_PropertySingleCollectionType, API_PropertyGuidValueType,    API_PropertyDefaultMeasureType) },

    { "length", PropertyTypeTuple (API_PropertySingleCollectionType, API_PropertyRealValueType,  API_PropertyLengthMeasureType) },
    { "area",   PropertyTypeTuple (API_PropertySingleCollectionType, API_PropertyRealValueType,  API_PropertyAreaMeasureType) },
    { "volume", PropertyTypeTuple (API_PropertySingleCollectionType, API_PropertyRealValueType,  API_PropertyVolumeMeasureType) },
    { "angle",  PropertyTypeTuple (API_PropertySingleCollectionType, API_PropertyRealValueType,  API_PropertyAngleMeasureType) },

    { "singleEnum", PropertyTypeTuple (API_PropertySingleChoiceEnumerationCollectionType,   API_PropertyStringValueType, API_PropertyDefaultMeasureType) },
    { "multiEnum",  PropertyTypeTuple (API_PropertyMultipleChoiceEnumerationCollectionType, API_PropertyStringValueType, API_PropertyDefaultMeasureType) },

    { "numberList",  PropertyTypeTuple (API_PropertyListCollectionType, API_PropertyRealValueType,    API_PropertyDefaultMeasureType) },
    { "integerList", PropertyTypeTuple (API_PropertyListCollectionType, API_PropertyIntegerValueType, API_PropertyDefaultMeasureType) },
    { "stringList",  PropertyTypeTuple (API_PropertyListCollectionType, API_PropertyStringValueType,  API_PropertyDefaultMeasureType) },
    { "booleanList", PropertyTypeTuple (API_PropertyListCollectionType, API_PropertyBooleanValueType,  API_PropertyDefaultMeasureType) },

    { "lengthList", PropertyTypeTuple (API_PropertyListCollectionType, API_PropertyRealValueType, API_PropertyLengthMeasureType) },
    { "areaList",   PropertyTypeTuple (API_PropertyListCollectionType, API_PropertyRealValueType, API_PropertyAreaMeasureType) },
    { "volumeList", PropertyTypeTuple (API_PropertyListCollectionType, API_PropertyRealValueType, API_PropertyVolumeMeasureType) },
    { "angleList",  PropertyTypeTuple (API_PropertyListCollectionType, API_PropertyRealValueType, API_PropertyAngleMeasureType) },
};

static GS::UniString GetPropertyTypeString (API_PropertyDefinitionType type)
{
    static GS::HashTable<API_PropertyDefinitionType, GS::UniString> TypeToString ({
        { API_PropertyStaticBuiltInDefinitionType, "StaticBuiltIn" },
        { API_PropertyDynamicBuiltInDefinitionType, "DynamicBuiltIn" },
        { API_PropertyCustomDefinitionType, "Custom" }
    });
    if (!TypeToString.ContainsKey (type)) {
        return GS::EmptyUniString;
    }
    return TypeToString[type];
}

static GS::UniString GetPropertyTypeString (API_PropertyCollectionType type)
{
    static GS::HashTable<API_PropertyCollectionType, GS::UniString> TypeToString ({
        { API_PropertyUndefinedCollectionType, "Undefined" },
        { API_PropertySingleCollectionType, "Single" },
        { API_PropertyListCollectionType, "List" },
        { API_PropertySingleChoiceEnumerationCollectionType, "SingleChoiceEnumeration" },
        { API_PropertyMultipleChoiceEnumerationCollectionType, "MultipleChoiceEnumeration" }
    });
    if (!TypeToString.ContainsKey (type)) {
        return GS::EmptyUniString;
    }
    return TypeToString[type];
}

static GS::UniString GetPropertyTypeString (API_VariantType type)
{
    static GS::HashTable<API_VariantType, GS::UniString> TypeToString ({
        { API_PropertyUndefinedValueType, "Undefined" },
        { API_PropertyIntegerValueType, "Integer" },
        { API_PropertyRealValueType, "Real" },
        { API_PropertyStringValueType, "String" },
        { API_PropertyBooleanValueType, "Boolean" },
        { API_PropertyGuidValueType, "Guid" }
    });
    if (!TypeToString.ContainsKey (type)) {
        return GS::EmptyUniString;
    }
    return TypeToString[type];
}

static GS::UniString GetPropertyTypeString (API_PropertyMeasureType type)
{
    static GS::HashTable<API_PropertyMeasureType, GS::UniString> TypeToString ({
        { API_PropertyUndefinedMeasureType, "Undefined" },
        { API_PropertyDefaultMeasureType, "Default" },
        { API_PropertyLengthMeasureType, "Length" },
        { API_PropertyAreaMeasureType, "Area" },
        { API_PropertyVolumeMeasureType, "Volume" },
        { API_PropertyAngleMeasureType, "Angle" }
    });
    if (!TypeToString.ContainsKey (type)) {
        return GS::EmptyUniString;
    }
    return TypeToString[type];
}

static API_Guid GetRandomGuid ()
{
    GS::Guid guid;
    guid.Generate ();
    return GSGuid2APIGuid (guid);
}

static API_Guid FindEnumValueGuid (const GS::Array<API_SingleEnumerationVariant>& possibleEnumValues,
                                   const GS::UniString& enumValueIdTypeStr,
                                   const GS::UniString& valueStr)
{
    if (enumValueIdTypeStr != "displayValue" && enumValueIdTypeStr != "nonLocalizedValue") return APINULLGuid;
    const bool isNonLocalizedValue = enumValueIdTypeStr == "nonLocalizedValue";
    API_Guid found = APINULLGuid;

    for (const API_SingleEnumerationVariant& v : possibleEnumValues) {
        if (isNonLocalizedValue) {
            if (v.nonLocalizedValue.HasValue () && *v.nonLocalizedValue == valueStr) {
                if (found != APINULLGuid) return APINULLGuid;
                found = v.keyVariant.guidValue;
            }
        } else if (v.displayVariant.uniStringValue == valueStr) {
            if (found != APINULLGuid) return APINULLGuid;
            found = v.keyVariant.guidValue;
        }
    }

    return found;
}

// 19 September 2026, 15:43 CEST. One typed default reader for creation and editing.
// The caller owns a temporary definition: failure cannot change the native property.
static bool ReadPropertyDefault (const GS::ObjectState& input, API_PropertyDefinition& definition)
{
    API_PropertyDefaultValue result = {};
    if (input.Contains ("expressions")) {
        if (input.Contains ("basicDefaultValue") || !input.Get ("expressions", result.propertyExpressions) ||
            result.propertyExpressions.IsEmpty () || result.propertyExpressions.GetSize () > 1000) return false;
        for (const auto& expression : result.propertyExpressions) if (expression.IsEmpty ()) return false;
        result.hasExpression = true;
        definition.defaultValue = result;
        return true;
    }
    const auto* basic = input.Get ("basicDefaultValue");
    if (basic == nullptr) return false;
    GS::UniString type, status;
    if (!basic->Get ("type", type) || !basic->Get ("status", status)) return false;
    const auto* tuple = PropertyTypeDictionary.GetPtr (type);
    if (tuple == nullptr || std::get<0> (*tuple) != definition.collectionType || std::get<1> (*tuple) != definition.valueType ||
        std::get<2> (*tuple) != definition.measureType) return false;
    result.hasExpression = false;
    auto& value = result.basicValue;
    if (status == "userUndefined" || status == "notAvailable") {
        value.variantStatus = status == "userUndefined" ? API_VariantStatusUserUndefined : API_VariantStatusNull;
        definition.defaultValue = result;
        return true;
    }
    if (status != "normal" || !basic->Contains ("value")) return false;
    value.variantStatus = API_VariantStatusNormal;
    auto& single = value.singleVariant.variant;
    single.type = definition.valueType;
    auto readChoice = [&] (const GS::ObjectState& choice, API_Variant& variant) -> bool {
        GS::UniString selector, text;
        if (!choice.Get ("type", selector) || (selector != "displayValue" && selector != "nonLocalizedValue") ||
            !choice.Get (selector.ToCStr ().Get (), text)) return false;
        variant.type = API_PropertyGuidValueType;
        variant.guidValue = FindEnumValueGuid (definition.possibleEnumValues, selector, text);
        return variant.guidValue != APINULLGuid;
    };
    const auto validInteger = [] (double number) {
        return std::isfinite (number) && std::floor (number) == number && number >= std::numeric_limits<Int32>::min () && number <= std::numeric_limits<Int32>::max ();
    };
    if (definition.collectionType == API_PropertySingleChoiceEnumerationCollectionType) {
        const auto* choice = basic->Get ("value");
        if (choice == nullptr || !readChoice (*choice, single)) return false;
    } else if (definition.collectionType == API_PropertyMultipleChoiceEnumerationCollectionType) {
        GS::Array<GS::ObjectState> choices;
        if (!basic->Get ("value", choices) || choices.GetSize () > 10000) return false;
        GS::Array<API_Guid> selected;
        for (const auto& wrapped : choices) {
            const auto* choice = wrapped.Get ("enumValueId");
            API_Variant variant = {};
            if (choice == nullptr || !readChoice (*choice, variant) || selected.Contains (variant.guidValue)) return false;
            selected.Push (variant.guidValue);
            value.listVariant.variants.Push (variant);
        }
    } else if (definition.collectionType == API_PropertySingleCollectionType) {
        switch (definition.valueType) {
            case API_PropertyRealValueType:
                if (!basic->Get ("value", single.doubleValue) || !std::isfinite (single.doubleValue)) return false;
                break;
            case API_PropertyIntegerValueType: {
                double number = 0;
                if (!basic->Get ("value", number) || !validInteger (number)) return false;
                single.intValue = static_cast<Int32> (number);
                break;
            }
            case API_PropertyStringValueType: if (!basic->Get ("value", single.uniStringValue)) return false; break;
            case API_PropertyBooleanValueType: if (!basic->Get ("value", single.boolValue)) return false; break;
            case API_PropertyGuidValueType: {
                GS::String guid;
                if (!basic->Get ("value", guid)) return false;
                single.guidValue = APIGuidFromString (guid.ToCStr ());
                if (single.guidValue == APINULLGuid) return false;
                break;
            }
            default: return false;
        }
    } else if (definition.collectionType == API_PropertyListCollectionType) {
        if (definition.valueType == API_PropertyRealValueType || definition.valueType == API_PropertyIntegerValueType) {
            GS::Array<double> numbers;
            if (!basic->Get ("value", numbers) || numbers.GetSize () > 10000) return false;
            for (double number : numbers) {
                API_Variant variant = {}; variant.type = definition.valueType;
                if (!std::isfinite (number) || (definition.valueType == API_PropertyIntegerValueType && !validInteger (number))) return false;
                if (definition.valueType == API_PropertyIntegerValueType) variant.intValue = static_cast<Int32> (number);
                else variant.doubleValue = number;
                value.listVariant.variants.Push (variant);
            }
        } else if (definition.valueType == API_PropertyStringValueType) {
            GS::Array<GS::UniString> strings;
            if (!basic->Get ("value", strings) || strings.GetSize () > 10000) return false;
            for (const auto& text : strings) { API_Variant variant = {}; variant.type = definition.valueType; variant.uniStringValue = text; value.listVariant.variants.Push (variant); }
        } else if (definition.valueType == API_PropertyBooleanValueType) {
            GS::Array<bool> booleans;
            if (!basic->Get ("value", booleans) || booleans.GetSize () > 10000) return false;
            for (bool boolean : booleans) { API_Variant variant = {}; variant.type = definition.valueType; variant.boolValue = boolean; value.listVariant.variants.Push (variant); }
        } else return false;
    } else return false;
    definition.defaultValue = result;
    return true;
}

static bool SamePropertyVariant (const API_Variant& left, const API_Variant& right)
{
    if (left.type != right.type) return false;
    switch (left.type) {
        case API_PropertyRealValueType: return std::isfinite (left.doubleValue) && std::isfinite (right.doubleValue) && left.doubleValue == right.doubleValue;
        case API_PropertyIntegerValueType: return left.intValue == right.intValue;
        case API_PropertyStringValueType: return left.uniStringValue == right.uniStringValue;
        case API_PropertyBooleanValueType: return left.boolValue == right.boolValue;
        case API_PropertyGuidValueType: return left.guidValue == right.guidValue;
        default: return false;
    }
}

static bool SamePropertyDefault (const API_PropertyDefinition& left, const API_PropertyDefinition& right)
{
    const auto& a = left.defaultValue;
    const auto& b = right.defaultValue;
    if (a.hasExpression != b.hasExpression) return false;
    if (a.hasExpression) return a.propertyExpressions == b.propertyExpressions;
    if (a.basicValue.variantStatus != b.basicValue.variantStatus) return false;
    if (a.basicValue.variantStatus != API_VariantStatusNormal) return true;
    if (left.collectionType == API_PropertySingleCollectionType || left.collectionType == API_PropertySingleChoiceEnumerationCollectionType)
        return SamePropertyVariant (a.basicValue.singleVariant.variant, b.basicValue.singleVariant.variant);
    const auto& x = a.basicValue.listVariant.variants;
    const auto& y = b.basicValue.listVariant.variants;
    if (x.GetSize () != y.GetSize ()) return false;
    for (UIndex i = 0; i < x.GetSize (); ++i) if (!SamePropertyVariant (x[i], y[i])) return false;
    return true;
}

class PropertyConversionUtils : public API_PropertyConversionUtilsInterface
{
private:
    const GS::UniString degreeSymbol = L ("\u00B0");
    const GS::UniString minuteSymbol = "'";
    const GS::UniString secondSymbol = "\"";
    const GS::UniString gradientSymbol = "G";
    const GS::UniString radianSymbol = "R";
    const GS::UniString northSymbol = "N";
    const GS::UniString southSymbol = "S";
    const GS::UniString eastSymbol = "E";
    const GS::UniString westSymbol = "w";

public:
    PropertyConversionUtils () = default;
    virtual ~PropertyConversionUtils () = default;

    virtual const GS::UniString& GetDegreeSymbol1 () const { return degreeSymbol; }
    virtual const GS::UniString& GetDegreeSymbol2 () const { return degreeSymbol; }
    virtual const GS::UniString& GetMinuteSymbol () const { return minuteSymbol; }
    virtual const GS::UniString& GetSecondSymbol () const { return secondSymbol; }

    virtual const GS::UniString& GetGradientSymbol () const { return gradientSymbol; }
    virtual const GS::UniString& GetRadianSymbol () const { return radianSymbol; }

    virtual const GS::UniString& GetNorthSymbol () const { return northSymbol; }
    virtual const GS::UniString& GetSouthSymbol () const { return southSymbol; }
    virtual const GS::UniString& GetEastSymbol () const { return eastSymbol; }
    virtual const GS::UniString& GetWestSymbol () const { return westSymbol; }

    virtual GS::uchar_t GetDecimalDelimiterChar () const { return '.'; }
    virtual GS::Optional<GS::UniChar> GetThousandSeparatorChar () const { return ' '; }

    virtual API_LengthTypeID GetLengthType () const { return API_LengthTypeID::Meter; }
    virtual API_AreaTypeID GetAreaType () const { return API_AreaTypeID::SquareMeter; }
    virtual API_VolumeTypeID GetVolumeType () const { return API_VolumeTypeID::CubicMeter; }
    virtual API_AngleTypeID GetAngleType () const { return API_AngleTypeID::DecimalDegree; }
};

GetAllPropertiesCommand::GetAllPropertiesCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String GetAllPropertiesCommand::GetName () const
{
    return "GetAllProperties";
}

GS::Optional<GS::UniString> GetAllPropertiesCommand::GetRawResponseSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "properties": {
                "type": "array",
                "description": "A list of property identifiers.",
                "items": {
                    "$ref": "#/PropertyDetails"
                }
            }
        },
        "additionalProperties": false,
        "required": [
            "properties"
        ]
    })";
}

GS::ObjectState GetAllPropertiesCommand::Execute (const GS::ObjectState& /*parameters*/, GS::ProcessControl& /*processControl*/) const
{
    GS::ObjectState response;
    auto propertyAdder = response.AddList<GS::ObjectState> ("properties");

    GS::Array<API_PropertyGroup> groups;
    ACAPI_Property_GetPropertyGroups (groups);
    for (const API_PropertyGroup& group : groups) {
        GS::Array<API_PropertyDefinition> definitions;
        ACAPI_Property_GetPropertyDefinitions (group.guid, definitions);
        for (const API_PropertyDefinition& definition : definitions) {
            GS::ObjectState details;

            GS::ObjectState propertyId;
            propertyId.Add ("guid", APIGuidToString (definition.guid));
            details.Add ("propertyId", propertyId);

            details.Add ("propertyType", GetPropertyTypeString (definition.definitionType));
            details.Add ("propertyGroupName", group.name);
            details.Add ("propertyName", definition.name);
            details.Add ("propertyCollectionType", GetPropertyTypeString (definition.collectionType));
            details.Add ("propertyValueType", GetPropertyTypeString (definition.valueType));
            details.Add ("propertyMeasureType", GetPropertyTypeString (definition.measureType));
            details.Add ("propertyIsEditable", definition.canValueBeEditable);
            details.Add ("isExpressionBased", definition.defaultValue.hasExpression);
            if (definition.defaultValue.hasExpression) {
                const auto& expressionList = details.AddList<GS::UniString> ("expressions");
                for (const GS::UniString& expr : definition.defaultValue.propertyExpressions) {
                    expressionList (expr);
                }
            }

            propertyAdder (details);
        }
    }

    return response;
}

constexpr uint32_t PackTypes (API_PropertyCollectionType colType, API_VariantType valType)
{
    return (static_cast<uint32_t> (colType) << 16) | static_cast<uint32_t> (valType);
}

static GSErrCode GetPropertyValueString (const API_Property& propertyValue, GS::UniString& resultString)
{
    if (propertyValue.value.variantStatus == API_VariantStatusUserUndefined) {
        resultString = "<Undefined>";
        return NoError;
    }

    switch (PackTypes (propertyValue.definition.collectionType, propertyValue.definition.valueType)) {
        case PackTypes (API_PropertySingleCollectionType, API_PropertyStringValueType):
            resultString = propertyValue.value.singleVariant.variant.uniStringValue;
            return NoError;

        case PackTypes (API_PropertySingleCollectionType, API_PropertyBooleanValueType):
            resultString = propertyValue.value.singleVariant.variant.boolValue ? "True" : "False";
            return NoError;

        case PackTypes (API_PropertySingleCollectionType, API_PropertyIntegerValueType):
            resultString = GS::UniString::Printf ("%d", (int) propertyValue.value.singleVariant.variant.intValue);
            return NoError;

        case PackTypes (API_PropertySingleChoiceEnumerationCollectionType, API_PropertyStringValueType):
            {
                const API_Guid& selectedGuid = propertyValue.value.singleVariant.variant.guidValue;
                for (const API_SingleEnumerationVariant& enumVar : propertyValue.definition.possibleEnumValues) {
                    if (enumVar.keyVariant.guidValue == selectedGuid) {
                        resultString = enumVar.displayVariant.uniStringValue;
                        return NoError;
                    }
                }
                return ACAPI_Property_GetPropertyValueString (propertyValue, &resultString);
            }

        default:
            return ACAPI_Property_GetPropertyValueString (propertyValue, &resultString);
    }
}

GetPropertyValuesOfElementsCommand::GetPropertyValuesOfElementsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String GetPropertyValuesOfElementsCommand::GetName () const
{
    return "GetPropertyValuesOfElements";
}

GS::Optional<GS::UniString> GetPropertyValuesOfElementsCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "elements": {
                "$ref": "#/Elements"
            },
            "properties": {
                "$ref": "#/PropertyIds"
            }
        },
        "additionalProperties": false,
        "required": [
            "elements",
            "properties"
        ]
    })";
}

GS::Optional<GS::UniString> GetPropertyValuesOfElementsCommand::GetRawResponseSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "propertyValuesForElements": {
                "$ref": "#/PropertyValuesOrErrorArray",
                "description": "List of property value lists. The order of the outer list is that of the given elements. The order of the inner lists are that of the given properties."
            }
        },
        "additionalProperties": false,
        "required": [
            "propertyValuesForElements"
        ]
    })";
}

GS::ObjectState GetPropertyValuesOfElementsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<GS::ObjectState> elements;
    parameters.Get ("elements", elements);

    GS::Array<GS::ObjectState> properties;
    parameters.Get ("properties", properties);

    GS::ObjectState response;
    const auto& propertyValuesForElements = response.AddList<GS::ObjectState> ("propertyValuesForElements");

    GS::Array<API_Guid> propertyGuids;
    for (const GS::ObjectState& property : properties) {
        const GS::ObjectState* propertyId = property.Get ("propertyId");
        if (propertyId != nullptr) {
            const API_Guid propertyGuid = GetGuidFromObjectState (*propertyId);
            if (propertyGuid != APINULLGuid) {
                propertyGuids.Push (propertyGuid);
            }
        }
    }

    for (const GS::ObjectState& element : elements) {
        const GS::ObjectState* elementId = element.Get ("elementId");
        if (elementId == nullptr) {
            propertyValuesForElements (CreateErrorResponse (APIERR_BADPARS, "elementId is missing"));
            continue;
        }

        const API_Guid elemGuid = GetGuidFromObjectState (*elementId);

        GS::Array<API_Property> fetchedProperties;
        GSErrCode err = ACAPI_Element_GetPropertyValuesByGuid (elemGuid, propertyGuids, fetchedProperties);
        if (err != NoError) {
            propertyValuesForElements (CreateErrorResponse (err, "Failed to get property values for element"));
            continue;
        }

        GS::HashTable<API_Guid, const API_Property*> propertyMap;
        for (const API_Property& prop : fetchedProperties) {
            if (!propertyMap.ContainsKey (prop.definition.guid)) {
                propertyMap.Add (prop.definition.guid, &prop);
            }
        }

        GS::ObjectState propertyValuesForElement;
        const auto& propertyValues = propertyValuesForElement.AddList<GS::ObjectState> ("propertyValues");

        for (const GS::ObjectState& property : properties) {
            const GS::ObjectState* propertyId = property.Get ("propertyId");
            if (propertyId == nullptr) {
                propertyValues (CreateErrorResponse (APIERR_BADPARS, "The 'propertyId' field is missing"));
                continue;
            }

            const API_Guid propertyGuid = GetGuidFromObjectState (*propertyId);

            if (!propertyMap.ContainsKey (propertyGuid)) {
                propertyValues (CreateErrorResponse (APIERR_BADPROPERTY, "Property not found or invalid"));
                continue;
            }

            const API_Property& propertyValue = *propertyMap[propertyGuid];
            if (propertyValue.status == API_Property_NotAvailable || propertyValue.status == API_Property_NotEvaluated) {
                propertyValues (CreateErrorResponse (APIERR_BADPROPERTY, "Not available or not evaluated property"));
                continue;
            }

            GS::UniString propertyValueString;
            err = GetPropertyValueString (propertyValue, propertyValueString);

            if (err != NoError) {
                propertyValues (CreateErrorResponse (err, "Failed to get property value as string"));
                continue;
            }

            propertyValues (GS::ObjectState ("propertyValue", GS::ObjectState ("value", propertyValueString)));
        }

        propertyValuesForElements (propertyValuesForElement);
    }

    return response;
}

SetPropertyValuesOfElementsCommand::SetPropertyValuesOfElementsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String SetPropertyValuesOfElementsCommand::GetName () const
{
    return "SetPropertyValuesOfElements";
}

GS::Optional<GS::UniString> SetPropertyValuesOfElementsCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "elementPropertyValues": {
                "$ref": "#/ElementPropertyValues"
            }
        },
        "additionalProperties": false,
        "required": [
            "elementPropertyValues"
        ]
    })";
}

GS::Optional<GS::UniString> SetPropertyValuesOfElementsCommand::GetRawResponseSchema () const
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

GS::ObjectState SetPropertyValuesOfElementsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<GS::ObjectState> items;
    parameters.Get ("elementPropertyValues", items);
    GS::ObjectState response;
    const auto& results = response.AddList<GS::ObjectState> ("executionResults");
    PropertyConversionUtils conversionUtils;
    const GSErrCode transactionError = ACAPI_CallUndoableCommand ("Set property values", [&] () -> GSErrCode {
        // Input order is significant: repeated owner/property pairs apply sequentially.
        for (const GS::ObjectState& item : items) {
            const GS::ObjectState* owner = item.Get ("elementId");
            const GS::ObjectState* property = item.Get ("propertyId");
            const GS::ObjectState* value = item.Get ("propertyValue");
            GS::UniString text;
            if (owner == nullptr || property == nullptr || value == nullptr || !value->Get ("value", text)) {
                results (CreateFailedExecutionResult (APIERR_BADPARS, "Owner, propertyId and propertyValue.value are required."));
                continue;
            }
            const API_Guid ownerGuid = GetGuidFromObjectState (*owner);
            const API_Guid propertyGuid = GetGuidFromObjectState (*property);
            if (ownerGuid == APINULLGuid || propertyGuid == APINULLGuid) {
                results (CreateFailedExecutionResult (APIERR_BADPARS, "Owner and property GUIDs must be valid."));
                continue;
            }
            GS::Array<API_Guid> propertyIds;
            propertyIds.Push (propertyGuid);
            GS::Array<API_Property> values;
            GSErrCode err = ACAPI_Element_GetPropertyValuesByGuid (ownerGuid, propertyIds, values);
            if (err != NoError || values.GetSize () != 1 || values[0].definition.guid != propertyGuid) {
                results (CreateFailedExecutionResult (err != NoError ? err : APIERR_BADPARS, "Native property read did not return the requested property."));
                continue;
            }
            err = ACAPI_Property_SetPropertyValueFromString (text, conversionUtils, &values[0]);
            if (err == NoError) err = ACAPI_Element_SetProperty (ownerGuid, values[0]);
            if (err != NoError) results (CreateFailedExecutionResult (err, "Failed to set the requested property value."));
            else results (CreateSuccessfulExecutionResult ());
        }
        return NoError;
    });
    if (transactionError != NoError) return CreateErrorResponse (transactionError, "Property transaction failed; committed changes are not confirmed.");
    return response;
}

GetPropertyValuesOfAttributesCommand::GetPropertyValuesOfAttributesCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String GetPropertyValuesOfAttributesCommand::GetName () const
{
    return "GetPropertyValuesOfAttributes";
}

GS::Optional<GS::UniString> GetPropertyValuesOfAttributesCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "attributeIds": {
                "$ref": "#/AttributeIds"
            },
            "properties": {
                "$ref": "#/PropertyIds"
            }
        },
        "additionalProperties": false,
        "required": [
            "attributeIds",
            "properties"
        ]
    })";
}

GS::Optional<GS::UniString> GetPropertyValuesOfAttributesCommand::GetRawResponseSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "propertyValuesForAttributes": {
                "$ref": "#/PropertyValuesOrErrorArray",
                "description": "List of property value lists. The order of the outer list is that of the given attributes. The order of the inner lists are that of the given properties."
            }
        },
        "additionalProperties": false,
        "required": [
            "propertyValuesForAttributes"
        ]
    })";
}

GS::ObjectState GetPropertyValuesOfAttributesCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<GS::ObjectState> attributeIds;
    parameters.Get ("attributeIds", attributeIds);

    GS::Array<GS::ObjectState> properties;
    parameters.Get ("properties", properties);

    GS::ObjectState response;
    const auto& propertyValuesForAttributes = response.AddList<GS::ObjectState> ("propertyValuesForAttributes");

    for (const GS::ObjectState& attribute : attributeIds) {
        const GS::ObjectState* attributeId = attribute.Get ("attributeId");
        if (attributeId == nullptr) {
            propertyValuesForAttributes (CreateErrorResponse (APIERR_BADPARS, "attributeId is missing"));
            continue;
        }

        const API_Guid attGuid = GetGuidFromObjectState (*attributeId);

        GS::ObjectState propertyValuesForAttribute;
        const auto& propertyValues = propertyValuesForAttribute.AddList<GS::ObjectState> ("propertyValues");

        for (const GS::ObjectState& property : properties) {
            const GS::ObjectState* propertyId = property.Get ("propertyId");
            if (propertyId == nullptr) {
                propertyValues (CreateErrorResponse (APIERR_BADPARS, "propertyId is missing"));
                continue;
            }

            const API_Guid propertyGuid = GetGuidFromObjectState (*propertyId);

            API_Property propertyValue;
            API_Attr_Head attrHead = GetAttributeHeadFromGuid (attGuid);
            GSErrCode err = ACAPI_Attribute_GetPropertyValue (attrHead, propertyGuid, propertyValue);

            if (err != NoError) {
                propertyValues (CreateErrorResponse (err, "Failed to get property value"));
                continue;
            }

            if (propertyValue.status == API_Property_NotAvailable || propertyValue.status == API_Property_NotEvaluated) {
                propertyValues (CreateErrorResponse (APIERR_BADPROPERTY, "Not available or not evaluated property"));
                continue;
            }

            GS::UniString propertyValueString;
            err = ACAPI_Property_GetPropertyValueString (propertyValue, &propertyValueString);

            if (err != NoError) {
                propertyValues (CreateErrorResponse (err, "Failed to get property value as string"));
                continue;
            }

            propertyValues (GS::ObjectState ("propertyValue", GS::ObjectState ("value", propertyValueString)));
        }

        propertyValuesForAttributes (propertyValuesForAttribute);
    }

    return response;
}

SetPropertyValuesOfAttributesCommand::SetPropertyValuesOfAttributesCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String SetPropertyValuesOfAttributesCommand::GetName () const
{
    return "SetPropertyValuesOfAttributes";
}

GS::Optional<GS::UniString> SetPropertyValuesOfAttributesCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "attributePropertyValues": {
                "$ref": "#/AttributePropertyValues"
            }
        },
        "additionalProperties": false,
        "required": [
            "attributePropertyValues"
        ]
    })";
}

GS::Optional<GS::UniString> SetPropertyValuesOfAttributesCommand::GetRawResponseSchema () const
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

GS::ObjectState SetPropertyValuesOfAttributesCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<GS::ObjectState> items;
    parameters.Get ("attributePropertyValues", items);
    GS::ObjectState response;
    const auto& results = response.AddList<GS::ObjectState> ("executionResults");
    PropertyConversionUtils conversionUtils;
    const GSErrCode transactionError = ACAPI_CallUndoableCommand ("Set property values", [&] () -> GSErrCode {
        // Input order is significant: repeated owner/property pairs apply sequentially.
        for (const GS::ObjectState& item : items) {
            const GS::ObjectState* owner = item.Get ("attributeId");
            const GS::ObjectState* property = item.Get ("propertyId");
            const GS::ObjectState* value = item.Get ("propertyValue");
            GS::UniString text;
            if (owner == nullptr || property == nullptr || value == nullptr || !value->Get ("value", text)) {
                results (CreateFailedExecutionResult (APIERR_BADPARS, "Owner, propertyId and propertyValue.value are required."));
                continue;
            }
            const API_Guid ownerGuid = GetGuidFromObjectState (*owner);
            const API_Guid propertyGuid = GetGuidFromObjectState (*property);
            if (ownerGuid == APINULLGuid || propertyGuid == APINULLGuid) {
                results (CreateFailedExecutionResult (APIERR_BADPARS, "Owner and property GUIDs must be valid."));
                continue;
            }
            GS::Array<API_Guid> propertyIds;
            propertyIds.Push (propertyGuid);
            GS::Array<API_Property> values;
            API_Attr_Head attributeHead = GetAttributeHeadFromGuid (ownerGuid);
            GSErrCode err = ACAPI_Attribute_GetPropertyValuesByGuid (attributeHead, propertyIds, values);
            if (err != NoError || values.GetSize () != 1 || values[0].definition.guid != propertyGuid) {
                results (CreateFailedExecutionResult (err != NoError ? err : APIERR_BADPARS, "Native property read did not return the requested property."));
                continue;
            }
            err = ACAPI_Property_SetPropertyValueFromString (text, conversionUtils, &values[0]);
            if (err == NoError) err = ACAPI_Attribute_SetProperty (attributeHead, values[0]);
            if (err != NoError) results (CreateFailedExecutionResult (err, "Failed to set the requested property value."));
            else results (CreateSuccessfulExecutionResult ());
        }
        return NoError;
    });
    if (transactionError != NoError) return CreateErrorResponse (transactionError, "Property transaction failed; committed changes are not confirmed.");
    return response;
}

CreatePropertyGroupsCommand::CreatePropertyGroupsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String CreatePropertyGroupsCommand::GetName () const
{
    return "CreatePropertyGroups";
}

GS::Optional<GS::UniString> CreatePropertyGroupsCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "ifExists": {
                "enum": ["Error", "ReuseIfMatching", "Overwrite"],
                "default": "Error",
                "description": "Match custom groups by exact name. Reuse requires the supplied description to match. Overwrite changes only the description when supplied; it preserves the group identity and definitions. Ambiguous names and built-in groups fail."
            },
            "propertyGroups": {
                "type": "array",
                "minItems": 1,
                "maxItems": 1000,
                "description": "The parameters of the new property groups.",
                "items": {
                    "$ref": "#/PropertyGroupArrayItem"
                }
            }
        },
        "additionalProperties": false,
        "required": [
            "propertyGroups"
        ]
    })";
}

GS::Optional<GS::UniString> CreatePropertyGroupsCommand::GetRawResponseSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "propertyGroupIds": {
                "type": "array",
                "description": "The identifiers of the created property groups.",
                "items": {
                    "oneOf": [{"$ref": "#/PropertyGroupIdArrayItem"}, {"$ref": "#/ErrorItem"}]
                }
            }
        },
        "additionalProperties": false,
        "required": [
            "propertyGroupIds"
        ]
    })";
}

GS::ObjectState CreatePropertyGroupsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& processControl) const
{
    GS::Array<GS::ObjectState> propertyGroups;
    parameters.Get ("propertyGroups", propertyGroups);
    GS::UniString policy = "Error";
    parameters.Get ("ifExists", policy);
    if (propertyGroups.IsEmpty () || propertyGroups.GetSize () > 1000 || (policy != "Error" && policy != "ReuseIfMatching" && policy != "Overwrite"))
        return CreateErrorResponse (APIERR_BADPARS, "Supply 1..1000 groups and a supported ifExists policy.");

    GS::ObjectState response;
    const auto& propertyGroupIds = response.AddList<GS::ObjectState> ("propertyGroupIds");

    const GSErrCode transactionError = ACAPI_CallUndoableCommand ("CreatePropertyGroups", [&]() -> GSErrCode {
        for (const GS::ObjectState& g : propertyGroups) {
            if (processControl.TestBreak ()) {
                propertyGroupIds (CreateErrorResponse (APIERR_CANCEL, "Cancelled before processing this group."));
                continue;
            }
            const GS::ObjectState* propertyGroup = g.Get ("propertyGroup");
            if (propertyGroup == nullptr) {
                propertyGroupIds (CreateErrorResponse (APIERR_BADPARS, "propertyGroup is missing"));
                continue;
            }

            API_PropertyGroup apiPropertyGroup = {};
            if (!propertyGroup->Get ("name", apiPropertyGroup.name) || apiPropertyGroup.name.IsEmpty ()) {
                propertyGroupIds (CreateErrorResponse (APIERR_BADPARS, "name is missing or empty"));
                continue;
            }

            const bool descriptionSupplied = propertyGroup->Get ("description", apiPropertyGroup.description);
            if (policy != "Error") {
                GS::Array<API_PropertyGroup> existing;
                const auto readError = ACAPI_Property_GetPropertyGroups (existing);
                if (readError != NoError) {
                    propertyGroupIds (CreateErrorResponse (readError, "Cannot inspect existing property groups; nothing created for this input."));
                    continue;
                }
                const API_PropertyGroup* match = nullptr;
                UInt32 matches = 0;
                for (const auto& candidate : existing) if (candidate.name == apiPropertyGroup.name) { match = &candidate; ++matches; }
                if (matches > 1 || (match != nullptr && match->groupType != API_PropertyCustomGroupType)) {
                    propertyGroupIds (CreateErrorResponse (APIERR_BADPARS, "Group name is ambiguous or belongs to a built-in group."));
                    continue;
                }
                if (match != nullptr) {
                    if (descriptionSupplied && apiPropertyGroup.description != match->description) {
                        if (policy == "ReuseIfMatching") {
                            propertyGroupIds (CreateErrorResponse (APIERR_BADPARS, "Existing group's description differs; nothing changed."));
                            continue;
                        }
                        auto updated = *match;
                        updated.description = apiPropertyGroup.description;
                        const auto changeError = ACAPI_Property_ChangePropertyGroup (updated);
                        if (changeError != NoError) {
                            propertyGroupIds (CreateErrorResponse (changeError, "Failed to change the property group description."));
                            continue;
                        }
                    }
                    propertyGroupIds (CreateIdObjectState ("propertyGroupId", match->guid));
                    continue;
                }
            }
            GSErrCode err = ACAPI_Property_CreatePropertyGroup (apiPropertyGroup);
            if (err != NoError) {
                propertyGroupIds (CreateErrorResponse (err, "failed to create the property group"));
                continue;
            }

            propertyGroupIds (CreateIdObjectState ("propertyGroupId", apiPropertyGroup.guid));
        }

        return NoError;
    });

    if (transactionError != NoError) return CreateErrorResponse (transactionError, "Native transaction failed; committed changes are not confirmed.");
    return response;
}

DeletePropertyGroupsCommand::DeletePropertyGroupsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String DeletePropertyGroupsCommand::GetName () const
{
    return "DeletePropertyGroups";
}

GS::Optional<GS::UniString> DeletePropertyGroupsCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "propertyGroupIds": {
                "type": "array",
                "description": "The identifiers of property groups to delete.",
                "items": {
                    "$ref": "#/PropertyGroupIdArrayItem"
                }
            }
        },
        "additionalProperties": false,
        "required": [
            "propertyGroupIds"
        ]
    })";
}

GS::Optional<GS::UniString> DeletePropertyGroupsCommand::GetRawResponseSchema () const
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

GS::ObjectState DeletePropertyGroupsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<GS::ObjectState> propertyGroupIds;
    parameters.Get ("propertyGroupIds", propertyGroupIds);

    GS::ObjectState response;
    const auto& executionResults = response.AddList<GS::ObjectState> ("executionResults");

    const GSErrCode transactionError = ACAPI_CallUndoableCommand ("DeletePropertyGroups", [&]() -> GSErrCode {
        for (const GS::ObjectState& p : propertyGroupIds) {
            const GS::ObjectState* propertyGroupId = p.Get ("propertyGroupId");
            if (propertyGroupId == nullptr) {
                executionResults (CreateFailedExecutionResult (APIERR_BADPARS, "propertyGroupId is missing"));
                continue;
            }

            GSErrCode err = ACAPI_Property_DeletePropertyGroup (GetGuidFromObjectState (*propertyGroupId));
            if (err != NoError) {
                executionResults (CreateFailedExecutionResult (err, "failed to delete property group"));
                continue;
            }

            executionResults (CreateSuccessfulExecutionResult ());
        }

        return NoError;
    });

    if (transactionError != NoError) return CreateErrorResponse (transactionError, "Native transaction failed; committed changes are not confirmed.");
    return response;
}

CreatePropertyDefinitionsCommand::CreatePropertyDefinitionsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String CreatePropertyDefinitionsCommand::GetName () const
{
    return "CreatePropertyDefinitions";
}

GS::Optional<GS::UniString> CreatePropertyDefinitionsCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "ifExists": {
                "enum": ["Error", "ReuseIfMatching"],
                "default": "Error",
                "description": "Reuse an exact-name custom definition in the resolved group only when type, units, description, editability, availability and ordered enumeration choices match. A supplied default must match too; an omitted default preserves the existing one. Conflicts never overwrite."
            },
            "propertyDefinitions": {
                "type": "array",
                "minItems": 1,
                "maxItems": 1000,
                "description": "The parameters of the new properties.",
                "items": {
                    "$ref" : "#/PropertyDefinitionArrayItem"
                }
            }
        },
        "additionalProperties": false,
        "required": [
            "propertyDefinitions"
        ]
    })";
}

GS::Optional<GS::UniString> CreatePropertyDefinitionsCommand::GetRawResponseSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "propertyIds": {
                "$ref" : "#/PropertyIdOrErrorArray"
            }
        },
        "additionalProperties": false,
        "required": [
            "propertyIds"
        ]
    })";
}

GS::ObjectState CreatePropertyDefinitionsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& processControl) const
{
    GS::Array<GS::ObjectState> propertyDefinitions;
    parameters.Get ("propertyDefinitions", propertyDefinitions);
    if (propertyDefinitions.IsEmpty () || propertyDefinitions.GetSize () > 1000) return CreateErrorResponse (APIERR_BADPARS, "Supply 1..1000 property definitions.");
    GS::UniString policy = "Error";
    parameters.Get ("ifExists", policy);
    if (policy != "Error" && policy != "ReuseIfMatching") return CreateErrorResponse (APIERR_BADPARS, "Unsupported ifExists policy.");

    GS::ObjectState response;
    const auto& propertyIds = response.AddList<GS::ObjectState> ("propertyIds");

    const GSErrCode transactionError = ACAPI_CallUndoableCommand ("CreatePropertyDefinitions", [&]() -> GSErrCode {
        for (const GS::ObjectState& p : propertyDefinitions) {
            if (processControl.TestBreak ()) { propertyIds (CreateErrorResponse (APIERR_CANCEL, "Cancelled before creating this definition.")); continue; }
            const GS::ObjectState* propertyDefinition = p.Get ("propertyDefinition");
            if (propertyDefinition == nullptr) {
                propertyIds (CreateErrorResponse (APIERR_BADPARS, "property is missing"));
                continue;
            }

            API_PropertyDefinition apiPropertyDefinition = {};
            apiPropertyDefinition.definitionType = API_PropertyCustomDefinitionType;

            const GS::ObjectState* group = propertyDefinition->Get ("group");
            if (group == nullptr) {
                propertyIds (CreateErrorResponse (APIERR_BADPARS, "group is missing"));
                continue;
            }
            const GS::ObjectState* groupGuid = group->Get ("propertyGroupId");
            if (groupGuid != nullptr) {
                apiPropertyDefinition.groupGuid = GetGuidFromObjectState (*groupGuid);
            } else {
                GS::UniString groupName;
                if (group->Get ("name", groupName) && !groupName.IsEmpty ()) {
                    GS::Array<API_PropertyGroup> groups;
                    const auto groupError = ACAPI_Property_GetPropertyGroups (groups);
                    if (groupError != NoError) { propertyIds (CreateErrorResponse (groupError, "Cannot resolve the property group.")); continue; }
                    UInt32 matches = 0;
                    for (const auto& candidate : groups) if (candidate.name == groupName) { apiPropertyDefinition.groupGuid = candidate.guid; ++matches; }
                    if (matches != 1) { propertyIds (CreateErrorResponse (APIERR_BADPARS, "Property group name is missing or ambiguous; use its identifier.")); continue; }
                }
            }
            if (apiPropertyDefinition.groupGuid == APINULLGuid) {
                propertyIds (CreateErrorResponse (APIERR_BADPARS, "both group/name and group/propertyGroupId are missing or invalid"));
                continue;
            }

            if (!propertyDefinition->Get ("name", apiPropertyDefinition.name) || apiPropertyDefinition.name.IsEmpty ()) {
                propertyIds (CreateErrorResponse (APIERR_BADPARS, "name is missing or empty"));
                continue;
            }

            propertyDefinition->Get ("description", apiPropertyDefinition.description);

            GS::UniString typeStr;
            if (!propertyDefinition->Get ("type", typeStr) || typeStr.IsEmpty ()) {
                propertyIds (CreateErrorResponse (APIERR_BADPARS, "type is missing or empty"));
                continue;
            }
            const PropertyTypeTuple* typeTuple = PropertyTypeDictionary.GetPtr (typeStr);
            if (typeTuple == nullptr) {
                propertyIds (CreateErrorResponse (APIERR_BADPARS, GS::UniString::Printf ("invalid type '%T'", typeStr.ToPrintf ())));
                continue;
            }
            apiPropertyDefinition.collectionType = std::get<0> (*typeTuple);
            apiPropertyDefinition.valueType = std::get<1> (*typeTuple);
            apiPropertyDefinition.measureType = std::get<2> (*typeTuple);

            GS::Array<GS::ObjectState> availability;
            propertyDefinition->Get ("availability", availability);
            GSErrCode availabilityError = availability.GetSize () > 10000 ? APIERR_BADPARS : NoError;
            if (availabilityError == NoError) for (const auto& input : availability) {
                API_ClassificationItem classification = {};
                classification.guid = GetGuidFromArrayItem ("classificationItemId", input);
                if (classification.guid == APINULLGuid || apiPropertyDefinition.availability.Contains (classification.guid)) { availabilityError = APIERR_BADPARS; break; }
                availabilityError = ACAPI_Classification_GetClassificationItem (classification);
                if (availabilityError != NoError) break;
                apiPropertyDefinition.availability.Push (classification.guid);
            }
            if (availabilityError != NoError) { propertyIds (CreateErrorResponse (availabilityError, "Invalid, duplicate, excessive or unreadable classification availability.")); continue; }

            GS::Array<GS::ObjectState> possibleEnumValues;
            propertyDefinition->Get ("possibleEnumValues", possibleEnumValues);
            const bool enumeration = apiPropertyDefinition.collectionType == API_PropertySingleChoiceEnumerationCollectionType || apiPropertyDefinition.collectionType == API_PropertyMultipleChoiceEnumerationCollectionType;
            bool invalidEnum = possibleEnumValues.GetSize () > 10000 || (!enumeration && !possibleEnumValues.IsEmpty ()) || (enumeration && possibleEnumValues.IsEmpty ());
            GS::Array<GS::UniString> displays, localizedKeys;
            if (!invalidEnum) for (const auto& wrapped : possibleEnumValues) {
                const auto* spec = wrapped.Get ("enumValue");
                API_SingleEnumerationVariant variant = {};
                variant.displayVariant.type = apiPropertyDefinition.valueType;
                variant.keyVariant.type = API_PropertyGuidValueType;
                variant.keyVariant.guidValue = GetRandomGuid ();
                if (spec == nullptr || !spec->Get ("displayValue", variant.displayVariant.uniStringValue) || displays.Contains (variant.displayVariant.uniStringValue)) { invalidEnum = true; break; }
                displays.Push (variant.displayVariant.uniStringValue);
                GS::UniString localizedKey;
                if (spec->Get ("nonLocalizedValue", localizedKey)) {
                    if (localizedKeys.Contains (localizedKey)) { invalidEnum = true; break; }
                    localizedKeys.Push (localizedKey);
                    variant.nonLocalizedValue = localizedKey;
                }
                apiPropertyDefinition.possibleEnumValues.Push (variant);
            }
            if (invalidEnum) { propertyIds (CreateErrorResponse (APIERR_BADPARS, "Supply distinct enumeration choices only for an enumeration property (1..10000 choices).")); continue; }

            const auto* defaultValue = propertyDefinition->Get ("defaultValue");
            if (defaultValue != nullptr) {
                if (!ReadPropertyDefault (*defaultValue, apiPropertyDefinition)) {
                    propertyIds (CreateErrorResponse (APIERR_BADPARS, "Default value does not match the property's type, units, choices or expression rules."));
                    continue;
                }
            } else {
                apiPropertyDefinition.defaultValue.hasExpression = false;
                apiPropertyDefinition.defaultValue.basicValue.variantStatus = API_VariantStatusNull;
            }

            propertyDefinition->Get ("isEditable", apiPropertyDefinition.canValueBeEditable);

            if (policy == "ReuseIfMatching") {
                GS::Array<API_PropertyDefinition> definitions;
                const auto readError = ACAPI_Property_GetPropertyDefinitions (apiPropertyDefinition.groupGuid, definitions);
                if (readError != NoError) { propertyIds (CreateErrorResponse (readError, "Cannot inspect existing definitions; nothing created for this input.")); continue; }
                const API_PropertyDefinition* match = nullptr;
                UInt32 matches = 0;
                for (const auto& candidate : definitions) if (candidate.name == apiPropertyDefinition.name) { match = &candidate; ++matches; }
                if (matches > 1) { propertyIds (CreateErrorResponse (APIERR_BADPARS, "Property name is ambiguous in this group.")); continue; }
                if (match != nullptr) {
                    bool equal = match->definitionType == API_PropertyCustomDefinitionType && match->description == apiPropertyDefinition.description &&
                        match->collectionType == apiPropertyDefinition.collectionType && match->valueType == apiPropertyDefinition.valueType &&
                        match->measureType == apiPropertyDefinition.measureType && match->canValueBeEditable == apiPropertyDefinition.canValueBeEditable &&
                        match->availability.GetSize () == apiPropertyDefinition.availability.GetSize () &&
                        match->possibleEnumValues.GetSize () == apiPropertyDefinition.possibleEnumValues.GetSize ();
                    if (equal) for (const auto& id : apiPropertyDefinition.availability) if (!match->availability.Contains (id)) equal = false;
                    if (equal) for (UIndex i = 0; i < apiPropertyDefinition.possibleEnumValues.GetSize (); ++i) {
                        auto& requested = apiPropertyDefinition.possibleEnumValues[i];
                        const auto& existing = match->possibleEnumValues[i];
                        if (!SamePropertyVariant (requested.displayVariant, existing.displayVariant) || requested.nonLocalizedValue != existing.nonLocalizedValue) { equal = false; break; }
                        // Resolve default choices to the existing native keys, never newly generated keys.
                        requested.keyVariant = existing.keyVariant;
                    }
                    if (equal && defaultValue != nullptr) equal = ReadPropertyDefault (*defaultValue, apiPropertyDefinition) && SamePropertyDefault (apiPropertyDefinition, *match);
                    if (!equal) { propertyIds (CreateErrorResponse (APIERR_BADPARS, "Existing property definition differs; nothing changed. Use its identifier with UpdatePropertyDefinitions for an explicit edit.")); continue; }
                    propertyIds (CreateIdObjectState ("propertyId", match->guid));
                    continue;
                }
            }

            GSErrCode err = ACAPI_Property_CreatePropertyDefinition (apiPropertyDefinition);
            if (err != NoError) {
                propertyIds (CreateErrorResponse (err, "failed to create the property"));
                continue;
            }

            propertyIds (CreateIdObjectState ("propertyId", apiPropertyDefinition.guid));
        }

        return NoError;
    });

    if (transactionError != NoError) return CreateErrorResponse (transactionError, "Native transaction failed; committed changes are not confirmed.");
    return response;
}

DeletePropertyDefinitionsCommand::DeletePropertyDefinitionsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String DeletePropertyDefinitionsCommand::GetName () const
{
    return "DeletePropertyDefinitions";
}

GS::Optional<GS::UniString> DeletePropertyDefinitionsCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "propertyIds": {
                "type": "array",
                "description": "The identifiers of properties to delete.",
                "items": {
                    "$ref": "#/PropertyIdArrayItem"
                }
            }
        },
        "additionalProperties": false,
        "required": [
            "propertyIds"
        ]
    })";
}

GS::Optional<GS::UniString> DeletePropertyDefinitionsCommand::GetRawResponseSchema () const
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

GS::ObjectState DeletePropertyDefinitionsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<GS::ObjectState> propertyIds;
    parameters.Get ("propertyIds", propertyIds);

    GS::ObjectState response;
    const auto& executionResults = response.AddList<GS::ObjectState> ("executionResults");

    const GSErrCode transactionError = ACAPI_CallUndoableCommand ("DeletePropertyDefinitions", [&]() -> GSErrCode {
        for (const GS::ObjectState& p : propertyIds) {
            const GS::ObjectState* propertyId = p.Get ("propertyId");
            if (propertyId == nullptr) {
                executionResults (CreateFailedExecutionResult (APIERR_BADPARS, "propertyId is missing"));
                continue;
            }

            GSErrCode err = ACAPI_Property_DeletePropertyDefinition (GetGuidFromObjectState (*propertyId));
            if (err != NoError) {
                executionResults (CreateFailedExecutionResult (err, "failed to delete property"));
                continue;
            }

            executionResults (CreateSuccessfulExecutionResult ());
        }

        return NoError;
    });

    if (transactionError != NoError) return CreateErrorResponse (transactionError, "Native transaction failed; committed changes are not confirmed.");
    return response;
}

UpdatePropertyDefinitionsCommand::UpdatePropertyDefinitionsCommand () :
    CommandBase (CommonSchema::Used)
{}

GS::String UpdatePropertyDefinitionsCommand::GetName () const
{
    return "UpdatePropertyDefinitions";
}

GS::Optional<GS::UniString> UpdatePropertyDefinitionsCommand::GetInputParametersSchema () const
{
    return R"({
    "type": "object",
    "properties": {
        "propertyDefinitions": {
            "type": "array",
            "description": "Edit custom property definitions in place; omitted fields are preserved. Formula edits still require an expression-based property.",
            "items": {
                "type": "object",
                "properties": {
                    "propertyId": {
                        "$ref": "#/PropertyId"
                    },
                    "expressions": {
                        "type": "array",
                        "description": "The new expression strings for the property.",
                        "items": {
                            "type": "string"
                        },
                        "minItems": 1,
                        "maxItems": 1000
                    },
                    "name": {
                        "type": "string",
                        "minLength": 1
                    },
                    "description": {
                        "type": "string"
                    },
                    "isEditable": {
                        "type": "boolean"
                    },
                    "availability": {
                        "type": "array",
                        "maxItems": 10000,
                        "description": "Replace classification availability. An empty list removes all availability.",
                        "items": {
                            "$ref": "#/ClassificationItemIdArrayItem"
                        }
                    },
                    "defaultValue": {
                        "$ref": "#/PropertyDefaultValue",
                        "description": "Replace the default with a typed value or expressions, including switching between the two. Cannot be combined with expressions. Existing enumeration choices and property type are preserved."
                    }
                },
                "additionalProperties": false,
                "required": [
                    "propertyId"
                ]
            },
            "minItems": 1,
            "maxItems": 1000
        }
    },
    "additionalProperties": false,
    "required": [
        "propertyDefinitions"
    ]
})";
}

GS::Optional<GS::UniString> UpdatePropertyDefinitionsCommand::GetRawResponseSchema () const
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

GS::ObjectState UpdatePropertyDefinitionsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& processControl) const
{
    GS::Array<GS::ObjectState> propertyDefinitions;
    parameters.Get ("propertyDefinitions", propertyDefinitions);
    if (propertyDefinitions.IsEmpty () || propertyDefinitions.GetSize () > 1000)
        return CreateErrorResponse (APIERR_BADPARS, "Supply 1..1000 property definitions.");

    GS::ObjectState response;
    const auto& executionResults = response.AddList<GS::ObjectState> ("executionResults");

    const GSErrCode transactionError = ACAPI_CallUndoableCommand ("UpdatePropertyDefinitions", [&]() -> GSErrCode {
        for (const GS::ObjectState& item : propertyDefinitions) {
            if (processControl.TestBreak ()) { executionResults (CreateFailedExecutionResult (APIERR_CANCEL, "Cancelled before processing this definition.")); continue; }
            const GS::ObjectState* propertyId = item.Get ("propertyId");
            if (propertyId == nullptr) {
                executionResults (CreateFailedExecutionResult (APIERR_BADPARS, "propertyId is missing"));
                continue;
            }

            API_PropertyDefinition definition = {};
            definition.guid = GetGuidFromObjectState (*propertyId);
            const auto readError = ACAPI_Property_GetPropertyDefinition (definition);
            if (readError != NoError) {
                executionResults (CreateFailedExecutionResult (readError, "Cannot read the requested property definition."));
                continue;
            }
            if (definition.definitionType != API_PropertyCustomDefinitionType) {
                executionResults (CreateFailedExecutionResult (APIERR_BADPARS, "Built-in property definitions cannot be edited."));
                continue;
            }
            const bool hasEdits = item.Contains ("name") || item.Contains ("description") || item.Contains ("isEditable") || item.Contains ("availability") || item.Contains ("expressions") || item.Contains ("defaultValue");
            if (!hasEdits || (item.Get ("name", definition.name) && definition.name.IsEmpty ())) {
                executionResults (CreateFailedExecutionResult (APIERR_BADPARS, "Supply at least one supported setting; name cannot be empty."));
                continue;
            }
            item.Get ("description", definition.description);
            item.Get ("isEditable", definition.canValueBeEditable);
            GS::Array<GS::ObjectState> availability;
            if (item.Get ("availability", availability)) {
                if (availability.GetSize () > 10000) {
                    executionResults (CreateFailedExecutionResult (APIERR_BADPARS, "Classification availability exceeds 10000 entries."));
                    continue;
                }
                definition.availability.Clear ();
                GSErrCode availabilityError = NoError;
                for (const auto& input : availability) {
                    API_ClassificationItem classification = {};
                    classification.guid = GetGuidFromArrayItem ("classificationItemId", input);
                    if (classification.guid == APINULLGuid || definition.availability.Contains (classification.guid)) { availabilityError = APIERR_BADPARS; break; }
                    availabilityError = ACAPI_Classification_GetClassificationItem (classification);
                    if (availabilityError != NoError) break;
                    definition.availability.Push (classification.guid);
                }
                if (availabilityError != NoError) {
                    executionResults (CreateFailedExecutionResult (availabilityError, "Classification availability contains an invalid, duplicate or unreadable item."));
                    continue;
                }
            }
            if (const auto* defaultValue = item.Get ("defaultValue")) {
                if (item.Contains ("expressions") || !ReadPropertyDefault (*defaultValue, definition)) {
                    executionResults (CreateFailedExecutionResult (APIERR_BADPARS, "Default value is incompatible or conflicts with expressions; nothing changed."));
                    continue;
                }
            }
            GS::Array<GS::UniString> expressions;
            if (item.Get ("expressions", expressions)) {
                bool invalid = !definition.defaultValue.hasExpression || expressions.IsEmpty () || expressions.GetSize () > 1000;
                for (const auto& expression : expressions) if (expression.IsEmpty ()) invalid = true;
                if (invalid) {
                    executionResults (CreateFailedExecutionResult (APIERR_BADPARS, "Formula edits require an expression-based property and 1..1000 nonempty expressions."));
                    continue;
                }
                definition.defaultValue.propertyExpressions = expressions;
            }

            GSErrCode err = ACAPI_Property_ChangePropertyDefinition (definition);
            if (err != NoError) {
                executionResults (CreateFailedExecutionResult (err, "failed to update property definition"));
                continue;
            }

            executionResults (CreateSuccessfulExecutionResult ());
        }

        return NoError;
    });

    if (transactionError != NoError) return CreateErrorResponse (transactionError, "Native transaction failed; committed changes are not confirmed.");
    return response;
}

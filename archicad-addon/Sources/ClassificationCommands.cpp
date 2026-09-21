#include "ClassificationCommands.hpp"
#include "MigrationHelper.hpp"
#include "GSProcessControl.hpp"
#include <string>

#ifdef ServerMainVers_2900
#include <chrono>
#endif

GetClassificationsOfElementsCommand::GetClassificationsOfElementsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String GetClassificationsOfElementsCommand::GetName () const
{
    return "GetClassificationsOfElements";
}

GS::Optional<GS::UniString> GetClassificationsOfElementsCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "elements": {
                "$ref": "#/Elements"
            },
            "classificationSystemIds": {
                "$ref": "#/ClassificationSystemIds"
            }
        },
        "additionalProperties": false,
        "required": [
            "elements",
            "classificationSystemIds"
        ]
    })";
}

GS::Optional<GS::UniString> GetClassificationsOfElementsCommand::GetRawResponseSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "elementClassifications": {
                "$ref": "#/ElementClassificationsOrErrors",
                "description": "The list of element classification item identifiers. Order of the ids are the same as in the input. Non-existing elements or non-existing classification systems are represented by error objects."
            }
        },
        "additionalProperties": false,
        "required": [
            "elementClassifications"
        ]
    })";
}

GS::ObjectState GetClassificationsOfElementsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<GS::ObjectState> elements;
    parameters.Get ("elements", elements);

    GS::Array<GS::ObjectState> classificationSystemIds;
    parameters.Get ("classificationSystemIds", classificationSystemIds);

    GS::ObjectState response;
    const auto& elementClassifications = response.AddList<GS::ObjectState> ("elementClassifications");

    for (const GS::ObjectState& element : elements) {
        const GS::ObjectState* elementId = element.Get ("elementId");
        if (elementId == nullptr) {
            elementClassifications (CreateErrorResponse (APIERR_BADPARS, "elementId is missing"));
            continue;
        }

        const API_Guid elemGuid = GetGuidFromObjectState (*elementId);

        GS::ObjectState elementClassification;
        const auto& classificationIds = elementClassification.AddList<GS::ObjectState> ("classificationIds");

        for (const GS::ObjectState& classificationSystemId : classificationSystemIds) {
            const GS::ObjectState* systemId = classificationSystemId.Get ("classificationSystemId");
            if (systemId == nullptr) {
                classificationIds (CreateErrorResponse (APIERR_BADPARS, "classificationSystemId is missing"));
                continue;
            }

            const API_Guid systemGuid = GetGuidFromObjectState (*systemId);

            API_ClassificationItem item;
            GSErrCode err = ACAPI_Element_GetClassificationInSystem (elemGuid, systemGuid, item);

            if (err != NoError) {
                classificationIds (CreateErrorResponse (err, "Failed to get classification item"));
                continue;
            }

            classificationIds (GS::ObjectState (
                "classificationSystemId", GS::ObjectState ("guid", APIGuidToString (systemGuid)),
                "classificationItemId", GS::ObjectState ("guid", APIGuidToString (item.guid))));
        }

        elementClassifications (elementClassification);
    }

    return response;
}

SetClassificationsOfElementsCommand::SetClassificationsOfElementsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String SetClassificationsOfElementsCommand::GetName () const
{
    return "SetClassificationsOfElements";
}

GS::Optional<GS::UniString> SetClassificationsOfElementsCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "elementClassifications": {
                "$ref": "#/ElementClassifications"
            }
        },
        "additionalProperties": false,
        "required": [
            "elementClassifications"
        ]
    })";
}

GS::Optional<GS::UniString> SetClassificationsOfElementsCommand::GetRawResponseSchema () const
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

GS::ObjectState SetClassificationsOfElementsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<GS::ObjectState> elementClassifications;
    parameters.Get ("elementClassifications", elementClassifications);

    GS::ObjectState response;
    const auto& executionResults = response.AddList<GS::ObjectState> ("executionResults");

    const GSErrCode transactionError = ACAPI_CallUndoableCommand ("SetClassificationsOfElementsCommand", [&] () -> GSErrCode {
        for (const GS::ObjectState& elementClassification : elementClassifications) {
            const GS::ObjectState* elementId = elementClassification.Get ("elementId");
            if (elementId == nullptr) {
                executionResults (CreateFailedExecutionResult (APIERR_BADPARS, "elementId is missing"));
                continue;
            }

            const API_Guid elemGuid = GetGuidFromObjectState (*elementId);

            const GS::ObjectState* classificationId = elementClassification.Get ("classificationId");
            if (classificationId == nullptr) {
                executionResults (CreateFailedExecutionResult (APIERR_BADPARS, "classificationId is missing"));
                continue;
            }

            const GS::ObjectState* classificationItemId = classificationId->Get ("classificationItemId");
            if (classificationItemId != nullptr) {
                const API_Guid classificationItemGuid = GetGuidFromObjectState (*classificationItemId);

                const GSErrCode err = ACAPI_Element_AddClassificationItem (elemGuid, classificationItemGuid);
                if (err != NoError) {
                    executionResults (CreateFailedExecutionResult (err, "Failed to set classification item for element"));
                    continue;
                }
            } else {
                const GS::ObjectState* classificationSystemId = classificationId->Get ("classificationSystemId");
                if (classificationSystemId == nullptr) {
                    executionResults (CreateFailedExecutionResult (APIERR_BADPARS, "classificationSystemId is missing"));
                    continue;
                }

                const API_Guid classificationSystemGuid = GetGuidFromObjectState (*classificationSystemId);

                API_ClassificationItem item;
                GSErrCode err = ACAPI_Element_GetClassificationInSystem (elemGuid, classificationSystemGuid, item);
                if (err != NoError) {
                    executionResults (CreateFailedExecutionResult (err, "Failed to get classification item for element"));
                    continue;
                }

                err = ACAPI_Element_RemoveClassificationItem (elemGuid, item.guid);
                if (err != NoError) {
                    executionResults (CreateFailedExecutionResult (err, "Failed to remove classification item for element"));
                    continue;
                }
            }

            executionResults (CreateSuccessfulExecutionResult ());
        }

        return NoError;
    });

    if (transactionError != NoError) return CreateErrorResponse (transactionError, "Native transaction failed; committed changes are not confirmed.");
    return response;
}

CreateClassificationSystemsCommand::CreateClassificationSystemsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String CreateClassificationSystemsCommand::GetName () const
{
    return "CreateClassificationSystems";
}

GS::Optional<GS::UniString> CreateClassificationSystemsCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "classificationSystemsWithItems": {
                "$ref": "#/ClassificationSystemsWithItems"
            }
        },
        "additionalProperties": false,
        "required": [
           "classificationSystemsWithItems"
        ]
    })";
}

GS::Optional<GS::UniString> CreateClassificationSystemsCommand::GetRawResponseSchema () const
{
    return R"({
    "type": "object",
    "properties": {
        "executionResults": {
            "$ref": "#/ExecutionResults"
        },
        "createdItems": {
            "type": "array",
            "description": "Every item created before completion or failure, with its originating input index. On a failed outer transaction these IDs are diagnostic only; inspect before reuse.",
            "items": {
                "type": "object",
                "properties": {
                    "inputIndex": {
                        "type": "integer"
                    },
                    "classificationItemId": {
                        "$ref": "#/ClassificationItemId"
                    },
                    "classificationSystemId": {
                        "$ref": "#/ClassificationSystemId"
                    },
                    "parentClassificationItemId": {
                        "$ref": "#/ClassificationItemId"
                    },
                    "id": {
                        "type": "string"
                    }
                },
                "required": [
                    "inputIndex",
                    "classificationItemId",
                    "classificationSystemId",
                    "parentClassificationItemId",
                    "id"
                ],
                "additionalProperties": false
            }
        },
        "transactionStatus": {
            "enum": [
                "committed",
                "failed"
            ]
        },
        "createdSystems": {
            "type": "array",
            "items": {
                "type": "object",
                "properties": {
                    "inputIndex": {
                        "type": "integer"
                    },
                    "classificationSystemId": {
                        "$ref": "#/ClassificationSystemId"
                    }
                },
                "required": [
                    "inputIndex",
                    "classificationSystemId"
                ],
                "additionalProperties": false
            }
        }
    },
    "additionalProperties": false,
    "required": [
        "executionResults"
    ]
})";
}

// 19 September 2026, 15:32 CEST. Bound the complete tree before creating anything;
// record each native identity and propagate every descendant failure.
static bool BoundedClassificationTree (const GS::Array<GS::ObjectState>& items,UInt32 depth,UInt32& remaining)
{
    if (depth>64 || items.GetSize()>remaining) return false;
    remaining-=items.GetSize();
    for (const auto& item:items) {
        GS::Array<GS::ObjectState> children;
        if (item.Get("children",children) && !children.IsEmpty() && !BoundedClassificationTree(children,depth+1,remaining)) return false;
    }
    return true;
}
static void RecordClassificationItem (const API_ClassificationItem& item,const API_Guid& system,const API_Guid& parent,Int32 inputIndex,GS::Array<GS::ObjectState>& created)
{
    created.Push(GS::ObjectState("inputIndex",inputIndex,"classificationItemId",GS::ObjectState("guid",APIGuidToString(item.guid)),"classificationSystemId",GS::ObjectState("guid",APIGuidToString(system)),
        "parentClassificationItemId",GS::ObjectState("guid",APIGuidToString(parent)),"id",item.id));
}
static GSErrCode CreateClassificationItemsRecursively (const GS::Array<GS::ObjectState>& items,const API_Guid& system,const API_Guid& parent,
    Int32 inputIndex,GS::Array<GS::ObjectState>& created,GS::ProcessControl& control)
{
    for (const auto& spec:items) {
        if (control.TestBreak()) return APIERR_CANCEL;
        API_ClassificationItem item={};
        spec.Get("name",item.name); spec.Get("description",item.description); spec.Get("id",item.id);
        auto err=ACAPI_Classification_CreateClassificationItem(item,system,parent,APINULLGuid);
        if (err!=NoError) return err;
        RecordClassificationItem(item,system,parent,inputIndex,created);
        GS::Array<GS::ObjectState> children;
        if (spec.Get("children",children) && !children.IsEmpty()) {
            err=CreateClassificationItemsRecursively(children,system,item.guid,inputIndex,created,control);
            if (err!=NoError) return err;
        }
    }
    return NoError;
}
static bool ClassificationDate (const GS::UniString& text,unsigned int& year,unsigned int& month,unsigned int& day)
{
    const std::string value(text.ToCStr(CC_UTF8).Get());
    if (value.size()!=10 || value[4]!='-' || value[7]!='-') return false;
    for (size_t i=0;i<value.size();++i) if (i!=4 && i!=7 && (value[i]<'0' || value[i]>'9')) return false;
    year=static_cast<unsigned int>(std::stoul(value.substr(0,4)));
    month=static_cast<unsigned int>(std::stoul(value.substr(5,2)));
    day=static_cast<unsigned int>(std::stoul(value.substr(8,2)));
    if (year==0 || month<1 || month>12 || day<1) return false;
    const unsigned int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    return day<=days[month-1]+(month==2 && year%4==0 && (year%100!=0 || year%400==0) ? 1U : 0U);
}
GS::ObjectState CreateClassificationSystemsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& control) const
{
    GS::Array<GS::ObjectState> inputs;
    if (!parameters.Get("classificationSystemsWithItems",inputs) || inputs.IsEmpty() || inputs.GetSize()>1000) return CreateErrorResponse(APIERR_BADPARS,"Supply 1..1000 classification systems.");
    UInt32 remaining=10000;
    for (const auto& input:inputs) {
        GS::Array<GS::ObjectState> tree; input.Get("classificationItems",tree);
        if (!BoundedClassificationTree(tree,0,remaining)) return CreateErrorResponse(APIERR_BADPARS,"Classification request exceeds 10000 items or 64 levels; nothing created.");
    }
    GS::Array<GS::ObjectState> results,created,systems;
    const auto transaction=ACAPI_CallUndoableCommand("Create Classification Systems",[&] () -> GSErrCode {
        Int32 inputIndex=-1;
        for (const auto& input:inputs) {
            ++inputIndex;
            if (control.TestBreak()) { results.Push(CreateFailedExecutionResult(APIERR_CANCEL,"Cancelled before creating this system.")); continue; }
            const auto* spec=input.Get("classificationSystem");
            if (spec==nullptr) { results.Push(CreateFailedExecutionResult(APIERR_BADPARS,"Classification system details are missing.")); continue; }
            API_ClassificationSystem system={};
            spec->Get("name",system.name); spec->Get("description",system.description); spec->Get("source",system.source); spec->Get("version",system.editionVersion);
            GS::UniString date; spec->Get("date",date); unsigned int year=0,month=0,day=0;
            if (!ClassificationDate(date,year,month,day)) { results.Push(CreateFailedExecutionResult(APIERR_BADPARS,"Classification edition date must be a valid YYYY-MM-DD date.")); continue; }
#ifdef ServerMainVers_2900
            system.editionDate=std::chrono::year_month_day(std::chrono::year(static_cast<int>(year)),std::chrono::month(month),std::chrono::day(day));
#else
            system.editionDate=GSDateRecord(static_cast<unsigned short>(year),static_cast<unsigned short>(month),static_cast<unsigned short>(day));
#endif
            auto err=ACAPI_Classification_CreateClassificationSystem(system);
            if (err!=NoError) { results.Push(CreateFailedExecutionResult(err,"Failed to create classification system.")); continue; }
            systems.Push(GS::ObjectState("inputIndex",inputIndex,"classificationSystemId",GS::ObjectState("guid",APIGuidToString(system.guid))));
            GS::Array<GS::ObjectState> tree; input.Get("classificationItems",tree);
            err=CreateClassificationItemsRecursively(tree,system.guid,APINULLGuid,inputIndex,created,control);
            results.Push(err==NoError ? CreateSuccessfulExecutionResult() : CreateFailedExecutionResult(err,"System was created but its item tree is incomplete. Inspect createdSystems and createdItems before retrying."));
        }
        return NoError;
    });
    if (transaction!=NoError) {
        results.Clear();
        for (UIndex i=0;i<inputs.GetSize();++i) results.Push(CreateFailedExecutionResult(transaction,"Outer transaction failed; returned created IDs are diagnostic only and must be inspected."));
    }
    return GS::ObjectState("executionResults",results,"createdSystems",systems,"createdItems",created,"transactionStatus",transaction==NoError ? "committed" : "failed");
}

CreateClassificationItemsCommand::CreateClassificationItemsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String CreateClassificationItemsCommand::GetName () const
{
    return "CreateClassificationItems";
}

GS::Optional<GS::UniString> CreateClassificationItemsCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "newClassificationItems": {
                "$ref": "#/NewClassificationItems"
            }
        },
        "additionalProperties": false,
        "required": [
           "newClassificationItems"
        ]
    })";
}

GS::Optional<GS::UniString> CreateClassificationItemsCommand::GetRawResponseSchema () const
{
    return R"({
    "type": "object",
    "properties": {
        "executionResults": {
            "$ref": "#/ExecutionResults"
        },
        "createdItems": {
            "type": "array",
            "description": "Every item created before completion or failure, with its originating input index. On a failed outer transaction these IDs are diagnostic only; inspect before reuse.",
            "items": {
                "type": "object",
                "properties": {
                    "inputIndex": {
                        "type": "integer"
                    },
                    "classificationItemId": {
                        "$ref": "#/ClassificationItemId"
                    },
                    "classificationSystemId": {
                        "$ref": "#/ClassificationSystemId"
                    },
                    "parentClassificationItemId": {
                        "$ref": "#/ClassificationItemId"
                    },
                    "id": {
                        "type": "string"
                    }
                },
                "required": [
                    "inputIndex",
                    "classificationItemId",
                    "classificationSystemId",
                    "parentClassificationItemId",
                    "id"
                ],
                "additionalProperties": false
            }
        },
        "transactionStatus": {
            "enum": [
                "committed",
                "failed"
            ]
        }
    },
    "additionalProperties": false,
    "required": [
        "executionResults"
    ]
})";
}

GS::ObjectState CreateClassificationItemsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& control) const
{
    GS::Array<GS::ObjectState> inputs;
    if (!parameters.Get("newClassificationItems",inputs) || inputs.IsEmpty() || inputs.GetSize()>1000) return CreateErrorResponse(APIERR_BADPARS,"Supply 1..1000 classification item roots.");
    UInt32 remaining=10000;
    for (const auto& input:inputs) if (const auto* spec=input.Get("classificationItemDetails")) {
        GS::Array<GS::ObjectState> root; root.Push(*spec);
        if (!BoundedClassificationTree(root,0,remaining)) return CreateErrorResponse(APIERR_BADPARS,"Classification request exceeds 10000 items or 64 levels; nothing created.");
    }
    GS::Array<GS::ObjectState> results,created;
    const auto transaction=ACAPI_CallUndoableCommand("Create Classification Items",[&] () -> GSErrCode {
        Int32 inputIndex=-1;
        for (const auto& input:inputs) {
            ++inputIndex;
            if (control.TestBreak()) { results.Push(CreateFailedExecutionResult(APIERR_CANCEL,"Cancelled before creating this item.")); continue; }
            const auto* spec=input.Get("classificationItemDetails");
            const API_Guid system=GetGuidFromArrayItem("classificationSystemId",input);
            const API_Guid parent=GetGuidFromArrayItem("parentClassificationItemId",input);
            const API_Guid next=GetGuidFromArrayItem("nextClassificationItemId",input);
            if (spec==nullptr || system==APINULLGuid || (input.Contains("parentClassificationItemId") && parent==APINULLGuid) || (input.Contains("nextClassificationItemId") && next==APINULLGuid)) {
                results.Push(CreateFailedExecutionResult(APIERR_BADPARS,"Invalid classification item details or system/parent/next identity.")); continue;
            }
            API_ClassificationItem item={};
            spec->Get("name",item.name); spec->Get("description",item.description); spec->Get("id",item.id);
            auto err=ACAPI_Classification_CreateClassificationItem(item,system,parent,next);
            if (err!=NoError) { results.Push(CreateFailedExecutionResult(err,"Failed to create classification item.")); continue; }
            RecordClassificationItem(item,system,parent,inputIndex,created);
            GS::Array<GS::ObjectState> children; spec->Get("children",children);
            err=CreateClassificationItemsRecursively(children,system,item.guid,inputIndex,created,control);
            results.Push(err==NoError ? CreateSuccessfulExecutionResult() : CreateFailedExecutionResult(err,"Root item was created but its descendants are incomplete. Inspect createdItems before retrying."));
        }
        return NoError;
    });
    if (transaction!=NoError) {
        results.Clear();
        for (UIndex i=0;i<inputs.GetSize();++i) results.Push(CreateFailedExecutionResult(transaction,"Outer transaction failed; returned created IDs are diagnostic only and must be inspected."));
    }
    return GS::ObjectState("executionResults",results,"createdItems",created,"transactionStatus",transaction==NoError ? "committed" : "failed");
}

DeleteClassificationSystemsCommand::DeleteClassificationSystemsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String DeleteClassificationSystemsCommand::GetName () const
{
    return "DeleteClassificationSystems";
}

GS::Optional<GS::UniString> DeleteClassificationSystemsCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "classificationSystemIds": {
                "$ref": "#/ClassificationSystemIds"
            }
        },
        "additionalProperties": false,
        "required": [
            "classificationSystemIds"
        ]
    })";
}

GS::Optional<GS::UniString> DeleteClassificationSystemsCommand::GetRawResponseSchema () const
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

GS::ObjectState DeleteClassificationSystemsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<GS::ObjectState> classificationSystemIds;
    parameters.Get ("classificationSystemIds", classificationSystemIds);

    GS::ObjectState response;
    const auto& executionResults = response.AddList<GS::ObjectState> ("executionResults");

    const GSErrCode transactionError = ACAPI_CallUndoableCommand ("DeleteClassificationSystemsCommand", [&]() -> GSErrCode {
        for (const GS::ObjectState& classificationSystemId : classificationSystemIds) {
            const GS::ObjectState* systemId = classificationSystemId.Get ("classificationSystemId");
            if (systemId == nullptr) {
                executionResults (CreateFailedExecutionResult (APIERR_BADPARS, "classificationSystemId is missing"));
                continue;
            }

            const API_Guid systemGuid = GetGuidFromObjectState (*systemId);

            GSErrCode err = ACAPI_Classification_DeleteClassificationSystem (systemGuid);
            if (err != NoError) {
                executionResults (CreateFailedExecutionResult (err, "failed to delete classification system"));
                continue;
            }

            executionResults (CreateSuccessfulExecutionResult ());
        }

        return NoError;
    });

    if (transactionError != NoError) return CreateErrorResponse (transactionError, "Native transaction failed; committed changes are not confirmed.");
    return response;
}

DeleteClassificationItemsCommand::DeleteClassificationItemsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String DeleteClassificationItemsCommand::GetName () const
{
    return "DeleteClassificationItems";
}

GS::Optional<GS::UniString> DeleteClassificationItemsCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "classificationItemIds": {
                "$ref": "#/ClassificationItemIds"
            }
        },
        "additionalProperties": false,
        "required": [
            "classificationItemIds"
        ]
    })";
}

GS::Optional<GS::UniString> DeleteClassificationItemsCommand::GetRawResponseSchema () const
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

GS::ObjectState DeleteClassificationItemsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<GS::ObjectState> classificationItemIds;
    parameters.Get ("classificationItemIds", classificationItemIds);

    GS::ObjectState response;
    const auto& executionResults = response.AddList<GS::ObjectState> ("executionResults");

    const GSErrCode transactionError = ACAPI_CallUndoableCommand ("DeleteClassificationItemsCommand", [&]() -> GSErrCode {
        for (const GS::ObjectState& classificationItemId : classificationItemIds) {
            const GS::ObjectState* itemId = classificationItemId.Get ("classificationItemId");
            if (itemId == nullptr) {
                executionResults (CreateFailedExecutionResult (APIERR_BADPARS, "classificationItemId is missing"));
                continue;
            }

            const API_Guid itemGuid = GetGuidFromObjectState (*itemId);

            GSErrCode err = ACAPI_Classification_DeleteClassificationItem (itemGuid);
            if (err != NoError) {
                executionResults (CreateFailedExecutionResult (err, "failed to delete classification item"));
                continue;
            }

            executionResults (CreateSuccessfulExecutionResult ());
        }

        return NoError;
    });

    if (transactionError != NoError) return CreateErrorResponse (transactionError, "Native transaction failed; committed changes are not confirmed.");
    return response;
}

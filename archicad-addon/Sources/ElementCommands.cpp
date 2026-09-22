#include "ElementCommands.hpp"
#include "MigrationHelper.hpp"
#include "GSUnID.hpp"
#include "Plane.hpp"
#include "CoordTypedef.hpp"
#include "ModelEdge.hpp"
#include "ModelMeshBody.hpp"
#include "NativeImage.hpp"
#include "MemoryOChannel32.hpp"
#include "Base64Converter.hpp"
#include "NativeFieldFilterDefinitions.hpp"
#include "ClassificationCommands.hpp"
#include "PropertyCommands.hpp"
#include "NativeProjectCommands.hpp"
#ifdef ServerMainVers_2800
#include "ACAPI/ZoneBoundaryQuery.hpp"
#endif

#include <algorithm>
#include <cmath>

// Shared "line-family settings" fields present on Line/PolyLine/Arc/Circle/Spline
// (API_LineType/API_PolyLineType/API_ArcType/API_SplineType all share this exact shape).
// NOTE: penWeight is deliberately NOT exposed here (GET or SET) - confirmed live that a raw
// penWeight override does not reliably take visible effect in Archicad; line thickness is
// controlled by which pen (linePenIndex) is assigned, since each pen has its own weight defined
// in the project's pen table. Exposing a non-functional field would be misleading.
static void AddLineFamilySettingsDetails (GS::ObjectState& os, const API_ExtendedPenType& linePen, API_AttributeIndex ltypeInd, bool roomSeparator)
{
    os.Add ("roomSeparator", roomSeparator);
    os.Add ("linePenIndex", linePen.penIndex);
    os.Add ("lineTypeId", CreateGuidObjectState (GetAttributeGuidFromIndex (API_LinetypeID, ltypeInd)));
}

// Returns true if any field was actually set (caller still needs to set its own type's mask bits).
static bool SetLineFamilySettingsFields (const GS::ObjectState& typeSpecificDetails, API_ExtendedPenType& linePen, API_AttributeIndex& ltypeInd, bool& roomSeparator,
                                         bool& penChanged, bool& ltypeChanged, bool& roomSepChanged)
{
    short penIndex = 0;
    if (typeSpecificDetails.Get ("linePenIndex", penIndex)) {
        linePen.penIndex = penIndex;
        linePen.colorOverridePenIndex = 0;
        penChanged = true;
    }
    const GS::ObjectState* lineTypeId = typeSpecificDetails.Get ("lineTypeId");
    if (lineTypeId != nullptr) {
        ltypeInd = GetAttributeIndexFromGuid (API_LinetypeID, GetGuidFromObjectState (*lineTypeId));
        ltypeChanged = true;
    }
    if (typeSpecificDetails.Get ("roomSeparator", roomSeparator)) {
        roomSepChanged = true;
    }
    return penChanged || ltypeChanged || roomSepChanged;
}

struct API_RoomUpdateParams {
    bool keepStampPos;
    bool undoTopTrim;
    bool undoBotTrim;
    bool filler_1[5];

    API_RoomUpdateParams() : keepStampPos(true), undoTopTrim(false), undoBotTrim(false)
    {}
};

typedef enum {
    APIInternal_UpdateRoomsID    = 'UPDR',
    APIInternal_PostCommandIdID  = 'ESPC',
} API_InternalID;

extern "C" {
    GSErrCode ACAPI_Internal (API_InternalID code, void* par1 = nullptr, void* par2 = nullptr, void* par3 = nullptr);
}

static API_ElemFilterFlags ConvertFilterStringToFlag (const GS::UniString& filter)
{
    if (filter == "IsEditable")
        return APIFilt_IsEditable;
    if (filter == "IsVisibleByLayer")
        return APIFilt_OnVisLayer;
    if (filter == "IsVisibleByRenovation")
        return APIFilt_IsVisibleByRenovation;
    if (filter == "IsVisibleByStructureDisplay")
        return APIFilt_IsInStructureDisplay;
    if (filter == "IsVisibleIn3D")
        return APIFilt_In3D;
    if (filter == "OnActualFloor")
        return APIFilt_OnActFloor;
    if (filter == "OnActualLayout")
        return APIFilt_OnActLayout;
    if (filter == "InMyWorkspace")
        return APIFilt_InMyWorkspace;
    if (filter == "IsIndependent")
        return APIFilt_IsIndependent;
    if (filter == "InCroppedView")
        return APIFilt_InCroppedView;
    if (filter == "HasAccessRight")
        return APIFilt_HasAccessRight;
    if (filter == "IsOverriddenByRenovation")
        return APIFilt_IsOverridden;
    return APIFilt_None;
}

static GS::UniString DrawingNumberingTypeToString (API_NumberingTypeValues numberingType)
{
    switch (numberingType) {
        case APINumbering_ByViewId:   return "ByViewId";
        case APINumbering_CustomNum:  return "CustomNumber";
        default:
        case APINumbering_ByLayout:   return "ByLayout";
    }
}

static API_NumberingTypeValues DrawingNumberingTypeFromString (const GS::UniString& str)
{
    if (str == "ByViewId")
        return APINumbering_ByViewId;
    if (str == "CustomNumber")
        return APINumbering_CustomNum;
    return APINumbering_ByLayout;
}

static API_Guid GetParentElemOfSectElem (const API_Guid& elemGuid)
{
    API_Element element = {};
    element.header.guid = elemGuid;
    if (ACAPI_Element_GetHeader (&element.header) != NoError ||
        GetElemTypeId (element.header) != API_SectElemID ||
        ACAPI_Element_Get (&element) != NoError) {
        return elemGuid;
    }
    return element.sectElem.parentGuid;
}

static GS::UniString StructureTypeToString (API_ModelElemStructureType structureType)
{
    switch (structureType) {
        case API_BasicStructure:
            return "Basic";
        case API_CompositeStructure:
            return "Composite";
        case API_ProfileStructure:
            return "Profile";
        default:
            return "Basic";
    }
}

template <typename ListProxyType>
static GSErrCode GetElementsFromCurrentDatabase (const GS::ObjectState& parameters, ListProxyType& elementsListProxy)
{
    API_ElemTypeID elemType = API_ZombieElemID;
    GS::UniString elementTypeStr;
    if (parameters.Get ("elementType", elementTypeStr)) {
        elemType = GetElementTypeFromNonLocalizedName (elementTypeStr);
    }

    bool includeSubElemObjects = false;
    API_ElemFilterFlags filterFlags = APIFilt_None;
    GS::Array<GS::UniString> filters;
    if (parameters.Get ("filters", filters)) {
        for (const GS::UniString& filter : filters) {
            if (filter == "IncludeSubElemObjects") {
                includeSubElemObjects = true;
            } else {
                filterFlags |= ConvertFilterStringToFlag (filter);
            }
        }
    }

    GS::Array<API_Guid> elemList;
    GSErrCode err = ACAPI_Element_GetElemList (elemType, &elemList, filterFlags);
    if (err != NoError) {
        return err;
    }

    if (elemType == API_ObjectID && !includeSubElemObjects) {
        for (const API_Guid& elemGuid : elemList) {
            const API_Guid parentGuid = GetParentElemOfSectElem (elemGuid);
            API_Element elem = {};
            elem.header.guid = parentGuid;
            if (ACAPI_Element_Get (&elem) == NoError && elem.object.owner == APINULLGuid) {
                elementsListProxy (CreateElementIdObjectState (parentGuid));
            }
        }
    } else {
        for (const API_Guid& elemGuid : elemList) {
            elementsListProxy (CreateElementIdObjectState (GetParentElemOfSectElem (elemGuid)));
        }
    }
    return NoError;
}

// The elements drawn in a section/elevation/interior elevation database, together with the
// owner element each of them was generated from. Every other listing command converts a
// section element to its owner (GetParentElemOfSectElem above), so this is the only way to
// obtain a raw API_SectElemID guid - which is what CreateAssociativeDimensionsOnSection
// requires for its sectionElementId (#509).
template <typename ListProxyType>
static GSErrCode GetSectionElementsFromCurrentDatabase (ListProxyType& sectionElementsListProxy)
{
    GS::Array<API_Guid> elemList;
    GSErrCode err = ACAPI_Element_GetElemList (API_SectElemID, &elemList);
    if (err != NoError) {
        return err;
    }

    for (const API_Guid& elemGuid : elemList) {
        API_Element element = {};
        element.header.guid = elemGuid;
        if (ACAPI_Element_Get (&element) != NoError) {
            continue;
        }

        GS::ObjectState sectionElement;
        sectionElement.Add ("sectionElementId", CreateGuidObjectState (elemGuid));
        sectionElement.Add ("ownerElementId", CreateGuidObjectState (element.sectElem.parentGuid));

        // The owner lives in the model, not in this database, so its header is not always
        // readable while the section database is the current one - the type is reported only
        // when it resolves, rather than guessed.
        API_Elem_Head ownerHead = {};
        ownerHead.guid = element.sectElem.parentGuid;
        if (ACAPI_Element_GetHeader (&ownerHead) == NoError) {
            sectionElement.Add ("ownerElementType", GetElementTypeNonLocalizedName (GetElemTypeId (ownerHead)));
        }

        sectionElementsListProxy (sectionElement);
    }

    return NoError;
}

GetSectionElementsCommand::GetSectionElementsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String GetSectionElementsCommand::GetName () const
{
    return "GetSectionElements";
}

GS::Optional<GS::UniString> GetSectionElementsCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "databases": {
                "$ref": "#/Databases",
                "description": "The section, elevation or interior elevation databases to list the section elements of. If omitted, the current database is used."
            }
        },
        "additionalProperties": false,
        "required": []
    })";
}

GS::Optional<GS::UniString> GetSectionElementsCommand::GetRawResponseSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "sectionElements": {
                "type": "array",
                "description": "The elements drawn in the given databases, each with the owner element it was generated from.",
                "items": {
                    "type": "object",
                    "properties": {
                        "sectionElementId": {
                            "$ref": "#/ElementId",
                            "description": "The identifier of the section element itself, accepted by CreateAssociativeDimensionsOnSection as sectionElementId."
                        },
                        "ownerElementId": {
                            "$ref": "#/ElementId",
                            "description": "The identifier of the owner element the section element was generated from - this is what every other listing command returns."
                        },
                        "ownerElementType": {
                            "$ref": "#/ElementType",
                            "description": "The type of the owner element. Only present when the owner's header is readable from the section database."
                        }
                    },
                    "additionalProperties": false,
                    "required": [
                        "sectionElementId",
                        "ownerElementId"
                    ]
                }
            },
            "executionResultForDatabases": {
                "$ref": "#/ExecutionResults"
            }
        },
        "additionalProperties": false,
        "required": [
            "sectionElements"
        ]
    })";
}

GS::ObjectState GetSectionElementsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::ObjectState response;
    const auto& sectionElements = response.AddList<GS::ObjectState> ("sectionElements");

    GS::Array<GS::ObjectState> databases;
    if (!parameters.Get ("databases", databases) || databases.IsEmpty ()) {
        const GSErrCode err = GetSectionElementsFromCurrentDatabase (sectionElements);
        if (err != NoError) {
            return CreateErrorResponse (err, "Failed to list the section elements of the current database.");
        }
        return response;
    }

    const auto& executionResultForDatabases = response.AddList<GS::ObjectState> ("executionResultForDatabases");
    const GS::Array<API_Guid> databaseIds = databases.Transform<API_Guid> (GetGuidFromDatabaseArrayItem);

    auto action = [&]() -> GSErrCode {
        return GetSectionElementsFromCurrentDatabase (sectionElements);
    };
    auto actionSuccess = [&]() -> void {
        executionResultForDatabases (CreateSuccessfulExecutionResult ());
    };
    auto actionFailure = [&](GSErrCode err, const GS::UniString& errMsg) -> void {
        executionResultForDatabases (CreateFailedExecutionResult (err, errMsg));
    };

    const GSErrCode err = ExecuteActionForEachDatabase (databaseIds, action, actionSuccess, actionFailure);
    if (err != NoError) {
        return CreateErrorResponse (err, "Failed to retrieve the starting database or to switch back to it after execution.");
    }

    return response;
}

GetElementsByTypeCommand::GetElementsByTypeCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String GetElementsByTypeCommand::GetName () const
{
    return "GetElementsByType";
}

GS::Optional<GS::UniString> GetElementsByTypeCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "elementType": {
                "$ref": "#/ElementType"
            },
            "filters": {
                "type": "array",
                "items": {
                    "$ref": "#/ElementFilter"
                },
                "minItems": 1
            },
            "databases": {
                "$ref": "#/Databases"
            }
        },
        "additionalProperties": false,
        "required": [
            "elementType"
        ]
    })";
}

GS::Optional<GS::UniString> GetElementsByTypeCommand::GetRawResponseSchema () const
{
    return R"({
        "$ref": "#/ElementsWithExecutionResultsOrError"
    })";
}

GS::ObjectState GetElementsByTypeCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::UniString elementTypeStr;
    if (parameters.Get ("elementType", elementTypeStr)) {
        if (GetElementTypeFromNonLocalizedName (elementTypeStr) == API_ZombieElemID) {
            return CreateErrorResponse (APIERR_BADPARS,
                GS::UniString::Printf ("Invalid elementType '%T'.", elementTypeStr.ToPrintf ()));
        }
    }

    GS::ObjectState response;
    const auto& elements = response.AddList<GS::ObjectState> ("elements");

    GS::Array<GS::ObjectState> databases;
    bool databasesParameterExists = parameters.Get ("databases", databases);
    if (!databasesParameterExists || databases.IsEmpty ()) {
        GetElementsFromCurrentDatabase (parameters, elements);
    }
    else {
        const auto& executionResultForDatabases = response.AddList<GS::ObjectState> ("executionResultForDatabases");

        const GS::Array<API_Guid> databaseIds = databases.Transform<API_Guid> (GetGuidFromDatabaseArrayItem);

        auto action = [&]() -> GSErrCode {
            return GetElementsFromCurrentDatabase (parameters, elements);
        };
        auto actionSuccess = [&]() -> void {
            executionResultForDatabases (CreateSuccessfulExecutionResult ());
        };
        auto actionFailure = [&](GSErrCode err, const GS::UniString& errMsg) -> void {
            executionResultForDatabases (CreateFailedExecutionResult (err, errMsg));
        };

        GSErrCode err = ExecuteActionForEachDatabase (databaseIds, action,  actionSuccess, actionFailure);
        if (err != NoError) {
            return CreateErrorResponse (err, "Failed to retrieve the starting database or to switch back to it after execution.");
        }
    }

    return response;
}

GS::String GetAllElementsCommand::GetName () const
{
    return "GetAllElements";
}

GS::Optional<GS::UniString> GetAllElementsCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "filters": {
                "type": "array",
                "items": {
                    "$ref": "#/ElementFilter"
                },
                "minItems": 1
            },
            "databases": {
                "$ref": "#/Databases"
            }
        },
        "additionalProperties": false,
        "required": []
    })";
}

GetDetailsOfElementsCommand::GetDetailsOfElementsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String GetDetailsOfElementsCommand::GetName () const
{
    return "GetDetailsOfElements";
}

GS::Optional<GS::UniString> GetDetailsOfElementsCommand::GetInputParametersSchema () const
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

GS::Optional<GS::UniString> GetDetailsOfElementsCommand::GetRawResponseSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "detailsOfElements": {
                "type": "array",
                "items": {
                    "type": "object",
                    "description": "Details of an element.",
                    "properties": {
                        "type": {
                            "$ref": "#/ElementType"
                        },
                        "id": {
                            "type": "string"
                        },
                        "floorIndex": {
                            "type": "number"
                        },
                        "layerIndex": {
                            "type": "number"
                        },
                        "drawIndex": {
                            "type": "number"
                        },
                        "details": {
                            "$ref": "#/TypeSpecificDetails"
                        },
                        "floorPlanPolygons": {
                            "type": "array",
                            "description": "Cut-fill polygons as drawn on the floor plan (wall joins resolved by ArchiCAD). Available for elements with a cut-fill representation (walls, columns, beams). Absent when the element has no cut fill or when the floor plan database is not accessible.",
                            "items": {
                                "type": "object",
                                "properties": {
                                    "coordinates": {
                                        "type": "array",
                                        "items": {
                                            "$ref": "#/Coordinate2D"
                                        }
                                    }
                                }
                            }
                        }
                    },
                    "additionalProperties": false,
                    "required": [
                        "type",
                        "id",
                        "floorIndex",
                        "layerIndex",
                        "drawIndex",
                        "details"
                    ]
                }
            }
        },
        "additionalProperties": false,
        "required": [
            "detailsOfElements"
        ]
    })";
}

// ── Floor plan polygon infrastructure (used by GetDetailsOfElements) ─────────

struct FloorPlanCollectorContext {
    GS::Array<GS::Array<API_Coord>> polys;
    bool inCutFill = false;
};

static thread_local FloorPlanCollectorContext* tl_floorPlanCtx = nullptr;

static GSErrCode CollectCutFillPolygons (const API_PrimElement* primElem,
                                          const void* par1, const void* par2, const void* /*par3*/)
{
    if (tl_floorPlanCtx == nullptr)
        return NoError;

    switch (primElem->header.typeID) {
        case API_PrimCtrl_HatchBorderBegID: {
            const auto* border = static_cast<const API_PrimHatchBorder*> (par1);
            tl_floorPlanCtx->inCutFill = (border != nullptr && border->determination == APIHatch_CutFills);
            break;
        }
        case API_PrimCtrl_HatchBorderEndID:
            tl_floorPlanCtx->inCutFill = false;
            break;
        case API_PrimPolyID: {
            if (!tl_floorPlanCtx->inCutFill)
                break;
            const Int32 nCoords = primElem->poly.nCoords;
            if (nCoords < 3)
                break;
            const auto* coords = static_cast<const API_Coord*> (par1);
            const auto* pends  = static_cast<const Int32*> (par2);
            Int32 start = 1;
            const Int32 nSubPolys = primElem->poly.nSubPolys;
            for (Int32 sub = 1; sub <= nSubPolys; ++sub) {
                const Int32 end = (pends != nullptr) ? pends[sub] : nCoords;
                GS::Array<API_Coord> ring;
                for (Int32 i = start; i <= end; ++i)
                    ring.Push (coords[i]);
                if (ring.GetSize () >= 3)
                    tl_floorPlanCtx->polys.Push (ring);
                start = end + 1;
            }
            break;
        }
        default:
            break;
    }
    return NoError;
}

static void AddFloorPlanPolygonsIfAvailable (const API_Guid& guid, GS::ObjectState& os)
{
    FloorPlanCollectorContext ctx;
    tl_floorPlanCtx = &ctx;
    const GS::OnExit ctxGuard ([&] () { tl_floorPlanCtx = nullptr; });

    API_Elem_Head elemHead = {};
    elemHead.guid = guid;

    API_ShapePrimsParams params = {};
    params.dontClip   = true;
    params.allStories = true;
    params.polygon    = nullptr;

#if defined(ServerMainVers_2700)
    const GSErrCode fpErr = ACAPI_DrawingPrimitive_ShapePrimsExt (elemHead, CollectCutFillPolygons, &params);
#else
    const GSErrCode fpErr = ACAPI_Element_ShapePrimsExt (elemHead, CollectCutFillPolygons, &params);
#endif
    if (fpErr != NoError)
        return;
    if (ctx.polys.IsEmpty ())
        return;

    const auto& polygonsOS = os.AddList<GS::ObjectState> ("floorPlanPolygons");
    for (const GS::Array<API_Coord>& poly : ctx.polys) {
        GS::ObjectState polyOS;
        const auto& coordsOS = polyOS.AddList<GS::ObjectState> ("coordinates");
        for (const API_Coord& c : poly)
            coordsOS (Create2DCoordinateObjectState (c));
        polygonsOS (polyOS);
    }
}

static GS::ObjectState CreateColorObjectState (const API_RGBColor& color)
{
    return GS::ObjectState ("red", color.f_red, "green", color.f_green, "blue", color.f_blue);
}

static void AddLibPartBasedElementDetails (GS::ObjectState& os, const Int32 libInd, const API_Guid& owner, API_ElemTypeID ownerType = API_ZombieElemID)
{
    API_LibPart	lp = {};
    lp.index = libInd;
    ACAPI_LibraryPart_Get (&lp);
    os.Add ("libPart", GS::ObjectState (
        "name", GS::UniString (lp.docu_UName),
        "parentUnID", CreateGuidObjectState (GS::UnID (lp.parentUnID).GetMainGuid ()),
        "ownUnID", CreateGuidObjectState (GS::UnID (lp.ownUnID).GetMainGuid ())));

    if (owner != APINULLGuid) {
        os.Add ("ownerElementId", CreateGuidObjectState (owner));
        if (ownerType == API_ZombieElemID) {
            API_Elem_Head elemHead = {};
            elemHead.guid = owner;
            ACAPI_Element_GetHeader (&elemHead);
            ownerType = GetElemTypeId (elemHead);
        }
        os.Add ("ownerElementType", GetElementTypeNonLocalizedName (ownerType));
    }
}

// Reads every non-reserved field of API_ObjectType (shared verbatim as API_LampType) into
// `typeSpecificDetails`, in the shape the "ObjectDetails" schema definition
// (CommonSchemaDefinitions.json) expects - the same shape CreateObjects/CreateLamps/ModifyObjects/
// ModifyLamps accept back. lightColor/lightIsOn are Lamp-only (elem.object.lightColor/lightIsOn
// exist on the shared struct but are only meaningful for API_LampID). lookId is intentionally
// never exposed - it's an internal 2D-draw "same look" dedup id, not meant to be read or set
// externally. Per the SDK's own remarks, per-story visibility (showRelAbove/showRelBelow) and
// linkToSettings.newCreationMode were "not extended" for Object/Lamp the way they were for Morph -
// still exposed here for schema symmetry, but Archicad may silently no-op them on write.
static void AddObjectLampDetails (GS::ObjectState& typeSpecificDetails, const API_Element& elem, const Stories& stories)
{
    const bool isLamp = GetElemTypeId (elem.header) == API_LampID;
#ifdef ServerMainVers_2600
    auto ownerType = elem.object.ownerType;
#else
    auto ownerType = elem.object.ownerID;
#endif
    AddLibPartBasedElementDetails (typeSpecificDetails, elem.object.libInd, elem.object.owner, GetElemTypeId (ownerType));
    typeSpecificDetails.Add ("origin", Create3DCoordinateObjectState ({elem.object.pos.x, elem.object.pos.y, GetZPos (elem.header.floorInd, elem.object.level, stories)}));

    double zDimension = 0.0;
    API_ElementMemo objectMemo = {};
    const GS::OnExit objectMemoGuard ([&objectMemo] () { ACAPI_DisposeElemMemoHdls (&objectMemo); });
    ACAPI_Element_GetMemo (elem.header.guid, &objectMemo, APIMemoMask_AddPars);
    const GSSize nParams = BMGetHandleSize ((GSHandle) objectMemo.params) / sizeof (API_AddParType);
    for (GSIndex ii = 0; ii < nParams; ++ii) {
        API_AddParType& actParam = (*objectMemo.params)[ii];

        const GS::String name (actParam.name);
        if (name == "ZZYZX") {
            zDimension = actParam.value.real;
            break;
        }
    }
    typeSpecificDetails.Add ("dimensions", Create3DCoordinateObjectState ({elem.object.xRatio, elem.object.yRatio, zDimension}));
    typeSpecificDetails.Add ("angle", elem.object.angle);

    typeSpecificDetails.Add ("pen", (Int32) elem.object.pen);
    if (elem.object.ltypeInd != APIInvalidAttributeIndex) {
        typeSpecificDetails.Add ("lineTypeId", CreateGuidObjectState (GetAttributeGuidFromIndex (API_LinetypeID, elem.object.ltypeInd)));
    }
    if (elem.object.mat != APIInvalidAttributeIndex) {
        typeSpecificDetails.Add ("surfaceId", CreateGuidObjectState (GetAttributeGuidFromIndex (API_MaterialID, elem.object.mat)));
    }
    if (elem.object.sectFill != APIInvalidAttributeIndex) {
        typeSpecificDetails.Add ("sectionFillId", CreateGuidObjectState (GetAttributeGuidFromIndex (API_FilltypeID, elem.object.sectFill)));
    }
    typeSpecificDetails.Add ("sectionFillPen", (Int32) elem.object.sectFillPen);
    typeSpecificDetails.Add ("sectionFillBackgroundPen", (Int32) elem.object.sectBGPen);
    typeSpecificDetails.Add ("sectionContourPen", (Int32) elem.object.sectContPen);
    typeSpecificDetails.Add ("useObjectPens", elem.object.useObjPens);
    typeSpecificDetails.Add ("useObjectLineTypes", elem.object.useObjLtypes);
    typeSpecificDetails.Add ("useObjectMaterials", elem.object.useObjMaterials);
    typeSpecificDetails.Add ("useObjectSectionAttributes", elem.object.useObjSectAttrs);
    typeSpecificDetails.Add ("reflected", elem.object.reflected);
    typeSpecificDetails.Add ("useFixSize", elem.object.useXYFixSize);
    typeSpecificDetails.Add ("fixPoint", (Int32) elem.object.fixPoint);
    typeSpecificDetails.Add ("offset", Create2DCoordinateObjectState (elem.object.offset));
    typeSpecificDetails.Add ("useFixedAngle", elem.object.fixedAngle != 0);
    typeSpecificDetails.Add ("isAutoOnStoryVisibility", elem.object.isAutoOnStoryVisibility);

    if (isLamp) {
        typeSpecificDetails.Add ("lightColor", CreateColorObjectState (elem.object.lightColor));
        typeSpecificDetails.Add ("lightIsOn", elem.object.lightIsOn);
    }

    {
        GS::ObjectState visibilityOS;
        visibilityOS.Add ("showOnHome", elem.object.visibility.showOnHome);
        visibilityOS.Add ("showAllAbove", elem.object.visibility.showAllAbove);
        visibilityOS.Add ("showAllBelow", elem.object.visibility.showAllBelow);
        visibilityOS.Add ("showRelAbove", (Int32) elem.object.visibility.showRelAbove);
        visibilityOS.Add ("showRelBelow", (Int32) elem.object.visibility.showRelBelow);
        typeSpecificDetails.Add ("visibility", visibilityOS);
    }
    {
        GS::ObjectState linkOS;
        linkOS.Add ("homeStoryDifference", (Int32) elem.object.linkToSettings.homeStoryDifference);
        linkOS.Add ("newCreationMode", elem.object.linkToSettings.newCreationMode);
        typeSpecificDetails.Add ("linkToSettings", linkOS);
    }
}

static std::vector<API_Coord> GetCWPanelSurfaceCoords (const API_Guid& cwPanelGuid)
{
    std::vector<PolygonData> polygonData = GetPolygonsFromMemoCoords (cwPanelGuid);
    if (polygonData.empty ()) {
        return {};
    }

    return polygonData[0].coords;
}

static Geometry::Point2d GridMeshVertexToPoint2d (const API_GridMeshVertex& vertex)
{
    return Geometry::Point2d (vertex.surfaceParam.x, vertex.surfaceParam.y);
}

static const API_GridMeshEdge& GetGridMeshEdge (const API_GridMesh& gridMesh, const API_GridEdgeInfo& edgeInfo)
{
    return edgeInfo.mainAxis
        ? gridMesh.meshEdgesMainAxis[edgeInfo.id]
        : gridMesh.meshEdgesSecondaryAxis[edgeInfo.id];
}

static std::pair<API_Coord, API_Coord> GetFrameSurfaceParamCoords (const API_CWFrameType& cwFrame, const API_GridMesh& ownerGridMesh)
{
    const API_Coord& begRel = cwFrame.begRel;
    const API_Coord& endRel = cwFrame.endRel;
    const API_GridMeshPolygon& gridMeshPolygon = ownerGridMesh.meshPolygons[cwFrame.cellID];
    std::vector<API_GridElemID> polygonCoordsIDs;
    const API_GridMeshEdge& edgeX = GetGridMeshEdge(ownerGridMesh, gridMeshPolygon.edges[0]);
    const API_GridMeshEdge& edgeY = GetGridMeshEdge(ownerGridMesh, gridMeshPolygon.edges[1]);
    const Geometry::Point2d vX1 = GridMeshVertexToPoint2d(ownerGridMesh.meshVertices[edgeX.begID]);
    const Geometry::Point2d vX2 = GridMeshVertexToPoint2d(ownerGridMesh.meshVertices[edgeX.endID]);
    const Geometry::Point2d vY1 = GridMeshVertexToPoint2d(ownerGridMesh.meshVertices[edgeY.begID]);
    const Geometry::Point2d vY2 = GridMeshVertexToPoint2d(ownerGridMesh.meshVertices[edgeY.endID]);
    const Geometry::Point2d& cellOrigo = vX1;
    const Geometry::Vector2d vX = vX2 - vX1;
    const Geometry::Vector2d vY = vY2 - vY1;
    return {API_Coord {cellOrigo.x + vX.x * begRel.x + vY.x * begRel.x, cellOrigo.y + vX.y * begRel.y + vY.y * begRel.y},
            API_Coord {cellOrigo.x + vX.x * endRel.x + vY.x * endRel.x, cellOrigo.y + vX.y * endRel.y + vY.y * endRel.y}};
}

static const API_CWFrameType* FindNextFrameOfPanel (std::vector<const API_CWFrameType*>& framePtrs, std::vector<API_Coord3D>& polygonCoords)
{
    for (auto it = framePtrs.begin (); it != framePtrs.end (); ++it) {
        const API_CWFrameType* framePtr = *it;
        if (IsSame3DCoordinate (polygonCoords.back (), framePtr->begC)) {
            polygonCoords.push_back (framePtr->endC);
            framePtrs.erase (it);
            return framePtr;
        } else if (IsSame3DCoordinate (polygonCoords.back (), framePtr->endC)) {
            polygonCoords.push_back (framePtr->begC);
            framePtrs.erase (it);
            return framePtr;
        }
    }
    return nullptr;
}

using CWSegmentGridCellID = std::pair<UInt32, API_GridElemID>;

GS::ObjectState GetDetailsOfElementsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<GS::ObjectState> elements;
    parameters.Get ("elements", elements);

    GS::ObjectState response;
    const auto& detailsOfElements = response.AddList<GS::ObjectState> ("detailsOfElements");

    const Stories stories = GetStories ();

    for (const GS::ObjectState& element : elements) {
        const GS::ObjectState* elementId = element.Get ("elementId");
        if (elementId == nullptr) {
            detailsOfElements (CreateErrorResponse (APIERR_BADPARS, "elementId is missing"));
            continue;
        }

        API_Element elem = {};
        elem.header.guid = GetGuidFromObjectState (*elementId);
        GSErrCode err = ACAPI_Element_Get (&elem);

        if (err != NoError) {
            detailsOfElements (CreateErrorResponse (err, "Failed to get the details of element"));
            continue;
        }

        GS::ObjectState detailsOfElement;
        const API_ElemTypeID typeID = GetElemTypeId (elem.header);

        detailsOfElement.Add ("type", GetElementTypeNonLocalizedName (typeID));
        detailsOfElement.Add ("floorIndex", elem.header.floorInd);
        detailsOfElement.Add ("layerIndex", GetAttributeIndex (elem.header.layer));
        detailsOfElement.Add ("drawIndex", static_cast<short> (elem.header.drwIndex));

        {
            API_ElementMemo memo = {};
            const GS::OnExit guard ([&memo] () { ACAPI_DisposeElemMemoHdls (&memo); });
            ACAPI_Element_GetMemo (elem.header.guid, &memo, APIMemoMask_ElemInfoString);

            detailsOfElement.Add ("id", memo.elemInfoString != nullptr ? *memo.elemInfoString : GS::EmptyUniString);
        }

        GS::ObjectState typeSpecificDetails;

        switch (typeID) {
            case API_WallID:
                switch (elem.wall.type) {
                    case APIWtyp_Normal:
                        typeSpecificDetails.Add ("geometryType", "Straight");
                        typeSpecificDetails.Add ("arcAngle", elem.wall.angle);
                        break;
                    case APIWtyp_Trapez:
                        typeSpecificDetails.Add ("geometryType", "Trapezoid");
                        break;
                    case APIWtyp_Poly:
                        {
                            typeSpecificDetails.Add ("geometryType", "Polygonal");
                            AddPolygonFromMemoCoords (elem.header.guid, typeSpecificDetails, "polygonOutline", "polygonArcs");
                            break;
                        }
                }
                typeSpecificDetails.Add ("structureType", StructureTypeToString (elem.wall.modelElemStructureType));
                typeSpecificDetails.Add ("zCoordinate", GetZPos (elem.header.floorInd, elem.wall.bottomOffset, stories));
                typeSpecificDetails.Add ("begCoordinate", Create2DCoordinateObjectState (elem.wall.begC));
                typeSpecificDetails.Add ("endCoordinate", Create2DCoordinateObjectState (elem.wall.endC));
                typeSpecificDetails.Add ("height", elem.wall.height);
                typeSpecificDetails.Add ("bottomOffset", elem.wall.bottomOffset);
                typeSpecificDetails.Add ("offset", elem.wall.offset);
                typeSpecificDetails.Add ("flipped", elem.wall.flipped);
                typeSpecificDetails.Add ("junctionOrder", elem.wall.sequence);
                if (elem.wall.type == APIWtyp_Poly) {
                    typeSpecificDetails.Add ("begThickness", 0);
                    typeSpecificDetails.Add ("endThickness", 0);
                } else {
                    typeSpecificDetails.Add ("begThickness", elem.wall.thickness);
                    typeSpecificDetails.Add ("endThickness", elem.wall.thickness1);
                }
                if (StructureTypeToString (elem.wall.modelElemStructureType) == "Composite") {
                    typeSpecificDetails.Add ("compositeId", CreateGuidObjectState (GetAttributeGuidFromIndex (API_CompWallID, elem.wall.composite)));
                } else if (StructureTypeToString (elem.wall.modelElemStructureType) == "Basic") {
                    typeSpecificDetails.Add ("buildingMaterialId", CreateGuidObjectState (GetAttributeGuidFromIndex (API_BuildingMaterialID, elem.wall.buildingMaterial)));
                } else if (StructureTypeToString (elem.wall.modelElemStructureType) == "Profile") {
                    typeSpecificDetails.Add ("profileId", CreateGuidObjectState (GetAttributeGuidFromIndex (API_ProfileID, elem.wall.profileAttr)));
                }
                typeSpecificDetails.Add ("referenceLineLocation", WallReferenceLineLocationToString (elem.wall.referenceLineLocation));
                {
                    const char* profileTypeStr = "Normal";
                    if (elem.wall.profileType == APISect_Slanted) {
                        profileTypeStr = "Slanted";
                    } else if (elem.wall.profileType == APISect_Trapez) {
                        profileTypeStr = "Trapez";
                    } else if (elem.wall.profileType == APISect_Poly) {
                        profileTypeStr = "Poly";
                    }
                    typeSpecificDetails.Add ("profileType", profileTypeStr);
                }
                typeSpecificDetails.Add ("slantAlpha", elem.wall.slantAlpha);
                typeSpecificDetails.Add ("slantBeta", elem.wall.slantBeta);
                typeSpecificDetails.Add ("topOffset", elem.wall.topOffset);
                typeSpecificDetails.Add ("relativeTopStory", elem.wall.relativeTopStory);
                typeSpecificDetails.Add ("zoneRel", ZoneRelToString (elem.wall.zoneRel));
                typeSpecificDetails.Add ("visibility", CreateStoryVisibilityObjectState (elem.wall.visibility));
                typeSpecificDetails.Add ("isAutoOnStoryVisibility", elem.wall.isAutoOnStoryVisibility);
                typeSpecificDetails.Add ("referenceMaterial", CreateOverriddenMaterialObjectState (elem.wall.refMat));
                typeSpecificDetails.Add ("oppositeMaterial", CreateOverriddenMaterialObjectState (elem.wall.oppMat));
                typeSpecificDetails.Add ("sideMaterial", CreateOverriddenMaterialObjectState (elem.wall.sidMat));
#ifdef ServerMainVers_2700
                typeSpecificDetails.Add ("cutFillPen", CreateOverriddenPenObjectState (elem.wall.cutFillPen));
                typeSpecificDetails.Add ("cutFillBackgroundPen", CreateOverriddenPenObjectState (elem.wall.cutFillBackgroundPen));
#else
                typeSpecificDetails.Add ("cutFillPen", GS::ObjectState ("overridden", false));
                typeSpecificDetails.Add ("cutFillBackgroundPen", GS::ObjectState ("overridden", false));
#endif
                break;

            case API_BeamID: {
                typeSpecificDetails.Add ("zCoordinate", GetZPos (elem.header.floorInd, elem.beam.level, stories));
                typeSpecificDetails.Add ("begCoordinate", Create2DCoordinateObjectState (elem.beam.begC));
                typeSpecificDetails.Add ("endCoordinate", Create2DCoordinateObjectState (elem.beam.endC));
                typeSpecificDetails.Add ("level", elem.beam.level);
                typeSpecificDetails.Add ("offset", elem.beam.offset);
                typeSpecificDetails.Add ("slantAngle", elem.beam.slantAngle);
                typeSpecificDetails.Add ("arcAngle", elem.beam.curveAngle);
                typeSpecificDetails.Add ("verticalCurveHeight", elem.beam.verticalCurveHeight);
                const char* beamShapeStr = "Straight";
                if (elem.beam.beamShape == API_HorizontallyCurvedBeam) {
                    beamShapeStr = "HorizontallyCurved";
                } else if (elem.beam.beamShape == API_VerticallyCurvedBeam) {
                    beamShapeStr = "VerticallyCurved";
                }
                typeSpecificDetails.Add ("beamShape", beamShapeStr);
                typeSpecificDetails.Add ("isSlanted", elem.beam.isSlanted);
                typeSpecificDetails.Add ("isFlipped", elem.beam.isFlipped);
                typeSpecificDetails.Add ("profileAngle", elem.beam.profileAngle);
                typeSpecificDetails.Add ("anchorPoint", AnchorIdToString (static_cast<API_AnchorID> (elem.beam.anchorPoint)));
#ifdef ServerMainVers_2700
                typeSpecificDetails.Add ("cutFillPen", CreateOverriddenPenObjectState (elem.beam.cutFillPen));
                typeSpecificDetails.Add ("cutFillBackgroundPen", CreateOverriddenPenObjectState (elem.beam.cutFillBackgroundPen));
#else
                typeSpecificDetails.Add ("cutFillPen", GS::ObjectState ("overridden", false));
                typeSpecificDetails.Add ("cutFillBackgroundPen", GS::ObjectState ("overridden", false));
#endif
                typeSpecificDetails.Add ("coverFill", CreateCoverFillObjectState (
                    elem.beam.useCoverFill, elem.beam.useCoverFillFromSurface, elem.beam.coverFillOrientationComesFrom3D,
                    elem.beam.coverFillType, elem.beam.coverFillForegroundPen, elem.beam.coverFillBackgroundPen,
                    elem.beam.coverFillTransformationType, elem.beam.coverFillTransformation));
                AddBeamHolesFromMemo (elem.header.guid, typeSpecificDetails, "holes");
                AddBeamSectionFromMemo (elem.header.guid, typeSpecificDetails);
            } break;

            case API_SlabID:
                typeSpecificDetails.Add ("structureType", StructureTypeToString (elem.slab.modelElemStructureType));
                typeSpecificDetails.Add ("thickness", elem.slab.thickness);
                typeSpecificDetails.Add ("level", elem.slab.level);
                typeSpecificDetails.Add ("offsetFromTop", elem.slab.offsetFromTop);
                typeSpecificDetails.Add ("zCoordinate", GetZPos (elem.header.floorInd, elem.slab.level, stories));
                typeSpecificDetails.Add ("referencePlaneLocation", SlabReferencePlaneLocationToString (elem.slab.referencePlaneLocation));
                if (StructureTypeToString (elem.slab.modelElemStructureType) == "Composite") {
                    typeSpecificDetails.Add ("compositeId", CreateGuidObjectState (GetAttributeGuidFromIndex (API_CompWallID, elem.slab.composite)));
                } else {
                    typeSpecificDetails.Add ("buildingMaterialId", CreateGuidObjectState (GetAttributeGuidFromIndex (API_BuildingMaterialID, elem.slab.buildingMaterial)));
                }
                typeSpecificDetails.Add ("topMaterial", CreateOverriddenMaterialObjectState (elem.slab.topMat));
                typeSpecificDetails.Add ("sideMaterial", CreateOverriddenMaterialObjectState (elem.slab.sideMat));
                typeSpecificDetails.Add ("bottomMaterial", CreateOverriddenMaterialObjectState (elem.slab.botMat));
#ifdef ServerMainVers_2700
                typeSpecificDetails.Add ("cutFillPen", CreateOverriddenPenObjectState (elem.slab.cutFillPen));
                typeSpecificDetails.Add ("cutFillBackgroundPen", CreateOverriddenPenObjectState (elem.slab.cutFillBackgroundPen));
#else
                typeSpecificDetails.Add ("cutFillPen", GS::ObjectState ("overridden", false));
                typeSpecificDetails.Add ("cutFillBackgroundPen", GS::ObjectState ("overridden", false));
#endif
                {
                    GS::ObjectState floorFillOs;
                    floorFillOs.Add ("use", elem.slab.useFloorFill);
                    floorFillOs.Add ("foregroundPen", elem.slab.floorFillPen);
                    floorFillOs.Add ("backgroundPen", elem.slab.floorFillBGPen);
                    floorFillOs.Add ("fillId", CreateGuidObjectState (GetAttributeGuidFromIndex (API_FilltypeID, elem.slab.floorFillInd)));
                    floorFillOs.Add ("use3DHatching", elem.slab.use3DHatching);
                    floorFillOs.Add ("orientation", CreateHatchOrientationObjectState (elem.slab.hatchOrientation));
                    typeSpecificDetails.Add ("floorFill", floorFillOs);
                }
                AddPolygonWithHolesFromMemoCoords (elem.header.guid, typeSpecificDetails, "polygonOutline", "polygonArcs", "holes", "polygonOutline", "polygonArcs");
                break;

            case API_ZoneID:
                typeSpecificDetails.Add ("name", GS::UniString (elem.zone.roomName));
                typeSpecificDetails.Add ("numberStr", GS::UniString (elem.zone.roomNoStr));
                typeSpecificDetails.Add ("categoryAttributeId", CreateGuidObjectState (GetAttributeGuidFromIndex (API_ZoneCatID, elem.zone.catInd)));
                typeSpecificDetails.Add ("stampPosition", Create2DCoordinateObjectState (elem.zone.pos));
                typeSpecificDetails.Add ("stampAngle", elem.zone.stampAngle);
                typeSpecificDetails.Add ("fixedStampAngle", elem.zone.fixedAngle);
                typeSpecificDetails.Add ("isManual", elem.zone.manual);
                typeSpecificDetails.Add ("zCoordinate", GetZPos (elem.header.floorInd, elem.zone.roomBaseLev, stories));
                AddPolygonWithHolesFromMemoCoords (elem.header.guid, typeSpecificDetails, "polygonOutline", "polygonArcs", "holes", "polygonOutline", "polygonArcs");
                break;

            case API_ColumnID:
                typeSpecificDetails.Add ("origin", Create2DCoordinateObjectState (elem.column.origoPos));
                typeSpecificDetails.Add ("coreAnchor", AnchorIdToString (static_cast<API_AnchorID> (elem.column.coreAnchor)));
                typeSpecificDetails.Add ("zCoordinate", GetZPos (elem.header.floorInd, elem.column.bottomOffset, stories));
                typeSpecificDetails.Add ("height", elem.column.height);
                typeSpecificDetails.Add ("bottomOffset", elem.column.bottomOffset);
                typeSpecificDetails.Add ("axisRotationAngle", elem.column.axisRotationAngle);
                typeSpecificDetails.Add ("isSlanted", elem.column.isSlanted);
                typeSpecificDetails.Add ("slantAngle", elem.column.slantAngle);
                typeSpecificDetails.Add ("slantDirectionAngle", elem.column.slantDirectionAngle);
                typeSpecificDetails.Add ("isFlipped", elem.column.isFlipped);
                typeSpecificDetails.Add ("wrapping", elem.column.wrapping);
                typeSpecificDetails.Add ("topOffset", elem.column.topOffset);
                typeSpecificDetails.Add ("relativeTopStory", elem.column.relativeTopStory);
#ifdef ServerMainVers_2700
                typeSpecificDetails.Add ("cutFillPen", CreateOverriddenPenObjectState (elem.column.cutFillPen));
                typeSpecificDetails.Add ("cutFillBackgroundPen", CreateOverriddenPenObjectState (elem.column.cutFillBackgroundPen));
#else
                typeSpecificDetails.Add ("cutFillPen", GS::ObjectState ("overridden", false));
                typeSpecificDetails.Add ("cutFillBackgroundPen", GS::ObjectState ("overridden", false));
#endif
                typeSpecificDetails.Add ("coverFill", CreateCoverFillObjectState (
                    elem.column.useCoverFill, elem.column.useCoverFillFromSurface, elem.column.coverFillOrientationComesFrom3D,
                    elem.column.coverFillType, elem.column.coverFillForegroundPen, elem.column.coverFillBackgroundPen,
                    elem.column.coverFillTransformationType, elem.column.coverFillTransformation));
                AddColumnSectionFromMemo (elem.header.guid, typeSpecificDetails);
                break;

            case API_DoorID:
            case API_WindowID:
                AddLibPartBasedElementDetails (typeSpecificDetails, elem.window.openingBase.libInd, elem.window.owner);
                typeSpecificDetails.Add ("width", elem.window.openingBase.width);
                typeSpecificDetails.Add ("height", elem.window.openingBase.height);
                typeSpecificDetails.Add ("sillHeight", elem.window.lower);
                typeSpecificDetails.Add ("centerOffset", elem.window.objLoc);
                typeSpecificDetails.Add ("reflected", elem.window.openingBase.reflected);
                typeSpecificDetails.Add ("refSide", elem.window.openingBase.refSide);
                typeSpecificDetails.Add ("oSide", elem.window.openingBase.oSide);
                break;

            case API_LabelID:
                AddLibPartBasedElementDetails (typeSpecificDetails, ((elem.label.labelClass == APILblClass_Symbol) ? elem.label.u.symbol.libInd : -1), elem.label.parent, GetElemTypeId (elem.label.parentType));
                typeSpecificDetails.Add ("begCoordinate", Create2DCoordinateObjectState (elem.label.begC));
                typeSpecificDetails.Add ("midCoordinate", Create2DCoordinateObjectState (elem.label.midC));
                typeSpecificDetails.Add ("endCoordinate", Create2DCoordinateObjectState (elem.label.endC));
                typeSpecificDetails.Add ("hasLeaderLine", elem.label.hasLeaderLine);
                break;

            case API_ObjectID:
            case API_LampID:
                AddObjectLampDetails (typeSpecificDetails, elem, stories);
                break;

            case API_DetailID:
            case API_WorksheetID: {
                typeSpecificDetails.Add ("basePoint", Create2DCoordinateObjectState (elem.detail.pos));
                typeSpecificDetails.Add ("angle", elem.detail.angle);
                typeSpecificDetails.Add ("markerId", CreateGuidObjectState (elem.detail.markId));
                typeSpecificDetails.Add ("detailName", GS::UniString (elem.detail.detailName));
                typeSpecificDetails.Add ("detailIdStr", GS::UniString (elem.detail.detailIdStr));
                typeSpecificDetails.Add ("isHorizontalMarker", elem.detail.horizontalMarker);
                typeSpecificDetails.Add ("isWindowOpened", elem.detail.windOpened);
                AddPolygonFromMemoCoords (elem.header.guid, typeSpecificDetails, "clipPolygon");
                GS::ObjectState linkDataOS;
                switch (elem.detail.linkData.referringLevel) {
                    case API_ReferringLevel::ReferredToView:
                        linkDataOS.Add ("referredView", CreateGuidObjectState (elem.detail.linkData.referredView));
                        break;
                    case API_ReferringLevel::ReferredToDrawing:
                        linkDataOS.Add ("referredDrawing", CreateGuidObjectState (elem.detail.linkData.referredDrawing));
                        break;
                    case API_ReferringLevel::ReferredToViewPoint:
                        linkDataOS.Add ("referredPMViewPoint", CreateGuidObjectState (elem.detail.linkData.referredPMViewPoint));
                        break;
                    default:
                        break;
                }
                typeSpecificDetails.Add ("linkData", linkDataOS);
            } break;

            case API_PolyLineID: {
                AddPolygonFromMemoCoords (elem.header.guid, typeSpecificDetails, "coordinates", "arcs");
                AddLineFamilySettingsDetails (typeSpecificDetails, elem.polyLine.linePen, elem.polyLine.ltypeInd, elem.polyLine.roomSeparator);
                typeSpecificDetails.Add ("zCoordinate", GetZPos (elem.header.floorInd, 0, stories));
            } break;

            case API_HatchID: {
                AddPolygonWithHolesFromMemoCoords (elem.header.guid, typeSpecificDetails, "coordinates", "arcs", "holes", "polygonOutline", "polygonArcs");
                typeSpecificDetails.Add ("contourPenIndex", elem.hatch.contPen.penIndex);
                typeSpecificDetails.Add ("fillPenIndex", elem.hatch.fillPen.penIndex);
                typeSpecificDetails.Add ("fillBackgroundPenIndex", elem.hatch.fillBGPen);
                typeSpecificDetails.Add ("fillId", CreateGuidObjectState (GetAttributeGuidFromIndex (API_FilltypeID, elem.hatch.fillInd)));
                typeSpecificDetails.Add ("buildingMaterialId", CreateGuidObjectState (GetAttributeGuidFromIndex (API_BuildingMaterialID, elem.hatch.buildingMaterial)));
                typeSpecificDetails.Add ("roomSpecial", (Int32) elem.hatch.roomSpecial);
                typeSpecificDetails.Add ("showArea", elem.hatch.showArea);
                typeSpecificDetails.Add ("zCoordinate", GetZPos (elem.header.floorInd, 0, stories));
            } break;

            case API_LineID: {
                typeSpecificDetails.Add ("begCoordinate", Create2DCoordinateObjectState (elem.line.begC));
                typeSpecificDetails.Add ("endCoordinate", Create2DCoordinateObjectState (elem.line.endC));
                AddLineFamilySettingsDetails (typeSpecificDetails, elem.line.linePen, elem.line.ltypeInd, elem.line.roomSeparator);
                typeSpecificDetails.Add ("zCoordinate", GetZPos (elem.header.floorInd, 0, stories));
            } break;

            case API_ArcID:
            case API_CircleID: {
                typeSpecificDetails.Add ("origin", Create2DCoordinateObjectState (elem.arc.origC));
                typeSpecificDetails.Add ("radius", elem.arc.r);
                typeSpecificDetails.Add ("angle", elem.arc.angle);
                typeSpecificDetails.Add ("ratio", elem.arc.ratio);
                if (typeID == API_ArcID) {
                    typeSpecificDetails.Add ("begAngle", elem.arc.begAng);
                    typeSpecificDetails.Add ("endAngle", elem.arc.endAng);
                }
                typeSpecificDetails.Add ("reflected", elem.arc.reflected);
                AddLineFamilySettingsDetails (typeSpecificDetails, elem.arc.linePen, elem.arc.ltypeInd, elem.arc.roomSeparator);
                typeSpecificDetails.Add ("zCoordinate", GetZPos (elem.header.floorInd, 0, stories));
            } break;

            case API_HotspotID: {
                typeSpecificDetails.Add ("position", Create2DCoordinateObjectState (elem.hotspot.pos));
                typeSpecificDetails.Add ("height", elem.hotspot.height);
                typeSpecificDetails.Add ("penIndex", elem.hotspot.pen);
            } break;

            case API_SplineID: {
                // Spline has no poly/nCoords summary field (unlike Line/PolyLine/Hatch) - the
                // point count is implicit in the coords memo handle's byte size. Index 0 is a
                // real point here (not a dummy sentinel), and no closing duplicate is stored even
                // when closed=true (per the ACAPI_SplineType reference docs).
                API_ElementMemo splineMemo = {};
                if (ACAPI_Element_GetMemo (elem.header.guid, &splineMemo, APIMemoMask_Polygon) == NoError && splineMemo.coords != nullptr) {
                    const Int32 nPoints = BMGetHandleSize (reinterpret_cast<GSHandle> (splineMemo.coords)) / sizeof (API_Coord);
                    const auto& coords = typeSpecificDetails.AddList<GS::ObjectState> ("coordinates");
                    for (Int32 i = 0; i < nPoints; ++i) {
                        coords (Create2DCoordinateObjectState ((*splineMemo.coords)[i]));
                    }
                }
                ACAPI_DisposeElemMemoHdls (&splineMemo);
                typeSpecificDetails.Add ("closed", elem.spline.closed);
                AddLineFamilySettingsDetails (typeSpecificDetails, elem.spline.linePen, elem.spline.ltypeInd, elem.spline.roomSeparator);
                typeSpecificDetails.Add ("zCoordinate", GetZPos (elem.header.floorInd, 0, stories));
            } break;

            case API_CurtainWallID: {
                typeSpecificDetails.Add ("height", elem.curtainWall.height);
                typeSpecificDetails.Add ("angle", elem.curtainWall.angle);
                typeSpecificDetails.Add ("flipped", elem.curtainWall.flipped);
            } break;

            case API_CurtainWallSegmentID: {
                typeSpecificDetails.Add ("begCoordinate", Create3DCoordinateObjectState (elem.cwSegment.begC));
                typeSpecificDetails.Add ("endCoordinate", Create3DCoordinateObjectState (elem.cwSegment.endC));
                typeSpecificDetails.Add ("extrusionVector", Create3DCoordinateObjectState (elem.cwSegment.extrusion));
                typeSpecificDetails.Add ("gridOrigin", Create3DCoordinateObjectState (elem.cwSegment.gridOrigin));
                typeSpecificDetails.Add ("gridAngle", elem.cwSegment.gridAngle);
                if (elem.cwSegment.segmentType == API_CWSegmentTypeID::APICWSeT_Arc) {
                    typeSpecificDetails.Add ("arcOrigin", Create3DCoordinateObjectState (elem.cwSegment.arcOrigin));
                    typeSpecificDetails.Add ("isNegativeArc", elem.cwSegment.negArc);
                }
            } break;

            case API_CurtainWallPanelID: {
                API_Element ownerCW = {};
                ownerCW.header.guid = elem.cwPanel.owner;
                ACAPI_Element_Get(&ownerCW);

                API_ElementMemo cwMemo = {};
                const GS::OnExit cwMemoGuard ([&cwMemo] () { ACAPI_DisposeElemMemoHdls (&cwMemo); });
                ACAPI_Element_GetMemo(ownerCW.header.guid, &cwMemo, APIMemoMask_CWallSegments | APIMemoMask_CWallFrames);
                const API_Guid ownerCWSegment = cwMemo.cWallSegments[elem.cwPanel.segmentID].head.guid;

                API_ElementMemo cwSegmentMemo = {};
                const GS::OnExit cwSegmentMemoGuard ([&cwSegmentMemo] () { ACAPI_DisposeElemMemoHdls (&cwSegmentMemo); });
                ACAPI_Element_GetMemo(ownerCWSegment, &cwSegmentMemo, APIMemoMask_CWSegGridMesh);
                const API_GridMesh& ownerGridMesh = *cwSegmentMemo.cWSegGridMesh;

                API_ElementMemo cwPanelMemo = {};
                const GS::OnExit cwPanelMemoGuard ([&cwPanelMemo] () { ACAPI_DisposeElemMemoHdls (&cwPanelMemo); });
                ACAPI_Element_GetMemo(elem.header.guid, &cwPanelMemo, APIMemoMask_CWallPanels);
                const GS::HashTable<API_Guid, GS::Array<API_GridElemID>>& cWallPanelGridIDTable = *cwPanelMemo.cWallPanelGridIDTable;

                const GS::Array<API_GridElemID>& gridIDs = cWallPanelGridIDTable[elem.header.guid];
                GS::Array<API_GridElemID> gridMeshPolygonIDs = gridIDs;
                for (const API_GridElemID& polygonID : gridIDs) {
                    const API_GridMeshPolygon& gridMeshPolygon = ownerGridMesh.meshPolygons[polygonID];
                    gridMeshPolygonIDs.Append (gridMeshPolygon.neighbourIDs[API_GridMeshDirection::API_GridMeshRight]);
                    gridMeshPolygonIDs.Append (gridMeshPolygon.neighbourIDs[API_GridMeshDirection::API_GridMeshUpper]);
                }

                const std::vector<API_Coord> panelSurfaceCoords = GetCWPanelSurfaceCoords (elem.header.guid);

                std::vector<const API_CWFrameType*> framePtrs;
                {
                    std::vector<const API_CWFrameType*> cornerFramesOfNextSegment;
                    for (UIndex i = 0; i < ownerCW.curtainWall.nFrames; ++i) {
                        const API_CWFrameType& frame = cwMemo.cWallFrames[i];
                        if (frame.segmentID == elem.cwPanel.segmentID) {
                            if (gridMeshPolygonIDs.Contains (frame.cellID)) {
                                for (GSIndex i = 0; i < panelSurfaceCoords.size () - 1; ++i) {
                                    const API_Coord& c1 = panelSurfaceCoords[i];
                                    const API_Coord& c2 = panelSurfaceCoords[i + 1];
                                    auto frameSurfaceCoords = GetFrameSurfaceParamCoords (frame, ownerGridMesh);

                                    if ((IsSame2DCoordinate (frameSurfaceCoords.first, c1) && IsSame2DCoordinate (frameSurfaceCoords.second, c2)) ||
                                        (IsSame2DCoordinate (frameSurfaceCoords.first, c2) && IsSame2DCoordinate (frameSurfaceCoords.second, c1))) {
                                        framePtrs.push_back (&frame);
                                        break;
                                    }
                                }
                                if (framePtrs.size () >= elem.cwPanel.edgesNum) {
                                    break;
                                }
                            }
                        } else if (frame.classID == APICWFrameClass_Corner && frame.segmentID == elem.cwPanel.segmentID + 1) {
                            cornerFramesOfNextSegment.push_back (&frame);
                        }
                    }
                    if (framePtrs.size () < elem.cwPanel.edgesNum) {
                        framePtrs.insert (framePtrs.end (), cornerFramesOfNextSegment.begin (), cornerFramesOfNextSegment.end ());
                    }
                }

                const auto& polygonCoordinates = typeSpecificDetails.AddList<GS::ObjectState> ("polygonCoordinates");
                const auto& frames = typeSpecificDetails.AddList<GS::ObjectState> ("frames");
                if (!framePtrs.empty ()) {
                    std::vector<API_Coord3D> polygonCoords = { framePtrs[0]->begC, framePtrs[0]->endC };
                    frames (CreateElementIdObjectState (framePtrs[0]->head.guid));
                    framePtrs.erase (framePtrs.begin ());
                    while (polygonCoords.size() != (elem.cwPanel.edgesNum + 1) && !framePtrs.empty ()) {
                        auto* framePtr = FindNextFrameOfPanel (framePtrs, polygonCoords);
                        if (framePtr == nullptr) {
                            break;
                        }
                        frames (CreateElementIdObjectState (framePtr->head.guid));
                    }
                    for (const auto& c : polygonCoords) {
                        polygonCoordinates (Create3DCoordinateObjectState (c));
                    }
                }
                typeSpecificDetails.Add ("isHidden", elem.cwPanel.hidden);
                typeSpecificDetails.Add ("segmentIndex", elem.cwPanel.segmentID);
                typeSpecificDetails.Add ("className", GS::UniString (elem.cwPanel.className));
            } break;

            case API_CurtainWallFrameID: {
                typeSpecificDetails.Add ("begCoordinate", Create3DCoordinateObjectState (elem.cwFrame.begC));
                typeSpecificDetails.Add ("endCoordinate", Create3DCoordinateObjectState (elem.cwFrame.endC));
                typeSpecificDetails.Add ("orientationVector", Create3DCoordinateObjectState (elem.cwFrame.orientation));
                typeSpecificDetails.Add ("panelConnectionHole", GS::ObjectState ("d", elem.cwFrame.d, "w", elem.cwFrame.w));
                typeSpecificDetails.Add ("frameContour", GS::ObjectState ("a1", elem.cwFrame.a1, "a2", elem.cwFrame.a2, "b1", elem.cwFrame.b1, "b2", elem.cwFrame.b2));
                typeSpecificDetails.Add ("segmentIndex", elem.cwFrame.segmentID);
                typeSpecificDetails.Add ("className", GS::UniString (elem.cwFrame.className));
                typeSpecificDetails.Add ("type",
                    elem.cwFrame.classID == APICWFrameClass_Merged ? "Deleted" :
                    elem.cwFrame.classID == APICWFrameClass_Boundary ? "Boundary" :
                    elem.cwFrame.classID == APICWFrameClass_Corner ? "Corner" :
                    elem.cwFrame.classID == APICWFrameClass_Division ? "Division" : "Custom");
            } break;

            case API_MeshID: {
                typeSpecificDetails.Add ("level", elem.mesh.level);
                if (elem.mesh.skirt == 3) {
                    typeSpecificDetails.Add ("skirtType", "SurfaceOnlyWithoutSkirt");
                } else if (elem.mesh.skirt == 2) {
                    typeSpecificDetails.Add ("skirtType", "WithSkirt");
                } else {
                    typeSpecificDetails.Add ("skirtType", "SolidBodyWithSkirt");
                }
                typeSpecificDetails.Add ("skirtLevel", elem.mesh.skirtLevel);
                if (elem.mesh.smoothRidges == APIRidge_AllSharp) {
                    typeSpecificDetails.Add ("ridges", GS::UniString ("AllSharp"));
                } else if (elem.mesh.smoothRidges == APIRidge_AllSmooth) {
                    typeSpecificDetails.Add ("ridges", GS::UniString ("AllSmooth"));
                } else {
                    typeSpecificDetails.Add ("ridges", GS::UniString ("UserDefined"));
                }
                typeSpecificDetails.Add ("showLines",    elem.mesh.showLines != 0);
                typeSpecificDetails.Add ("contourPen",   (Int32)elem.mesh.contPen);
                typeSpecificDetails.Add ("levelPen",     (Int32)elem.mesh.levelPen);
                typeSpecificDetails.Add ("lineTypeIndex", GetAttributeIndex (elem.mesh.ltypeInd));
                constexpr bool includeZCoords = true;
                AddPolygonWithHolesFromMemoCoords (elem.header.guid, typeSpecificDetails, "polygonCoordinates", "polygonArcs", "holes", "polygonCoordinates", "polygonArcs", includeZCoords);
                if (elem.mesh.levelLines.nSubLines > 0) {
                    API_ElementMemo memo = {};
                    const GS::OnExit guard ([&memo] () { ACAPI_DisposeElemMemoHdls (&memo); });
                    if (ACAPI_Element_GetMemo (elem.header.guid, &memo, APIMemoMask_MeshLevel) == NoError && memo.meshLevelCoords != nullptr && memo.meshLevelEnds != nullptr) {
                        const auto& sublines = typeSpecificDetails.AddList<GS::ObjectState> ("sublines");
                        const GSSize nSublines = BMhGetSize (reinterpret_cast<GSHandle> (memo.meshLevelEnds)) / sizeof (Int32);
                        Int32 iCoord = 0;
                        for (Int32 i = 0; i < nSublines; ++i) {
                            GS::ObjectState subline;
                            const auto& coordinates = subline.AddList<GS::ObjectState> ("coordinates");

                            const Int32 nCoords = (*memo.meshLevelEnds)[i];
                            for (; iCoord < nCoords; ++iCoord) {
                                const API_MeshLevelCoord& coord = (*memo.meshLevelCoords)[iCoord];
                                coordinates (Create3DCoordinateObjectState (coord.c));
                            }
                            sublines (subline);
                        }
                    }
                }
            } break;

            case API_DrawingID: {
                typeSpecificDetails.Add ("pos",           Create2DCoordinateObjectState (elem.drawing.pos));
                typeSpecificDetails.Add ("angle",         elem.drawing.angle);
                typeSpecificDetails.Add ("ratio",         elem.drawing.ratio);
                typeSpecificDetails.Add ("drawingScale",  elem.drawing.drawingScale);
                typeSpecificDetails.Add ("modelOffset",   Create2DCoordinateObjectState (elem.drawing.modelOffset));
                typeSpecificDetails.Add ("isCutWithFrame", elem.drawing.isCutWithFrame);
                typeSpecificDetails.Add ("navigatorItemId", CreateGuidObjectState (elem.drawing.drawingGuid));
                typeSpecificDetails.Add ("bounds", GS::ObjectState (
                    "xMin", elem.drawing.bounds.xMin,
                    "yMin", elem.drawing.bounds.yMin,
                    "xMax", elem.drawing.bounds.xMax,
                    "yMax", elem.drawing.bounds.yMax));
                if (elem.drawing.isCutWithFrame) {
                    AddPolygonFromMemoCoords (elem.header.guid, typeSpecificDetails, "clipPolygon");
                }
                typeSpecificDetails.Add ("nameType",      DrawingNameTypeToString (elem.drawing.nameType));
                if (elem.drawing.nameType == APIName_CustomName) {
                    typeSpecificDetails.Add ("customName", GS::UniString (elem.drawing.name));
                }
                typeSpecificDetails.Add ("numberingType",  DrawingNumberingTypeToString (elem.drawing.numberingType));
                if (elem.drawing.numberingType == APINumbering_CustomNum) {
                    typeSpecificDetails.Add ("customNumber", GS::UniString (elem.drawing.customNumber));
                }
                typeSpecificDetails.Add ("isInNumbering", elem.drawing.isInNumbering);
                // Library part index of the title (API_DrawingTitle::libInd) - a negative/invalid
                // index here means no title object is instantiated at all (nothing to show); setting
                // it to a valid index (e.g. copied from another drawing) is what makes Archicad
                // create the title's own placed element.
                typeSpecificDetails.Add ("titleLibraryPartIndex", elem.drawing.title.libInd);
                if (elem.drawing.title.guid != APINULLGuid) {
                    // The drawing title is itself a placed, GDL-parameterized library part
                    // element (API_DrawingTitle::guid, "object based elements from Archicad 10")
                    // - its own settings (font/pen/size aside) are reachable like any other
                    // element via this id, e.g. with Get/SetGDLParametersOfElements.
                    typeSpecificDetails.Add ("titleElementId", CreateGuidObjectState (elem.drawing.title.guid));
                }
            } break;

            case API_MorphID:
                AddMorphBodyFromMemo (elem, typeSpecificDetails);
                break;

            default:
                typeSpecificDetails.Add ("error", "Not yet supported element type");
                break;
        }

        detailsOfElement.Add ("details", typeSpecificDetails);
        AddFloorPlanPolygonsIfAvailable (elem.header.guid, detailsOfElement);

        detailsOfElements (detailsOfElement);
    }

    return response;
}

SetDetailsOfElementsCommand::SetDetailsOfElementsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String SetDetailsOfElementsCommand::GetName () const
{
    return "SetDetailsOfElements";
}

GS::Optional<GS::UniString> SetDetailsOfElementsCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "elementsWithDetails": {
                "type": "array",
                "description": "The elements with parameters.",
                "items": {
                    "type": "object",
                    "properties": {
                        "elementId": {
                            "$ref": "#/ElementId"
                        },
                        "details": {
                            "type": "object",
                            "description": "Details of an element.",
                            "properties": {
                                "floorIndex": {
                                    "type": "number"
                                },
                                "layerIndex": {
                                    "type": "number"
                                },
                                "drawIndex": {
                                    "type": "number"
                                },
                                "typeSpecificDetails": {
                                    "$ref": "#/TypeSpecificSettings"
                                }
                            },
                            "additionalProperties": false,
                            "required": []
                        }
                    },
                    "additionalProperties": false,
                    "required": [
                        "elementId",
                        "details"
                    ]
                }
            }
        },
        "additionalProperties": false,
        "required": [
            "elementsWithDetails"
        ]
    })";
}

GS::Optional<GS::UniString> SetDetailsOfElementsCommand::GetRawResponseSchema () const
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

// ============================================================
// drawIndex repositioning — implementation notes
// ============================================================
//
// WHY NOT ACAPI_Element_Change WITH drwIndex MASK
// ------------------------------------------------
// ACAPI_Element_Change silently ignores the drwIndex field even when the
// mask bit is set (confirmed Graphisoft bug, AC27). The only working API
// is ACAPI_Grouping_Tool with the following commands:
//   APITool_BringForward  (+1, reliable)
//   APITool_SendBackward  (-1, reliable)
//   APITool_ResetOrder    (→ class default, unreliable for some types — see below)
//
// DEAD ENDS (do not reintroduce)
// --------------------------------
//   APITool_BringToFront : unreliable on Wall and its sub-elements; causes
//                          unexpected level jumps. Removed from all strategies.
//   APITool_SendToBack   : does NOT reliably reach level 1. In sparse files
//                          it stops at 2 or 3. Removed from all strategies.
//
// STRATEGIES FOR NORMAL ELEMENTS
// --------------------------------
// Target clamped to [1, 14]. Two strategies, cheapest wins:
//   Direct  : |target - current| × BringForward or SendBackward
//   ViaReset: 1 ResetOrder + |target - classDefault| steps
//             (disabled for Morph, Railing, PolyLine — ResetOrder is a no-op
//              for these types; a simulated default from GetDefaultDrwIndex is
//              used instead if they need to reach their default level)
//
// CLASS DEFAULTS (per ArchiCAD documentation, levels 1-4 and 11-14 are void)
//   5  : Drawing
//   6  : Hatch, Zone
//   7  : Wall, Slab, Roof, Shell, Morph, Mesh, Column, CurtainWall, Stair,
//         Railing, Door, Window, Skylight, Opening
//   8  : Lamp, Object
//   9  : Line, Arc, Spline, PolyLine, Beam, Hotspot, ChangeMarker, Detail,
//         Worksheet
//   10 : Dimension (all types), Label, Text, CutPlane, Elevation,
//         InteriorElevation
//
// OPENINGS (Door, Window, Skylight) — special case
// --------------------------------------------------
// ArchiCAD enforces a hard minimum of 7 for elements embedded in a host
// (wall or roof/shell). Levels 1-6 are unreachable regardless of strategy.
//
// Host types by opening type:
//   Door   → Wall
//   Window → Wall
//   Skylight → Roof or Shell
//
// Levels 8-14 require indirect host manipulation:
//   1. Step host UP  (target - hostOriginal) × BringForward  → opening follows
//   2. Step host DOWN the same number of times × SendBackward → host returns,
//      opening stays elevated (the two are decoupled once host passes the
//      opening's natural level)
//
// Reset (drawIndex = 0) on an elevated opening is a no-op via direct
// ResetOrder. It must be applied to the HOST instead, which resets all its
// openings to level 7.
//
// Reverse host lookup (opening → host) uses the owner guid stored directly on
// the opening element (API_WindowType::owner for door/window, API_SkylightType::owner
// for skylight) via ACAPI_Element_Get — no need to scan hosts.
//
// BATCH ORDERING CONSTRAINT
// --------------------------
// Openings are processed before all other elements within the same batch.
// This ensures the host manipulation reads the host's pre-batch position, not
// a position already shifted by another element in the same call.
//
// KNOWN LIMITATIONS
// -----------------
// • Door/Window/Skylight: levels 1-6 are unreachable (ArchiCAD constraint).
// • An opening cannot be below its host's current level. If the host wall/roof
//   is at level 9, the opening cannot reach levels 7 or 8.
// • Multiple openings in the same host wall/roof with different targets in a
//   single batch: all will end up at the same level (the last one processed),
//   because moving the host affects all its openings simultaneously.
// • Opening (API_OpeningID, generic void): completely immovable. All
//   Grouping_Tool calls are no-ops. ArchiCAD displays level 0 for these
//   elements (outside the 1-14 ordering system); the API may report 7
//   (class default) but the actual display order cannot be changed. Likely
//   due to multi-host nature (can span Wall, Slab, Roof, Shell, Beam, Column).
// • Morph, Railing, PolyLine: ResetOrder is a no-op; reset (drawIndex = 0)
//   is simulated by stepping from current position to the class default.
// ============================================================

static bool IsOpeningType (const API_Elem_Head& head)
{
    switch (GetElemTypeId (head)) {
        case API_DoorID:
        case API_WindowID:
        case API_SkylightID: return true;
        default:             return false;
    }
}

static bool IsResetOrderReliable (const API_Elem_Head& head)
{
    switch (GetElemTypeId (head)) {
        case API_MorphID:
        case API_RailingID:
        case API_PolyLineID: return false;
        default:             return true;
    }
}

static Int32 GetDefaultDrwIndex (const API_Elem_Head& head)
{
    switch (GetElemTypeId (head)) {
        case API_DrawingID:                                                      return 5;
        case API_HatchID:     case API_ZoneID:                                   return 6;
        case API_WallID:      case API_SlabID:      case API_RoofID:
        case API_ShellID:     case API_MorphID:     case API_MeshID:
        case API_ColumnID:    case API_CurtainWallID: case API_StairID:
        case API_RailingID:   case API_DoorID:      case API_WindowID:
        case API_SkylightID:  case API_OpeningID:                                return 7;
        case API_LampID:      case API_ObjectID:                                 return 8;
        case API_LineID:      case API_ArcID:       case API_SplineID:
        case API_PolyLineID:  case API_BeamID:      case API_HotspotID:
        case API_ChangeMarkerID: case API_DetailID: case API_WorksheetID:        return 9;
        case API_DimensionID: case API_RadialDimensionID:
        case API_LevelDimensionID: case API_AngleDimensionID:
        case API_LabelID:     case API_TextID:      case API_CutPlaneID:
        case API_ElevationID: case API_InteriorElevationID:                      return 10;
        default:                                                                  return 7;
    }
}

static void ApplyDrawIndexStrategy (GS::Array<API_Guid>& guids, const API_Elem_Head& head, Int32 target, Int32 current)
{
    target = GS::Max (1, GS::Min (target, 14));
    const Int32 defaultLevel = GetDefaultDrwIndex (head);
    const Int32 directCost   = GS::Abs (target - current);
    const Int32 viaResetCost = IsResetOrderReliable (head)
                             ? 1 + GS::Abs (target - defaultLevel)
                             : INT_MAX;

    if (viaResetCost < directCost) {
        ACAPI_Grouping_Tool (guids, APITool_ResetOrder, nullptr);
        const API_ToolCmdID step = (target > defaultLevel) ? APITool_BringForward : APITool_SendBackward;
        for (Int32 i = 0; i < GS::Abs (target - defaultLevel); ++i)
            ACAPI_Grouping_Tool (guids, step, nullptr);
    } else {
        const API_ToolCmdID step = (target > current) ? APITool_BringForward : APITool_SendBackward;
        for (Int32 i = 0; i < directCost; ++i)
            ACAPI_Grouping_Tool (guids, step, nullptr);
    }
}

static GS::HashTable<API_Guid, API_Guid> BuildOpeningToHostMap (const GS::HashSet<API_Guid>& targetGuids)
{
    GS::HashTable<API_Guid, API_Guid> result;
    for (const API_Guid& guid : targetGuids) {
        API_Element elem = {};
        elem.header.guid = guid;
        if (ACAPI_Element_Get (&elem) != NoError)
            continue;

        switch (GetElemTypeId (elem.header)) {
            case API_DoorID:
            case API_WindowID:   result.Add (guid, elem.window.owner);   break;
            case API_SkylightID: result.Add (guid, elem.skylight.owner); break;
            default: break;
        }
    }
    return result;
}

// JSON has no separate integer type, so clients routinely send 2.0 where an index is
// expected. GS::ObjectState::Get with an integer target rejects such a value, which used to
// drop the field without any error (#530). This accepts a floating point value as well and
// floors it - std::floor, not truncation, so a negative index (stories below 0) rounds the
// same way as a positive one.
template <typename IntType>
static bool GetIndexValue (const GS::ObjectState& os, const char* fieldName, IntType& target)
{
    if (os.Get (fieldName, target)) {
        return true;
    }
    double doubleValue = 0.0;
    if (os.Get (fieldName, doubleValue)) {
        target = static_cast<IntType> (std::floor (doubleValue));
        return true;
    }
    return false;
}

static GS::Optional<GS::UniString> CheckGenericDetailApplicability (const API_Element& element, const GS::ObjectState& details)
{
    const GS::ObjectState* specific = details.Get ("typeSpecificDetails");
    if (specific == nullptr) return {};
    GS::HashSet<GS::String> allowed;
    switch (GetElemTypeId (element.header)) {
        case API_WallID:
            allowed.Add ("begCoordinate");
            allowed.Add ("endCoordinate");
            allowed.Add ("height");
            allowed.Add ("bottomOffset");
            allowed.Add ("offset");
            allowed.Add ("begThickness");
            allowed.Add ("endThickness");
            break;
        case API_ZoneID:
            allowed.Add ("stampPosition");
            allowed.Add ("stampAngle");
            allowed.Add ("fixedStampAngle");
            allowed.Add ("name");
            allowed.Add ("numberStr");
            allowed.Add ("categoryAttributeId");
            break;
        case API_LineID:
            allowed.Add ("begCoordinate");
            allowed.Add ("endCoordinate");
            allowed.Add ("roomSeparator");
            allowed.Add ("linePenIndex");
            allowed.Add ("lineTypeId");
            break;
        case API_ArcID:
            allowed.Add ("origin");
            allowed.Add ("radius");
            allowed.Add ("angle");
            allowed.Add ("ratio");
            allowed.Add ("begAngle");
            allowed.Add ("endAngle");
            allowed.Add ("reflected");
            allowed.Add ("roomSeparator");
            allowed.Add ("linePenIndex");
            allowed.Add ("lineTypeId");
            break;
        case API_CircleID:
            allowed.Add ("origin");
            allowed.Add ("radius");
            allowed.Add ("angle");
            allowed.Add ("ratio");
            allowed.Add ("begAngle");
            allowed.Add ("endAngle");
            allowed.Add ("reflected");
            allowed.Add ("roomSeparator");
            allowed.Add ("linePenIndex");
            allowed.Add ("lineTypeId");
            break;
        case API_HotspotID:
            allowed.Add ("position");
            allowed.Add ("height");
            allowed.Add ("penIndex");
            break;
        case API_SplineID:
            allowed.Add ("roomSeparator");
            allowed.Add ("linePenIndex");
            allowed.Add ("lineTypeId");
            break;
        case API_PolyLineID:
            allowed.Add ("coordinates");
            allowed.Add ("arcs");
            allowed.Add ("roomSeparator");
            allowed.Add ("linePenIndex");
            allowed.Add ("lineTypeId");
            break;
        case API_HatchID:
            allowed.Add ("coordinates");
            allowed.Add ("arcs");
            allowed.Add ("holes");
            allowed.Add ("contourPenIndex");
            allowed.Add ("fillPenIndex");
            allowed.Add ("fillBackgroundPenIndex");
            allowed.Add ("fillId");
            allowed.Add ("buildingMaterialId");
            allowed.Add ("roomSpecial");
            allowed.Add ("showArea");
            break;
        case API_DrawingID:
            allowed.Add ("pos");
            allowed.Add ("angle");
            allowed.Add ("ratio");
            allowed.Add ("modelOffset");
            allowed.Add ("clipPolygon");
            allowed.Add ("nameType");
            allowed.Add ("customName");
            allowed.Add ("numberingType");
            allowed.Add ("customNumber");
            allowed.Add ("isInNumbering");
            allowed.Add ("titleLibraryPartIndex");
            break;
        default: return GS::UniString ("typeSpecificDetails is unsupported for this native type; use its typed modifier.");
    }
    for (const GS::String& field : specific->GetFieldNames ()) {
        if (!allowed.Contains (field)) return GS::UniString ("Unsupported field for this native type: ") + GS::UniString (field);
    }
    if (GetElemTypeId (element.header) == API_WallID && element.wall.type != APIWtyp_Trapez &&
        (specific->Contains ("begThickness") || specific->Contains ("endThickness")))
        return GS::UniString ("begThickness/endThickness require a trapezoid wall. Use ModifyWalls.thickness for a straight Basic wall.");
    if (GetElemTypeId (element.header) == API_CircleID && (specific->Contains ("begAngle") || specific->Contains ("endAngle")))
        return GS::UniString ("begAngle/endAngle require an Arc, not a Circle.");
    return {};
}

GS::ObjectState SetDetailsOfElementsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<GS::ObjectState> elementsWithDetails;
    parameters.Get ("elementsWithDetails", elementsWithDetails);

    GS::ObjectState response;
    std::vector<UIndex> sourceIndices;
    std::vector<GS::ObjectState> resultsByInput (elementsWithDetails.GetSize ());
    UIndex resultPosition = 0;
    const auto executionResults = [&] (const GS::ObjectState& result) {
        resultsByInput[sourceIndices[resultPosition++]] = result;
    };

    // Pre-scan: identify openings (Door/Window/Skylight) that need host manipulation:
    //   - target > 7  : elevate via host (step up, step back)
    //   - target == 0 : reset via host ResetOrder (direct ResetOrder on opening doesn't work when elevated)
    GS::HashSet<API_Guid> openingsNeedingHost;
    for (const GS::ObjectState& ewd : elementsWithDetails) {
        const GS::ObjectState* eid = ewd.Get ("elementId");
        const GS::ObjectState* det = ewd.Get ("details");
        if (eid == nullptr || det == nullptr) continue;
        short tgt = -1;
        GetIndexValue (*det, "drawIndex", tgt);
        if (tgt > 0 && tgt <= 7) continue;   // 1-7: no host needed
        API_Element hdr = {};
        hdr.header.guid = GetGuidFromObjectState (*eid);
        if (ACAPI_Element_GetHeader (&hdr.header) != NoError) continue;
        if (IsOpeningType (hdr.header))
            openingsNeedingHost.Add (hdr.header.guid);
    }
    const GS::HashTable<API_Guid, API_Guid> openingToHost = BuildOpeningToHostMap (openingsNeedingHost);

    // Process openings first so host manipulation starts from the host's natural position,
    // not a position already shifted by another element in the same batch.
    GS::Array<GS::ObjectState> sortedElements;
    for (UIndex index = 0; index < elementsWithDetails.GetSize (); ++index) {
        const auto& ewd = elementsWithDetails[index];
        const GS::ObjectState* eid = ewd.Get ("elementId");
        if (eid != nullptr && openingsNeedingHost.Contains (GetGuidFromObjectState (*eid))) {
            sortedElements.Push (ewd);
            sourceIndices.push_back (index);
        }
    }
    for (UIndex index = 0; index < elementsWithDetails.GetSize (); ++index) {
        const auto& ewd = elementsWithDetails[index];
        const GS::ObjectState* eid = ewd.Get ("elementId");
        if (eid == nullptr || !openingsNeedingHost.Contains (GetGuidFromObjectState (*eid))) {
            sortedElements.Push (ewd);
            sourceIndices.push_back (index);
        }
    }

    const GSErrCode transactionError = ACAPI_CallUndoableCommand ("SetDetailsOfElementsCommand", [&]() {
        for (const GS::ObjectState& elementWithDetails : sortedElements) {
            const GS::ObjectState* elementId = elementWithDetails.Get ("elementId");
            if (elementId == nullptr) {
                executionResults (CreateFailedExecutionResult (APIERR_BADPARS, "elementId is missing"));
                continue;
            }

            API_Element elem = {};
            elem.header.guid = GetGuidFromObjectState (*elementId);
            GSErrCode err = ACAPI_Element_Get (&elem);

            if (err != NoError) {
                executionResults (CreateFailedExecutionResult (err, "Failed to find the element"));
                continue;
            }

            const GS::ObjectState* details = elementWithDetails.Get ("details");
            if (details == nullptr) {
                executionResults (CreateFailedExecutionResult (APIERR_BADPARS, "details field is missing"));
                continue;
            }

            auto applicabilityError = CheckGenericDetailApplicability (elem, *details);
            if (applicabilityError.HasValue ()) {
                executionResults (CreateFailedExecutionResult (APIERR_BADPARS, applicabilityError.Get ()));
                continue;
            }
            API_Element mask = {};
            ACAPI_ELEMENT_MASK_CLEAR (mask);
            bool hasElementChanges = false;
            if (GetIndexValue (*details, "floorIndex", elem.header.floorInd)) {
                ACAPI_ELEMENT_MASK_SET (mask, API_Elem_Head, floorInd);
                hasElementChanges = true;
            }
            Int32 layerIndex;
            if (GetIndexValue (*details, "layerIndex", layerIndex)) {
                elem.header.layer = ACAPI_CreateAttributeIndex (layerIndex);
                ACAPI_ELEMENT_MASK_SET (mask, API_Elem_Head, layer);
                hasElementChanges = true;
            }
            short drwIndexTarget = -1;
            GetIndexValue (*details, "drawIndex", drwIndexTarget);

            const GS::ObjectState* typeSpecificDetails = details->Get ("typeSpecificDetails");
            if (typeSpecificDetails != nullptr) {
                switch (GetElemTypeId (elem.header)) {
                    case API_WallID: {
                        const GS::ObjectState* begCoordinate = typeSpecificDetails->Get ("begCoordinate");
                        if (begCoordinate != nullptr) {
                            elem.wall.begC = Get2DCoordinateFromObjectState (*begCoordinate);
                            ACAPI_ELEMENT_MASK_SET (mask, API_WallType, begC);
                        }
                        const GS::ObjectState* endCoordinate = typeSpecificDetails->Get ("endCoordinate");
                        if (endCoordinate != nullptr) {
                            elem.wall.endC = Get2DCoordinateFromObjectState (*endCoordinate);
                            ACAPI_ELEMENT_MASK_SET (mask, API_WallType, endC);
                        }
                        if (typeSpecificDetails->Get ("height", elem.wall.height)) {
                            ACAPI_ELEMENT_MASK_SET (mask, API_WallType, height);
                        }
                        if (typeSpecificDetails->Get ("bottomOffset", elem.wall.bottomOffset)) {
                            ACAPI_ELEMENT_MASK_SET (mask, API_WallType, bottomOffset);
                        }
                        if (typeSpecificDetails->Get ("offset", elem.wall.offset)) {
                            ACAPI_ELEMENT_MASK_SET (mask, API_WallType, offset);
                        }

                        switch (elem.wall.type) {
                            case APIWtyp_Trapez: {
                                if (typeSpecificDetails->Get ("begThickness", elem.wall.thickness)) {
                                    ACAPI_ELEMENT_MASK_SET (mask, API_WallType, thickness);
                                }
                                if (typeSpecificDetails->Get ("endThickness", elem.wall.thickness1)) {
                                    ACAPI_ELEMENT_MASK_SET (mask, API_WallType, thickness1);
                                }
                            } break;
                            default:
                            break;
                        }
                    } break;
                    case API_ZoneID: {
                        const GS::ObjectState* stampPosition = typeSpecificDetails->Get ("stampPosition");
                        if (stampPosition != nullptr) {
                            elem.zone.pos = Get2DCoordinateFromObjectState (*stampPosition);
                            ACAPI_ELEMENT_MASK_SET (mask, API_ZoneType, pos);
                        }
                        if (typeSpecificDetails->Get ("stampAngle", elem.zone.stampAngle)) {
                            ACAPI_ELEMENT_MASK_SET (mask, API_ZoneType, stampAngle);
                        }
                        if (typeSpecificDetails->Get ("fixedStampAngle", elem.zone.fixedAngle)) {
                            ACAPI_ELEMENT_MASK_SET (mask, API_ZoneType, fixedAngle);
                        }
                        if (SetUCharProperty (typeSpecificDetails, "name", elem.zone.roomName)) {
                            ACAPI_ELEMENT_MASK_SET (mask, API_ZoneType, roomName);
                        }
                        if (SetUCharProperty (typeSpecificDetails, "numberStr", elem.zone.roomNoStr)) {
                            ACAPI_ELEMENT_MASK_SET (mask, API_ZoneType, roomNoStr);
                        }
                        const GS::ObjectState* categoryAttributeId = typeSpecificDetails->Get ("categoryAttributeId");
                        if (categoryAttributeId != nullptr) {
                            elem.zone.catInd = GetAttributeIndexFromGuid (API_ZoneCatID, GetGuidFromObjectState (*categoryAttributeId));
                            ACAPI_ELEMENT_MASK_SET (mask, API_ZoneType, catInd);
                        }
                    } break;
                    case API_LineID: {
                        const GS::ObjectState* begCoordinate = typeSpecificDetails->Get ("begCoordinate");
                        if (begCoordinate != nullptr) {
                            elem.line.begC = Get2DCoordinateFromObjectState (*begCoordinate);
                            ACAPI_ELEMENT_MASK_SET (mask, API_LineType, begC);
                        }
                        const GS::ObjectState* endCoordinate = typeSpecificDetails->Get ("endCoordinate");
                        if (endCoordinate != nullptr) {
                            elem.line.endC = Get2DCoordinateFromObjectState (*endCoordinate);
                            ACAPI_ELEMENT_MASK_SET (mask, API_LineType, endC);
                        }
                        bool penChanged = false, ltypeChanged = false, roomSepChanged = false;
                        SetLineFamilySettingsFields (*typeSpecificDetails, elem.line.linePen, elem.line.ltypeInd, elem.line.roomSeparator, penChanged, ltypeChanged, roomSepChanged);
                        if (penChanged) ACAPI_ELEMENT_MASK_SET (mask, API_LineType, linePen);
                        if (ltypeChanged) ACAPI_ELEMENT_MASK_SET (mask, API_LineType, ltypeInd);
                        if (roomSepChanged) ACAPI_ELEMENT_MASK_SET (mask, API_LineType, roomSeparator);
                    } break;
                    case API_ArcID:
                    case API_CircleID: {
                        const GS::ObjectState* origin = typeSpecificDetails->Get ("origin");
                        if (origin != nullptr) {
                            elem.arc.origC = Get2DCoordinateFromObjectState (*origin);
                            ACAPI_ELEMENT_MASK_SET (mask, API_ArcType, origC);
                        }
                        if (typeSpecificDetails->Get ("radius", elem.arc.r)) {
                            ACAPI_ELEMENT_MASK_SET (mask, API_ArcType, r);
                        }
                        if (typeSpecificDetails->Get ("angle", elem.arc.angle)) {
                            ACAPI_ELEMENT_MASK_SET (mask, API_ArcType, angle);
                        }
                        if (typeSpecificDetails->Get ("ratio", elem.arc.ratio)) {
                            ACAPI_ELEMENT_MASK_SET (mask, API_ArcType, ratio);
                        }
                        if (GetElemTypeId (elem.header) == API_ArcID) {
                            if (typeSpecificDetails->Get ("begAngle", elem.arc.begAng)) {
                                ACAPI_ELEMENT_MASK_SET (mask, API_ArcType, begAng);
                            }
                            if (typeSpecificDetails->Get ("endAngle", elem.arc.endAng)) {
                                ACAPI_ELEMENT_MASK_SET (mask, API_ArcType, endAng);
                            }
                        }
                        if (typeSpecificDetails->Get ("reflected", elem.arc.reflected)) {
                            ACAPI_ELEMENT_MASK_SET (mask, API_ArcType, reflected);
                        }
                        bool penChanged = false, ltypeChanged = false, roomSepChanged = false;
                        SetLineFamilySettingsFields (*typeSpecificDetails, elem.arc.linePen, elem.arc.ltypeInd, elem.arc.roomSeparator, penChanged, ltypeChanged, roomSepChanged);
                        if (penChanged) ACAPI_ELEMENT_MASK_SET (mask, API_ArcType, linePen);
                        if (ltypeChanged) ACAPI_ELEMENT_MASK_SET (mask, API_ArcType, ltypeInd);
                        if (roomSepChanged) ACAPI_ELEMENT_MASK_SET (mask, API_ArcType, roomSeparator);
                    } break;
                    case API_HotspotID: {
                        const GS::ObjectState* position = typeSpecificDetails->Get ("position");
                        if (position != nullptr) {
                            elem.hotspot.pos = Get2DCoordinateFromObjectState (*position);
                            ACAPI_ELEMENT_MASK_SET (mask, API_HotspotType, pos);
                        }
                        if (typeSpecificDetails->Get ("height", elem.hotspot.height)) {
                            ACAPI_ELEMENT_MASK_SET (mask, API_HotspotType, height);
                        }
                        short hotspotPen = 0;
                        if (typeSpecificDetails->Get ("penIndex", hotspotPen)) {
                            elem.hotspot.pen = hotspotPen;
                            ACAPI_ELEMENT_MASK_SET (mask, API_HotspotType, pen);
                        }
                    } break;
                    case API_SplineID: {
                        // Geometry (coordinates/closed) cannot be modified via ACAPI_Element_Change
                        // for Spline (documented ACAPI limitation) - only these settings fields.
                        bool penChanged = false, ltypeChanged = false, roomSepChanged = false;
                        SetLineFamilySettingsFields (*typeSpecificDetails, elem.spline.linePen, elem.spline.ltypeInd, elem.spline.roomSeparator, penChanged, ltypeChanged, roomSepChanged);
                        if (penChanged) ACAPI_ELEMENT_MASK_SET (mask, API_SplineType, linePen);
                        if (ltypeChanged) ACAPI_ELEMENT_MASK_SET (mask, API_SplineType, ltypeInd);
                        if (roomSepChanged) ACAPI_ELEMENT_MASK_SET (mask, API_SplineType, roomSeparator);
                    } break;
                    case API_DrawingID: {
                        GS::Array<GS::ObjectState> clipCoords;
                        if (typeSpecificDetails->Get ("clipPolygon", clipCoords) && clipCoords.GetSize () >= 3) {
                            Int32 nUnique = (Int32) clipCoords.GetSize ();
                            if (nUnique > 1) {
                                API_Coord first = Get2DCoordinateFromObjectState (clipCoords.GetFirst ());
                                API_Coord last  = Get2DCoordinateFromObjectState (clipCoords.GetLast ());
                                if (first.x == last.x && first.y == last.y)
                                    --nUnique;
                            }
                            elem.drawing.isCutWithFrame    = true;
                            elem.drawing.poly.nSubPolys    = 1;
                            elem.drawing.poly.nCoords      = nUnique + 1; // +1 for closing vertex
                            elem.drawing.poly.nArcs        = 0;
                            ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, isCutWithFrame);
                            ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, poly);
                        }
                        if (typeSpecificDetails->Get ("ratio", elem.drawing.ratio)) {
                            ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, ratio);
                        }
                        if (typeSpecificDetails->Get ("angle", elem.drawing.angle)) {
                            ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, angle);
                        }
                        const GS::ObjectState* posState = typeSpecificDetails->Get ("pos");
                        if (posState != nullptr) {
                            elem.drawing.pos = Get2DCoordinateFromObjectState (*posState);
                            ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, pos);
                        }
                        const GS::ObjectState* modelOffsetState = typeSpecificDetails->Get ("modelOffset");
                        if (modelOffsetState != nullptr) {
                            elem.drawing.modelOffset = Get2DCoordinateFromObjectState (*modelOffsetState);
                            ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, modelOffset);
                        }
                        // drawingGuid (the Drawing's source link) is intentionally not settable here:
                        // confirmed live that ACAPI_Element_Change silently discards any change to it
                        // after creation - it can only be set once, at CreateDrawings time.
                        GS::UniString nameTypeStr;
                        if (typeSpecificDetails->Get ("nameType", nameTypeStr)) {
                            elem.drawing.nameType = DrawingNameTypeFromString (nameTypeStr);
                            ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, nameType);
                        }
                        if (SetCharProperty (typeSpecificDetails, "customName", elem.drawing.name)) {
                            ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, name);
                        }
                        GS::UniString numberingTypeStr;
                        if (typeSpecificDetails->Get ("numberingType", numberingTypeStr)) {
                            elem.drawing.numberingType = DrawingNumberingTypeFromString (numberingTypeStr);
                            ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, numberingType);
                        }
                        if (SetCharProperty (typeSpecificDetails, "customNumber", elem.drawing.customNumber)) {
                            ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, customNumber);
                        }
                        if (typeSpecificDetails->Get ("isInNumbering", elem.drawing.isInNumbering)) {
                            ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, isInNumbering);
                        }
                        if (typeSpecificDetails->Get ("titleLibraryPartIndex", elem.drawing.title.libInd)) {
                            ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, title.libInd);
                        }
                    } break;
                    default:
                    break;
                }
                hasElementChanges = true;
            }

            API_ElementMemo clipMemo = {};
            UInt64 memoMask = 0;
            bool hasMemoChanges = false;

            if (typeSpecificDetails != nullptr && GetElemTypeId (elem.header) == API_DrawingID) {
                GS::Array<GS::ObjectState> clipCoords;
                if (typeSpecificDetails->Get ("clipPolygon", clipCoords) && clipCoords.GetSize () >= 3) {
                    // Drop explicit closing vertex if caller already duplicated first point at the end
                    if (clipCoords.GetSize () > 1) {
                        API_Coord first = Get2DCoordinateFromObjectState (clipCoords.GetFirst ());
                        API_Coord last  = Get2DCoordinateFromObjectState (clipCoords.GetLast ());
                        if (first.x == last.x && first.y == last.y)
                            clipCoords.Pop ();
                    }
                    const Int32 nUnique = clipCoords.GetSize ();
                    // AC polygon convention: coords[1..nUnique] = vertices, coords[nUnique+1] = closing duplicate of coords[1]
                    // Handle size = nUnique+2 (index 0 unused, 1..nUnique vertices, nUnique+1 closing)
                    clipMemo.coords = reinterpret_cast<API_Coord**> (BMAllocateHandle ((nUnique + 2) * sizeof (API_Coord), ALLOCATE_CLEAR, 0));
                    clipMemo.pends  = reinterpret_cast<Int32**>     (BMAllocateHandle (2 * sizeof (Int32), ALLOCATE_CLEAR, 0));
                    if (clipMemo.coords != nullptr && clipMemo.pends != nullptr) {
                        for (Int32 i = 0; i < nUnique; ++i)
                            (*clipMemo.coords)[i + 1] = Get2DCoordinateFromObjectState (clipCoords[i]);
                        (*clipMemo.coords)[nUnique + 1] = (*clipMemo.coords)[1]; // closing vertex
                        (*clipMemo.pends)[1] = nUnique + 1;
                        memoMask = APIMemoMask_Polygon;
                        hasMemoChanges = true;
                    }
                }
            } else if (typeSpecificDetails != nullptr && GetElemTypeId (elem.header) == API_PolyLineID) {
                // vertexIDs[0] must hold the MAX vertex ID used across the shape (not be left at
                // 0/uninitialized) - confirmed via a real ACAPI_Element_Create usage example
                // (Graphisoft community forum). This was the missing piece in every earlier
                // from-scratch attempt, which is why they all failed with APIERR_BADPOLY even for
                // an identity write - nothing to do with mask bits, GetMemo-seeding, or the
                // coords[0]=(-1,0) sentinel (which was already correct).
                GS::Array<GS::ObjectState> coordinates;
                if (typeSpecificDetails->Get ("coordinates", coordinates) && coordinates.GetSize () >= 2) {
                    GS::Array<GS::ObjectState> arcs;
                    typeSpecificDetails->Get ("arcs", arcs);
                    const GS::Array<API_PolyArc> polyArcs = GetPolyArcs (arcs, 1);

                    const Int32 nCoords = (Int32) coordinates.GetSize ();
                    clipMemo.coords    = reinterpret_cast<API_Coord**>   (BMAllocateHandle ((nCoords + 1) * sizeof (API_Coord), ALLOCATE_CLEAR, 0));
                    clipMemo.pends     = reinterpret_cast<Int32**>       (BMAllocateHandle (2 * sizeof (Int32), ALLOCATE_CLEAR, 0));
                    clipMemo.parcs     = reinterpret_cast<API_PolyArc**> (BMAllocateHandle (polyArcs.GetSize () * sizeof (API_PolyArc), ALLOCATE_CLEAR, 0));
                    clipMemo.vertexIDs = reinterpret_cast<UInt32**>      (BMAllocateHandle ((nCoords + 1) * sizeof (UInt32), ALLOCATE_CLEAR, 0));
                    if (clipMemo.coords != nullptr && clipMemo.pends != nullptr && clipMemo.vertexIDs != nullptr) {
                        (*clipMemo.coords)[0] = { -1.0, 0.0 }; // open-polyline dummy coord
                        (*clipMemo.vertexIDs)[0] = (UInt32) nCoords; // max vertex ID used
                        for (Int32 i = 0; i < nCoords; ++i) {
                            (*clipMemo.coords)[i + 1] = Get2DCoordinateFromObjectState (coordinates[i]);
                            (*clipMemo.vertexIDs)[i + 1] = (UInt32) (i + 1);
                        }
                        (*clipMemo.pends)[0] = 0;
                        (*clipMemo.pends)[1] = nCoords;
                        if (clipMemo.parcs != nullptr) {
                            Int32 iArc = 0;
                            for (const API_PolyArc& a : polyArcs)
                                (*clipMemo.parcs)[iArc++] = a;
                        }
                        elem.polyLine.poly.nCoords   = nCoords;
                        elem.polyLine.poly.nSubPolys = 1;
                        elem.polyLine.poly.nArcs     = (Int32) polyArcs.GetSize ();
                        ACAPI_ELEMENT_MASK_SET (mask, API_PolyLineType, poly);
                        memoMask = APIMemoMask_Polygon;
                        hasMemoChanges = true;
                        hasElementChanges = true;
                    }
                }
                {
                    bool penChanged = false, ltypeChanged = false, roomSepChanged = false;
                    if (SetLineFamilySettingsFields (*typeSpecificDetails, elem.polyLine.linePen, elem.polyLine.ltypeInd, elem.polyLine.roomSeparator, penChanged, ltypeChanged, roomSepChanged)) {
                        if (penChanged) ACAPI_ELEMENT_MASK_SET (mask, API_PolyLineType, linePen);
                        if (ltypeChanged) ACAPI_ELEMENT_MASK_SET (mask, API_PolyLineType, ltypeInd);
                        if (roomSepChanged) ACAPI_ELEMENT_MASK_SET (mask, API_PolyLineType, roomSeparator);
                        hasElementChanges = true;
                    }
                }
            } else if (typeSpecificDetails != nullptr && GetElemTypeId (elem.header) == API_HatchID) {
                // Hatch is a CLOSED polygon (unlike PolyLine): coords[0] dummy is (0,0) not
                // (-1,0), and every contour (the main outline, plus each hole) needs its own
                // explicit closing vertex duplicating that contour's first point, whose vertexID
                // also duplicates that first point's ID. Vertex IDs restart at 1 for EACH contour
                // (not continued sequentially across contours) - confirmed live: global numbering
                // across contours reproducibly failed with APIERR_BADPOLY for a hatch with holes,
                // per-contour numbering works.
                GS::Array<GS::ObjectState> coordinates;
                if (typeSpecificDetails->Get ("coordinates", coordinates) && coordinates.GetSize () >= 3) {
                    GS::Array<GS::ObjectState> arcs;
                    typeSpecificDetails->Get ("arcs", arcs);
                    GS::Array<GS::ObjectState> holes;
                    typeSpecificDetails->Get ("holes", holes);

                    // Each contour: N unique coords -> N+1 stored coords (with closing duplicate).
                    Int32 totalUnique = (Int32) coordinates.GetSize ();
                    Int32 totalCoords = totalUnique + 1;
                    Int32 totalArcs = (Int32) arcs.GetSize ();
                    GS::Array<GS::Array<GS::ObjectState>> holeCoordsList;
                    GS::Array<GS::Array<GS::ObjectState>> holeArcsList;
                    for (const GS::ObjectState& hole : holes) {
                        GS::Array<GS::ObjectState> holeCoords, holeArcs;
                        if (GetHoleGeometry (hole, holeCoords, holeArcs) && holeCoords.GetSize () >= 3) {
                            totalUnique += (Int32) holeCoords.GetSize ();
                            totalCoords += (Int32) holeCoords.GetSize () + 1;
                            totalArcs += (Int32) holeArcs.GetSize ();
                            holeCoordsList.Push (holeCoords);
                            holeArcsList.Push (holeArcs);
                        }
                    }
                    const Int32 nSubPolys = 1 + (Int32) holeCoordsList.GetSize ();

                    clipMemo.coords    = reinterpret_cast<API_Coord**>   (BMAllocateHandle ((totalCoords + 1) * sizeof (API_Coord), ALLOCATE_CLEAR, 0));
                    clipMemo.pends     = reinterpret_cast<Int32**>       (BMAllocateHandle ((nSubPolys + 1) * sizeof (Int32), ALLOCATE_CLEAR, 0));
                    clipMemo.parcs     = reinterpret_cast<API_PolyArc**> (BMAllocateHandle (totalArcs * sizeof (API_PolyArc), ALLOCATE_CLEAR, 0));
                    clipMemo.vertexIDs = reinterpret_cast<UInt32**>      (BMAllocateHandle ((totalCoords + 1) * sizeof (UInt32), ALLOCATE_CLEAR, 0));
                    if (clipMemo.coords != nullptr && clipMemo.pends != nullptr && clipMemo.vertexIDs != nullptr) {
                        Int32 maxContourUnique = (Int32) coordinates.GetSize ();
                        for (const auto& hc : holeCoordsList) {
                            maxContourUnique = GS::Max (maxContourUnique, (Int32) hc.GetSize ());
                        }
                        (*clipMemo.coords)[0] = { 0.0, 0.0 };
                        (*clipMemo.vertexIDs)[0] = (UInt32) maxContourUnique;
                        (*clipMemo.pends)[0] = 0;

                        Int32 iCoord = 0;
                        Int32 iArc = 0;
                        Int32 subPolyIndex = 1;

                        auto writeContour = [&] (const GS::Array<GS::ObjectState>& contourCoords, const GS::Array<GS::ObjectState>& contourArcs) {
                            const Int32 firstIndex = iCoord + 1;
                            const Int32 nUniqueLocal = (Int32) contourCoords.GetSize ();
                            for (Int32 i = 0; i < nUniqueLocal; ++i) {
                                ++iCoord;
                                (*clipMemo.coords)[iCoord] = Get2DCoordinateFromObjectState (contourCoords[i]);
                                (*clipMemo.vertexIDs)[iCoord] = (UInt32) (i + 1); // per-contour, restarts at 1
                            }
                            ++iCoord;
                            (*clipMemo.coords)[iCoord] = (*clipMemo.coords)[firstIndex]; // closing duplicate
                            (*clipMemo.vertexIDs)[iCoord] = (*clipMemo.vertexIDs)[firstIndex];
                            (*clipMemo.pends)[subPolyIndex++] = iCoord;

                            if (clipMemo.parcs != nullptr) {
                                const GS::Array<API_PolyArc> localArcs = GetPolyArcs (contourArcs, firstIndex);
                                for (const API_PolyArc& a : localArcs)
                                    (*clipMemo.parcs)[iArc++] = a;
                            }
                        };

                        writeContour (coordinates, arcs);
                        for (Int32 i = 0; i < (Int32) holeCoordsList.GetSize (); ++i) {
                            writeContour (holeCoordsList[i], holeArcsList[i]);
                        }

                        elem.hatch.poly.nCoords   = totalCoords;
                        elem.hatch.poly.nSubPolys = nSubPolys;
                        elem.hatch.poly.nArcs     = totalArcs;
                        ACAPI_ELEMENT_MASK_SET (mask, API_HatchType, poly);
                        memoMask = APIMemoMask_Polygon;
                        hasMemoChanges = true;
                        hasElementChanges = true;
                    }
                }

                short contourPenIndex = 0;
                if (typeSpecificDetails->Get ("contourPenIndex", contourPenIndex)) {
                    elem.hatch.contPen.penIndex = contourPenIndex;
                    elem.hatch.contPen.colorOverridePenIndex = 0;
                    ACAPI_ELEMENT_MASK_SET (mask, API_HatchType, contPen);
                    hasElementChanges = true;
                }
                short fillPenIndex = 0;
                if (typeSpecificDetails->Get ("fillPenIndex", fillPenIndex)) {
                    elem.hatch.fillPen.penIndex = fillPenIndex;
                    elem.hatch.fillPen.colorOverridePenIndex = 0;
                    ACAPI_ELEMENT_MASK_SET (mask, API_HatchType, fillPen);
                    hasElementChanges = true;
                }
                if (typeSpecificDetails->Get ("fillBackgroundPenIndex", elem.hatch.fillBGPen)) {
                    ACAPI_ELEMENT_MASK_SET (mask, API_HatchType, fillBGPen);
                    hasElementChanges = true;
                }
                const GS::ObjectState* fillId = typeSpecificDetails->Get ("fillId");
                if (fillId != nullptr) {
                    elem.hatch.fillInd = GetAttributeIndexFromGuid (API_FilltypeID, GetGuidFromObjectState (*fillId));
                    ACAPI_ELEMENT_MASK_SET (mask, API_HatchType, fillInd);
                    hasElementChanges = true;
                }
                const GS::ObjectState* buildingMaterialId = typeSpecificDetails->Get ("buildingMaterialId");
                if (buildingMaterialId != nullptr) {
                    elem.hatch.buildingMaterial = GetAttributeIndexFromGuid (API_BuildingMaterialID, GetGuidFromObjectState (*buildingMaterialId));
                    ACAPI_ELEMENT_MASK_SET (mask, API_HatchType, buildingMaterial);
                    hasElementChanges = true;
                }
                Int32 roomSpecial = 0;
                if (typeSpecificDetails->Get ("roomSpecial", roomSpecial)) {
                    elem.hatch.roomSpecial = (char) roomSpecial;
                    ACAPI_ELEMENT_MASK_SET (mask, API_HatchType, roomSpecial);
                    hasElementChanges = true;
                }
                if (typeSpecificDetails->Get ("showArea", elem.hatch.showArea)) {
                    ACAPI_ELEMENT_MASK_SET (mask, API_HatchType, showArea);
                    hasElementChanges = true;
                }
            }

            constexpr bool withDel = true;
            if (hasElementChanges || hasMemoChanges) {
                err = ACAPI_Element_Change (&elem, &mask,
                                            hasMemoChanges ? &clipMemo : nullptr,
                                            memoMask, withDel);
                ACAPI_DisposeElemMemoHdls (&clipMemo);
                if (err != NoError) {
                    executionResults (CreateFailedExecutionResult (err, DescribeElementChangeFailure (err, "Failed to change element.")));
                    continue;
                }
            }

            if (drwIndexTarget >= 0) {
                GS::Array<API_Guid> guids = { elem.header.guid };
                // Every ACAPI_Grouping_Tool call below previously had its GSErrCode discarded
                // and this element was reported successful regardless of what actually happened
                // in Archicad - drwIndex changes could silently fail (missing host, a rejected
                // step, or a tool call that reported success without moving the element) and the
                // caller had no way to know. Every branch now checks its own SDK call results and
                // ends with a readback against the element's (or host's) actual drwIndex, so a
                // silent no-op is reported as a failure instead of a false success.
                bool drwIndexOk = true;
                GS::UniString drwIndexFailure;

                if (IsOpeningType (elem.header) && drwIndexTarget > 7) {
                    // Openings (Door/Window/Skylight) can only reach levels above 7 by
                    // temporarily elevating their host wall/roof step by step, then
                    // bringing it back the same way (BringToFront is unreliable on walls).
                    const API_Guid* hostGuidPtr = openingToHost.GetPtr (elem.header.guid);
                    if (hostGuidPtr == nullptr) {
                        drwIndexOk = false;
                        drwIndexFailure = "Opening has no locatable host element; draw index was not changed.";
                    } else {
                        API_Element hostElem = {};
                        hostElem.header.guid = *hostGuidPtr;
                        if (ACAPI_Element_Get (&hostElem) != NoError) {
                            drwIndexOk = false;
                            drwIndexFailure = "Host element could not be read; draw index was not changed.";
                        } else {
                            const Int32 hostOriginal = (Int32) hostElem.header.drwIndex;
                            const Int32 target       = GS::Max (8, GS::Min ((Int32) drwIndexTarget, 14));
                            const Int32 steps        = target - hostOriginal;
                            GS::Array<API_Guid> hostGuids = { *hostGuidPtr };
                            if (steps > 0) {
                                // Step host up — opening follows
                                for (Int32 i = 0; i < steps && drwIndexOk; ++i) {
                                    if (ACAPI_Grouping_Tool (hostGuids, APITool_BringForward, nullptr) != NoError) {
                                        drwIndexOk = false;
                                        drwIndexFailure = "Host could not be brought forward far enough; opening draw index change is incomplete.";
                                    }
                                }
                                // Step host back down — opening stays elevated
                                for (Int32 i = 0; i < steps && drwIndexOk; ++i) {
                                    if (ACAPI_Grouping_Tool (hostGuids, APITool_SendBackward, nullptr) != NoError) {
                                        drwIndexOk = false;
                                        drwIndexFailure = "Host could not be restored after elevating the opening; draw index change is incomplete.";
                                    }
                                }
                                if (drwIndexOk) {
                                    API_Elem_Head hostRecheck;
                                    if (!LoadElementHeaderByGuid (*hostGuidPtr, hostRecheck) || (Int32) hostRecheck.drwIndex != hostOriginal) {
                                        drwIndexOk = false;
                                        drwIndexFailure = "Host draw index did not return to its original position; opening elevation is unconfirmed.";
                                    }
                                }
                            }
                        }
                    }
                } else if (drwIndexTarget == 0 && IsOpeningType (elem.header)) {
                    // Reset elevated opening via its host (direct ResetOrder on opening doesn't work)
                    const API_Guid* hostGuidPtr = openingToHost.GetPtr (elem.header.guid);
                    if (hostGuidPtr == nullptr) {
                        drwIndexOk = false;
                        drwIndexFailure = "Opening has no locatable host element; draw index was not reset.";
                    } else {
                        GS::Array<API_Guid> hostGuids = { *hostGuidPtr };
                        if (ACAPI_Grouping_Tool (hostGuids, APITool_ResetOrder, nullptr) != NoError) {
                            drwIndexOk = false;
                            drwIndexFailure = "Host draw order could not be reset; opening draw index was not reset.";
                        }
                    }
                } else if (drwIndexTarget == 0 && IsResetOrderReliable (elem.header)) {
                    if (ACAPI_Grouping_Tool (guids, APITool_ResetOrder, nullptr) != NoError) {
                        drwIndexOk = false;
                        drwIndexFailure = "Draw order could not be reset to its class default.";
                    } else {
                        API_Elem_Head recheck;
                        if (!LoadElementHeaderByGuid (elem.header.guid, recheck) || (Int32) recheck.drwIndex != GetDefaultDrwIndex (elem.header)) {
                            drwIndexOk = false;
                            drwIndexFailure = "Draw order reset call reported success but the element's draw index did not reach its class default.";
                        }
                    }
                } else if (drwIndexTarget != 0 || !IsResetOrderReliable (elem.header)) {
                    const short resolvedTarget = (drwIndexTarget == 0) ? (short) GetDefaultDrwIndex (elem.header) : drwIndexTarget;
                    ApplyDrawIndexStrategy (guids, elem.header, (Int32) resolvedTarget, (Int32) elem.header.drwIndex);
                    const Int32 clampedTarget = GS::Max (1, GS::Min ((Int32) resolvedTarget, 14));
                    API_Elem_Head recheck;
                    if (!LoadElementHeaderByGuid (elem.header.guid, recheck) || (Int32) recheck.drwIndex != clampedTarget) {
                        drwIndexOk = false;
                        drwIndexFailure = "Draw index could not be moved to the requested level.";
                    }
                }

                if (!drwIndexOk) {
                    executionResults (CreateFailedExecutionResult (APIERR_GENERAL, hasElementChanges || hasMemoChanges
                        ? drwIndexFailure + " Other requested detail changes on this element were applied."
                        : drwIndexFailure));
                    continue;
                }
            }

            executionResults (CreateSuccessfulExecutionResult ());
        }

        return NoError;
    });

    if (transactionError != NoError) return CreateErrorResponse (transactionError, "SetDetails transaction failed; committed outcome is not confirmed.");
    const auto& addResult = response.AddList<GS::ObjectState> ("executionResults");
    for (const auto& result : resultsByInput) addResult (result);
    return response;
}

GetSelectedElementsCommand::GetSelectedElementsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String GetSelectedElementsCommand::GetName () const
{
    return "GetSelectedElements";
}

GS::Optional<GS::UniString> GetSelectedElementsCommand::GetRawResponseSchema () const
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

GS::ObjectState GetSelectedElementsCommand::Execute (const GS::ObjectState& /*parameters*/, GS::ProcessControl& /*processControl*/) const
{
    API_SelectionInfo selectionInfo;
    GS::Array<API_Neig> selectedNeigs;
    GSErrCode err = ACAPI_Selection_Get (&selectionInfo, &selectedNeigs, false);
    if (err != NoError && err != APIERR_NOSEL) {
        return CreateErrorResponse (err, "Failed to retrieve selected elements.");
    }

    GS::ObjectState response;
    const auto& elementsList = response.AddList<GS::ObjectState> ("elements");
    if (err == APIERR_NOSEL || selectionInfo.typeID == API_SelEmpty) {
        return response;
    }

    GS::Array<API_Guid> selElemGuids;
    for (API_Neig& selectedNeig : selectedNeigs) {
        elementsList (CreateElementIdObjectState (GetParentElemOfSectElem (selectedNeig.guid)));
    }

    return response;
}

ChangeSelectionOfElementsCommand::ChangeSelectionOfElementsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String ChangeSelectionOfElementsCommand::GetName () const
{
    return "ChangeSelectionOfElements";
}

GS::Optional<GS::UniString> ChangeSelectionOfElementsCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "addElementsToSelection": {
                "$ref": "#/Elements"
            },
            "removeElementsFromSelection": {
                "$ref": "#/Elements"
            }
        },
        "additionalProperties": false,
        "required": [
        ]
    })";
}

GS::Optional<GS::UniString> ChangeSelectionOfElementsCommand::GetRawResponseSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "executionResultsOfAddToSelection": {
                "$ref": "#/ExecutionResults"
            },
            "executionResultsOfRemoveFromSelection": {
                "$ref": "#/ExecutionResults"
            }
        },
        "additionalProperties": false,
        "required": [
            "executionResultsOfAddToSelection",
            "executionResultsOfRemoveFromSelection"
        ]
    })";
}

GS::ObjectState ChangeSelectionOfElementsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<GS::ObjectState> addElementsToSelection;
    parameters.Get ("addElementsToSelection", addElementsToSelection);
    GS::Array<GS::ObjectState> removeElementsFromSelection;
    parameters.Get ("removeElementsFromSelection", removeElementsFromSelection);

    GS::ObjectState response;
    const auto& executionResultsOfAddToSelection = response.AddList<GS::ObjectState> ("executionResultsOfAddToSelection");
    const auto& executionResultsOfRemoveFromSelection = response.AddList<GS::ObjectState> ("executionResultsOfRemoveFromSelection");

    for (const GS::ObjectState& element : addElementsToSelection) {
        const GS::ObjectState* elementId = element.Get ("elementId");
        if (elementId == nullptr) {
            executionResultsOfAddToSelection (CreateFailedExecutionResult (APIERR_BADPARS, "elementId is missing"));
            continue;
        }

        const GSErrCode err = ACAPI_Selection_Select ({ API_Neig (GetGuidFromObjectState (*elementId)) }, true);
        if (err != NoError) {
            executionResultsOfAddToSelection (CreateFailedExecutionResult (err, "Failed to add to selection"));
        } else {
            executionResultsOfAddToSelection (CreateSuccessfulExecutionResult ());
        }
    }

    for (const GS::ObjectState& element : removeElementsFromSelection) {
        const GS::ObjectState* elementId = element.Get ("elementId");
        if (elementId == nullptr) {
            executionResultsOfRemoveFromSelection (CreateFailedExecutionResult (APIERR_BADPARS, "elementId is missing"));
            continue;
        }

        const GSErrCode err = ACAPI_Selection_Select ({ API_Neig (GetGuidFromObjectState (*elementId)) }, false);
        if (err != NoError) {
            executionResultsOfRemoveFromSelection (CreateFailedExecutionResult (err, "Failed to remove from selection"));
        } else {
            executionResultsOfRemoveFromSelection (CreateSuccessfulExecutionResult ());
        }
    }

    return response;
}

GetSubelementsOfHierarchicalElementsCommand::GetSubelementsOfHierarchicalElementsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String GetSubelementsOfHierarchicalElementsCommand::GetName () const
{
    return "GetSubelementsOfHierarchicalElements";
}

GS::Optional<GS::UniString> GetSubelementsOfHierarchicalElementsCommand::GetInputParametersSchema () const
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

GS::Optional<GS::UniString> GetSubelementsOfHierarchicalElementsCommand::GetRawResponseSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "subelements": {
                "type": "array",
                "items": {
                    "type": "object",
                    "description": "Subelements grouped by type.",
                    "properties": {
                        "cWallSegments": {
                            "$ref": "#/Elements"
                        },
                        "cWallFrames": {
                            "$ref": "#/Elements"
                        },
                        "cWallPanels": {
                            "$ref": "#/Elements"
                        },
                        "cWallJunctions": {
                            "$ref": "#/Elements"
                        },
                        "cWallAccessories": {
                            "$ref": "#/Elements"
                        },
                        "stairRisers": {
                            "$ref": "#/Elements"
                        },
                        "stairTreads": {
                            "$ref": "#/Elements"
                        },
                        "stairStructures": {
                            "$ref": "#/Elements"
                        },
                        "railingNodes": {
                            "$ref": "#/Elements"
                        },
                        "railingSegments": {
                            "$ref": "#/Elements"
                        },
                        "railingPosts": {
                            "$ref": "#/Elements"
                        },
                        "railingRailEnds": {
                            "$ref": "#/Elements"
                        },
                        "railingRailConnections": {
                            "$ref": "#/Elements"
                        },
                        "railingHandrailEnds": {
                            "$ref": "#/Elements"
                        },
                        "railingHandrailConnections": {
                            "$ref": "#/Elements"
                        },
                        "railingToprailEnds": {
                            "$ref": "#/Elements"
                        },
                        "railingToprailConnections": {
                            "$ref": "#/Elements"
                        },
                        "railingRails": {
                            "$ref": "#/Elements"
                        },
                        "railingToprails": {
                            "$ref": "#/Elements"
                        },
                        "railingHandrails": {
                            "$ref": "#/Elements"
                        },
                        "railingPatterns": {
                            "$ref": "#/Elements"
                        },
                        "railingInnerPosts": {
                            "$ref": "#/Elements"
                        },
                        "railingPanels": {
                            "$ref": "#/Elements"
                        },
                        "railingBalusterSets": {
                            "$ref": "#/Elements"
                        },
                        "railingBalusters": {
                            "$ref": "#/Elements"
                        },
                        "beamSegments": {
                            "$ref": "#/Elements"
                        },
                        "columnSegments": {
                            "$ref": "#/Elements"
                        }
                    },
                    "additionalProperties": false,
                    "required": []
                }
            }
        },
        "additionalProperties": false,
        "required": [
            "subelements"
        ]
    })";
}

template<typename APIElemType>
static void AddSubelementsToObjectState (GS::ObjectState& subelements, APIElemType* subelemArray, const char* subelementType)
{
    const GSSize nSubelementsWithThisType = BMGetPtrSize (reinterpret_cast<GSPtr>(subelemArray)) / sizeof (APIElemType);
    if (nSubelementsWithThisType == 0) {
        return;
    }

    const auto& subelementsWithThisType = subelements.AddList<GS::ObjectState> (subelementType);
    for (GSIndex i = 0; i < nSubelementsWithThisType; ++i) {
        subelementsWithThisType (CreateElementIdObjectState (subelemArray[i].head.guid));
    }
}

GS::ObjectState GetSubelementsOfHierarchicalElementsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<GS::ObjectState> elements;
    parameters.Get ("elements", elements);

    GS::ObjectState response;
    const auto& subelementsOfHierarchicalElements = response.AddList<GS::ObjectState> ("subelements");

    for (const GS::ObjectState& hierarchicalElement : elements) {
        const GS::ObjectState* elementId = hierarchicalElement.Get ("elementId");
        if (elementId == nullptr) {
            subelementsOfHierarchicalElements (CreateErrorResponse (APIERR_BADPARS, "elementId of hierarchicalElement is missing"));
            continue;
        }

        const API_Guid elemGuid = GetGuidFromObjectState (*elementId);

        API_ElementMemo memo = {};
        const GS::OnExit guard ([&memo] () { ACAPI_DisposeElemMemoHdls (&memo); });
        GSErrCode err = ACAPI_Element_GetMemo (elemGuid, &memo, APIMemoMask_All);

        if (err != NoError) {
            subelementsOfHierarchicalElements (CreateErrorResponse (err, "Failed to get the subelements"));
            continue;
        }

        GS::ObjectState subelements;

#define AddSubelements(memoArrayFieldName) AddSubelementsToObjectState(subelements, memo.memoArrayFieldName, #memoArrayFieldName)

        AddSubelements (cWallSegments);
        AddSubelements (cWallFrames);
        AddSubelements (cWallPanels);
        AddSubelements (cWallJunctions);
        AddSubelements (cWallAccessories);

        AddSubelements (stairRisers);
        AddSubelements (stairTreads);
        AddSubelements (stairStructures);

        AddSubelements (railingNodes);
        AddSubelements (railingSegments);
        AddSubelements (railingPosts);
        AddSubelements (railingRailEnds);
        AddSubelements (railingRailConnections);
        AddSubelements (railingHandrailEnds);
        AddSubelements (railingHandrailConnections);
        AddSubelements (railingToprailEnds);
        AddSubelements (railingToprailConnections);
        AddSubelements (railingRails);
        AddSubelements (railingToprails);
        AddSubelements (railingHandrails);
        AddSubelements (railingPatterns);
        AddSubelements (railingInnerPosts);
        AddSubelements (railingPanels);
        AddSubelements (railingBalusterSets);
        AddSubelements (railingBalusters);

        AddSubelements (beamSegments);

        AddSubelements (columnSegments);

#undef AddSubelementsToObjectState

        subelementsOfHierarchicalElements (subelements);
    }

    return response;
}

GetConnectedElementsCommand::GetConnectedElementsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String GetConnectedElementsCommand::GetName () const
{
    return "GetConnectedElements";
}

GS::Optional<GS::UniString> GetConnectedElementsCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "elements": {
                "$ref": "#/Elements",
                "description": "The owner (host) elements whose connected elements are collected, e.g. Walls, Curtain Walls, Stairs or Railings."
            },
            "connectedElementType": {
                "$ref": "#/ElementType",
                "description": "The type of the connected elements to collect, e.g. Window or Door for a Wall owner, or a subelement type of a Curtain Wall, Stair or Railing owner."
            }
        },
        "additionalProperties": false,
        "required": [
            "elements",
            "connectedElementType"
        ]
    })";
}

GS::Optional<GS::UniString> GetConnectedElementsCommand::GetRawResponseSchema () const
{
    return R"({
        "$ref": "#/ConnectedElementsOrError"
    })";
}

GS::ObjectState GetConnectedElementsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<GS::ObjectState> elements;
    parameters.Get ("elements", elements);

    API_ElemTypeID elemType = API_ZombieElemID;
    GS::UniString elementTypeStr;
    if (parameters.Get ("connectedElementType", elementTypeStr)) {
        elemType = GetElementTypeFromNonLocalizedName (elementTypeStr);
        if (elemType == API_ZombieElemID) {
            return CreateErrorResponse (APIERR_BADPARS,
                GS::UniString::Printf ("Invalid connectedElementType '%T'.", elementTypeStr.ToPrintf ()));
        }
    }

    GS::ObjectState response;
    const auto& connectedElementsOfInputElements = response.AddList<GS::ObjectState> ("connectedElements");

    for (const GS::ObjectState& ownerElementOS : elements) {
        const GS::ObjectState* elementId = ownerElementOS.Get ("elementId");
        if (elementId == nullptr) {
            connectedElementsOfInputElements (CreateErrorResponse (APIERR_BADPARS, "elementId of owner element is missing"));
            continue;
        }

        const API_Guid ownerElemGuid = GetGuidFromObjectState (*elementId);

        GS::ObjectState elementsOS;
        const auto& elements = elementsOS.AddList<GS::ObjectState> ("elements");
        GS::Array<API_Guid> connectedElements;
        const GSErrCode connectionError = ACAPI_Grouping_GetConnectedElements (ownerElemGuid, elemType, &connectedElements);
        if (connectionError == NoError) {
            for (const API_Guid& elem : connectedElements) {
                elements (CreateElementIdObjectState (elem));
            }
        } else {
            connectedElementsOfInputElements (CreateErrorResponse (connectionError, "Cannot read native element connections."));
            continue;
        }

        connectedElementsOfInputElements (elementsOS);
    }

    return response;
}

GetRelationsOfElementsCommand::GetRelationsOfElementsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String GetRelationsOfElementsCommand::GetName () const
{
    return "GetRelationsOfElements";
}

GS::Optional<GS::UniString> GetRelationsOfElementsCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "elements": {
                "$ref": "#/Elements"
            },
            "otherElementType": {
                "$ref": "#/ElementType",
                "description": "Optional filter: only relations to elements of this type are returned."
            }
        },
        "additionalProperties": false,
        "required": [
            "elements"
        ]
    })";
}

GS::Optional<GS::UniString> GetRelationsOfElementsCommand::GetRawResponseSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "relations": {
                "type": "array",
                "description": "Type-specific relations of each element, aligned with the input.",
                "items": {
                    "$ref": "#/ElementRelationsOrError"
                }
            }
        },
        "additionalProperties": false,
        "required": [
            "relations"
        ]
    })";
}

#ifdef ServerMainVers_2600

static GS::ObjectState ConvertConnectionGuidItem (const API_ConnectionGuidItem& item)
{
    GS::ObjectState itemOS;
    itemOS.Add ("elementId", CreateGuidObjectState (item.guid));
    itemOS.Add ("connectedWithBeginPoint", item.conWithBeg);
    return itemOS;
}

static void AddConnectionList (GS::ObjectState& os, const char* fieldName, const API_ConnectionGuidItem* const* itemsHandle, Int32 itemCount)
{
    const auto& list = os.AddList<GS::ObjectState> (fieldName);
    if (itemsHandle == nullptr || *itemsHandle == nullptr) {
        return;
    }
    for (Int32 i = 0; i < itemCount; ++i) {
        list (ConvertConnectionGuidItem ((*itemsHandle)[i]));
    }
}

static void AddConnectionList (GS::ObjectState& os, const char* fieldName, const GS::Array<API_ConnectionGuidItem>* items)
{
    const auto& list = os.AddList<GS::ObjectState> (fieldName);
    if (items == nullptr) {
        return;
    }
    for (const API_ConnectionGuidItem& item : *items) {
        list (ConvertConnectionGuidItem (item));
    }
}

static GS::ObjectState ConvertZoneBoundaryPart (const API_WallPart& part, bool addRoomEdgeIndex)
{
    GS::ObjectState partOS;
    partOS.Add ("elementId", CreateGuidObjectState (part.guid));
    if (addRoomEdgeIndex) {
        partOS.Add ("roomEdgeIndex", part.roomEdge);
    }
    partOS.Add ("begDistance", part.tBeg);
    partOS.Add ("endDistance", part.tEnd);
    return partOS;
}

static GS::ObjectState GetWallRelations (const API_Guid& guid, const API_ElemType& otherType)
{
    API_WallRelation relation = {};
    const GSErrCode err = ACAPI_Element_GetRelations (guid, otherType, &relation);
    if (err != NoError) {
        return CreateErrorResponse (err, "Failed to get the relations of the element.");
    }

    GS::ObjectState connections;
    AddConnectionList (connections, "connectedToBeginPoint", relation.conBeg, relation.nConBeg);
    AddConnectionList (connections, "connectedToEndPoint", relation.conEnd, relation.nConEnd);
    AddConnectionList (connections, "connectedWithReferenceLineToEndPoints", relation.conRef, relation.nConRef);
    AddConnectionList (connections, "connectedToReferenceLine", relation.con, relation.nCon);
    AddConnectionList (connections, "crossingReferenceLine", relation.conX, relation.nConX);
    ACAPI_DisposeWallRelationHdls (&relation);

    return GS::ObjectState ("wallConnections", connections);
}

static GS::ObjectState GetBeamRelations (const API_Guid& guid, const API_ElemType& otherType)
{
    API_BeamRelation relation = {};
    const GSErrCode err = ACAPI_Element_GetRelations (guid, otherType, &relation);
    if (err != NoError) {
        return CreateErrorResponse (err, "Failed to get the relations of the element.");
    }

    GS::ObjectState connections;
    AddConnectionList (connections, "connectedToBeginPoint", relation.conBeg);
    AddConnectionList (connections, "connectedToEndPoint", relation.conEnd);
    AddConnectionList (connections, "connectedWithReferenceLineToEndPoints", relation.conRef);
    AddConnectionList (connections, "connectedToReferenceLine", relation.con);
    AddConnectionList (connections, "crossingReferenceLine", relation.conX);
    ACAPI_DisposeBeamRelationHdls (&relation);

    return GS::ObjectState ("beamConnections", connections);
}

static GS::ObjectState GetBeamSegmentRelations (const API_Guid& guid, const API_ElemType& otherType)
{
    API_BeamSegmentRelation relation = {};
    const GSErrCode err = ACAPI_Element_GetRelations (guid, otherType, &relation);
    if (err != NoError) {
        return CreateErrorResponse (err, "Failed to get the relations of the element.");
    }

    GS::ObjectState connections;
    AddConnectionList (connections, "connectedToBeginPoint", relation.conBeg);
    AddConnectionList (connections, "connectedToEndPoint", relation.conEnd);
    AddConnectionList (connections, "connectedWithReferenceLineToEndPoints", relation.conRef);
    AddConnectionList (connections, "connectedToReferenceLine", relation.con);
    AddConnectionList (connections, "crossingReferenceLine", relation.conX);
    ACAPI_DisposeBeamSegmentRelationHdls (&relation);

    return GS::ObjectState ("beamSegmentConnections", connections);
}

static GS::ObjectState GetZoneRelations (const API_Guid& guid, const API_ElemType& otherType)
{
    API_RoomRelation relation = {};
    const GSErrCode err = ACAPI_Element_GetRelations (guid, otherType, &relation);
    if (err != NoError) {
        return CreateErrorResponse (err, "Failed to get the relations of the element.");
    }

    GS::ObjectState zoneRelations;
    const auto& elementsGroupedByType = zoneRelations.AddList<GS::ObjectState> ("elementsGroupedByType");
    relation.elementsGroupedByType.Enumerate ([&] (const API_ElemType& elemType, const GS::Array<API_Guid>& elemGuids) {
        GS::ObjectState elementsOfTypeOS;
        elementsOfTypeOS.Add ("elementType", GetElementTypeNonLocalizedName (elemType.typeID));
        const auto& elementsOS = elementsOfTypeOS.AddList<GS::ObjectState> ("elements");
        for (const API_Guid& elemGuid : elemGuids) {
            elementsOS (CreateElementIdObjectState (elemGuid));
        }
        elementsGroupedByType (elementsOfTypeOS);
    });

    const auto& wallParts = zoneRelations.AddList<GS::ObjectState> ("wallParts");
    for (const API_WallPart& part : relation.wallPart) {
        wallParts (ConvertZoneBoundaryPart (part, true));
    }

    const auto& beamParts = zoneRelations.AddList<GS::ObjectState> ("beamParts");
    for (const API_BeamPart& part : relation.beamPart) {
        GS::ObjectState partOS;
        partOS.Add ("elementId", CreateGuidObjectState (part.guid));
        partOS.Add ("begDistance", part.tBeg);
        partOS.Add ("endDistance", part.tEnd);
        beamParts (partOS);
    }

    const auto& curtainWallSegmentParts = zoneRelations.AddList<GS::ObjectState> ("curtainWallSegmentParts");
    for (const API_CWSegmentPart& part : relation.cwSegmentPart) {
        curtainWallSegmentParts (ConvertZoneBoundaryPart (part, true));
    }

    ACAPI_DisposeRoomRelationHdls (&relation);

    return GS::ObjectState ("zoneRelations", zoneRelations);
}

static GS::ObjectState GetOpeningRelations (const API_Guid& guid, const API_ElemType& otherType)
{
    API_CWPanelRelation relation = {};
    const GSErrCode err = ACAPI_Element_GetRelations (guid, otherType, &relation);
    if (err != NoError) {
        return CreateErrorResponse (err, "Failed to get the relations of the element.");
    }

    GS::ObjectState openingRelations;
    if (relation.fromRoom != APINULLGuid) {
        openingRelations.Add ("fromRoom", CreateGuidObjectState (relation.fromRoom));
    }
    if (relation.toRoom != APINULLGuid) {
        openingRelations.Add ("toRoom", CreateGuidObjectState (relation.toRoom));
    }

    return GS::ObjectState ("openingRelations", openingRelations);
}

static GS::ObjectState GetRoofOrShellRelations (const API_Guid& guid, const API_ElemType& otherType)
{
    API_RoofRelation relation = {};
    // The rooms array is provided by the caller; if the API replaces the
    // pointer with its own allocation, that one is released below.
    GS::Array<API_Guid> roomGuids;
    relation.rooms = &roomGuids;
    const GSErrCode err = ACAPI_Element_GetRelations (guid, otherType, &relation);
    if (err != NoError) {
        if (relation.rooms != nullptr && relation.rooms != &roomGuids) {
            delete relation.rooms;
        }
        return CreateErrorResponse (err, "Failed to get the relations of the element.");
    }

    GS::ObjectState roofOrShellRelations;
    const auto& connectedRooms = roofOrShellRelations.AddList<GS::ObjectState> ("connectedRooms");
    if (relation.rooms != nullptr) {
        for (const API_Guid& roomGuid : *relation.rooms) {
            connectedRooms (CreateElementIdObjectState (roomGuid));
        }
    }
    if (relation.rooms != nullptr && relation.rooms != &roomGuids) {
        delete relation.rooms;
    }

    return GS::ObjectState ("roofOrShellRelations", roofOrShellRelations);
}

#endif

GS::ObjectState GetRelationsOfElementsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
#ifdef ServerMainVers_2600
    GS::Array<GS::ObjectState> elements;
    parameters.Get ("elements", elements);

    API_ElemType otherType = API_ZombieElemID;
    GS::UniString otherTypeStr;
    if (parameters.Get ("otherElementType", otherTypeStr)) {
        otherType = GetElementTypeFromNonLocalizedName (otherTypeStr);
        if (otherType == API_ZombieElemID) {
            return CreateErrorResponse (APIERR_BADPARS,
                GS::UniString::Printf ("Invalid otherElementType '%T'.", otherTypeStr.ToPrintf ()));
        }
    }

    GS::ObjectState response;
    const auto& relations = response.AddList<GS::ObjectState> ("relations");

    for (const GS::ObjectState& elementOS : elements) {
        const GS::ObjectState* elementId = elementOS.Get ("elementId");
        if (elementId == nullptr) {
            relations (CreateErrorResponse (APIERR_BADPARS, "elementId is missing"));
            continue;
        }

        API_Elem_Head elemHead = {};
        elemHead.guid = GetGuidFromObjectState (*elementId);
        if (ACAPI_Element_GetHeader (&elemHead) != NoError) {
            relations (CreateErrorResponse (APIERR_BADID, "Failed to find the element."));
            continue;
        }

        switch (elemHead.type.typeID) {
            case API_WallID:
                relations (GetWallRelations (elemHead.guid, otherType));
                break;
            case API_BeamID:
                relations (GetBeamRelations (elemHead.guid, otherType));
                break;
            case API_BeamSegmentID:
                relations (GetBeamSegmentRelations (elemHead.guid, otherType));
                break;
            case API_ZoneID:
                relations (GetZoneRelations (elemHead.guid, otherType));
                break;
            case API_CurtainWallPanelID:
            case API_SkylightID:
            case API_WindowID:
            case API_DoorID:
                relations (GetOpeningRelations (elemHead.guid, otherType));
                break;
            case API_RoofID:
            case API_ShellID:
                relations (GetRoofOrShellRelations (elemHead.guid, otherType));
                break;
            default:
                relations (CreateErrorResponse (APIERR_BADPARS,
                    GS::UniString::Printf ("Element type '%T' is not supported. Supported types: Wall, Beam, BeamSegment, Zone, CurtainWallPanel, Skylight, Window, Door, Roof, Shell.",
                        GetElementTypeNonLocalizedName (elemHead.type.typeID).ToPrintf ())));
                break;
        }
    }

    return response;
#else
    UNUSED_PARAMETER (parameters);
    return CreateErrorResponse (APIERR_NOTSUPPORTED, "GetRelationsOfElements requires Archicad 26 or newer.");
#endif
}

GetZoneBoundariesCommand::GetZoneBoundariesCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String GetZoneBoundariesCommand::GetName () const
{
    return "GetZoneBoundaries";
}

GS::Optional<GS::UniString> GetZoneBoundariesCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "zoneElementId": {
                "$ref": "#/ElementId",
                "description": "The identifier of a single Zone. Prefer the zones array: querying many Zones in one call is much faster than one call per Zone."
            },
            "zones": {
                "$ref": "#/Elements",
                "description": "A list of Zones. Only one of zoneElementId and zones can be given."
            }
        },
        "additionalProperties": false,
        "required": [
        ]
    })";
}

GS::Optional<GS::UniString> GetZoneBoundariesCommand::GetRawResponseSchema () const
{
    return R"({
        "type": "object",
        "oneOf": [
            {
                "$ref": "#/ZoneBoundariesOrError"
            },
            {
                "$ref": "#/ZoneBoundariesOfZonesWrapper"
            }
        ]
    })";
}

#ifdef ServerMainVers_2800

static GS::ObjectState GetBoundariesOfZone (ACAPI::ZoneBoundaryQuery& query, const API_Guid& zoneGuid)
{
    const ACAPI::Result<std::vector<ACAPI::ZoneBoundary>> boundaries = query.GetZoneBoundaries (zoneGuid);

    if (boundaries.IsErr ()) {
        return CreateErrorResponse (boundaries.UnwrapErr ().kind, "Failed to get zone boundary");
    }

    GS::ObjectState zoneBoundariesOS;
    const auto& zoneBoundaries = zoneBoundariesOS.AddList<GS::ObjectState> ("zoneBoundaries");

    for (const ACAPI::ZoneBoundary& boundary : boundaries.Unwrap ()) {
        GS::ObjectState boundaryOS;
        boundaryOS.Add ("connectedElementId", CreateGuidObjectState (boundary.GetElemId ()));
        boundaryOS.Add ("isExternal", boundary.IsExternal ());
        boundaryOS.Add ("neighbouringZoneElementId", CreateGuidObjectState (boundary.GetNeighbouringZoneId ()));
        boundaryOS.Add ("area", boundary.GetArea ());

        const auto& polygonOutline = boundaryOS.AddList<GS::ObjectState> ("polygonOutline");
        const ModelerAPI::MeshBody& body = boundary.GetBody ();
        const ModelerAPI::Polygon& poly = boundary.GetPolygon ();
        {
            ModelerAPI::Edge edge;
            ModelerAPI::Vertex vertex;
            for (Int32 edgeIdx = 1; edgeIdx <= poly.GetEdgeCount (); ++edgeIdx) {

                const Int32 edgeIndex = poly.GetEdgeIndex (edgeIdx);

                if (edgeIndex == 0) {
                    body.GetVertex (edge.GetVertexIndex2 (), &vertex);
                    polygonOutline (Create3DCoordinateObjectState (*reinterpret_cast<API_Coord3D*> (&vertex)));
                    break;
                }

                body.GetEdge (edgeIndex, &edge);
                body.GetVertex (edge.GetVertexIndex1 (), &vertex);

                polygonOutline (Create3DCoordinateObjectState (*reinterpret_cast<API_Coord3D*> (&vertex)));

                if (edgeIdx == poly.GetEdgeCount ()) {
                    body.GetVertex (edge.GetVertexIndex2 (), &vertex);
                    polygonOutline (Create3DCoordinateObjectState (*reinterpret_cast<API_Coord3D*> (&vertex)));
                }
            }
        }

        zoneBoundaries (boundaryOS);
    }

    return zoneBoundariesOS;
}

#endif

GS::ObjectState GetZoneBoundariesCommand::Execute (
    const GS::ObjectState& parameters,
#ifdef ServerMainVers_2800
    GS::ProcessControl& processControl) const
#else
    GS::ProcessControl& /*processControl*/) const
#endif
{
    const GS::ObjectState* zoneElementId = parameters.Get ("zoneElementId");
    GS::Array<GS::ObjectState> zones;
    const bool zonesGiven = parameters.Get ("zones", zones);

    if (zoneElementId == nullptr && !zonesGiven) {
        return CreateErrorResponse (APIERR_BADPARS, "One of zoneElementId and zones is required");
    }

    if (zoneElementId != nullptr && zonesGiven) {
        return CreateErrorResponse (APIERR_BADPARS, "Only one of zoneElementId and zones can be given");
    }

#ifdef ServerMainVers_2800
    ACAPI::ZoneBoundaryQuery query = ACAPI::CreateZoneBoundaryQuery ();

    ACAPI::Result updateResult = query.Modify (
        [&] (ACAPI::ZoneBoundaryQuery::Modifier& modifier) -> GSErrCode {
            ACAPI::Result<void> result = modifier.Update (processControl);
            return result.IsOk () ? NoError : result.UnwrapErr ().kind;
        }
    );

    if (updateResult.IsErr ()) {
        return CreateErrorResponse (updateResult.UnwrapErr ().kind, "Failed to execute zone boundary query");
    }

    if (zoneElementId != nullptr) {
        return GetBoundariesOfZone (query, GetGuidFromObjectState (*zoneElementId));
    }

    GS::ObjectState response;
    const auto& zoneBoundariesOfZones = response.AddList<GS::ObjectState> ("zoneBoundariesOfZones");

    for (const GS::ObjectState& zone : zones) {
        const GS::ObjectState* elementId = zone.Get ("elementId");
        if (elementId == nullptr) {
            zoneBoundariesOfZones (CreateErrorResponse (APIERR_BADPARS, "elementId is missing"));
            continue;
        }

        zoneBoundariesOfZones (GetBoundariesOfZone (query, GetGuidFromObjectState (*elementId)));
    }

    return response;
#else
    return CreateErrorResponse (APIERR_NOTSUPPORTED, "This command is only supported in ArchiCAD 28 or later.");
#endif
}

UpdateZonesCommand::UpdateZonesCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String UpdateZonesCommand::GetName () const
{
    return "UpdateZones";
}

GS::Optional<GS::UniString> UpdateZonesCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "keepStampPosition": {
                "type": "boolean",
                "description": "Keep the position of the Zone Stamps. The default is true."
            },
            "undoTopTrim": {
                "type": "boolean",
                "description": "Undo the trimming of the top of the Zones. The default is false."
            },
            "undoBottomTrim": {
                "type": "boolean",
                "description": "Undo the trimming of the bottom of the Zones. The default is false."
            }
        },
        "additionalProperties": false,
        "required": [
        ]
    })";
}

GS::Optional<GS::UniString> UpdateZonesCommand::GetRawResponseSchema () const
{
    return R"({
        "$ref": "#/ExecutionResult"
    })";
}

GS::ObjectState UpdateZonesCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    API_RoomUpdateParams roomUpdateParams;
    parameters.Get ("keepStampPosition", roomUpdateParams.keepStampPos);
    parameters.Get ("undoTopTrim", roomUpdateParams.undoTopTrim);
    parameters.Get ("undoBottomTrim", roomUpdateParams.undoBotTrim);

    GSErrCode err = NoError;

    const GSErrCode transactionError = ACAPI_CallUndoableCommand ("UpdateZonesCommand", [&]() {
        err = ACAPI_Internal (APIInternal_UpdateRoomsID, &roomUpdateParams);

        return err;
    });
    if (transactionError != NoError) return CreateErrorResponse (transactionError, "Native transaction failed; committed changes are not confirmed.");

    return err == NoError
        ? CreateSuccessfulExecutionResult ()
        : CreateFailedExecutionResult (err, "Failed to update zones.");
}

GetCollisionsCommand::GetCollisionsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String GetCollisionsCommand::GetName () const
{
    return "GetCollisions";
}

GS::Optional<GS::UniString> GetCollisionsCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "elementsGroup1": {
                "$ref": "#/Elements"
            },
            "elementsGroup2": {
                "$ref": "#/Elements"
            },
            "settings": {
                "type": "object",
                "properties": {
                    "volumeTolerance": {
                        "type": "number",
                        "description": "Intersection body volume greater then this value will be considered as a collision. Default value is 0.001."
                    },
                    "performSurfaceCheck": {
                        "type": "boolean",
                        "description": "Enables surface collision check. If disabled the surfaceTolerance value will be ignored. By default it's false."
                    },
                    "surfaceTolerance": {
                        "type": "number",
                        "description": "Intersection body surface area greater then this value will be considered as a collision. Default value is 0.001."
                    }
                },
                "additionalProperties": false,
                "required": [
                    "volumeTolerance",
                    "performSurfaceCheck",
                    "surfaceTolerance"
                ]
            }
        },
        "additionalProperties": false,
        "required": [
            "elementsGroup1",
            "elementsGroup2"
        ]
    })";
}

GS::Optional<GS::UniString> GetCollisionsCommand::GetRawResponseSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "collisions": {
                "type": "array",
                "items": {
                    "type": "object",
                    "properties": {
                        "elementId1": {
                            "$ref": "#/ElementId"
                        },
                        "elementId2": {
                            "$ref": "#/ElementId"
                        },
                        "hasBodyCollision": {
                            "type": "boolean"
                        },
                        "hasClearenceCollision": {
                            "type": "boolean"
                        }
                    },
                    "additionalProperties": false,
                    "required": [
                        "elementId1",
                        "elementId2",
                        "hasBodyCollision",
                        "hasClearenceCollision"
                    ]
                }
            }
        },
        "additionalProperties": false,
        "required": [
            "collisions"
        ]
    })";
}

GS::ObjectState GetCollisionsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<GS::ObjectState> elementsGroup1;
    parameters.Get ("elementsGroup1", elementsGroup1);
    GS::Array<GS::ObjectState> elementsGroup2;
    parameters.Get ("elementsGroup2", elementsGroup2);

    API_CollisionDetectionSettings collisionSettings = {};
    collisionSettings.volumeTolerance = 0.001;
    collisionSettings.performSurfaceCheck = false;
    collisionSettings.surfaceTolerance = 0.001;
    GS::ObjectState settings;
    if (parameters.Get ("settings", settings)) {
        settings.Get ("volumeTolerance", collisionSettings.volumeTolerance);
        settings.Get ("performSurfaceCheck", collisionSettings.performSurfaceCheck);
        settings.Get ("surfaceTolerance", collisionSettings.surfaceTolerance);
    }

    const GS::Array<API_Guid> elemIds1 = elementsGroup1.Transform<API_Guid> (GetGuidFromElementsArrayItem);
    const GS::Array<API_Guid> elemIds2 = elementsGroup2.Transform<API_Guid> (GetGuidFromElementsArrayItem);
    GS::Array<GS::Pair<API_CollisionElem, API_CollisionElem>> resultArray;
    GSErrCode err = ACAPI_Element_GetCollisions (elemIds1, elemIds2, resultArray, collisionSettings);
    if (err != NoError) {
        return CreateErrorResponse (err, "Failed to perform collision detection.");
    }

    GS::ObjectState response;
    const auto& collisions = response.AddList<GS::ObjectState> ("collisions");

    for (const auto& collisionElement : resultArray) {
        collisions (GS::ObjectState (
            "elementId1", CreateGuidObjectState (collisionElement.first.collidedElemGuid),
            "elementId2", CreateGuidObjectState (collisionElement.second.collidedElemGuid),
            "hasBodyCollision", collisionElement.first.hasBodyCollision,
#ifdef ServerMainVers_3000
            "hasClearanceCollision", collisionElement.second.hasClearanceCollision));
#else
            "hasClearenceCollision", collisionElement.second.hasClearenceCollision));
#endif
    }

    return response;
}

MoveElementsCommand::MoveElementsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String MoveElementsCommand::GetName () const
{
    return "MoveElements";
}

GS::Optional<GS::UniString> MoveElementsCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "elementsWithMoveVectors": {
                "type": "array",
                "description": "The elements with move vector pairs.",
                "items": {
                    "type": "object",
                    "properties": {
                        "elementId": {
                            "$ref": "#/ElementId"
                        },
                        "moveVector": {
                            "type": "object",
                            "description" : "Move vector of a 3D point.",
                            "properties" : {
                                "x": {
                                    "type": "number",
                                    "description" : "X value of the vector."
                                },
                                "y" : {
                                    "type": "number",
                                    "description" : "Y value of the vector."
                                },
                                "z" : {
                                    "type": "number",
                                    "description" : "Z value of the vector."
                                }
                            },
                            "additionalProperties": false,
                            "required" : [
                                "x",
                                "y",
                                "z"
                            ]
                        },
                        "copy": {
                            "type": "boolean",
                            "description" : "Optional parameter. If true, then a copy of the element will be moved. By default it's false."
                        }
                    },
                    "additionalProperties": false,
                    "required": [
                        "elementId",
                        "moveVector"
                    ]
                }
            }
        },
        "additionalProperties": false,
        "required": [
            "elementsWithMoveVectors"
        ]
    })";
}

GS::Optional<GS::UniString> MoveElementsCommand::GetRawResponseSchema () const
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

static GSErrCode MoveElement (const API_Guid& elemGuid, const API_Vector3D& moveVector, bool withCopy)
{
    GS::Array<API_Neig> elementsToEdit = { API_Neig (elemGuid) };

    API_EditPars editPars = {};
    editPars.typeID = APIEdit_Drag;
    editPars.endC = moveVector;
    editPars.withDelete = !withCopy;

    return ACAPI_Element_Edit (&elementsToEdit, editPars);
}

GS::ObjectState	MoveElementsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<GS::ObjectState> elementsWithMoveVectors;
    parameters.Get ("elementsWithMoveVectors", elementsWithMoveVectors);

    GS::ObjectState response;
    const auto& executionResults = response.AddList<GS::ObjectState> ("executionResults");

    const GSErrCode transactionError = ACAPI_CallUndoableCommand ("Move Elements", [&]() -> GSErrCode {
        for (const GS::ObjectState& elementWithMoveVector : elementsWithMoveVectors) {
            const GS::ObjectState* elementId = elementWithMoveVector.Get ("elementId");
            if (elementId == nullptr) {
                executionResults (CreateFailedExecutionResult (APIERR_BADPARS, "elementId is missing"));
                continue;
            }

            const GS::ObjectState* moveVector = elementWithMoveVector.Get ("moveVector");
            if (moveVector == nullptr) {
                executionResults (CreateFailedExecutionResult (APIERR_BADPARS, "moveVector is missing"));
                continue;
            }

            const API_Guid elemGuid = GetGuidFromObjectState (*elementId);

            bool copy = false;
            elementWithMoveVector.Get ("copy", copy);

            const GSErrCode err = MoveElement (elemGuid,
                                               Get3DCoordinateFromObjectState (*moveVector),
                                               copy);
            if (err != NoError) {
                const GS::UniString errorMsg = GS::UniString::Printf ("Failed to move element with guid %T!", APIGuidToString (elemGuid).ToPrintf ());
                executionResults (CreateFailedExecutionResult (err, errorMsg));
            } else {
                executionResults (CreateSuccessfulExecutionResult ());
            }
        }

        return NoError;
    });
    if (transactionError != NoError) return CreateErrorResponse (transactionError, "Native transaction failed; committed changes are not confirmed.");

    return response;
}

static GSErrCode RotateElement (const API_Guid& elemGuid, const API_Coord& beginPoint, const API_Coord& endPoint, const API_Coord& origin, bool withCopy)
{
    GS::Array<API_Neig> elementsToEdit = { API_Neig (elemGuid) };

    API_EditPars editPars = {};
    editPars.typeID = APIEdit_Rotate;
    editPars.begC = API_Coord3D { beginPoint.x, beginPoint.y, 0.0 };
    editPars.endC = API_Coord3D { endPoint.x, endPoint.y, 0.0 };
    editPars.origC = origin;
    editPars.withDelete = !withCopy;

    return ACAPI_Element_Edit (&elementsToEdit, editPars);
}

RotateElementsCommand::RotateElementsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String RotateElementsCommand::GetName () const
{
    return "RotateElements";
}

GS::Optional<GS::UniString> RotateElementsCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "elementsWithRotations": {
                "type": "array",
                "description": "The elements with rotation settings.",
                "items": {
                    "type": "object",
                    "properties": {
                        "elementId": {
                            "$ref": "#/ElementId"
                        },
                        "rotation": {
                            "type": "object",
                            "description": "Rotation parameters for an element.",
                            "properties": {
                                "beginPoint": {
                                    "type": "object",
                                    "description": "Starting point of the rotation arc.",
                                    "properties": {
                                        "x": { "type": "number" },
                                        "y": { "type": "number" }
                                    },
                                    "additionalProperties": false,
                                    "required": ["x", "y"]
                                },
                                "endPoint": {
                                    "type": "object",
                                    "description": "End point of the rotation arc.",
                                    "properties": {
                                        "x": { "type": "number" },
                                        "y": { "type": "number" }
                                    },
                                    "additionalProperties": false,
                                    "required": ["x", "y"]
                                },
                                "origin": {
                                    "type": "object",
                                    "description": "Center of rotation.",
                                    "properties": {
                                        "x": { "type": "number" },
                                        "y": { "type": "number" }
                                    },
                                    "additionalProperties": false,
                                    "required": ["x", "y"]
                                }
                            },
                            "additionalProperties": false,
                            "required": ["beginPoint", "endPoint", "origin"]
                        },
                        "copy": {
                            "type": "boolean",
                            "description": "Optional parameter. If true, a copy of the element is rotated. By default it's false."
                        }
                    },
                    "additionalProperties": false,
                    "required": ["elementId", "rotation"]
                }
            }
        },
        "additionalProperties": false,
        "required": ["elementsWithRotations"]
    })";
}

GS::Optional<GS::UniString> RotateElementsCommand::GetRawResponseSchema () const
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

GS::ObjectState RotateElementsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<GS::ObjectState> elementsWithRotations;
    parameters.Get ("elementsWithRotations", elementsWithRotations);

    GS::ObjectState response;
    const auto& executionResults = response.AddList<GS::ObjectState> ("executionResults");

    const GSErrCode transactionError = ACAPI_CallUndoableCommand ("Rotate Elements", [&]() -> GSErrCode {
        for (const GS::ObjectState& elementWithRotation : elementsWithRotations) {
            const GS::ObjectState* elementId = elementWithRotation.Get ("elementId");
            if (elementId == nullptr) {
                executionResults (CreateFailedExecutionResult (APIERR_BADPARS, "elementId is missing"));
                continue;
            }

            const GS::ObjectState* rotation = elementWithRotation.Get ("rotation");
            if (rotation == nullptr) {
                executionResults (CreateFailedExecutionResult (APIERR_BADPARS, "rotation is missing"));
                continue;
            }

            const GS::ObjectState* beginPoint = rotation->Get ("beginPoint");
            const GS::ObjectState* endPoint = rotation->Get ("endPoint");
            const GS::ObjectState* origin = rotation->Get ("origin");
            if (beginPoint == nullptr || endPoint == nullptr || origin == nullptr) {
                executionResults (CreateFailedExecutionResult (APIERR_BADPARS, "rotation beginPoint, endPoint, or origin is missing"));
                continue;
            }

            const API_Guid elemGuid = GetGuidFromObjectState (*elementId);

            bool copy = false;
            elementWithRotation.Get ("copy", copy);

            const GSErrCode err = RotateElement (elemGuid,
                                                Get2DCoordinateFromObjectState (*beginPoint),
                                                Get2DCoordinateFromObjectState (*endPoint),
                                                Get2DCoordinateFromObjectState (*origin),
                                                copy);
            if (err != NoError) {
                const GS::UniString errorMsg = GS::UniString::Printf ("Failed to rotate element with guid %T!", APIGuidToString (elemGuid).ToPrintf ());
                executionResults (CreateFailedExecutionResult (err, errorMsg));
            } else {
                executionResults (CreateSuccessfulExecutionResult ());
            }
        }

        return NoError;
    });
    if (transactionError != NoError) return CreateErrorResponse (transactionError, "Native transaction failed; committed changes are not confirmed.");

    return response;
}

FilterElementsCommand::FilterElementsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String FilterElementsCommand::GetName () const
{
    return "FilterElements";
}

GS::Optional<GS::UniString> FilterElementsCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "elements": {
                "$ref": "#/Elements"
            },
            "filters": {
                "type": "array",
                "items": {
                    "$ref": "#/ElementFilter"
                },
                "minItems": 1
            }
        },
        "additionalProperties": false,
        "required": [
            "elements"
        ]
    })";
}

GS::Optional<GS::UniString> FilterElementsCommand::GetRawResponseSchema () const
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

GS::ObjectState FilterElementsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<GS::ObjectState> elements;
    parameters.Get ("elements", elements);

    GS::Array<GS::UniString> filters;
    parameters.Get ("filters", filters);

    API_ElemFilterFlags filterFlags = APIFilt_None;
    for (const GS::UniString& filter : filters) {
        filterFlags |= ConvertFilterStringToFlag (filter);
    }
    if (filterFlags == APIFilt_None) {
        return CreateErrorResponse (APIERR_BADPARS, "Invalid or missing filters!");
    }

    GS::ObjectState response;
    const auto& filteredElements = response.AddList<GS::ObjectState> ("elements");

    for (const GS::ObjectState& element : elements) {
        const GS::ObjectState* elementId = element.Get ("elementId");
        if (elementId == nullptr) {
            continue;
        }

        const API_Guid elemGuid = GetGuidFromObjectState (*elementId);
        if (!ACAPI_Element_Filter (elemGuid, filterFlags)) {
            continue;
        }

        filteredElements (CreateElementIdObjectState (elemGuid));
    }

    return response;
}

HighlightElementsCommand::HighlightElementsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String HighlightElementsCommand::GetName () const
{
    return "HighlightElements";
}

GS::Optional<GS::UniString> HighlightElementsCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "elements": {
                "$ref": "#/Elements"
            },
            "highlightedColors": {
                "type": "array",
                "description": "A list of colors to highlight elements.",
                "items": {
                    "type": "array",
                    "description": "Color of the highlighted element as an [r, g, b, a] array. Each component must be in the 0-255 range.",
                    "items": {
                        "type": "integer"
                    },
                    "minItems": 4,
                    "maxItems": 4
                }
            },
            "wireframe3D": {
                "type": "boolean",
                "description" : "Optional parameter. Switch non highlighted elements in the 3D window to wireframe."
            },
            "nonHighlightedColor": {
                "type": "array",
                "description": "Optional parameter. Color of the non highlighted elements as an [r, g, b, a] array. Each component must be in the 0-255 range.",
                "items": {
                    "type": "integer"
                },
                "minItems": 4,
                "maxItems": 4
            }
        },
        "additionalProperties": false,
        "required": [
            "elements",
            "highlightedColors"
        ]
    })";
}

GS::Optional<GS::UniString> HighlightElementsCommand::GetRawResponseSchema () const
{
    return R"({
        "$ref": "#/ExecutionResult"
    })";
}

#ifdef ServerMainVers_2600

static API_RGBAColor GetRGBAColorFromArray (const GS::Array<GS::Int32>& color)
{
    return API_RGBAColor {
        color[0] / 255.0,
        color[1] / 255.0,
        color[2] / 255.0,
        color[3] / 255.0
    };
}

static GS::Optional<API_RGBAColor> GetRGBAColorFromObjectState (const GS::ObjectState& os, const GS::String& name)
{
    GS::Array<GS::Int32> color;
    if (os.Get (name, color)) {
        return GetRGBAColorFromArray (color);
    } else {
        return {};
    }
}

GS::ObjectState HighlightElementsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<GS::ObjectState> elements;
    parameters.Get ("elements", elements);

    if (elements.IsEmpty ()) {
        ACAPI_UserInput_ClearElementHighlight ();
        // need to call redraw for changes to take effect
        ACAPI_View_Redraw ();
        return CreateSuccessfulExecutionResult ();
    }

    GS::Array<GS::Array<GS::Int32>> highlightedColors;
    parameters.Get ("highlightedColors", highlightedColors);

    if (highlightedColors.GetSize () != elements.GetSize ()) {
        return CreateFailedExecutionResult (APIERR_BADPARS, "The size of 'elements' array and 'highlightedColors' array does not match.");
    }

    GS::HashTable<API_Guid, API_RGBAColor> elementsWithColors;
    for (USize i = 0; i < elements.GetSize (); ++i) {
        GS::ObjectState elementId;
        if (elements[i].Get ("elementId", elementId)) {
            const API_Guid elemGuid = GetGuidFromObjectState (elementId);
            const API_RGBAColor color = GetRGBAColorFromArray (highlightedColors[i]);
            elementsWithColors.Add (elemGuid, color);
        }
    }

    GS::Optional<bool> wireframe3D;
    bool tmp;
    if (parameters.Get ("wireframe3D", tmp)) {
        wireframe3D = tmp;
    }

    const GS::Optional<API_RGBAColor> nonHighlightedColor = GetRGBAColorFromObjectState (parameters, "nonHighlightedColor");

    ACAPI_UserInput_SetElementHighlight (elementsWithColors, wireframe3D, nonHighlightedColor);

    // need to call redraw for changes to take effect
    ACAPI_View_Redraw ();

    return CreateSuccessfulExecutionResult ();
}

#else

GS::ObjectState HighlightElementsCommand::Execute (const GS::ObjectState& /*parameters*/, GS::ProcessControl& /*processControl*/) const
{
    return CreateFailedExecutionResult (APIERR_GENERAL, GetName () + " command is not supported for this AC version.");
}

#endif


static void UpdateGlobalBoundsWithPoint (API_Box3D& globalBounds, const API_Coord3D& pt)
{
    if (pt.x < globalBounds.xMin) globalBounds.xMin = pt.x;
    if (pt.x > globalBounds.xMax) globalBounds.xMax = pt.x;
    if (pt.y < globalBounds.yMin) globalBounds.yMin = pt.y;
    if (pt.y > globalBounds.yMax) globalBounds.yMax = pt.y;
    if (pt.z < globalBounds.zMin) globalBounds.zMin = pt.z;
    if (pt.z > globalBounds.zMax) globalBounds.zMax = pt.z;
}

static void InitializeEmptyBounds (API_Box3D& bounds)
{
    bounds.xMin = bounds.yMin = bounds.zMin = 1e30;
    bounds.xMax = bounds.yMax = bounds.zMax = -1e30;
}

// Extends bounds with the solid 3D bodies of the element. foundSolidBody is only ever set to
// true here, so the same accumulator can be run over several elements in a row.
static GSErrCode AccumulateSolidBodyBounds (const API_Elem_Head& elemHead, API_Box3D& bounds, bool& foundSolidBody)
{
    API_ElemInfo3D info3D = {};
    GSErrCode err = ACAPI_ModelAccess_Get3DInfo (elemHead, &info3D);
    if (err != NoError) {
        return err;
    }

    for (Int32 iBody = info3D.fbody; iBody <= info3D.lbody; ++iBody) {
        API_Component3D bodyComp = {};
        bodyComp.header.typeID = API_BodyID;
        bodyComp.header.index = iBody;

        if (ACAPI_ModelAccess_GetComponent (&bodyComp) != NoError) continue;

        if (bodyComp.body.nPgon == 0) { // Skip non-solid bodies
            continue;
        }

        foundSolidBody = true;

        // body.xmin..zmax is the body's bounding box in world coordinates: measured on a
        // live model it equals the min/max of the body's vertices *after* they are
        // transformed by body.tranmat. Applying tranmat to it again adds the placement a
        // second time, which is what made a stair report twice its height (#563). It went
        // unnoticed for Roofs and Zones only because their tranmat is the identity.
        UpdateGlobalBoundsWithPoint (bounds, API_Coord3D { bodyComp.body.xmin, bodyComp.body.ymin, bodyComp.body.zmin });
        UpdateGlobalBoundsWithPoint (bounds, API_Coord3D { bodyComp.body.xmax, bodyComp.body.ymax, bodyComp.body.zmax });
    }

    return NoError;
}

static GSErrCode CalculateSolidBodyBounds (const API_Elem_Head& elemHead, API_Box3D& outBounds)
{
    InitializeEmptyBounds (outBounds);

    bool foundSolidBody = false;
    GSErrCode err = AccumulateSolidBodyBounds (elemHead, outBounds, foundSolidBody);
    if (err != NoError) {
        return err;
    }

    if (!foundSolidBody) {
        return APIERR_GENERAL;
    }

    return NoError;
}

template<typename APIElemType>
static void AccumulateSubelementBounds (APIElemType* subelemArray, API_Box3D& bounds, bool& foundSolidBody)
{
    if (subelemArray == nullptr) {
        return;
    }

    const GSSize nSubelements = BMGetPtrSize (reinterpret_cast<GSPtr>(subelemArray)) / sizeof (APIElemType);
    for (GSIndex i = 0; i < nSubelements; ++i) {
        AccumulateSolidBodyBounds (subelemArray[i].head, bounds, foundSolidBody);
    }
}

// A Stair carries its 3D geometry in its subelements (risers, treads and structures), so neither
// ACAPI_Element_CalcBounds nor the 3D model of the Stair element itself gives back the vertical
// extent of the flight - both answer with zMin == zMax == 0 (#563). The bounds are the union of
// the solid bodies of the Stair and of all of its subelements.
static GSErrCode CalculateStairBounds (const API_Elem_Head& stairElemHead, API_Box3D& outBounds)
{
    InitializeEmptyBounds (outBounds);

    bool foundSolidBody = false;
    AccumulateSolidBodyBounds (stairElemHead, outBounds, foundSolidBody);

    API_ElementMemo memo = {};
    const GS::OnExit guard ([&memo] () { ACAPI_DisposeElemMemoHdls (&memo); });
    if (ACAPI_Element_GetMemo (stairElemHead.guid, &memo, APIMemoMask_All) == NoError) {
        AccumulateSubelementBounds (memo.stairRisers, outBounds, foundSolidBody);
        AccumulateSubelementBounds (memo.stairTreads, outBounds, foundSolidBody);
        AccumulateSubelementBounds (memo.stairStructures, outBounds, foundSolidBody);
    }

    if (!foundSolidBody) {
        return APIERR_GENERAL;
    }

    return NoError;
}

Get3DBoundingBoxesCommand::Get3DBoundingBoxesCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String Get3DBoundingBoxesCommand::GetName () const
{
    return "Get3DBoundingBoxes";
}

GS::Optional<GS::UniString> Get3DBoundingBoxesCommand::GetInputParametersSchema () const
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

GS::Optional<GS::UniString> Get3DBoundingBoxesCommand::GetRawResponseSchema () const
{
    return R"({
        "type": "object",
            "properties": {
            "boundingBoxes3D": {
                "$ref": "#/BoundingBoxes3D"
            }
        },
        "additionalProperties": false,
        "required": [
            "boundingBoxes3D"
        ]
    })";
}

GS::ObjectState Get3DBoundingBoxesCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<GS::ObjectState> elements;
    parameters.Get ("elements", elements);

    GS::ObjectState response;
    const auto& boundingBoxes3D = response.AddList<GS::ObjectState> ("boundingBoxes3D");

    for (const GS::ObjectState& element : elements) {
        const GS::ObjectState* elementId = element.Get ("elementId");
        if (elementId == nullptr) {
            boundingBoxes3D (CreateErrorResponse (APIERR_BADPARS, "elementId is missing"));
            continue;
        }

        API_Elem_Head elemHead = {};
        elemHead.guid = GetGuidFromObjectState (*elementId);
        GSErrCode err = ACAPI_Element_GetHeader (&elemHead);
        if (err != NoError) {
            boundingBoxes3D (CreateErrorResponse (err, "Failed to find element in Archicad"));
            continue;
        }
        const API_ElemTypeID typeID = GetElemTypeId (elemHead);

        API_Box3D box3D = {};
        if (typeID == API_RoofID || typeID == API_ZoneID) {
            err = CalculateSolidBodyBounds (elemHead, box3D);
        } else if (typeID == API_StairID) {
            err = CalculateStairBounds (elemHead, box3D);
            if (err != NoError) {
                // The Stair has no solid body at all - for example it is filtered out of the 3D
                // model - so fall back to the old answer instead of failing the whole element.
                err = ACAPI_Element_CalcBounds (&elemHead, &box3D);
            }
        } else {
            err = ACAPI_Element_CalcBounds (&elemHead, &box3D);
        }
        if (err != NoError) {
            boundingBoxes3D (CreateErrorResponse (err, "Failed to get the 3D bounding box"));
            continue;
        }

        GS::ObjectState boundingBox3D ("xMin", box3D.xMin,
                                       "xMax", box3D.xMax,
                                       "yMin", box3D.yMin,
                                       "yMax", box3D.yMax,
                                       "zMin", box3D.zMin,
                                       "zMax", box3D.zMax);
        boundingBoxes3D (GS::ObjectState ("boundingBox3D", boundingBox3D));
    }

    return response;
}

DeleteElementsCommand::DeleteElementsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String DeleteElementsCommand::GetName () const
{
    return "DeleteElements";
}

GS::Optional<GS::UniString> DeleteElementsCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "elements": {
                "$ref": "#/Elements"
            },
            "allowGroupDeletion": {
                "type": "boolean",
                "description": "Archicad automatically extends a delete to every other member of a group if any requested element belongs to one (unless Suspend Groups is on) - this can silently delete far more than requested. False by default: any requested element that belongs to a group is refused and nothing is deleted. Set true only when deleting the whole group is actually intended."
            }
        },
        "additionalProperties": false,
        "required": [
            "elements"
        ]
    })";
}

GS::Optional<GS::UniString> DeleteElementsCommand::GetRawResponseSchema () const
{
    return R"({
        "$ref": "#/ExecutionResult"
    })";
}

GS::ObjectState DeleteElementsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<GS::ObjectState> elements;
    parameters.Get ("elements", elements);
    if (elements.IsEmpty ())
        return CreateErrorResponse (APIERR_BADPARS, "Supply at least one element.");

    bool allowGroupDeletion = false;
    parameters.Get ("allowGroupDeletion", allowGroupDeletion);

    // Validate every requested element BEFORE deleting anything, all-or-nothing: a
    // missing/invalid elementId used to silently resolve to APINULLGuid and still get
    // passed into ACAPI_Element_Delete's array with no check at all. Worse, that
    // function's own documented behaviour automatically extends the delete to every
    // other member of a group if any target belongs to one (unless Suspend Groups is
    // on) - confirmed live to be able to sweep away far more than requested, with no
    // warning. Refusing upfront, and requiring an explicit opt-in for the group case,
    // matches every other command this session: never silently do more than asked.
    GS::Array<API_Guid> guids;
    for (const auto& item : elements) {
        const API_Guid guid = GetGuidFromElementsArrayItem (item);
        if (guid == APINULLGuid) {
            return CreateErrorResponse (APIERR_BADPARS, "One or more elements has a missing or invalid elementId; nothing was deleted.");
        }
        API_Elem_Head head = {};
        head.guid = guid;
        const GSErrCode headerErr = ACAPI_Element_GetHeader (&head);
        if (headerErr != NoError) {
            return CreateErrorResponse (headerErr, GS::UniString ("An element does not exist or cannot be read (guid ") + APIGuidToString (guid) + "); nothing was deleted.");
        }
        if (!allowGroupDeletion) {
            API_Guid groupGuid = APINULLGuid;
            const GSErrCode groupErr = ACAPI_Grouping_GetGroup (guid, &groupGuid);
            if (groupErr == NoError && groupGuid != APINULLGuid) {
                return CreateErrorResponse (APIERR_GENERAL, GS::UniString ("An element (guid ") + APIGuidToString (guid) +
                    ") is part of a group; deleting it would delete every other member of that group too. Nothing was deleted. Set allowGroupDeletion:true to proceed anyway.");
            }
        }
        guids.Push (guid);
    }

    GSErrCode err = NoError;

    const GSErrCode transaction = ACAPI_CallUndoableCommand ("DeleteElementsCommand", [&]() {
        err = ACAPI_Element_Delete (guids);

        return err;
    });

    if (transaction != NoError)
        return CreateFailedExecutionResult (transaction, "Element deletion transaction failed; deletion is not confirmed.");

    if (err != NoError)
        return CreateFailedExecutionResult (err, "Failed to delete elements.");

    // ACAPI_Element_Delete returning NoError does not guarantee anything was actually
    // deleted - confirmed live that it silently no-ops (no error, nothing removed) for
    // at least one real case: an element on a hidden layer. Read every target back; if
    // any of them still resolve, the delete did not really happen for that one, same
    // "verify before trusting the SDK's own return code" principle applied everywhere
    // else this session.
    for (const API_Guid& guid : guids) {
        API_Elem_Head head = {};
        head.guid = guid;
        if (ACAPI_Element_GetHeader (&head) != NoError) continue; // genuinely gone, as expected
        GS::UniString reason = "it still exists after the delete call reported no error";
        API_Attribute layerAttr = {};
        layerAttr.header.typeID = API_LayerID;
        layerAttr.header.index = head.layer;
        if (ACAPI_Attribute_Get (&layerAttr) == NoError && (layerAttr.header.flags & APILay_Hidden) != 0) {
            reason = "it is on a hidden layer; Archicad silently refuses to delete elements on a hidden layer via the API instead of returning an error. Unhide the layer first, or delete it manually in Archicad";
        }
        return CreateErrorResponse (APIERR_GENERAL, GS::UniString ("An element (guid ") + APIGuidToString (guid) +
            ") was not actually deleted even though the delete call succeeded - " + reason + ".");
    }

    return CreateSuccessfulExecutionResult ();
}

LockElementsCommand::LockElementsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String LockElementsCommand::GetName () const
{
    return "LockElements";
}

GS::Optional<GS::UniString> LockElementsCommand::GetInputParametersSchema () const
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

GS::Optional<GS::UniString> LockElementsCommand::GetRawResponseSchema () const
{
    return R"({
        "$ref": "#/ExecutionResult"
    })";
}

GS::ObjectState LockElementsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<GS::ObjectState> elements;
    parameters.Get ("elements", elements);

    GSErrCode err = NoError;

    const GSErrCode transactionError = ACAPI_CallUndoableCommand ("LockElementsCommand", [&]() {
        err = ACAPI_Grouping_Tool (elements.Transform<API_Guid> (GetGuidFromElementsArrayItem), APITool_Lock, nullptr);
        return err;
    });
    if (transactionError != NoError) return CreateErrorResponse (transactionError, "Native transaction failed; committed changes are not confirmed.");

    return err == NoError
        ? CreateSuccessfulExecutionResult ()
        : CreateFailedExecutionResult (err, "Failed to lock elements.");
}

UnlockElementsCommand::UnlockElementsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String UnlockElementsCommand::GetName () const
{
    return "UnlockElements";
}

GS::Optional<GS::UniString> UnlockElementsCommand::GetInputParametersSchema () const
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

GS::Optional<GS::UniString> UnlockElementsCommand::GetRawResponseSchema () const
{
    return R"({
        "$ref": "#/ExecutionResult"
    })";
}

GS::ObjectState UnlockElementsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<GS::ObjectState> elements;
    parameters.Get ("elements", elements);

    GSErrCode err = NoError;

    const GSErrCode transactionError = ACAPI_CallUndoableCommand ("UnlockElementsCommand", [&]() {
        err = ACAPI_Grouping_Tool (elements.Transform<API_Guid> (GetGuidFromElementsArrayItem), APITool_Unlock, nullptr);
        return err;
    });
    if (transactionError != NoError) return CreateErrorResponse (transactionError, "Native transaction failed; committed changes are not confirmed.");

    return err == NoError
        ? CreateSuccessfulExecutionResult ()
        : CreateFailedExecutionResult (err, "Failed to unlock elements.");
}

GetElementPreviewImageCommand::GetElementPreviewImageCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String GetElementPreviewImageCommand::GetName () const
{
    return "GetElementPreviewImage";
}

GS::Optional<GS::UniString> GetElementPreviewImageCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "elementId": {
                "$ref": "#/ElementId"
            },
            "imageType": {
                "type": "string",
                "description": "The type of the preview image. Default is 3D.",
                "enum": ["2D", "Section", "3D"]
            },
            "format": {
                "type": "string",
                "description": "The image format. Default is png.",
                "enum": ["png", "jpg"]
            },
            "width": {
                "type": "integer",
                "description": "The width of the preview image in pixels. Default is 128."
            },
            "height": {
                "type": "integer",
                "description": "The height of the preview image in pixels. Default is 128."
            }
        },
        "additionalProperties": false,
        "required": [
            "elementId"
        ]
    })";
}

GS::Optional<GS::UniString> GetElementPreviewImageCommand::GetRawResponseSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "previewImage": {
                "type": "string",
                "description": "The base64 encoded preview image."
            }
        },
        "additionalProperties": false,
        "required": [
            "previewImage"
        ]
    })";
}

GS::ObjectState GetElementPreviewImageCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    API_VisualOverriddenImage image = {};
    image.view = APIImage_Model3D;
    GS::UniString imageTypeStr;
    if (parameters.Get ("imageType", imageTypeStr)) {
        if (imageTypeStr == "2D") {
            image.view = APIImage_Model2D;
        } else if (imageTypeStr == "Section") {
            image.view = APIImage_Section;
        } else if (imageTypeStr == "3D") {
            image.view = APIImage_Model3D;
        } else {
            return CreateErrorResponse (APIERR_BADPARS, "Invalid imageType parameter.");
        }
    }

    NewDisplay::NativeImage::Encoding encoding = NewDisplay::NativeImage::Encoding::PNG;
    GS::UniString formatStr;
    if (parameters.Get ("format", formatStr)) {
        if (formatStr == "png") {
            encoding = NewDisplay::NativeImage::Encoding::PNG;
        } else if (formatStr == "jpg") {
            encoding = NewDisplay::NativeImage::Encoding::JPEG;
        } else {
            return CreateErrorResponse (APIERR_BADPARS, "Invalid format parameter.");
        }
    }

    UInt32 width = 128;
    UInt32 height = 128;
    parameters.Get ("width", width);
    parameters.Get ("height", height);

    NewDisplay::NativeImage nativeImage (width, height, 32, nullptr);
    image.nativeImagePtr = &nativeImage;
    GSErrCode err = ACAPI_GraphicalOverride_GetVisualOverriddenImage (GetGuidFromElementsArrayItem (parameters), &image);
    BMhFree (image.vectorImageHandle);
    if (err != NoError) {
        return CreateErrorResponse (err, "Failed to get element preview image.");
    }

    GS::MemoryOChannel32 memChannel (GS::MemoryOChannel32::BMAllocation);
    if (!nativeImage.Encode (memChannel, encoding)) {
        return CreateErrorResponse (APIERR_GENERAL, "Failed to encode element preview image.");
    }

    auto str = Base64Converter::Encode (memChannel.GetDestination (), memChannel.GetDataSize ());
    str.DeleteAll (GS::UniChar(char('\n')));
    return GS::ObjectState ("previewImage", str);
}

GetRoomImageCommand::GetRoomImageCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String GetRoomImageCommand::GetName () const
{
    return "GetRoomImage";
}

GS::Optional<GS::UniString> GetRoomImageCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "zoneId": {
                "$ref": "#/ElementId"
            },
            "format": {
                "type": "string",
                "description": "The image format. Default is png.",
                "enum": ["png", "jpg"]
            },
            "width": {
                "type": "integer",
                "description": "The width of the preview image in pixels. Default is 256.",
                "minimum": 1, "maximum": 4096
            },
            "height": {
                "type": "integer",
                "description": "The height of the preview image in pixels. Default is 256.",
                "minimum": 1, "maximum": 4096
            },
            "offset": {
                "type": "number",
                "description": "Offset of the clip polygon from the edge of the zone. Default is 0.001."
            },
            "scale": {
                "type": "number",
                "description": "Scale of the view (e.g. 0.005 for 1:200). Default is 0.005.",
                "exclusiveMinimum": 0
            },
            "backgroundColor": {
                "$ref": "#/ColorRGB",
                "description": "Background color of the generated image. Default is white (1.0, 1.0, 1.0)."
            }
        },
        "additionalProperties": false,
        "required": [
            "zoneId"
        ]
    })";
}

GS::Optional<GS::UniString> GetRoomImageCommand::GetRawResponseSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "roomImage": {
                "type": "string",
                "description": "The base64 encoded room image."
            },
            "imageContext": {
                "type":"object","properties":{
                    "zoneId":{"$ref":"#/ElementId"},"widthPixels":{"type":"integer"},
                    "heightPixels":{"type":"integer"},"mimeType":{"type":"string"},
                    "scale":{"type":"number"},"clipOffsetMetres":{"type":"number"},
                    "source":{"const":"ACAPI_Element_GetRoomImage / APIImage_Model2D"},
                    "modificationStamp":{"type":"string"},"floorIndex":{"type":"integer"},
                    "pixelToProjectMappingAvailable":{"const":false}
                },"additionalProperties":false,
                "required":["zoneId","widthPixels","heightPixels","mimeType","scale","clipOffsetMetres","source","modificationStamp","floorIndex","pixelToProjectMappingAvailable"]
            }
        },
        "additionalProperties": false,
        "required": [
            "roomImage"
        ]
    })";
}

GS::ObjectState GetRoomImageCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    API_RoomImage image = {};
    image.roomGuid = GetGuidFromArrayItem ("zoneId", parameters);
    image.viewType = APIImage_Model2D;
    API_Element zone = {}; zone.header.guid = image.roomGuid;
    const GSErrCode zoneError = ACAPI_Element_Get (&zone);
    if (zoneError != NoError) return CreateErrorResponse (zoneError, "Cannot read the requested image zone.");
    if (GetElemTypeId (zone.header) != API_ZoneID) return CreateErrorResponse (APIERR_BADPARS, "Room image requires a Zone element.");

    NewDisplay::NativeImage::Encoding encoding = NewDisplay::NativeImage::Encoding::PNG;
    GS::UniString formatStr;
    if (parameters.Get ("format", formatStr)) {
        if (formatStr == "png") {
            encoding = NewDisplay::NativeImage::Encoding::PNG;
        } else if (formatStr == "jpg") {
            encoding = NewDisplay::NativeImage::Encoding::JPEG;
        } else {
            return CreateErrorResponse (APIERR_BADPARS, "Invalid format parameter.");
        }
    }

    UInt32 width = 256;
    UInt32 height = 256;
    parameters.Get ("width", width);
    parameters.Get ("height", height);

    image.offset = 0.001;
    parameters.Get ("offset", image.offset);

    image.scale = 0.005;
    parameters.Get ("scale", image.scale);
    if (width < 1 || width > 4096 || height < 1 || height > 4096 ||
        !std::isfinite (image.scale) || image.scale <= 0 || !std::isfinite (image.offset))
        return CreateErrorResponse (APIERR_BADPARS, "Image dimensions must be 1 to 4096 pixels, scale positive and clip offset finite.");

    image.backgroundColor = {1.0, 1.0, 1.0};
    GetColor(parameters, "backgroundColor", image.backgroundColor);

    NewDisplay::NativeImage nativeImage (width, height, 32, nullptr);
    image.nativeImagePtr = &nativeImage;
    GSErrCode err = ACAPI_Element_GetRoomImage (&image);
    if (err != NoError) {
        return CreateErrorResponse (err, "Failed to get room image.");
    }

    GS::MemoryOChannel32 memChannel (GS::MemoryOChannel32::BMAllocation);
    if (!nativeImage.Encode (memChannel, encoding)) {
        return CreateErrorResponse (APIERR_GENERAL, "Failed to encode room image.");
    }

    auto str = Base64Converter::Encode (memChannel.GetDestination (), memChannel.GetDataSize ());
    str.DeleteAll (GS::UniChar(char('\n')));
    GS::ObjectState context ("zoneId", CreateGuidObjectState (image.roomGuid),
        "widthPixels", width, "heightPixels", height,
        "mimeType", encoding == NewDisplay::NativeImage::Encoding::PNG ? "image/png" : "image/jpeg",
        "scale", image.scale, "clipOffsetMetres", image.offset,
        "source", "ACAPI_Element_GetRoomImage / APIImage_Model2D",
        "modificationStamp", GS::UniString (std::to_string (zone.header.modiStamp).c_str ()),
        "floorIndex", zone.header.floorInd, "pixelToProjectMappingAvailable", false);
    return GS::ObjectState ("roomImage", str, "imageContext", context);
}


// A bounded scan of the current database. Offsets are positions in the sorted
// candidate list, not result counts. Restart pagination after project edits.
GS::Optional<GS::UniString> GetElementContextCommand::GetInputParametersSchema () const
{
    return R"({"type":"object","properties":{
        "elementType":{"$ref":"#/ElementType"},
        "floorIndex":{"type":"integer"},"layerIndex":{"type":"integer"},
        "zoneId":{"$ref":"#/ElementId","description":"Restrict candidates to native zone relations, not inferred containment. Requires AC26+; zone and elements are read in the current database."},
        "propertyFilters":{"type":"array","minItems":1,"maxItems":16,"description":"All predicates must match. Only scalar string, boolean, integer and real properties; no display-string or enum comparison. Unavailable/undefined values do not match, including NotEquals. Numeric values use the property's native API units.","items":{
            "type":"object","properties":{"propertyId":{"$ref":"#/PropertyId"},
                "operator":{"type":"string","enum":["Equals","NotEquals","GreaterThan","LessThan"]},
                "value":{"type":["string","boolean","number"]},
                "tolerance":{"type":"number","minimum":0,"description":"Numeric equality tolerance, default zero. Ordering comparisons ignore the equality band only by rejecting this field."}},
            "required":["propertyId","operator","value"],"additionalProperties":false,
            "allOf":[{"if":{"properties":{"operator":{"enum":["GreaterThan","LessThan"]}},"required":["operator"]},
                "then":{"properties":{"value":{"type":"number"}},"not":{"required":["tolerance"]}}},
                {"if":{"required":["tolerance"]},"then":{"properties":{"value":{"type":"number"}}}}]
        }},
        "nativeFieldFilters":{"type":"array","minItems":1,"maxItems":16,"description":"All predicates must match. Native struct fields (dimensions, offsets, etc.), not Property Manager properties - use propertyFilters for those. Field availability depends on elementType; an unsupported name is rejected with the supported list for that type.","items":{
            "type":"object","properties":{"field":{"type":"string"},
                "operator":{"type":"string","enum":["Equals","NotEquals","GreaterThan","LessThan"]},
                "value":{"type":"number"},
                "tolerance":{"type":"number","minimum":0,"description":"Equality tolerance, default zero. Ordering comparisons ignore the equality band only by rejecting this field."}},
            "required":["field","operator","value"],"additionalProperties":false,
            "allOf":[{"if":{"properties":{"operator":{"enum":["GreaterThan","LessThan"]}},"required":["operator"]},"then":{"not":{"required":["tolerance"]}}}]
        }},
        "classificationItemId":{"type":"object","properties":{"guid":{"type":"string","format":"uuid"}},"required":["guid"],"additionalProperties":false,"description":"Exact directly assigned classification item, not descendants."},
        "renovationStatus":{"type":"string","enum":["Existing","New","Demolished"]},
        "offset":{"type":"integer","minimum":0},
        "limit":{"type":"integer","minimum":1,"maximum":100},
        "scanLimit":{"type":"integer","minimum":1,"maximum":1000},
        "includeDetails":{"type":"boolean"},
        "includeBounds":{"type":"boolean","description":"Return native axis-aligned project bounds in metres for each selected element."},
        "bounds3D":{"type":"object","description":"Native bounding-box overlap in project metres, not exact solid intersection.","properties":{
            "xMin":{"type":"number"},"yMin":{"type":"number"},"zMin":{"type":"number"},
            "xMax":{"type":"number"},"yMax":{"type":"number"},"zMax":{"type":"number"}
        },"required":["xMin","yMin","zMin","xMax","yMax","zMax"],"additionalProperties":false},
        "bounds2D":{"type":"object","description":"Axis-aligned project XY region in metres; native bounding-box overlap, not exact geometry intersection.","properties":{
            "xMin":{"type":"number"},"yMin":{"type":"number"},"xMax":{"type":"number"},"yMax":{"type":"number"}
        },"required":["xMin","yMin","xMax","yMax"],"additionalProperties":false}
    },"required":["elementType"],"additionalProperties":false,"not":{"required":["bounds2D","bounds3D"]}})";
}

GS::Optional<GS::UniString> GetElementContextCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{
        "elements":{"type":"array","items":{"type":"object","properties":{
            "elementId":{"$ref":"#/ElementId"},"type":{"type":"string"},
            "floorIndex":{"type":"integer"},"layerIndex":{"type":"integer"},"modificationStamp":{"type":"string"},
            "bounds3D":{"type":"object","properties":{"xMin":{"type":"number"},"yMin":{"type":"number"},"zMin":{"type":"number"},
                "xMax":{"type":"number"},"yMax":{"type":"number"},"zMax":{"type":"number"}},
                "required":["xMin","yMin","zMin","xMax","yMax","zMax"],"additionalProperties":false}
        },"required":["elementId","type","floorIndex","layerIndex"],"additionalProperties":false}},
        "details":{"type":"object"},"errors":{"type":"array","items":{"type":"object"}},
        "candidateCount":{"type":"integer"},"scannedCount":{"type":"integer"},
        "nextOffset":{"type":"integer"},"hasMore":{"type":"boolean"},
        "scope":{"type":"string"},"pagination":{"type":"string"},"databaseId":{"$ref":"#/DatabaseId"},
        "candidateSource":{"type":"string","enum":["NativeTypeEnumeration","NativeZoneRelations"]},"boundsUnits":{"const":"metres"}
    },"required":["elements","errors","candidateCount","scannedCount","nextOffset","hasMore","scope","pagination"],"additionalProperties":false})";
}

GS::ObjectState GetElementContextCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& processControl) const
{
    GS::UniString typeName;
    parameters.Get ("elementType", typeName);
    const API_ElemTypeID type = GetElementTypeFromNonLocalizedName (typeName);
    if (type == API_ZombieElemID)
        return CreateErrorResponse (APIERR_BADPARS, "A valid elementType is required.");
    API_DatabaseInfo currentDatabase = {};
    const GSErrCode databaseError = ACAPI_Database_GetCurrentDatabase (&currentDatabase);
    if (databaseError != NoError) return CreateErrorResponse (databaseError, "Cannot establish query database context.");
    GS::Array<GS::ObjectState> propertyFilters;
    parameters.Get ("propertyFilters", propertyFilters);
    if (propertyFilters.GetSize () > 16) return CreateErrorResponse (APIERR_BADPARS, "At most 16 property predicates are supported.");
    GS::Array<API_Guid> propertyIds;
    for (const auto& filter : propertyFilters) {
        API_PropertyDefinition definition;
        definition.guid = GetGuidFromArrayItem ("propertyId", filter);
        const GSErrCode definitionError = ACAPI_Property_GetPropertyDefinition (definition);
        if (definitionError != NoError) return CreateErrorResponse (definitionError, "Cannot resolve property filter definition.");
        if (definition.collectionType != API_PropertySingleCollectionType)
            return CreateErrorResponse (APIERR_NOTSUPPORTED, "Property predicates require scalar properties; lists and enumerations are not compared as display strings.");
        GS::UniString operation, textValue; bool boolValue = false; Int32 intValue = 0; double numberValue = 0, tolerance = 0;
        filter.Get ("operator", operation); filter.Get ("tolerance", tolerance);
        const bool ordered = operation == "GreaterThan" || operation == "LessThan";
        if ((operation != "Equals" && operation != "NotEquals" && !ordered) || !std::isfinite (tolerance) || tolerance < 0 || (ordered && filter.Contains ("tolerance")))
            return CreateErrorResponse (APIERR_BADPARS, "Invalid property operator or tolerance.");
        bool valid = false;
        switch (definition.valueType) {
            case API_PropertyStringValueType: valid = filter.Get ("value", textValue) && !ordered && !filter.Contains ("tolerance"); break;
            case API_PropertyBooleanValueType: valid = filter.Get ("value", boolValue) && !ordered && !filter.Contains ("tolerance"); break;
            case API_PropertyIntegerValueType: valid = filter.Get ("value", intValue); break;
            case API_PropertyRealValueType: valid = filter.Get ("value", numberValue) && std::isfinite (numberValue); break;
            default: break;
        }
        if (!valid) return CreateErrorResponse (APIERR_BADPARS, "Property predicate value/operator does not match the native scalar type.");
        propertyIds.Push (definition.guid);
    }

    GS::Array<GS::ObjectState> nativeFieldFilters;
    parameters.Get ("nativeFieldFilters", nativeFieldFilters);
    if (nativeFieldFilters.GetSize () > 16) return CreateErrorResponse (APIERR_BADPARS, "At most 16 native field predicates are supported.");
    std::vector<double (*) (const API_Element&)> nativeFieldReaders;
    for (const auto& filter : nativeFieldFilters) {
        GS::UniString fieldName, operation; double tolerance = 0;
        filter.Get ("field", fieldName); filter.Get ("operator", operation); filter.Get ("tolerance", tolerance);
        const bool ordered = operation == "GreaterThan" || operation == "LessThan";
        if ((operation != "Equals" && operation != "NotEquals" && !ordered) || !std::isfinite (tolerance) || tolerance < 0 || (ordered && filter.Contains ("tolerance")))
            return CreateErrorResponse (APIERR_BADPARS, "Invalid native field operator or tolerance.");
        double value = 0;
        if (!filter.Get ("value", value) || !std::isfinite (value))
            return CreateErrorResponse (APIERR_BADPARS, "Native field predicate value must be a finite number.");
        const auto reader = FindNativeFieldFilterReader (type, fieldName);
        if (reader == nullptr) {
            const GS::UniString available = ListNativeFieldFilterNames (type);
            return CreateErrorResponse (APIERR_BADPARS, GS::UniString ("Unsupported nativeFieldFilters field '") + fieldName + "' for elementType '" + typeName + "'. " +
                (available.IsEmpty () ? GS::UniString ("No native fields are filterable for this type.") : GS::UniString ("Supported fields: ") + available + "."));
        }
        nativeFieldReaders.push_back (reader);
    }

    Int32 offset = 0, limit = 50, scanLimit = 500, floor = 0, layer = 0;
    parameters.Get ("offset", offset);
    parameters.Get ("limit", limit);
    parameters.Get ("scanLimit", scanLimit);
    const bool filterFloor = parameters.Get ("floorIndex", floor);
    const bool filterLayer = parameters.Get ("layerIndex", layer);
    GS::UniString renovation;
    const bool filterRenovation = parameters.Get ("renovationStatus", renovation);
    if (filterRenovation && renovation != "Existing" && renovation != "New" && renovation != "Demolished")
        return CreateErrorResponse (APIERR_BADPARS, "Unknown renovation status.");
    const API_RenovationStatusType desiredRenovation = renovation == "Existing" ? API_ExistingStatus : renovation == "New" ? API_NewStatus : API_DemolishedStatus;
    const auto* classification = parameters.Get ("classificationItemId");
    API_Guid classificationGuid = APINULLGuid;
    if (classification != nullptr) {
        classificationGuid = GetGuidFromObjectState (*classification);
        API_ClassificationItem definition = {}; definition.guid = classificationGuid;
        const GSErrCode classificationError = ACAPI_Classification_GetClassificationItem (definition);
        if (classificationError != NoError) return CreateErrorResponse (classificationError, "Classification filter item is unavailable in this project.");
    }
    bool includeDetails = false;
    parameters.Get ("includeDetails", includeDetails);
    bool includeBounds = false; parameters.Get ("includeBounds", includeBounds);
    if (offset < 0 || limit < 1 || limit > 100 || scanLimit < 1 || scanLimit > 1000)
        return CreateErrorResponse (APIERR_BADPARS, "Invalid pagination limits.");
    const GS::ObjectState* bounds = parameters.Get ("bounds2D");
    const bool boundsAre3D = parameters.Contains ("bounds3D");
    if (bounds != nullptr && boundsAre3D) return CreateErrorResponse (APIERR_BADPARS, "Choose bounds2D or bounds3D, not both.");
    if (boundsAre3D) bounds = parameters.Get ("bounds3D");
    double xMin = 0, yMin = 0, xMax = 0, yMax = 0, zMin = 0, zMax = 0;
    if (bounds != nullptr) {
        if (!bounds->Get ("xMin", xMin) || !bounds->Get ("yMin", yMin) || !bounds->Get ("xMax", xMax) || !bounds->Get ("yMax", yMax) ||
            !std::isfinite (xMin) || !std::isfinite (yMin) || !std::isfinite (xMax) || !std::isfinite (yMax) || xMax < xMin || yMax < yMin)
            return CreateErrorResponse (APIERR_BADPARS, "Invalid bounds2D region.");
        if (boundsAre3D && (!bounds->Get ("zMin", zMin) || !bounds->Get ("zMax", zMax) || !std::isfinite (zMin) || !std::isfinite (zMax) || zMax < zMin))
            return CreateErrorResponse (APIERR_BADPARS, "Invalid bounds3D height interval.");
    }
    GS::Array<API_Guid> nativeIds;
    const auto* zoneId = parameters.Get ("zoneId");
    if (zoneId != nullptr) {
#ifdef ServerMainVers_2600
        API_Elem_Head zone = {}; zone.guid = GetGuidFromObjectState (*zoneId);
        GSErrCode err = ACAPI_Element_GetHeader (&zone);
        if (err != NoError) return CreateErrorResponse (err, "Cannot read query zone.");
        if (GetElemTypeId (zone) != API_ZoneID) return CreateErrorResponse (APIERR_BADPARS, "zoneId must identify a native zone.");
        API_RoomRelation relation = {};
        const GS::OnExit cleanup ([&] () { ACAPI_DisposeRoomRelationHdls (&relation); });
        err = ACAPI_Element_GetRelations (zone.guid, API_ElemType (type), &relation);
        if (err != NoError) return CreateErrorResponse (err, "Cannot query native zone relations for this element type.");
        GS::HashSet<API_Guid> seen;
        relation.elementsGroupedByType.Enumerate ([&] (const API_ElemType& relatedType, const GS::Array<API_Guid>& relatedIds) {
            if (relatedType.typeID != type) return;
            for (const auto& id : relatedIds) if (!seen.Contains (id)) { nativeIds.Push (id); seen.Add (id); }
        });
#else
        return CreateErrorResponse (APIERR_NOTSUPPORTED, "Native zone relation filtering requires AC26 or newer.");
#endif
    } else {
        const GSErrCode err = ACAPI_Element_GetElemList (type, &nativeIds);
        if (err != NoError) return CreateErrorResponse (err, "Cannot enumerate the current database.");
    }
    std::vector<API_Guid> ids;
    for (const auto& guid : nativeIds) ids.push_back (guid);
    std::sort (ids.begin (), ids.end (), [](const API_Guid& a, const API_Guid& b) {
        return APIGuidToString (a) < APIGuidToString (b);
    });
    GS::ObjectState response ("scope", "CurrentDatabase", "pagination", "SortedGuidOffsetRestartAfterEdits");
    response.Add ("databaseId", CreateGuidObjectState (DatabaseIdResolver::Instance ().GetIdOfDatabase (currentDatabase)));
    response.Add ("candidateSource", zoneId != nullptr ? "NativeZoneRelations" : "NativeTypeEnumeration");
    response.Add ("boundsUnits", "metres");
    const auto& add = response.AddList<GS::ObjectState> ("elements");
    const auto& addError = response.AddList<GS::ObjectState> ("errors");
    GS::Array<GS::ObjectState> selected;
    Int32 cursor = offset, scanned = 0;
    while (cursor < static_cast<Int32> (ids.size ()) && scanned < scanLimit && selected.GetSize () < static_cast<USize> (limit)) {
        API_Elem_Head head = {};
        head.guid = ids[cursor++];
        ++scanned;
        const GSErrCode getError = ACAPI_Element_GetHeader (&head);
        if (getError != NoError) {
            GS::ObjectState failure = CreateElementIdObjectState (head.guid);
            failure.Add ("error", *CreateErrorResponse (getError, "Cannot read candidate header.").Get ("error"));
            addError (failure);
            continue;
        }
        if ((filterFloor && head.floorInd != floor) || (filterLayer && GetAttributeIndex (head.layer) != layer)) continue;
        if (filterRenovation && head.renovationStatus != desiredRenovation) continue;
        if (!nativeFieldFilters.IsEmpty ()) {
            API_Element full = {};
            full.header.guid = head.guid;
            const GSErrCode fullError = ACAPI_Element_Get (&full);
            if (fullError != NoError) {
                GS::ObjectState failure = CreateElementIdObjectState (head.guid);
                failure.Add ("error", *CreateErrorResponse (fullError, "Cannot evaluate candidate native fields.").Get ("error"));
                addError (failure); continue;
            }
            bool matchesAll = true;
            for (UIndex index = 0; index < nativeFieldFilters.GetSize (); ++index) {
                const double actual = nativeFieldReaders[index] (full);
                if (!std::isfinite (actual)) { matchesAll = false; break; }
                const auto& filter = nativeFieldFilters[index];
                GS::UniString operation; filter.Get ("operator", operation);
                double wanted = 0, tolerance = 0;
                filter.Get ("value", wanted); filter.Get ("tolerance", tolerance);
                const bool equal = std::abs (actual - wanted) <= tolerance;
                const bool matches = operation == "Equals" ? equal : operation == "NotEquals" ? !equal :
                    operation == "GreaterThan" ? actual > wanted : actual < wanted;
                if (!matches) { matchesAll = false; break; }
            }
            if (!matchesAll) continue;
        }
        if (classification != nullptr) {
            GS::Array<GS::Pair<API_Guid, API_Guid>> assignments;
            const GSErrCode classificationError = ACAPI_Element_GetClassificationItems (head.guid, assignments);
            if (classificationError != NoError) {
                GS::ObjectState failure = CreateElementIdObjectState (head.guid);
                failure.Add ("error", *CreateErrorResponse (classificationError, "Cannot evaluate candidate classifications.").Get ("error"));
                addError (failure); continue;
            }
            bool matches = false;
            for (const auto& assignment : assignments) if (assignment.second == classificationGuid) matches = true;
            if (!matches) continue;
        }
        if (!propertyIds.IsEmpty ()) {
            GS::Array<API_Property> values;
            const GSErrCode propertyError = ACAPI_Element_GetPropertyValuesByGuid (head.guid, propertyIds, values);
            if (propertyError != NoError) {
                GS::ObjectState failure = CreateElementIdObjectState (head.guid);
                failure.Add ("error", *CreateErrorResponse (propertyError, "Cannot evaluate candidate properties.").Get ("error"));
                addError (failure); continue;
            }
            bool matchesAll = true;
            for (UIndex index = 0; index < propertyFilters.GetSize (); ++index) {
                const API_Property* actual = nullptr;
                for (const auto& value : values) if (value.definition.guid == propertyIds[index]) { actual = &value; break; }
                if (actual == nullptr || actual->status == API_Property_NotEvaluated) {
                    GS::ObjectState failure = CreateElementIdObjectState (head.guid);
                    failure.Add ("error", *CreateErrorResponse (APIERR_BADPROPERTY, "A requested candidate property was not returned or evaluated.").Get ("error"));
                    addError (failure); matchesAll = false; break;
                }
                if (actual->status == API_Property_NotAvailable ||
                    actual->value.variantStatus != API_VariantStatusNormal) { matchesAll = false; break; }
                const auto& filter = propertyFilters[index];
                GS::UniString operation; filter.Get ("operator", operation);
                const auto& value = actual->value.singleVariant.variant;
                bool equal = false, greater = false, less = false;
                if (actual->definition.valueType == API_PropertyStringValueType) {
                    GS::UniString wanted; filter.Get ("value", wanted); equal = value.uniStringValue == wanted;
                } else if (actual->definition.valueType == API_PropertyBooleanValueType) {
                    bool wanted = false; filter.Get ("value", wanted); equal = value.boolValue == wanted;
                } else {
                    double wanted = 0, tolerance = 0;
                    if (actual->definition.valueType == API_PropertyIntegerValueType) { Int32 integer = 0; filter.Get ("value", integer); wanted = integer; }
                    else filter.Get ("value", wanted);
                    filter.Get ("tolerance", tolerance);
                    const double number = actual->definition.valueType == API_PropertyIntegerValueType ? static_cast<double> (value.intValue) : value.doubleValue;
                    if (!std::isfinite (number)) { matchesAll = false; break; }
                    equal = std::abs (number-wanted) <= tolerance; greater = number > wanted; less = number < wanted;
                }
                const bool matches = operation == "Equals" ? equal : operation == "NotEquals" ? !equal : operation == "GreaterThan" ? greater : less;
                if (!matches) { matchesAll = false; break; }
            }
            if (!matchesAll) continue;
        }
        API_Box3D box = {};
        if (bounds != nullptr || includeBounds) {
            const GSErrCode boundsError = ACAPI_Element_CalcBounds (&head, &box);
            if (boundsError != NoError) {
                GS::ObjectState failure = CreateElementIdObjectState (head.guid);
                failure.Add ("error", *CreateErrorResponse (boundsError, "Cannot evaluate native bounds for this candidate.").Get ("error"));
                addError (failure); continue;
            }
            if (bounds != nullptr && (box.xMax < xMin || box.xMin > xMax || box.yMax < yMin || box.yMin > yMax ||
                (boundsAre3D && (box.zMax < zMin || box.zMin > zMax)))) continue;
        }
        GS::ObjectState entry = CreateElementIdObjectState (head.guid);
        selected.Push (entry);
        entry.Add ("type", GetElementTypeNonLocalizedName (GetElemTypeId (head)));
        entry.Add ("floorIndex", head.floorInd);
        entry.Add ("layerIndex", GetAttributeIndex (head.layer));
        entry.Add ("modificationStamp", GS::UniString (std::to_string (head.modiStamp).c_str ()));
        if (includeBounds) entry.Add ("bounds3D", GS::ObjectState ("xMin", box.xMin, "yMin", box.yMin, "zMin", box.zMin, "xMax", box.xMax, "yMax", box.yMax, "zMax", box.zMax));
        add (entry);
    }
    if (includeDetails && !selected.IsEmpty ())
        response.Add ("details", GetDetailsOfElementsCommand ().Execute (GS::ObjectState ("elements", selected), processControl));
    response.Add ("candidateCount", static_cast<Int32> (ids.size ()));
    response.Add ("scannedCount", scanned);
    response.Add ("nextOffset", cursor);
    response.Add ("hasMore", cursor < static_cast<Int32> (ids.size ()));
    return response;
}

GS::Optional<GS::UniString> FindDuplicateElementsCommand::GetInputParametersSchema () const
{
    return R"({"type":"object","properties":{
        "elementType":{"$ref":"#/ElementType"},
        "floorIndex":{"type":"integer"},"layerIndex":{"type":"integer"},
        "positionTolerance":{"type":"number","exclusiveMinimum":0,"description":"Metres. Maximum bounding-box center distance to treat elements as stacked/duplicated. Default 0.01 (1cm)."},
        "sizeTolerance":{"type":"number","exclusiveMinimum":0,"description":"Metres. Maximum per-axis bounding-box extent difference to treat elements as the same size. Default 0.01 (1cm)."},
        "offset":{"type":"integer","minimum":0},
        "scanLimit":{"type":"integer","minimum":1,"maximum":1000,"description":"How many candidates of this type to examine in this call, in stable sorted order. Default 500. Groups only ever contain elements examined within the same call; rerun with a later offset to cover more."}
    },"required":["elementType"],"additionalProperties":false})";
}

GS::Optional<GS::UniString> FindDuplicateElementsCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{
        "duplicateGroups":{"type":"array","items":{"type":"object","properties":{
            "elements":{"$ref":"#/Elements"},
            "center":{"$ref":"#/Coordinate3D"},
            "size":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}},"required":["x","y","z"],"additionalProperties":false}
        },"required":["elements","center","size"],"additionalProperties":false}},
        "errors":{"type":"array","items":{"type":"object"}},
        "candidateCount":{"type":"integer"},"scannedCount":{"type":"integer"},
        "nextOffset":{"type":"integer"},"hasMore":{"type":"boolean"},"boundsUnits":{"const":"metres"}
    },"required":["duplicateGroups","errors","candidateCount","scannedCount","nextOffset","hasMore"],"additionalProperties":false})";
}

GS::ObjectState FindDuplicateElementsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl&) const
{
    GS::UniString typeName;
    parameters.Get ("elementType", typeName);
    const API_ElemTypeID type = GetElementTypeFromNonLocalizedName (typeName);
    if (type == API_ZombieElemID)
        return CreateErrorResponse (APIERR_BADPARS, "A valid elementType is required.");

    Int32 offset = 0, scanLimit = 500, floor = 0, layer = 0;
    parameters.Get ("offset", offset);
    parameters.Get ("scanLimit", scanLimit);
    const bool filterFloor = parameters.Get ("floorIndex", floor);
    const bool filterLayer = parameters.Get ("layerIndex", layer);
    double positionTolerance = 0.01, sizeTolerance = 0.01;
    parameters.Get ("positionTolerance", positionTolerance);
    parameters.Get ("sizeTolerance", sizeTolerance);
    if (offset < 0 || scanLimit < 1 || scanLimit > 1000 || !std::isfinite (positionTolerance) || positionTolerance <= 0 ||
        !std::isfinite (sizeTolerance) || sizeTolerance <= 0)
        return CreateErrorResponse (APIERR_BADPARS, "Invalid pagination limits or tolerance.");

    GS::Array<API_Guid> nativeIds;
    const GSErrCode enumError = ACAPI_Element_GetElemList (type, &nativeIds);
    if (enumError != NoError) return CreateErrorResponse (enumError, "Cannot enumerate the current database.");
    std::vector<API_Guid> ids;
    for (const auto& guid : nativeIds) ids.push_back (guid);
    std::sort (ids.begin (), ids.end (), [](const API_Guid& a, const API_Guid& b) {
        return APIGuidToString (a) < APIGuidToString (b);
    });

    GS::ObjectState response;
    response.Add ("boundsUnits", "metres");
    const auto& addError = response.AddList<GS::ObjectState> ("errors");

    struct Group { std::vector<API_Guid> members; API_Box3D bounds; };
    std::vector<Group> groups;

    Int32 cursor = offset, scanned = 0;
    while (cursor < static_cast<Int32> (ids.size ()) && scanned < scanLimit) {
        const API_Guid candidateGuid = ids[cursor++];
        ++scanned;
        API_Elem_Head head = {};
        head.guid = candidateGuid;
        const GSErrCode headerError = ACAPI_Element_GetHeader (&head);
        if (headerError != NoError) {
            GS::ObjectState failure = CreateElementIdObjectState (candidateGuid);
            failure.Add ("error", *CreateErrorResponse (headerError, "Cannot read candidate header.").Get ("error"));
            addError (failure); continue;
        }
        if ((filterFloor && head.floorInd != floor) || (filterLayer && GetAttributeIndex (head.layer) != layer)) continue;

        API_Box3D box = {};
        const GSErrCode boundsError = ACAPI_Element_CalcBounds (&head, &box);
        if (boundsError != NoError) {
            GS::ObjectState failure = CreateElementIdObjectState (candidateGuid);
            failure.Add ("error", *CreateErrorResponse (boundsError, "Cannot evaluate native bounds for this candidate.").Get ("error"));
            addError (failure); continue;
        }

        const double centerX = (box.xMin + box.xMax) / 2, centerY = (box.yMin + box.yMax) / 2, centerZ = (box.zMin + box.zMax) / 2;
        const double sizeX = box.xMax - box.xMin, sizeY = box.yMax - box.yMin, sizeZ = box.zMax - box.zMin;

        bool placed = false;
        for (auto& group : groups) {
            const double gCenterX = (group.bounds.xMin + group.bounds.xMax) / 2, gCenterY = (group.bounds.yMin + group.bounds.yMax) / 2, gCenterZ = (group.bounds.zMin + group.bounds.zMax) / 2;
            const double gSizeX = group.bounds.xMax - group.bounds.xMin, gSizeY = group.bounds.yMax - group.bounds.yMin, gSizeZ = group.bounds.zMax - group.bounds.zMin;
            if (std::abs (centerX - gCenterX) <= positionTolerance && std::abs (centerY - gCenterY) <= positionTolerance && std::abs (centerZ - gCenterZ) <= positionTolerance &&
                std::abs (sizeX - gSizeX) <= sizeTolerance && std::abs (sizeY - gSizeY) <= sizeTolerance && std::abs (sizeZ - gSizeZ) <= sizeTolerance) {
                group.members.push_back (candidateGuid);
                placed = true;
                break;
            }
        }
        if (!placed) groups.push_back ({{candidateGuid}, box});
    }

    const auto& addGroup = response.AddList<GS::ObjectState> ("duplicateGroups");
    for (const auto& group : groups) {
        if (group.members.size () < 2) continue;
        GS::ObjectState groupEntry;
        const auto& members = groupEntry.AddList<GS::ObjectState> ("elements");
        for (const auto& guid : group.members) members (CreateElementIdObjectState (guid));
        groupEntry.Add ("center", Create3DCoordinateObjectState (API_Coord3D { (group.bounds.xMin + group.bounds.xMax) / 2, (group.bounds.yMin + group.bounds.yMax) / 2, (group.bounds.zMin + group.bounds.zMax) / 2 }));
        groupEntry.Add ("size", GS::ObjectState ("x", group.bounds.xMax - group.bounds.xMin, "y", group.bounds.yMax - group.bounds.yMin, "z", group.bounds.zMax - group.bounds.zMin));
        addGroup (groupEntry);
    }
    response.Add ("candidateCount", static_cast<Int32> (ids.size ()));
    response.Add ("scannedCount", scanned);
    response.Add ("nextOffset", cursor);
    response.Add ("hasMore", cursor < static_cast<Int32> (ids.size ()));
    return response;
}

GS::Optional<GS::UniString> ExplainElementCommand::GetInputParametersSchema () const
{
    return R"({"type":"object","properties":{
        "elements":{"type":"array","minItems":1,"maxItems":20,"items":{"type":"object","properties":{"elementId":{"$ref":"#/ElementId"}},"required":["elementId"],"additionalProperties":false}},
        "minimumWallOpeningArea":{"type":"number","minimum":0,"description":"Square metres, passed through to the native quantities. Default 0."},
        "classificationSystemIds":{"$ref":"#/ClassificationSystemIds","description":"Optional. If given, includes each element's classification in these systems. Omitted entirely from the result if not given - this command does not enumerate all classification systems in the project itself."},
        "propertyIds":{"$ref":"#/PropertyIds","description":"Optional. If given, includes each element's values for these specific properties. Omitted entirely from the result if not given - there is no 'every property' mode, since most are irrelevant to most elements."}
    },"required":["elements"],"additionalProperties":false})";
}

GS::Optional<GS::UniString> ExplainElementCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{
        "elements":{"type":"array","items":{"type":"object","properties":{
            "elementId":{"$ref":"#/ElementId"},
            "details":{"type":"object","description":"See GetDetailsOfElements."},
            "quantities":{"type":"object","description":"See GetNativeQuantities."},
            "relations":{"type":"object","description":"See GetRelationsOfElements."},
            "classifications":{"type":"object","description":"See GetClassificationsOfElements. Present only if classificationSystemIds was given."},
            "properties":{"type":"array","description":"See GetPropertyValuesOfElements. Present only if propertyIds was given."}
        },"required":["elementId","details","quantities","relations"],"additionalProperties":false}}
    },"required":["elements"],"additionalProperties":false})";
}

GS::ObjectState ExplainElementCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& processControl) const
{
    GS::Array<GS::ObjectState> elements;
    parameters.Get ("elements", elements);
    if (elements.IsEmpty () || elements.GetSize () > 20) return CreateErrorResponse (APIERR_BADPARS, "Supply 1 to 20 elements.");

    const GS::ObjectState elementsInput ("elements", elements);

    const GS::ObjectState detailsResponse = GetDetailsOfElementsCommand ().Execute (elementsInput, processControl);
    GS::Array<GS::ObjectState> detailsOfElements;
    detailsResponse.Get ("detailsOfElements", detailsOfElements);

    GS::ObjectState quantitiesInput = elementsInput;
    double minimumWallOpeningArea = 0;
    if (parameters.Get ("minimumWallOpeningArea", minimumWallOpeningArea)) quantitiesInput.Add ("minimumWallOpeningArea", minimumWallOpeningArea);
    const GS::ObjectState quantitiesResponse = GetNativeQuantitiesCommand ().Execute (quantitiesInput, processControl);
    GS::Array<GS::ObjectState> quantities;
    quantitiesResponse.Get ("quantities", quantities);

    const GS::ObjectState relationsResponse = GetRelationsOfElementsCommand ().Execute (elementsInput, processControl);
    GS::Array<GS::ObjectState> relations;
    relationsResponse.Get ("relations", relations);

    const GS::ObjectState* classificationSystemIds = parameters.Get ("classificationSystemIds");
    GS::Array<GS::ObjectState> elementClassifications;
    if (classificationSystemIds != nullptr) {
        GS::ObjectState classificationsInput = elementsInput;
        classificationsInput.Add ("classificationSystemIds", *classificationSystemIds);
        const GS::ObjectState classificationsResponse = GetClassificationsOfElementsCommand ().Execute (classificationsInput, processControl);
        classificationsResponse.Get ("elementClassifications", elementClassifications);
    }

    const GS::ObjectState* propertyIds = parameters.Get ("propertyIds");
    GS::Array<GS::ObjectState> propertyValuesForElements;
    if (propertyIds != nullptr) {
        GS::ObjectState propertiesInput = elementsInput;
        propertiesInput.Add ("properties", *propertyIds);
        const GS::ObjectState propertiesResponse = GetPropertyValuesOfElementsCommand ().Execute (propertiesInput, processControl);
        propertiesResponse.Get ("propertyValuesForElements", propertyValuesForElements);
    }

    GS::ObjectState response;
    const auto& add = response.AddList<GS::ObjectState> ("elements");
    for (UIndex i = 0; i < elements.GetSize (); ++i) {
        GS::ObjectState entry = CreateElementIdObjectState (GetGuidFromArrayItem ("elementId", elements[i]));
        if (i < detailsOfElements.GetSize ()) entry.Add ("details", detailsOfElements[i]);
        if (i < quantities.GetSize ()) entry.Add ("quantities", quantities[i]);
        if (i < relations.GetSize ()) entry.Add ("relations", relations[i]);
        if (classificationSystemIds != nullptr && i < elementClassifications.GetSize ()) entry.Add ("classifications", elementClassifications[i]);
        if (propertyIds != nullptr && i < propertyValuesForElements.GetSize ()) entry.Add ("properties", propertyValuesForElements[i]);
        add (entry);
    }
    return response;
}

GS::Optional<GS::UniString> TransformElementsCommand::GetInputParametersSchema () const
{
    return R"({"type":"object","properties":{
        "elements":{"type":"array","minItems":1,"maxItems":1000,"uniqueItems":true,
            "items":{"type":"object","properties":{"elementId":{"$ref":"#/ElementId"}},"required":["elementId"],"additionalProperties":false}},
        "operation":{"type":"string","enum":["Move","Rotate","Mirror","Resize"]},
        "copy":{"type":"boolean"},
        "repeatCount":{"type":"integer","minimum":1,"maximum":100,"description":"Number of copies, default 1. Requires copy:true. Move uses multiples of vector; Rotate uses multiples of angle; Resize uses powers of factor. Total targets times count must not exceed 1000."},
        "requireAllGroupMembers":{"type":"boolean","default":false,"description":"Allow grouped targets only if every member of each root group is explicitly included. Does not change Suspend Groups mode."},
        "vector":{"$ref":"#/Coordinate3D"},"origin":{"$ref":"#/Coordinate2D"},
        "axisStart":{"$ref":"#/Coordinate2D"},"axisEnd":{"$ref":"#/Coordinate2D"},
        "angle":{"type":"number","description":"Counterclockwise radians in project XY."},
        "factor":{"type":"number","exclusiveMinimum":0,"description":"Positive native planar resize factor."}
    },"required":["elements","operation"],"additionalProperties":false,
    "allOf":[{"if":{"required":["repeatCount"]},"then":{"properties":{"copy":{"const":true}},"required":["copy"]}},{"if":{"properties":{"operation":{"const":"Mirror"}},"required":["operation"]},"then":{"properties":{"repeatCount":{"const":1}}}}],
    "oneOf":[
        {"properties":{"operation":{"const":"Move"}},"required":["vector"],"not":{"anyOf":[{"required":["origin"]},{"required":["angle"]},{"required":["axisStart"]},{"required":["axisEnd"]},{"required":["factor"]}]}},
        {"properties":{"operation":{"const":"Rotate"}},"required":["origin","angle"],"not":{"anyOf":[{"required":["vector"]},{"required":["axisStart"]},{"required":["axisEnd"]},{"required":["factor"]}]}},
        {"properties":{"operation":{"const":"Mirror"}},"required":["axisStart","axisEnd"],"not":{"anyOf":[{"required":["vector"]},{"required":["origin"]},{"required":["angle"]},{"required":["factor"]}]}},
        {"properties":{"operation":{"const":"Resize"}},"required":["origin","factor"],"not":{"anyOf":[{"required":["vector"]},{"required":["axisStart"]},{"required":["axisEnd"]},{"required":["angle"]}]}}
    ]})";
}

GS::Optional<GS::UniString> TransformElementsCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{
        "success":{"type":"boolean"},"status":{"type":"string"},"verification":{"type":"string"},
        "nativeReturnedElements":{"$ref":"#/Elements"},"copy":{"type":"boolean"},
        "dependentLinksMayChange":{"type":"boolean"},
        "resultMappingAvailable":{"type":"boolean"},
        "results":{"type":"array","items":{"type":"object","properties":{
            "repetition":{"type":"integer"},"sourceElementId":{"$ref":"#/ElementId"},"resultElementId":{"$ref":"#/ElementId"},
            "status":{"type":"string"},"error":{"type":"object"}
        },"required":["sourceElementId","status"],"additionalProperties":false}}
    },"required":["success","status","verification","nativeReturnedElements","copy","dependentLinksMayChange","resultMappingAvailable","results"],"additionalProperties":false})";
}

GS::ObjectState TransformElementsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl&) const
{
    GS::Array<GS::ObjectState> requested;
    parameters.Get ("elements", requested);
    if (requested.IsEmpty () || requested.GetSize () > 1000)
        return CreateErrorResponse (APIERR_BADPARS, "Supply between 1 and 1000 explicit elements; current selection is never used.");
    GS::Array<API_Neig> native;
    GS::HashSet<API_Guid> unique;
    GS::HashSet<API_Guid> rootGroups;
    bool requireAllGroupMembers = false;
    parameters.Get ("requireAllGroupMembers", requireAllGroupMembers);
    for (const auto& entry : requested) {
        const API_Guid guid = GetGuidFromArrayItem ("elementId", entry);
        if (guid == APINULLGuid || unique.Contains (guid))
            return CreateErrorResponse (APIERR_BADPARS, "Element IDs must be valid and unique.");
        API_Elem_Head head = {};
        head.guid = guid;
        const GSErrCode err = ACAPI_Element_GetHeader (&head);
        if (err != NoError) return CreateErrorResponse (err, "Cannot read a requested element.");
        if (!ACAPI_Element_Filter (guid, APIFilt_IsEditable | APIFilt_InMyWorkspace | APIFilt_HasAccessRight))
            return CreateErrorResponse (APIERR_BADPARS, "A requested element is not editable in the current project context.");
        API_Guid group = APINULLGuid;
        if (ACAPI_Grouping_GetGroup (guid, &group) == NoError && group != APINULLGuid) {
            if (!requireAllGroupMembers)
                return CreateErrorResponse (APIERR_BADPARS, "Grouped elements require requireAllGroupMembers and an explicit complete member list.");
            API_Guid root = APINULLGuid;
            const GSErrCode groupError = ACAPI_Grouping_GetRootGroup (group, &root);
            if (groupError != NoError || root == APINULLGuid)
                return CreateErrorResponse (groupError != NoError ? groupError : APIERR_BADID, "Cannot resolve root group.");
            rootGroups.Add (root);
        }
        unique.Add (guid);
        native.Push (API_Neig (guid));
    }
    for (const auto& group : rootGroups) {
        GS::Array<API_Guid> members;
        const GSErrCode err = ACAPI_Grouping_GetAllGroupedElems (group, &members);
        if (err != NoError) return CreateErrorResponse (err, "Cannot enumerate the complete root group.");
        for (const auto& member : members) {
            API_Elem_Head memberHead = {}; memberHead.guid = member;
            const GSErrCode readError = ACAPI_Element_GetHeader (&memberHead);
            if (readError != NoError) return CreateErrorResponse (readError, "Cannot inspect a root-group member.");
            if (GetElemTypeId (memberHead) != API_GroupID && !unique.Contains (member))
                return CreateErrorResponse (APIERR_BADPARS, "Request omits a root-group member; no transformation was attempted.");
        }
    }
    const GS::Array<API_Neig> sources = native;
    API_EditPars edit = {};
    bool copy = false;
    parameters.Get ("copy", copy);
    edit.withDelete = !copy;
    Int32 repeatCount = 1;
    parameters.Get ("repeatCount", repeatCount);
    if (repeatCount < 1 || repeatCount > 100 || sources.GetSize () * repeatCount > 1000 ||
        (parameters.Contains ("repeatCount") && !copy))
        return CreateErrorResponse (APIERR_BADPARS, "Repeat requires copy:true, a count from 1 to 100 and at most 1000 total results.");
    GS::UniString operation;
    parameters.Get ("operation", operation);
    if (operation == "Move") {
        const auto* vector = parameters.Get ("vector");
        if (vector == nullptr) return CreateErrorResponse (APIERR_BADPARS, "Move requires vector.");
        edit.typeID = APIEdit_Drag;
        edit.endC = Get3DCoordinateFromObjectState (*vector);
    } else if (operation == "Mirror") {
        const auto* first = parameters.Get ("axisStart");
        const auto* last = parameters.Get ("axisEnd");
        if (first == nullptr || last == nullptr) return CreateErrorResponse (APIERR_BADPARS, "Mirror requires axisStart and axisEnd.");
        const auto a = Get2DCoordinateFromObjectState (*first), b = Get2DCoordinateFromObjectState (*last);
        if (std::hypot (b.x - a.x, b.y - a.y) < 1e-9)
            return CreateErrorResponse (APIERR_BADPARS, "Mirror axis has zero length.");
        edit.typeID = APIEdit_Mirror;
        edit.begC = {a.x, a.y, 0};
        edit.endC = {b.x, b.y, 0};
    } else if (operation == "Rotate" || operation == "Resize") {
        const auto* origin = parameters.Get ("origin");
        if (origin == nullptr) return CreateErrorResponse (APIERR_BADPARS, "Rotate and Resize require origin.");
        edit.origC = Get2DCoordinateFromObjectState (*origin);
        if (operation == "Rotate") {
            double angle;
            if (!parameters.Get ("angle", angle) || !std::isfinite (angle)) return CreateErrorResponse (APIERR_BADPARS, "Rotate requires a finite angle in radians.");
            edit.typeID = APIEdit_Rotate;
            edit.begC = {edit.origC.x + 1, edit.origC.y, 0};
            edit.endC = {edit.origC.x + std::cos (angle), edit.origC.y + std::sin (angle), 0};
        } else {
            double factor;
            if (!parameters.Get ("factor", factor) || !std::isfinite (factor) || factor <= 0) return CreateErrorResponse (APIERR_BADPARS, "Resize requires a finite positive factor.");
            edit.typeID = APIEdit_Resize;
            edit.begC = {edit.origC.x, edit.origC.y, 0};
            edit.endC = {edit.origC.x + 1, edit.origC.y, 0};
            edit.endC2 = {edit.origC.x + factor, edit.origC.y, 0};
        }
    } else return CreateErrorResponse (APIERR_BADPARS, "Unsupported transform operation.");
    const double coordinates[] = {edit.origC.x, edit.origC.y, edit.begC.x, edit.begC.y, edit.begC.z,
        edit.endC.x, edit.endC.y, edit.endC.z, edit.endC2.x, edit.endC2.y, edit.endC2.z};
    for (const double coordinate : coordinates)
        if (!std::isfinite (coordinate)) return CreateErrorResponse (APIERR_BADPARS, "All transformation coordinates must be finite.");
    if (operation == "Mirror" && repeatCount != 1)
        return CreateErrorResponse (APIERR_BADPARS, "Repeated copies support Move, Rotate and Resize; Mirror has one result per source.");
    std::vector<API_EditPars> repetitions;
    for (Int32 repetition=1; repetition<=repeatCount; ++repetition) {
        API_EditPars repeated = edit;
        if (operation == "Move") {
            repeated.endC.x *= repetition; repeated.endC.y *= repetition; repeated.endC.z *= repetition;
        } else if (operation == "Rotate") {
            double angle = 0; parameters.Get ("angle", angle); angle *= repetition;
            repeated.endC = {edit.origC.x + std::cos (angle), edit.origC.y + std::sin (angle), 0};
        } else if (operation == "Resize") {
            double factor = 1; parameters.Get ("factor", factor);
            const double repeatedFactor = std::pow (factor, repetition);
            if (!std::isfinite (repeatedFactor) || repeatedFactor <= 0)
                return CreateErrorResponse (APIERR_BADPARS, "Repeated resize factor is outside finite positive range.");
            repeated.endC2.x = edit.origC.x + repeatedFactor;
        }
        if (!std::isfinite (repeated.endC.x) || !std::isfinite (repeated.endC.y) || !std::isfinite (repeated.endC.z) || !std::isfinite (repeated.endC2.x))
            return CreateErrorResponse (APIERR_BADPARS, "Repeated transformation exceeds finite coordinate range.");
        repetitions.push_back (repeated);
    }
    bool mappingAvailable = true;
    GS::Array<API_Neig> allResults;
    const GSErrCode transaction = ACAPI_CallUndoableCommand ("Transform Elements", [&] () -> GSErrCode {
        for (const auto& repeated : repetitions) {
            native = sources;
            const GSErrCode err = ACAPI_Element_Edit (&native, repeated);
            if (err != NoError) return err;
            mappingAvailable &= native.GetSize () == sources.GetSize ();
            for (const auto& result : native) allResults.Push (result);
        }
        return NoError;
    });
    if (transaction != NoError) return CreateErrorResponse (transaction, "Native transformation transaction failed; committed changes are not confirmed.");
    native = allResults;
    bool allAccepted = mappingAvailable;
    GS::ObjectState response ("verification", "nativeResultIdsReadBack", "copy", copy,
        "dependentLinksMayChange", true, "resultMappingAvailable", mappingAvailable);
    const auto& add = response.AddList<GS::ObjectState> ("nativeReturnedElements");
    for (const auto& item : native) if (item.guid != APINULLGuid) add (CreateElementIdObjectState (item.guid));
    const auto& addResult = response.AddList<GS::ObjectState> ("results");
    if (mappingAvailable) {
        for (UIndex index=0; index<native.GetSize (); ++index) {
            const API_Guid sourceGuid = sources[index % sources.GetSize ()].guid;
            GS::ObjectState result ("sourceElementId", CreateGuidObjectState (sourceGuid), "repetition", static_cast<Int32> (index / sources.GetSize () + 1));
            API_Elem_Head head = {}; head.guid = native[index].guid;
            GSErrCode resultError = head.guid == APINULLGuid ? APIERR_REFUSEDPAR : ACAPI_Element_GetHeader (&head);
            if (resultError == NoError && copy && head.guid == sourceGuid) resultError = APIERR_REFUSEDPAR;
            if (head.guid != APINULLGuid) result.Add ("resultElementId", CreateGuidObjectState (head.guid));
            if (resultError == NoError) result.Add ("status", "executionAccepted");
            else {
                allAccepted = false;
                result.Add ("status", "notConfirmed");
                result.Add ("error", *CreateErrorResponse (resultError, "Native transformation did not return a readable result identity; for copying it must differ from the source.").Get ("error"));
            }
            addResult (result);
        }
    }
    response.Add ("success", allAccepted);
    response.Add ("status", !mappingAvailable ? "outcomeUnknown" : allAccepted ? "executionAccepted" : "partialOrRefused");
    return response;
}

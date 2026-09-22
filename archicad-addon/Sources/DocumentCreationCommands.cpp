#include "DocumentCreationCommands.hpp"

#include "MigrationHelper.hpp"
#include <cstring>
#include <cmath>
#include "GSProcessControl.hpp"

namespace {

GS::ObjectState CreateDatabasesResponse (const GS::Array<GS::ObjectState>& databases)
{
    GS::ObjectState response;
    const auto& databasesList = response.AddList<GS::ObjectState> ("databases");
    for (const auto& database : databases) {
        databasesList (database);
    }
    return response;
}

GS::ObjectState CreateExecutionResultsResponse (const GS::Array<GS::ObjectState>& executionResults)
{
    GS::ObjectState response;
    const auto& results = response.AddList<GS::ObjectState> ("executionResults");
    for (const auto& result : executionResults) {
        results (result);
    }
    return response;
}

GS::ObjectState CreateNavigatorItemsResponse (const GS::Array<GS::ObjectState>& navigatorItems)
{
    GS::ObjectState response;
    const auto& list = response.AddList<GS::ObjectState> ("navigatorItems");
    for (const auto& item : navigatorItems) {
        list (item);
    }
    return response;
}

bool GetItems (const GS::ObjectState& parameters, const char* fieldName, GS::Array<GS::ObjectState>& outItems, GS::ObjectState& errorResponse)
{
    if (!parameters.Get (fieldName, outItems)) {
        errorResponse = CreateErrorResponse (APIERR_BADPARS, GS::UniString::Printf ("Missing required array field '%s'.", fieldName));
        return false;
    }
    return true;
}

GSErrCode GetLayoutDatabaseGuids (GS::Array<API_Guid>& guids)
{
    GS::Array<API_DatabaseUnId> dbases;
    const GSErrCode err = ACAPI_Database_GetLayoutDatabases (nullptr, &dbases);
    if (err == NoError) {
        for (const auto& db : dbases) {
            API_DatabaseInfo info = {};
            info.typeID = APIWind_LayoutID;
            info.databaseUnId = db;
            guids.Push (DatabaseIdResolver::Instance ().GetIdOfDatabase (info));
        }
    }
    return err;
}

GS::Optional<API_Guid> FindNewLayoutDatabaseGuid (const GS::Array<API_Guid>& before, GSErrCode& error)
{
    GS::Array<API_Guid> after;
    error = GetLayoutDatabaseGuids (after);
    if (error != NoError) return {};
    GS::Optional<API_Guid> found;
    for (const auto& guid : after) {
        if (!before.Contains (guid)) {
            if (found.HasValue ()) { error = APIERR_GENERAL; return {}; }
            found = guid;
        }
    }
    return found;
}

GS::Optional<API_DatabaseInfo> FindMasterLayoutDatabaseByName (const GS::UniString& masterLayoutName, GSErrCode& error)
{
    GS::Optional<API_DatabaseInfo> foundMasterLayout;
    GS::Array<API_DatabaseUnId> dbases;
    error = ACAPI_Database_GetMasterLayoutDatabases (nullptr, &dbases);
    if (error == NoError) {
        for (const auto& db : dbases) {
            API_DatabaseInfo candidate = {};
            candidate.typeID = APIWind_MasterLayoutID;
            candidate.databaseUnId = db;
            error = ACAPI_Window_GetDatabaseInfo (&candidate);
            if (error != NoError) return {};
            if (GS::UniString (candidate.name) == masterLayoutName) {
                if (foundMasterLayout.HasValue ()) { error = APIERR_BADPARS; return {}; }
                foundMasterLayout = candidate;
            }
        }
    }

    return foundMasterLayout;
}

GSErrCode FindLayoutSibling (API_Guid parentGuid, const GS::UniString& layoutName, GS::Optional<API_DatabaseInfo>& found)
{
    API_NavigatorItem parent = {};
    GSErrCode err = ACAPI_Navigator_GetNavigatorItem (&parentGuid, &parent);
    if (err != NoError) return err;
    if (parent.mapId != API_LayoutMap || (parent.itemType != API_BookNavItem && parent.itemType != API_SubSetNavItem)) return APIERR_BADPARS;
    GS::Array<API_NavigatorItem> children;
    err = ACAPI_Navigator_GetNavigatorChildrenItems (&parent, &children);
    if (err != NoError) return err;
    for (const auto& child : children) {
        if (GS::UniString (child.uName) != layoutName) continue;
        if (found.HasValue () || child.itemType != API_LayoutNavItem) return APIERR_BADPARS;
        API_DatabaseInfo database = child.db;
        err = ACAPI_Window_GetDatabaseInfo (&database);
        if (err != NoError) return err;
        found = database;
    }
    return NoError;
}

GSErrCode GetLayoutInfoForDatabase (const API_DatabaseUnId& databaseUnId, API_LayoutInfo& layoutInfo)
{
    BNZeroMemory (&layoutInfo, sizeof (layoutInfo));
    return ACAPI_Navigator_GetLayoutSets (&layoutInfo, const_cast<API_DatabaseUnId*> (&databaseUnId));
}

}

CreateDetailsCommand::CreateDetailsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String CreateDetailsCommand::GetName () const
{
    return "CreateDetails";
}

GS::Optional<GS::UniString> CreateDetailsCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "detailsData": {
                "type": "array",
                "items": {
                    "type": "object",
                    "properties": {
                        "name": { "type": "string", "minLength": 1 },
                        "referenceId": { "type": "string", "minLength": 1 }
                    },
                    "additionalProperties": false,
                    "required": ["name", "referenceId"]
                }
            }
        },
        "additionalProperties": false,
        "required": ["detailsData"]
    })";
}

GS::Optional<GS::UniString> CreateDetailsCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{"databases":{"$ref":"#/DatabaseIdsOrErrors"}},"additionalProperties":false,"required":["databases"]})";
}

GS::ObjectState CreateDetailsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl&) const
{
    GS::Array<GS::ObjectState> items;
    GS::ObjectState errorResponse;
    if (!GetItems (parameters, "detailsData", items, errorResponse)) {
        return errorResponse;
    }

    GS::Array<GS::ObjectState> databases;
    for (const auto& item : items) {
        API_DatabaseInfo dbInfo = {};
        dbInfo.typeID = APIWind_DetailID;
        SetUCharProperty (&item, "name", dbInfo.name);
        SetUCharProperty (&item, "referenceId", dbInfo.ref);

        const GSErrCode err = ACAPI_Database_NewDatabase (&dbInfo);
        if (err != NoError) {
            databases.Push (CreateErrorResponse (err, "Failed to create detail database."));
            continue;
        }

        databases.Push (CreateDatabaseIdObjectState (DatabaseIdResolver::Instance ().GetIdOfDatabase (dbInfo)));
    }
    return CreateDatabasesResponse (databases);
}

CreateWorksheetsCommand::CreateWorksheetsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String CreateWorksheetsCommand::GetName () const
{
    return "CreateWorksheets";
}

GS::Optional<GS::UniString> CreateWorksheetsCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "worksheetsData": {
                "type": "array",
                "items": {
                    "type": "object",
                    "properties": {
                        "name": { "type": "string", "minLength": 1 },
                        "referenceId": { "type": "string", "minLength": 1 }
                    },
                    "additionalProperties": false,
                    "required": ["name", "referenceId"]
                }
            }
        },
        "additionalProperties": false,
        "required": ["worksheetsData"]
    })";
}

GS::Optional<GS::UniString> CreateWorksheetsCommand::GetRawResponseSchema () const
{
    return CreateDetailsCommand ().GetRawResponseSchema ();
}

GS::ObjectState CreateWorksheetsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl&) const
{
    GS::Array<GS::ObjectState> items;
    GS::ObjectState errorResponse;
    if (!GetItems (parameters, "worksheetsData", items, errorResponse)) {
        return errorResponse;
    }

    GS::Array<GS::ObjectState> databases;
    for (const auto& item : items) {
        API_DatabaseInfo dbInfo = {};
        dbInfo.typeID = APIWind_WorksheetID;
        SetUCharProperty (&item, "name", dbInfo.name);
        SetUCharProperty (&item, "referenceId", dbInfo.ref);

        const GSErrCode err = ACAPI_Database_NewDatabase (&dbInfo);
        if (err != NoError) {
            databases.Push (CreateErrorResponse (err, "Failed to create worksheet database."));
            continue;
        }

        databases.Push (CreateDatabaseIdObjectState (DatabaseIdResolver::Instance ().GetIdOfDatabase (dbInfo)));
    }
    return CreateDatabasesResponse (databases);
}

GS::Optional<GS::UniString> CreateMasterLayoutsCommand::GetInputParametersSchema () const
{
    return R"({"type":"object","properties":{"masters":{"type":"array","minItems":1,"maxItems":100,"items":{
        "type":"object","properties":{
            "name":{"type":"string","minLength":1,"maxLength":255},
            "widthMillimetres":{"type":"number","exclusiveMinimum":0},"heightMillimetres":{"type":"number","exclusiveMinimum":0},
            "leftMarginMillimetres":{"type":"number","minimum":0},"rightMarginMillimetres":{"type":"number","minimum":0},
            "topMarginMillimetres":{"type":"number","minimum":0},"bottomMarginMillimetres":{"type":"number","minimum":0},
            "ifExists":{"type":"string","enum":["Error","ReuseIfMatching"],"description":"Default Error. Reuse requires an exact name and matching paper size/margins, tolerance 0.000001 mm. Omitted margins mean zero. Existing masters are never modified."}
        },"required":["name","widthMillimetres","heightMillimetres"],"additionalProperties":false
    }}},"required":["masters"],"additionalProperties":false})";
}

GS::Optional<GS::UniString> CreateMasterLayoutsCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{"results":{"type":"array","items":{
        "type":"object","properties":{"inputIndex":{"type":"integer"},"status":{"type":"string","enum":["created","reused","failed","incomplete"]},
            "databaseId":{"$ref":"#/DatabaseId"},"error":{"type":"object"},"verification":{"type":"string"}},
        "required":["inputIndex","status"],"additionalProperties":false
    }}},"required":["results"],"additionalProperties":false})";
}

GS::ObjectState CreateMasterLayoutsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl&) const
{
    GS::Array<GS::ObjectState> masters; parameters.Get ("masters", masters);
    if (masters.IsEmpty () || masters.GetSize () > 100) return CreateErrorResponse (APIERR_BADPARS, "Supply 1 to 100 master layouts.");
    GS::ObjectState response;
    const auto& add = response.AddList<GS::ObjectState> ("results");
    Int32 inputIndex = -1;
    for (const auto& item : masters) {
        GS::ObjectState row ("inputIndex", ++inputIndex);
        const auto fail = [&] (GSErrCode error, const char* message, bool created) {
            row.Add ("status", created ? "incomplete" : "failed");
            row.Add ("error", *CreateErrorResponse (error, message).Get ("error")); add (row);
        };
        GS::UniString name, policy = "Error";
        item.Get ("name", name); item.Get ("ifExists", policy);
        API_LayoutInfo desired = {};
        item.Get ("widthMillimetres", desired.sizeX); item.Get ("heightMillimetres", desired.sizeY);
        item.Get ("leftMarginMillimetres", desired.leftMargin); item.Get ("rightMarginMillimetres", desired.rightMargin);
        item.Get ("topMarginMillimetres", desired.topMargin); item.Get ("bottomMarginMillimetres", desired.bottomMargin);
        bool finite = true;
        for (double value : {desired.sizeX, desired.sizeY, desired.leftMargin, desired.rightMargin, desired.topMargin, desired.bottomMargin})
            finite &= std::isfinite (value) && value >= 0;
        if (name.IsEmpty () || name.GetLength () >= API_UniLongNameLen || !finite || desired.sizeX <= 0 || desired.sizeY <= 0 ||
            desired.leftMargin + desired.rightMargin >= desired.sizeX || desired.topMargin + desired.bottomMargin >= desired.sizeY ||
            (policy != "Error" && policy != "ReuseIfMatching")) {
            fail (APIERR_BADPARS, "Supply a valid name and finite positive paper size with nonnegative margins leaving printable area.", false); continue;
        }
        GSErrCode err = NoError;
        const auto existing = FindMasterLayoutDatabaseByName (name, err);
        if (err != NoError) { fail (err, "Master name lookup failed or is ambiguous.", false); continue; }
        API_DatabaseInfo database = {};
        bool created = false;
        if (existing.HasValue ()) {
            if (policy != "ReuseIfMatching") { fail (APIERR_BADPARS, "A master with this name already exists.", false); continue; }
            database = existing.Get ();
        } else {
            database.typeID = APIWind_MasterLayoutID;
            GS::ucscpy (database.name, name.ToUStr ());
            err = ACAPI_Database_NewDatabase (&database);
            if (err != NoError) { fail (err, "Native master database creation failed.", false); continue; }
            created = true;
        }
        row.Add ("databaseId", CreateGuidObjectState (DatabaseIdResolver::Instance ().GetIdOfDatabase (database)));
        API_LayoutInfo actual = {};
        const GS::OnExit dispose ([&] () { delete actual.customData; });
        err = ACAPI_Navigator_GetLayoutSets (&actual, &database.databaseUnId);
        if (err != NoError) { fail (err, "Cannot read master settings; retained database ID is returned.", created); continue; }
        if (created) {
            actual.sizeX = desired.sizeX; actual.sizeY = desired.sizeY;
            actual.leftMargin = desired.leftMargin; actual.rightMargin = desired.rightMargin;
            actual.topMargin = desired.topMargin; actual.bottomMargin = desired.bottomMargin;
            err = ACAPI_Navigator_ChangeLayoutSets (&actual, &database.databaseUnId);
            if (err != NoError) { fail (err, "Master exists but paper settings failed; inspect retained database ID before retrying.", true); continue; }
            delete actual.customData; actual = {};
            err = ACAPI_Navigator_GetLayoutSets (&actual, &database.databaseUnId);
            if (err != NoError) { fail (err, "Master changed but readback failed; inspect retained database ID.", true); continue; }
        }
        const auto equal = [] (double a, double b) { return std::isfinite (a) && std::abs (a-b) <= 1e-6; };
        if (!equal (actual.sizeX, desired.sizeX) || !equal (actual.sizeY, desired.sizeY) ||
            !equal (actual.leftMargin, desired.leftMargin) || !equal (actual.rightMargin, desired.rightMargin) ||
            !equal (actual.topMargin, desired.topMargin) || !equal (actual.bottomMargin, desired.bottomMargin)) {
            fail (APIERR_BADPARS, "Native master paper settings do not match the requested specification.", created); continue;
        }
        row.Add ("status", created ? "created" : "reused"); row.Add ("verification", "nativePaperGeometryReadBack"); add (row);
    }
    return response;
}

CreateLayoutCommand::CreateLayoutCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String CreateLayoutCommand::GetName () const
{
    return "CreateLayout";
}

GS::Optional<GS::UniString> CreateLayoutCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "layoutsData": {
                "type": "array",
                "minItems":1, "maxItems":100,
                "items": {
                    "type": "object",
                    "properties": {
                        "masterLayoutName":      { "type": "string", "minLength": 1, "maxLength":255 },
                        "masterNavigatorItemId": { "$ref": "#/NavigatorItemId" },
                        "layoutName":            { "type": "string", "minLength": 1, "maxLength":255 },
                        "createMissingMaster": {"type":"boolean","default":true,"description":"Legacy default true creates a master with native defaults if the name is missing. Set false for explicit project setup through CreateMasterLayouts. Any new master ID is returned in createdMasterLayouts, including later sheet failure."},
                        "ifExists": {"type":"string","enum":["Create","Error","ReuseIfMatching"],"description":"Default Create preserves legacy behaviour. Other policies resolve an exact same-name sibling under the parent. Reuse requires the same master and matching supplied numbering/display settings; existing sheets are never modified."},
                        "parentNavigatorItemId": { "$ref": "#/NavigatorItemId" },
                        "layoutParameters": {
                            "type": "object",
                            "description":"Sheet numbering/display settings. Paper size and margins belong to the master layout: use CreateMasterLayouts or SetLayoutSettings on that master first.",
                            "properties": {
                                "customLayoutNumber":       { "type": "string" },
                                "customLayoutNumbering":    { "type": "boolean" },
                                "doNotIncludeInNumbering":  { "type": "boolean" },
                                "displayMasterLayoutBelow": { "type": "boolean" }
                            },
                            "additionalProperties": false
                        }
                    },
                    "additionalProperties": false,
                    "required": ["layoutName"],
                    "oneOf":[{"required":["masterLayoutName"],"not":{"required":["masterNavigatorItemId"]}},
                             {"required":["masterNavigatorItemId"],"not":{"required":["masterLayoutName"]}}]
                }
            }
        },
        "additionalProperties": false,
        "required": ["layoutsData"]
    })";
}

GS::Optional<GS::UniString> CreateLayoutCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{
        "databases":{"type":"array","items":{"$ref":"#/DatabaseIdOrError"}},
        "createdMasterLayouts":{"type":"array","items":{"type":"object","properties":{
            "databaseId":{"$ref":"#/DatabaseId"},"inputIndex":{"type":"integer"}},"required":["databaseId","inputIndex"],"additionalProperties":false}},
        "reusedLayouts":{"type":"array","items":{"type":"object","properties":{
            "databaseId":{"$ref":"#/DatabaseId"},"inputIndex":{"type":"integer"}},"required":["databaseId","inputIndex"],"additionalProperties":false}}
    },"required":["databases","createdMasterLayouts","reusedLayouts"],"additionalProperties":false})";
}

GS::ObjectState CreateLayoutCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl&) const
{
    GS::Array<GS::ObjectState> items;
    GS::ObjectState errorResponse;
    if (!GetItems (parameters, "layoutsData", items, errorResponse)) {
        return errorResponse;
    }

    GS::Array<GS::ObjectState> databases, createdMasters, reusedLayouts;
    Int32 inputIndex = -1;
    for (const auto& item : items) {
        ++inputIndex;
        GS::UniString layoutName, masterName, collisionPolicy = "Create";
        item.Get ("layoutName", layoutName); item.Get ("masterLayoutName", masterName); item.Get ("ifExists", collisionPolicy);
        if (layoutName.IsEmpty () || layoutName.GetLength () >= API_UniLongNameLen || masterName.GetLength () >= API_UniLongNameLen ||
            item.Contains ("masterLayoutName") == item.Contains ("masterNavigatorItemId") ||
            (collisionPolicy != "Create" && collisionPolicy != "Error" && collisionPolicy != "ReuseIfMatching")) {
            databases.Push (CreateErrorResponse (APIERR_BADPARS, "Supply valid layout/master names, exactly one master identity, and a supported collision policy.")); continue;
        }
        API_Guid parentNavGuid = APINULLGuid;
        if (const auto* parentOS = item.Get ("parentNavigatorItemId")) {
            parentNavGuid = GetGuidFromObjectState (*parentOS);
            if (parentNavGuid == APINULLGuid) { databases.Push (CreateErrorResponse (APIERR_BADPARS, "Invalid layout parent identity.")); continue; }
        } else {
            API_NavigatorSet set = {}; set.mapId = API_LayoutMap; Int32 index = 0;
            const GSErrCode rootError = ACAPI_Navigator_GetNavigatorSet (&set, &index);
            if (rootError != NoError) { databases.Push (CreateErrorResponse (rootError, "Cannot resolve Layout Book root.")); continue; }
            parentNavGuid = set.rootGuid;
        }
        GS::Optional<API_DatabaseInfo> existingLayout;
        const GSErrCode siblingError = FindLayoutSibling (parentNavGuid, layoutName, existingLayout);
        if (siblingError != NoError) { databases.Push (CreateErrorResponse (siblingError, "Invalid parent or ambiguous layout sibling.")); continue; }
        if (existingLayout.HasValue () && collisionPolicy == "Error") {
            databases.Push (CreateErrorResponse (APIERR_BADPARS, "A same-name layout already exists under this parent.")); continue;
        }
        API_DatabaseInfo masterLayoutDbInfo = {};
        const auto* requestedLayoutParameters = item.Get ("layoutParameters");
        GS::UniString requestedNumber;
        API_LayoutInfo stringLimits = {};
        if (requestedLayoutParameters != nullptr && requestedLayoutParameters->Get ("customLayoutNumber", requestedNumber) &&
            std::strlen (requestedNumber.ToCStr ().Get ()) >= sizeof (stringLimits.customLayoutNumber)) {
            databases.Push (CreateErrorResponse (APIERR_BADPARS, "Layout number exceeds native byte capacity; no master or sheet was created.")); continue;
        }
        if (requestedLayoutParameters != nullptr && (requestedLayoutParameters->Contains ("horizontalSize") || requestedLayoutParameters->Contains ("verticalSize") ||
            requestedLayoutParameters->Contains ("leftMargin") || requestedLayoutParameters->Contains ("rightMargin") ||
            requestedLayoutParameters->Contains ("topMargin") || requestedLayoutParameters->Contains ("bottomMargin"))) {
            databases.Push (CreateErrorResponse (APIERR_BADPARS, "Paper geometry belongs to the master layout. Use CreateMasterLayouts or modify that master before creating sheets.")); continue;
        }

        const GS::ObjectState* masterNavItemOS = item.Get ("masterNavigatorItemId");
        if (masterNavItemOS != nullptr) {
            API_Guid masterNavGuid = GetGuidFromObjectState (*masterNavItemOS);
            API_NavigatorItem masterNavItem = {};
            const GSErrCode navErr = ACAPI_Navigator_GetNavigatorItem (&masterNavGuid, &masterNavItem);
            if (navErr != NoError) {
                databases.Push (CreateErrorResponse (navErr, "Failed to get master layout from masterNavigatorItemId."));
                continue;
            }
            masterLayoutDbInfo = masterNavItem.db;
            if (masterNavItem.itemType != API_MasterLayoutNavItem) {
                databases.Push (CreateErrorResponse (APIERR_BADPARS, "masterNavigatorItemId must identify a master layout.")); continue;
            }
        } else {
            GS::UniString masterLayoutName;
            item.Get ("masterLayoutName", masterLayoutName);

            GSErrCode masterLookupError = NoError;
            const auto existingMasterLayout = FindMasterLayoutDatabaseByName (masterLayoutName, masterLookupError);
            if (masterLookupError != NoError) {
                databases.Push (CreateErrorResponse (masterLookupError, "Cannot resolve an unambiguous master layout name.")); continue;
            }
            if (existingMasterLayout.HasValue ()) {
                masterLayoutDbInfo = existingMasterLayout.Get ();
            } else {
                if (masterLayoutName.IsEmpty ()) {
                    databases.Push (CreateErrorResponse (APIERR_BADPARS, "Either masterLayoutName or masterNavigatorItemId must be provided."));
                    continue;
                }
                bool createMissingMaster = true; item.Get ("createMissingMaster", createMissingMaster);
                if (!createMissingMaster || (existingLayout.HasValue () && collisionPolicy == "ReuseIfMatching")) {
                    databases.Push (CreateErrorResponse (APIERR_BADPARS, "Requested master does not exist; create it explicitly with CreateMasterLayouts.")); continue;
                }
                masterLayoutDbInfo.typeID = APIWind_MasterLayoutID;
                SetUCharProperty (&item, "masterLayoutName", masterLayoutDbInfo.name);

                const GSErrCode createMasterErr = ACAPI_Database_NewDatabase (&masterLayoutDbInfo);
                if (createMasterErr != NoError) {
                    databases.Push (CreateErrorResponse (createMasterErr, "Failed to create master layout."));
                    continue;
                }
                createdMasters.Push (GS::ObjectState ("databaseId", CreateGuidObjectState (DatabaseIdResolver::Instance ().GetIdOfDatabase (masterLayoutDbInfo)), "inputIndex", inputIndex));
            }
        }

        API_LayoutInfo layoutInfo = {};
#ifdef ServerMainVers_2600
        SetUCharProperty (&item, "layoutName", layoutInfo.layoutName);
#else
        SetCharProperty (&item, "layoutName", layoutInfo.layoutName);
#endif

        API_LayoutInfo masterLayoutInfo = {};
        const GS::OnExit masterDataCleanup ([&] () { delete masterLayoutInfo.customData; });
        const GSErrCode masterSettingsError = GetLayoutInfoForDatabase (masterLayoutDbInfo.databaseUnId, masterLayoutInfo);
        if (masterSettingsError != NoError) {
            databases.Push (CreateErrorResponse (masterSettingsError, "Cannot read master settings; no sheet was created. Any new master is listed in createdMasterLayouts.")); continue;
        }
        {
            layoutInfo.sizeX = masterLayoutInfo.sizeX;
            layoutInfo.sizeY = masterLayoutInfo.sizeY;
            layoutInfo.leftMargin = masterLayoutInfo.leftMargin;
            layoutInfo.topMargin = masterLayoutInfo.topMargin;
            layoutInfo.rightMargin = masterLayoutInfo.rightMargin;
            layoutInfo.bottomMargin = masterLayoutInfo.bottomMargin;
            layoutInfo.showMasterBelow = masterLayoutInfo.showMasterBelow;
        }

        const GS::ObjectState* layoutParamsOS = item.Get ("layoutParameters");
        if (layoutParamsOS != nullptr) {
            SetCharProperty (layoutParamsOS, "customLayoutNumber",      layoutInfo.customLayoutNumber);
            layoutParamsOS->Get ("customLayoutNumbering",    layoutInfo.customLayoutNumbering);
            layoutParamsOS->Get ("doNotIncludeInNumbering",  layoutInfo.doNotIncludeInNumbering);
            layoutParamsOS->Get ("displayMasterLayoutBelow", layoutInfo.showMasterBelow);
        }

        if (existingLayout.HasValue () && collisionPolicy == "ReuseIfMatching") {
            API_LayoutInfo actual = {};
            const GS::OnExit actualCleanup ([&] () { delete actual.customData; });
            const GSErrCode readError = GetLayoutInfoForDatabase (existingLayout->databaseUnId, actual);
            if (readError != NoError) { databases.Push (CreateErrorResponse (readError, "Cannot read existing sheet settings for reuse.")); continue; }
            bool matches = existingLayout->masterLayoutUnId == masterLayoutDbInfo.databaseUnId;
            if (layoutParamsOS != nullptr) {
                if (layoutParamsOS->Contains ("customLayoutNumber")) matches &= std::strcmp (actual.customLayoutNumber, layoutInfo.customLayoutNumber) == 0;
                if (layoutParamsOS->Contains ("customLayoutNumbering")) matches &= actual.customLayoutNumbering == layoutInfo.customLayoutNumbering;
                if (layoutParamsOS->Contains ("doNotIncludeInNumbering")) matches &= actual.doNotIncludeInNumbering == layoutInfo.doNotIncludeInNumbering;
                if (layoutParamsOS->Contains ("displayMasterLayoutBelow")) matches &= actual.showMasterBelow == layoutInfo.showMasterBelow;
            }
            if (!matches) { databases.Push (CreateErrorResponse (APIERR_BADPARS, "Existing sheet uses a different master or requested settings; it was not modified.")); continue; }
            const API_Guid existingGuid = DatabaseIdResolver::Instance ().GetIdOfDatabase (existingLayout.Get ());
            databases.Push (CreateDatabaseIdObjectState (existingGuid));
            reusedLayouts.Push (GS::ObjectState ("databaseId", CreateGuidObjectState (existingGuid), "inputIndex", inputIndex));
            continue;
        }

        GS::Array<API_Guid> before;
        const GSErrCode beforeError = GetLayoutDatabaseGuids (before);
        if (beforeError != NoError) {
            databases.Push (CreateErrorResponse (beforeError, "Cannot enumerate layouts before creation; no layout was created.")); continue;
        }
#ifdef ServerMainVers_2700
        const GSErrCode err = ACAPI_Navigator_CreateLayout (&layoutInfo, &masterLayoutDbInfo.databaseUnId,
            (parentNavGuid != APINULLGuid) ? &parentNavGuid : nullptr);
#else
        // AC25/26: parent navigator item not supported by CreateLayout API
        const GSErrCode err = ACAPI_Navigator_CreateLayout (&layoutInfo, &masterLayoutDbInfo.databaseUnId);
#endif
        if (err != NoError) {
            databases.Push (CreateErrorResponse (err, "Failed to create layout."));
            continue;
        }

        GSErrCode identityError = NoError;
        const auto newLayoutGuid = FindNewLayoutDatabaseGuid (before, identityError);
        if (newLayoutGuid.HasValue ()) {
            databases.Push (CreateDatabaseIdObjectState (newLayoutGuid.Get ()));
        } else {
            databases.Push (CreateErrorResponse (identityError != NoError ? identityError : APIERR_GENERAL,
                "Layout creation ran but exactly one new database identity could not be confirmed. Inspect before retrying; existing same-name layouts are not returned as new."));
        }
    }

    GS::ObjectState response = CreateDatabasesResponse (databases);
    const auto& mastersList = response.AddList<GS::ObjectState> ("createdMasterLayouts");
    for (const auto& master : createdMasters) mastersList (master);
    const auto& reusedList = response.AddList<GS::ObjectState> ("reusedLayouts");
    for (const auto& reused : reusedLayouts) reusedList (reused);
    return response;
}

CreateLayoutSubsetCommand::CreateLayoutSubsetCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String CreateLayoutSubsetCommand::GetName () const
{
    return "CreateLayoutSubset";
}

GS::Optional<GS::UniString> CreateLayoutSubsetCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "subsetsData": {
                "type": "array",
                "items": {
                    "type": "object",
                    "properties": {
                        "name":                  { "type": "string", "minLength": 1 },
                        "parentNavigatorItemId": { "$ref": "#/NavigatorItemId" },
                        "ownPrefix":             { "type": "string" },
                        "customNumber":          { "type": "string" },
                        "numberingStyle":        { "type": "string", "enum": ["Undefined", "abc", "ABC", "1", "01", "001", "0001", "noID"] },
                        "startAt":               { "type": "integer" },
                        "continueNumbering":     { "type": "boolean" },
                        "useUpperPrefix":        { "type": "boolean" },
                        "includeToIDSequence":   { "type": "boolean" },
                        "customNumbering":       { "type": "boolean" },
                        "addOwnPrefix":          { "type": "boolean" }
                    },
                    "additionalProperties": false,
                    "required": ["name"]
                }
            }
        },
        "additionalProperties": false,
        "required": ["subsetsData"]
    })";
}

GS::Optional<GS::UniString> CreateLayoutSubsetCommand::GetRawResponseSchema () const
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

GS::ObjectState CreateLayoutSubsetCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl&) const
{
    GS::Array<GS::ObjectState> items;
    GS::ObjectState errorResponse;
    if (!GetItems (parameters, "subsetsData", items, errorResponse)) {
        return errorResponse;
    }

    GS::Array<GS::ObjectState> navigatorItems;
    for (const auto& item : items) {
        API_SubSet subSet = {};
        GSErrCode err = ACAPI_Navigator_GetSubSetDefault (&subSet);
        if (err != NoError) {
            navigatorItems.Push (CreateErrorResponse (err, "Failed to get subset defaults."));
            continue;
        }

        SetUCharProperty (&item, "name", subSet.name);

        // ownPrefix — backward compat: if ownPrefix string given without explicit addOwnPrefix, set it true
        if (item.Contains ("ownPrefix")) {
            subSet.addOwnPrefix = true;
            SetUCharProperty (&item, "ownPrefix", subSet.ownPrefix);
        }
        bool addOwnPrefixExplicit = false;
        if (item.Get ("addOwnPrefix", addOwnPrefixExplicit)) {
            subSet.addOwnPrefix = addOwnPrefixExplicit;
        }

        // customNumber — backward compat: if customNumber string given without explicit customNumbering, set it true
        if (item.Contains ("customNumber")) {
            subSet.customNumbering = true;
            SetUCharProperty (&item, "customNumber", subSet.customNumber);
        }
        bool customNumberingExplicit = false;
        if (item.Get ("customNumbering", customNumberingExplicit)) {
            subSet.customNumbering = customNumberingExplicit;
        }

        // numberingStyle
        GS::UniString numberingStyleStr;
        if (item.Get ("numberingStyle", numberingStyleStr)) {
            if      (numberingStyleStr == "abc")  subSet.numberingStyle = API_NS_abc;
            else if (numberingStyleStr == "ABC")  subSet.numberingStyle = API_NS_ABC;
            else if (numberingStyleStr == "1")    subSet.numberingStyle = API_NS_1;
            else if (numberingStyleStr == "01")   subSet.numberingStyle = API_NS_01;
            else if (numberingStyleStr == "001")  subSet.numberingStyle = API_NS_001;
            else if (numberingStyleStr == "0001") subSet.numberingStyle = API_NS_0001;
            else if (numberingStyleStr == "noID") subSet.numberingStyle = API_NS_noID;
            else                                  subSet.numberingStyle = API_NS_Undefined;
        }

        item.Get ("startAt",           subSet.startAt);
        item.Get ("continueNumbering", subSet.continueNumbering);
        item.Get ("useUpperPrefix",    subSet.useUpperPrefix);

        const GS::ObjectState* parent = item.Get ("parentNavigatorItemId");
        const API_Guid* parentGuidPtr = nullptr;
        API_Guid parentGuid = APINULLGuid;
        if (parent != nullptr) {
            parentGuid = GetGuidFromObjectState (*parent);
            parentGuidPtr = &parentGuid;
        }

        err = ACAPI_Navigator_CreateSubSet (&subSet, parentGuidPtr);
        if (err != NoError) {
            navigatorItems.Push (CreateErrorResponse (err, "Failed to create subset."));
            continue;
        }

        navigatorItems.Push (CreateSuccessfulExecutionResult ());
    }
    return CreateNavigatorItemsResponse (navigatorItems);
}

CreateDrawingsCommand::CreateDrawingsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String CreateDrawingsCommand::GetName () const
{
    return "CreateDrawings";
}

GS::Optional<GS::UniString> CreateDrawingsCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "drawingsData": {
                "type": "array",
                "items": {
                    "type": "object",
                    "properties": {
                        "navigatorItemId": { "$ref": "#/NavigatorItemId" },
                        "layoutDatabaseId": { "$ref": "#/DatabaseId" },
                        "name": { "type": "string", "minLength": 1 },
                        "position": { "$ref": "#/Coordinate2D" },
                        "scale": { "type": "number", "exclusiveMinimum": 0.0, "description": "Drawing magnification relative to its source; 1 means original size. Defaults to 1." },
                        "angle": { "type": "number", "description": "Placed drawing rotation in radians. Omitted value uses drawing defaults." },
                        "modelOffset": { "$ref": "#/Coordinate2D", "description": "Model origin offset within the drawing, in native model coordinates." },
                        "clipPolygon": { "type": "array", "items": { "$ref": "#/Coordinate2D" }, "minItems": 3 }
                    },
                    "additionalProperties": false,
                    "required": ["navigatorItemId", "name", "position"]
                }
            }
        },
        "additionalProperties": false,
        "required": ["drawingsData"]
    })";
}

GS::Optional<GS::UniString> CreateDrawingsCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{"elements":{"$ref":"#/ElementIdsOrErrors"},"contextRestorationError":{"type":"object","description":"Creation results remain available, but restoring the original database failed."}},"additionalProperties":false,"required":["elements"]})";
}

// Tells whether a navigator item can be the source of a Drawing. Only viewpoints and the views
// saved from them can be placed - containers (folders, subsets, books, the project root), the
// Layout Book's own items (layouts, master layouts) and the navigator item of an already placed
// Drawing cannot. Views carry the item type of the viewpoint they were saved from, so the same
// check covers both the Project Map and the View Map.
static bool IsPlaceableAsDrawing (API_NavigatorItemTypeID itemType)
{
    switch (itemType) {
        case API_StoryNavItem:
        case API_SectionNavItem:
        case API_ElevationNavItem:
        case API_InteriorElevationNavItem:
        case API_DetailDrawingNavItem:
        case API_WorksheetDrawingNavItem:
        case API_DocumentFrom3DNavItem:
        case API_PerspectiveNavItem:
        case API_AxonometryNavItem:
        case API_ScheduleNavItem:
        case API_ListNavItem:
        case API_TextListNavItem:
        case API_TocNavItem:
            return true;
        default:
            return false;
    }
}

// Creates a single Drawing from a "drawingsData"-shaped item (navigatorItemId, name, position,
// scale, optional clipPolygon). Shared by CreateDrawingsCommand and ChangeDrawingLinkCommand,
// which synthesizes the same item shape from an existing Drawing's own current appearance.
static GS::ObjectState CreateOneDrawing (const GS::ObjectState& item, const API_DrawingType* source = nullptr, API_AddParType** titleParameters = nullptr, const API_ElementMemo* originalClip = nullptr)
{
    // The source has to be resolved and checked here, before anything is handed to
    // ACAPI_Element_Create: a navigatorItemId that is not a placeable viewpoint - one that
    // resolves to nothing, or to a folder, a layout, or an already placed Drawing's own
    // navigator item - terminates Archicad inside element creation instead of returning an
    // error.
    const GS::ObjectState* navigatorItemIdState = item.Get ("navigatorItemId");
    if (navigatorItemIdState == nullptr) {
        return CreateErrorResponse (APIERR_BADPARS, "Missing required field 'navigatorItemId'.");
    }
    const API_Guid sourceGuid = GetGuidFromObjectState (*navigatorItemIdState);
    if (sourceGuid == APINULLGuid) {
        return CreateErrorResponse (APIERR_BADPARS, "navigatorItemId is corrupt or missing.");
    }
    {
        API_NavigatorItem sourceItem = {};
        const GSErrCode navErr = ACAPI_Navigator_GetNavigatorItem (&sourceGuid, &sourceItem);
        if (navErr != NoError) {
            return CreateErrorResponse (navErr, "Failed to get navigator item from navigatorItemId.");
        }
        if (!IsPlaceableAsDrawing (sourceItem.itemType)) {
            return CreateErrorResponse (APIERR_BADID, "navigatorItemId is not a view or viewpoint that can be placed as a Drawing.");
        }
    }

    API_Element element = {};
#ifdef ServerMainVers_2600
    element.header.type   = API_DrawingID;
#else
    element.header.typeID = API_DrawingID;
#endif
    GSErrCode err = ACAPI_Element_GetDefaults (&element, nullptr);
    if (err != NoError) {
        return CreateErrorResponse (err, "Failed to get drawing defaults.");
    }

    element.drawing.drawingGuid = sourceGuid;
    SetCharProperty (&item, "name", element.drawing.name);
    element.drawing.nameType = APIName_CustomName;
    element.drawing.anchorPoint = APIAnc_MM;
    if (source != nullptr) {
        // Copy writable appearance fields explicitly; never copy cached data or output-only scale.
        element.header.layer = source->head.layer;
        element.drawing.isCutWithFrame = source->isCutWithFrame;
        element.drawing.nameType = source->nameType;
        element.drawing.numberingType = source->numberingType;
        std::memcpy (element.drawing.customNumber, source->customNumber, sizeof (element.drawing.customNumber));
        element.drawing.isInNumbering = source->isInNumbering;
        element.drawing.manualUpdate = source->manualUpdate;
        element.drawing.includeInAutoTextsAndIES = source->includeInAutoTextsAndIES;
        element.drawing.rasterizeDPI = source->rasterizeDPI;
        element.drawing.anchorPoint = source->anchorPoint;
        element.drawing.useOwnOrigoAsAnchor = source->useOwnOrigoAsAnchor;
        element.drawing.colorMode = source->colorMode;
        element.drawing.penTableUsageMode = source->penTableUsageMode;
        element.drawing.penTableIndex = source->penTableIndex;
        element.drawing.isTransparentBk = source->isTransparentBk;
        element.drawing.hasBorderLine = source->hasBorderLine;
        element.drawing.borderLineType = source->borderLineType;
        element.drawing.borderPen = source->borderPen;
        element.drawing.borderSize = source->borderSize;
        element.drawing.title = source->title;
        element.drawing.title.guid = APINULLGuid;
    }
    element.drawing.pos = Get2DCoordinateFromObjectState (*item.Get ("position"));
    if (!item.Get ("scale", element.drawing.ratio)) {
        element.drawing.ratio = 1.0;
    }
    // Set placement during creation; drawingScale is SDK output-only and comes from the source view.
    item.Get ("angle", element.drawing.angle);
    const GS::ObjectState* modelOffsetState = item.Get ("modelOffset");
    if (modelOffsetState != nullptr) {
        element.drawing.modelOffset = Get2DCoordinateFromObjectState (*modelOffsetState);
    }

    // Optional clip polygon — drop closing vertex if present, then build memo
    GS::Array<GS::ObjectState> clipCoords;
    item.Get ("clipPolygon", clipCoords);
    if (clipCoords.GetSize () > 1) {
        API_Coord first = Get2DCoordinateFromObjectState (clipCoords.GetFirst ());
        API_Coord last  = Get2DCoordinateFromObjectState (clipCoords.GetLast ());
        if (IsSame2DCoordinate (first, last))
            clipCoords.Pop ();
    }
    const Int32 nClip = (Int32) clipCoords.GetSize ();
    if (item.Contains ("clipPolygon") && nClip < 3) {
        return CreateErrorResponse (APIERR_BADPARS, "Drawing clip polygon needs at least three vertices after removing its closing duplicate.");
    }

    API_ElementMemo memo = {};
    if (nClip < 3 && source == nullptr) {
        // No clip polygon requested on a fresh create (not a relink, which intentionally
        // carries the original element's crop above). ACAPI_Element_GetDefaults filled
        // isCutWithFrame from the Drawing tool defaults, which carry over the crop of the
        // last manually placed Drawing - clear it explicitly so the new Drawing always
        // shows its full, unclipped extent (#651).
        element.drawing.isCutWithFrame = false;
        element.drawing.poly.nSubPolys = 0;
        element.drawing.poly.nCoords   = 0;
        element.drawing.poly.nArcs     = 0;
    }
    if (nClip >= 3) {
        element.drawing.isCutWithFrame = true;
        element.drawing.poly.nSubPolys = 1;
        element.drawing.poly.nCoords   = nClip + 1;
        element.drawing.poly.nArcs     = 0;
        memo.coords = reinterpret_cast<API_Coord**> (BMAllocateHandle ((nClip + 2) * sizeof (API_Coord), ALLOCATE_CLEAR, 0));
        memo.pends  = reinterpret_cast<Int32**>     (BMAllocateHandle (2 * sizeof (Int32), ALLOCATE_CLEAR, 0));
        if (memo.coords == nullptr || memo.pends == nullptr) {
            ACAPI_DisposeElemMemoHdls (&memo);
            return CreateErrorResponse (APIERR_MEMFULL, "Cannot allocate drawing clip polygon.");
        }
        if (memo.coords != nullptr && memo.pends != nullptr) {
            for (Int32 i = 0; i < nClip; ++i)
                (*memo.coords)[i + 1] = Get2DCoordinateFromObjectState (clipCoords[i]);
            (*memo.coords)[nClip + 1] = (*memo.coords)[1];
            (*memo.pends)[1] = nClip + 1;
        }
    }
    // Borrow the original title parameters for this synchronous call. The
    // caller's original memo retains ownership of all nested parameter arrays.
    const bool borrowClip=source!=nullptr && source->isCutWithFrame && originalClip!=nullptr;
    if (borrowClip) {
        // Original native contours and arc sweeps are passed without flattening.
        // Only the temporary clip allocated above belongs to this memo.
        BMKillHandle(reinterpret_cast<GSHandle*>(&memo.coords));
        BMKillHandle(reinterpret_cast<GSHandle*>(&memo.pends));
        element.drawing.poly=source->poly;
        memo.coords=originalClip->coords; memo.pends=originalClip->pends; memo.parcs=originalClip->parcs;
    }
    memo.params=titleParameters;
    err = ACAPI_Element_Create (&element, &memo);
    memo.params=nullptr;
    if (borrowClip) { memo.coords=nullptr; memo.pends=nullptr; memo.parcs=nullptr; }
    ACAPI_DisposeElemMemoHdls (&memo);

    if (err != NoError) {
        return CreateErrorResponse (err, "Failed to create drawing.");
    }
    return CreateElementIdObjectState (element.header.guid);
}

// Switches to the layout database identified by layoutDatabaseId (the same database-activation
// dance CreateDrawingsCommand and ChangeDrawingLinkCommand both need before creating a Drawing).
static GSErrCode ActivateLayoutDatabase (const API_Guid& layoutDatabaseGuid)
{
    API_DatabaseInfo targetDatabase = DatabaseIdResolver::Instance ().GetDatabaseWithId (layoutDatabaseGuid);
    GSErrCode err = ACAPI_Window_GetDatabaseInfo (&targetDatabase);
    if (err != NoError) {
        return err;
    }
    err = ACAPI_Database_ChangeCurrentDatabase (&targetDatabase);
    if (err != NoError) {
        return err;
    }
    API_WindowInfo windowInfo = targetDatabase;
    return ACAPI_Window_ChangeWindow (&windowInfo);
}

GS::ObjectState CreateDrawingsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl&) const
{
    GS::Array<GS::ObjectState> items;
    GS::ObjectState errorResponse;
    if (!GetItems (parameters, "drawingsData", items, errorResponse)) {
        return errorResponse;
    }

    API_DatabaseInfo startingDatabase = {};
    const GSErrCode startingDatabaseErr = ACAPI_Database_GetCurrentDatabase (&startingDatabase);
    if (startingDatabaseErr != NoError) return CreateErrorResponse (startingDatabaseErr, "Cannot establish the starting drawing database; nothing was created.");
    GS::Array<GS::ObjectState> elementResults;
    const GSErrCode undoErr = ACAPI_CallUndoableCommand ("CreateDrawingsCommand", [&]() -> GSErrCode {
        for (const auto& item : items) {
            const GS::ObjectState* layoutDatabaseId = item.Get ("layoutDatabaseId");
            // Omission means the original database, never the previous item's layout.
            const GSErrCode contextErr = layoutDatabaseId != nullptr
                ? ActivateLayoutDatabase (GetGuidFromObjectState (*layoutDatabaseId))
                : ACAPI_Database_ChangeCurrentDatabase (&startingDatabase);
            if (contextErr != NoError) {
                elementResults.Push (CreateErrorResponse (contextErr, "Failed to activate the drawing database."));
                continue;
            }

            elementResults.Push (CreateOneDrawing (item));
        }

        return NoError;
    });

    const GSErrCode restoreErr = ACAPI_Database_ChangeCurrentDatabase (&startingDatabase);
    if (undoErr != NoError) {
        elementResults.Clear ();
        for (UIndex index = 0; index < items.GetSize (); ++index)
            elementResults.Push (CreateErrorResponse (undoErr, "Drawing transaction failed; creation is not confirmed."));
    }

    GS::ObjectState response;
    if (restoreErr != NoError) response.Add ("contextRestorationError", *CreateErrorResponse (restoreErr, "Could not restore the original database; inspect context before continuing.").Get ("error"));
    const auto& elements = response.AddList<GS::ObjectState> ("elements");
    for (const auto& elementResult : elementResults) {
        elements (elementResult);
    }
    return response;
}

// ============================================================================
// ChangeDrawingLink — simulates relinking a Drawing to a different source.
//
// Archicad has no API to change a Drawing's source link (API_DrawingType::drawingGuid)
// in place: confirmed live that ACAPI_Element_Change silently ignores any change to that
// field once the Drawing already exists, even though other fields (pos, angle, ...) change
// correctly through the same call. The only way to relink is what Graphisoft's own support
// engineers recommend on the forums for this exact problem: capture the old Drawing's
// appearance, create a brand new Drawing against the new source, reapply the appearance,
// then delete the old one. The new Drawing necessarily gets a new guid - any external
// references to the old guid (dimensions, markers, IDs) will need updating separately.
//
// Selected old Drawing placement fields (pos, angle, ratio, modelOffset,
// clipPolygon) is set on the new element BEFORE creation (via CreateOneDrawing), not through
// a follow-up ACAPI_Element_Change - calling Change on an element immediately after creating
// it, within the same undoable command, crashed Archicad's command layer in testing.
//
// Title parameters are copied, but title position is not silently claimed as preserved.
// Source and replacement title points are returned for a separate, revision-checked
// PositionDrawingTitles call after creation completes. Native/visual acceptance is pending.
// ============================================================================

ChangeDrawingLinkCommand::ChangeDrawingLinkCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String ChangeDrawingLinkCommand::GetName () const
{
    return "ChangeDrawingLink";
}

GS::Optional<GS::UniString> ChangeDrawingLinkCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "drawingsWithNewLinks": {
                "type": "array",
                "minItems": 1, "maxItems": 100,
                "items": {
                    "type": "object",
                    "description": "An existing Drawing and the navigator item it should be relinked to.",
                    "properties": {
                        "elementId": { "$ref": "#/ElementId" },
                        "navigatorItemId": { "$ref": "#/NavigatorItemId" },
                        "layoutDatabaseId": { "$ref": "#/DatabaseId" }
                    },
                    "additionalProperties": false,
                    "required": ["elementId", "navigatorItemId", "layoutDatabaseId"]
                }
            }
        },
        "additionalProperties": false,
        "required": ["drawingsWithNewLinks"]
    })";
}

GS::Optional<GS::UniString> ChangeDrawingLinkCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{
        "elements":{"$ref":"#/ElementIdsOrErrors"},
        "transactionStatus":{"type":"string","enum":["committed","failed"]},
        "replacements":{"type":"array","description":"Attempted old/new ID mappings. On a failed transaction these are diagnostic only and may no longer exist.","items":{
            "type":"object","properties":{"oldElementId":{"$ref":"#/ElementId"},"newElementId":{"$ref":"#/ElementId"},"sourceTitlePlacement":{"type":"object"},"replacementTitlePlacement":{"type":"object"},"titleParametersSupplied":{"type":"boolean","description":"Original title parameters were supplied to native creation; this is not independent readback verification."},"clipPreserved":{"type":"boolean"}},
            "required":["oldElementId","newElementId"],"additionalProperties":false}},
        "titleParametersAndPlacementNotPreserved":{"type":"boolean"},
        "failedInputIndex":{"type":"integer","minimum":0},
        "failurePhase":{"type":"string"},
        "contextRestorationError":{"type":"object"}
    },"required":["elements","transactionStatus","replacements","titleParametersAndPlacementNotPreserved"],"additionalProperties":false})";
}

// 19 September 2026, 19:40 CEST. Preserve measured title points for a separate
// PositionDrawingTitles request after relinking has completed. Do not mutate a
// replacement title in the transaction that created its drawing.
static GS::ObjectState DrawingTitlePoints (const API_Guid& drawing)
{
    GS::Array<API_ElementHotspot> hotspots;
    const auto error=ACAPI_Element_GetHotspots(drawing,&hotspots);
    if (error!=NoError) return CreateErrorResponse(error,"Cannot inspect drawing title placement points.");
    if (hotspots.GetSize()>10000) return CreateErrorResponse(APIERR_BADPARS,"Drawing hotspot count exceeds the inspection bound.");
    GS::Array<GS::ObjectState> points;
    for (UIndex index=0;index<hotspots.GetSize();++index) {
        const auto& point=hotspots[index];
        if (point.first.guid!=drawing || point.first.neigID!=APINeig_DrawingTitle) continue;
        if (!std::isfinite(point.second.x) || !std::isfinite(point.second.y) || points.GetSize()>=100)
            return CreateErrorResponse(APIERR_BADPARS,"Title placement points are invalid or exceed 100 entries.");
        points.Push(GS::ObjectState("hotspotIndex",index,"nativeSubIndex",point.first.inIndex,
            "nativePartType",static_cast<Int32>(point.first.elemPartType),"nativePartIndex",point.first.elemPartIndex,
            "positionMillimetres",GS::ObjectState("x",point.second.x*1000,"y",point.second.y*1000)));
    }
    return GS::ObjectState("points",points);
}
static GSErrCode ConfirmDrawingClip (const API_Element& original,const API_ElementMemo& oldMemo,const API_Guid& replacement)
{
    API_Element current={}; current.header.guid=replacement;
    auto error=ACAPI_Element_Get(&current);
    if (error!=NoError) return error;
    if (GetElemTypeId(current.header)!=API_DrawingID || current.drawing.isCutWithFrame!=original.drawing.isCutWithFrame) return APIERR_GENERAL;
    if (!original.drawing.isCutWithFrame) return NoError;
    const auto& expected=original.drawing.poly; const auto& actual=current.drawing.poly;
    if (expected.nCoords!=actual.nCoords || expected.nSubPolys!=actual.nSubPolys || expected.nArcs!=actual.nArcs) return APIERR_GENERAL;
    API_ElementMemo memo={};
    const GS::OnExit dispose([&] { ACAPI_DisposeElemMemoHdls(&memo); });
    error=ACAPI_Element_GetMemo(replacement,&memo,APIMemoMask_Polygon);
    if (error!=NoError) return error;
    if (memo.coords==nullptr || memo.pends==nullptr || BMhGetSize(reinterpret_cast<GSHandle>(memo.coords))<static_cast<GSSize>((actual.nCoords+1)*sizeof(API_Coord)) ||
        BMhGetSize(reinterpret_cast<GSHandle>(memo.pends))<static_cast<GSSize>((actual.nSubPolys+1)*sizeof(Int32))) return APIERR_BADPOLY;
    for (Int32 i=1;i<=actual.nCoords;++i) {
        const auto a=(*memo.coords)[i],b=(*oldMemo.coords)[i];
        if (!std::isfinite(a.x) || !std::isfinite(a.y) || std::hypot(a.x-b.x,a.y-b.y)>1e-8) return APIERR_GENERAL;
    }
    for (Int32 i=1;i<=actual.nSubPolys;++i) if ((*memo.pends)[i]!=(*oldMemo.pends)[i]) return APIERR_GENERAL;
    if (actual.nArcs>0) {
        if (memo.parcs==nullptr || BMhGetSize(reinterpret_cast<GSHandle>(memo.parcs))<static_cast<GSSize>(actual.nArcs*sizeof(API_PolyArc))) return APIERR_BADPOLY;
        for (Int32 i=0;i<actual.nArcs;++i) {
            const auto& a=(*memo.parcs)[i]; const auto& b=(*oldMemo.parcs)[i];
            if (a.begIndex!=b.begIndex || a.endIndex!=b.endIndex || !std::isfinite(a.arcAngle) || std::abs(a.arcAngle-b.arcAngle)>1e-10) return APIERR_GENERAL;
        }
    }
    return NoError;
}

GS::ObjectState ChangeDrawingLinkCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& control) const
{
    GS::Array<GS::ObjectState> items;
    parameters.Get ("drawingsWithNewLinks", items);
    if (items.IsEmpty () || items.GetSize () > 100)
        return CreateErrorResponse (APIERR_BADPARS, "Supply between 1 and 100 drawings to relink.");
    GS::HashSet<API_Guid> seen;
    for (const auto& item : items) {
        const API_Guid guid = GetGuidFromArrayItem ("elementId", item);
        if (guid == APINULLGuid || seen.Contains (guid) || item.Get ("navigatorItemId") == nullptr || item.Get ("layoutDatabaseId") == nullptr)
            return CreateErrorResponse (APIERR_BADPARS, "Each drawing must occur once with a source and layout database.");
        seen.Add (guid);
    }
    API_DatabaseInfo startingDatabase = {};
    GSErrCode err = ACAPI_Database_GetCurrentDatabase (&startingDatabase);
    if (err != NoError) return CreateErrorResponse (err, "Cannot establish the starting database; no relinking attempted.");
    GS::Array<GS::ObjectState> results, replacements;
    Int32 inputIndex = -1;
    GS::UniString phase = "startTransaction";
    const GSErrCode transaction = ACAPI_CallUndoableCommand ("ChangeDrawingLinkCommand", [&]() -> GSErrCode {
        for (const auto& item : items) {
            ++inputIndex;
            phase="checkCancellation";
            if (control.TestBreak()) return APIERR_CANCEL;
            phase = "activateLayout";
            err = ActivateLayoutDatabase (GetGuidFromObjectState (*item.Get ("layoutDatabaseId")));
            if (err != NoError) return err;
            phase = "readOriginalDrawing";
            const API_Guid oldGuid = GetGuidFromArrayItem ("elementId", item);
            API_Element oldElement = {};
            oldElement.header.guid = oldGuid;
            err = ACAPI_Element_Get (&oldElement);
            if (err != NoError) return err;
            if (GetElemTypeId (oldElement.header) != API_DrawingID) return APIERR_BADID;
            phase = "checkEditAccess";
            if (!ACAPI_Element_Filter (oldGuid, APIFilt_IsEditable | APIFilt_InMyWorkspace | APIFilt_HasAccessRight))
                return APIERR_NOACCESSRIGHT;
            API_ElementMemo oldMemo = {}, titleMemo = {};
            const GS::OnExit dispose ([&] () { ACAPI_DisposeElemMemoHdls (&oldMemo); ACAPI_DisposeElemMemoHdls (&titleMemo); });
            phase = "readTitleParametersAndClip";
            err = ACAPI_Element_GetMemo (oldGuid, &oldMemo, APIMemoMask_AddPars | APIMemoMask_Polygon);
            if (err != NoError) return err;
            API_AddParType** titleParameters=oldMemo.params;
            if (oldElement.drawing.title.libInd>0 && titleParameters==nullptr) {
                // Refuse to silently replace a configured title with its defaults.
                if (oldElement.drawing.title.guid==APINULLGuid) return APIERR_GENERAL;
                err=ACAPI_Element_GetMemo (oldElement.drawing.title.guid,&titleMemo,APIMemoMask_AddPars);
                if (err!=NoError || titleMemo.params==nullptr) return err==NoError?APIERR_GENERAL:err;
                titleParameters=titleMemo.params;
            }
            if (oldElement.drawing.isMultiPageDrawing) { phase="unsupportedMultiPageDrawing"; return APIERR_BADPARS; }
            if (oldElement.drawing.isCutWithFrame) {
                phase = "readNativeClipPolygon";
                const auto& poly=oldElement.drawing.poly;
                if (poly.nCoords<4 || poly.nCoords>10000 || poly.nSubPolys<1 || poly.nSubPolys>poly.nCoords/4 || poly.nArcs<0 || poly.nArcs>poly.nCoords ||
                    oldMemo.coords==nullptr || oldMemo.pends==nullptr ||
                    BMhGetSize(reinterpret_cast<GSHandle>(oldMemo.coords))<static_cast<GSSize>((poly.nCoords+1)*sizeof(API_Coord)) ||
                    BMhGetSize(reinterpret_cast<GSHandle>(oldMemo.pends))<static_cast<GSSize>((poly.nSubPolys+1)*sizeof(Int32))) return APIERR_BADPOLY;
                Int32 previous=0;
                for (Int32 contour=1;contour<=poly.nSubPolys;++contour) {
                    const Int32 end=(*oldMemo.pends)[contour];
                    if (end-previous<4 || end>poly.nCoords) return APIERR_BADPOLY;
                    if (!IsSame2DCoordinate((*oldMemo.coords)[previous+1],(*oldMemo.coords)[end])) return APIERR_BADPOLY;
                    previous=end;
                }
                if (previous!=poly.nCoords) return APIERR_BADPOLY;
                for (Int32 i=1;i<=poly.nCoords;++i) if (!std::isfinite((*oldMemo.coords)[i].x) || !std::isfinite((*oldMemo.coords)[i].y)) return APIERR_BADPOLY;
                if (poly.nArcs>0) {
                    if (oldMemo.parcs==nullptr || BMhGetSize(reinterpret_cast<GSHandle>(oldMemo.parcs))<static_cast<GSSize>(poly.nArcs*sizeof(API_PolyArc))) return APIERR_BADPOLY;
                    for (Int32 i=0;i<poly.nArcs;++i) {
                        const auto& arc=(*oldMemo.parcs)[i];
                        if (arc.begIndex<1 || arc.begIndex>poly.nCoords || arc.endIndex<1 || arc.endIndex>poly.nCoords || !std::isfinite(arc.arcAngle)) return APIERR_BADPOLY;
                    }
                }
            }
            GS::ObjectState replacement;
            replacement.Add ("navigatorItemId", *item.Get ("navigatorItemId"));
            replacement.Add ("name", GS::UniString (oldElement.drawing.name));
            replacement.Add ("position", Create2DCoordinateObjectState (oldElement.drawing.pos));
            replacement.Add ("scale", oldElement.drawing.ratio);
            replacement.Add ("angle", oldElement.drawing.angle);
            replacement.Add ("modelOffset", Create2DCoordinateObjectState (oldElement.drawing.modelOffset));
            const auto sourceTitlePlacement=DrawingTitlePoints(oldGuid);
            phase = "createReplacement";
            const auto created = CreateOneDrawing (replacement, &oldElement.drawing, titleParameters, &oldMemo);
            const auto* newId = created.Get ("elementId");
            if (newId == nullptr) {
                if (const auto* failure = created.Get ("error")) failure->Get ("code", err);
                return err == NoError ? APIERR_GENERAL : err;
            }
            const API_Guid newGuid = GetGuidFromObjectState (*newId);
            GS::ObjectState mapping("oldElementId",CreateGuidObjectState(oldGuid),"newElementId",CreateGuidObjectState(newGuid),
                "sourceTitlePlacement",sourceTitlePlacement,"replacementTitlePlacement",DrawingTitlePoints(newGuid),
                "titleParametersSupplied",titleParameters!=nullptr);
            phase="confirmReplacementClip";
            err=ConfirmDrawingClip(oldElement,oldMemo,newGuid);
            mapping.Add("clipPreserved",err==NoError);
            replacements.Push(mapping);
            if (err!=NoError) return err;

            GS::Array<API_Guid> toDelete;
            toDelete.Push (oldGuid);
            phase = "deleteOriginal";
            err = ACAPI_Element_Delete (toDelete);
            if (err != NoError) return err; // Abort the batch rather than commit a duplicate drawing.
            results.Push (CreateElementIdObjectState (newGuid));
        }
        phase = "commitTransaction";
        return NoError;
    });
    const GSErrCode restore = ACAPI_Database_ChangeCurrentDatabase (&startingDatabase);
    if (transaction != NoError) {
        results.Clear ();
        for (UIndex index = 0; index < items.GetSize (); ++index)
            results.Push (CreateErrorResponse (transaction, "Relink transaction failed. No replacement is confirmed; inspect diagnostic IDs before retrying. Multi-page drawings are rejected rather than reduced to a single page."));
    }
    GS::ObjectState response ("transactionStatus", transaction == NoError ? "committed" : "failed",
        "titleParametersAndPlacementNotPreserved", true);
    if (transaction != NoError) {
        response.Add ("failurePhase", phase);
        if (inputIndex >= 0) response.Add ("failedInputIndex", inputIndex);
    }
    const auto& addResult = response.AddList<GS::ObjectState> ("elements");
    for (const auto& result : results) addResult (result);
    const auto& addReplacement = response.AddList<GS::ObjectState> ("replacements");
    for (const auto& replacement : replacements) addReplacement (replacement);
    if (restore != NoError) response.Add ("contextRestorationError", *CreateErrorResponse (restore, "Could not restore the original database.").Get ("error"));
    return response;
}

// Returns a GUID-string → field-name map built from the Layout Book custom scheme.
static GS::HashTable<GS::UniString, GS::UniString> GetLayoutSchemeByGuidString ()
{
    GS::HashTable<GS::UniString, GS::UniString> result;
    API_LayoutBook book = {};
    if (ACAPI_Navigator_GetLayoutBook (&book) != NoError) {
        return result;
    }
    for (const auto& kv : book.customScheme) {
#ifdef ServerMainVers_2800
        result.Add (APIGuidToString (kv.key), kv.value);
#else
        result.Add (APIGuidToString (*kv.key), *kv.value);
#endif
    }
    return result;
}

GetLayoutSettingsCommand::GetLayoutSettingsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String GetLayoutSettingsCommand::GetName () const
{
    return "GetLayoutSettings";
}

GS::Optional<GS::UniString> GetLayoutSettingsCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "layoutDatabaseIds": {
                "type": "array",
                "items": {
                    "type": "object",
                    "properties": {
                        "databaseId":      { "$ref": "#/DatabaseId" },
                        "navigatorItemId": { "$ref": "#/NavigatorItemId" }
                    },
                    "additionalProperties": false
                }
            }
        },
        "additionalProperties": false,
        "required": ["layoutDatabaseIds"]
    })";
}

GS::Optional<GS::UniString> GetLayoutSettingsCommand::GetRawResponseSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "layoutSettings": {
                "type": "array",
                "items": {
                    "type": "object",
                    "properties": {
                        "layoutName":               { "type": "string" },
                        "horizontalSize":           { "type": "number" },
                        "verticalSize":             { "type": "number" },
                        "leftMargin":               { "type": "number" },
                        "topMargin":                { "type": "number" },
                        "rightMargin":              { "type": "number" },
                        "bottomMargin":             { "type": "number" },
                        "customLayoutNumber":       { "type": "string" },
                        "customLayoutNumbering":    { "type": "boolean" },
                        "doNotIncludeInNumbering":  { "type": "boolean" },
                        "displayMasterLayoutBelow": { "type": "boolean" },
                        "customData": {
                            "type": "array",
                            "items": {
                                "type": "object",
                                "properties": {
                                    "customSchemeKey":   { "type": "string" },
                                    "customSchemeName":  { "type": "string" },
                                    "customSchemeValue": { "type": "string" }
                                },
                                "required": ["customSchemeKey", "customSchemeValue"],
                                "additionalProperties": false
                            }
                        }
                    },
                    "additionalProperties": false
                }
            }
        },
        "additionalProperties": false,
        "required": ["layoutSettings"]
    })";
}

GS::ObjectState GetLayoutSettingsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl&) const
{
    GS::Array<GS::ObjectState> items;
    GS::ObjectState errorResponse;
    if (!GetItems (parameters, "layoutDatabaseIds", items, errorResponse)) {
        return errorResponse;
    }

    const auto schemeByGuid = GetLayoutSchemeByGuidString ();

    GS::ObjectState response;
    const auto& layoutSettingsList = response.AddList<GS::ObjectState> ("layoutSettings");

    for (const auto& item : items) {
        API_DatabaseInfo dbInfo = {};

        const GS::ObjectState* navIdOS = item.Get ("navigatorItemId");
        const GS::ObjectState* dbIdOS  = item.Get ("databaseId");

        if (navIdOS != nullptr) {
            API_Guid navGuid = GetGuidFromObjectState (*navIdOS);
            API_NavigatorItem navItem = {};
            const GSErrCode navErr = ACAPI_Navigator_GetNavigatorItem (&navGuid, &navItem);
            if (navErr != NoError) {
                layoutSettingsList (CreateErrorResponse (navErr, "Failed to get navigator item from navigatorItemId."));
                continue;
            }
            dbInfo = navItem.db;
        } else if (dbIdOS != nullptr) {
            dbInfo = DatabaseIdResolver::Instance ().GetDatabaseWithId (GetGuidFromObjectState (*dbIdOS));
        } else {
            layoutSettingsList (CreateErrorResponse (APIERR_BADPARS, "Missing databaseId or navigatorItemId."));
            continue;
        }

        API_LayoutInfo layoutInfo = {};
        const GSErrCode err = ACAPI_Navigator_GetLayoutSets (&layoutInfo, &dbInfo.databaseUnId);
        if (err != NoError) {
            layoutSettingsList (CreateErrorResponse (err, "Failed to get layout settings."));
            continue;
        }

        GS::ObjectState layoutResult (
            "layoutName",               GS::UniString (layoutInfo.layoutName),
            "horizontalSize",           layoutInfo.sizeX,
            "verticalSize",             layoutInfo.sizeY,
            "leftMargin",               layoutInfo.leftMargin,
            "topMargin",                layoutInfo.topMargin,
            "rightMargin",              layoutInfo.rightMargin,
            "bottomMargin",             layoutInfo.bottomMargin,
            "customLayoutNumber",       GS::UniString (layoutInfo.customLayoutNumber),
            "customLayoutNumbering",    layoutInfo.customLayoutNumbering,
            "doNotIncludeInNumbering",  layoutInfo.doNotIncludeInNumbering,
            "displayMasterLayoutBelow", layoutInfo.showMasterBelow);

        if (layoutInfo.customData != nullptr && !layoutInfo.customData->IsEmpty ()) {
            const auto& customDataList = layoutResult.AddList<GS::ObjectState> ("customData");
            for (auto& kv : *layoutInfo.customData) {
#ifdef ServerMainVers_2800
                const GS::UniString guidStr  = APIGuidToString (kv.key);
                const GS::UniString& value   = kv.value;
#else
                const GS::UniString guidStr  = APIGuidToString (*kv.key);
                const GS::UniString& value   = *kv.value;
#endif
                GS::UniString name;
                if (schemeByGuid.ContainsKey (guidStr)) {
                    schemeByGuid.Get (guidStr, &name);
                }
                GS::ObjectState entry;
                entry.Add ("customSchemeKey",   guidStr);
                entry.Add ("customSchemeValue", value);
                if (!name.IsEmpty ()) {
                    entry.Add ("customSchemeName", name);
                }
                customDataList (entry);
            }
        }

        delete layoutInfo.customData;
        layoutInfo.customData = nullptr;

        layoutSettingsList (layoutResult);
    }

    return response;
}

SetLayoutSettingsCommand::SetLayoutSettingsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String SetLayoutSettingsCommand::GetName () const
{
    return "SetLayoutSettings";
}

GS::Optional<GS::UniString> SetLayoutSettingsCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "layoutsData": {
                "type": "array",
                "items": {
                    "type": "object",
                    "properties": {
                        "layoutDatabaseId":         { "$ref": "#/DatabaseId" },
                        "layoutNavigatorItemId":    { "$ref": "#/NavigatorItemId" },
                        "layoutName":               { "type": "string" },
                        "horizontalSize":           { "type": "number" },
                        "verticalSize":             { "type": "number" },
                        "leftMargin":               { "type": "number" },
                        "topMargin":                { "type": "number" },
                        "rightMargin":              { "type": "number" },
                        "bottomMargin":             { "type": "number" },
                        "customLayoutNumber":       { "type": "string" },
                        "customLayoutNumbering":    { "type": "boolean" },
                        "doNotIncludeInNumbering":  { "type": "boolean" },
                        "showMasterBelow":          { "type": "boolean" },
                        "customDataMode": {"type":"string","enum":["Replace","Merge"],"description":"Replace retains legacy behaviour (default). Merge updates supplied fields and retains all other layout custom fields."},
                        "customData": {
                            "type": "array",
                            "items": {
                                "type": "object",
                                "properties": {
                                    "customSchemeKey":   { "type": "string" },
                                    "customSchemeName":  { "type": "string" },
                                    "customSchemeValue": { "type": "string" }
                                },
                                "required": ["customSchemeValue"],
                                "additionalProperties": false
                            }
                        }
                    },
                    "additionalProperties": false,
                    "oneOf":[{"required":["layoutDatabaseId"],"not":{"required":["layoutNavigatorItemId"]}},
                             {"required":["layoutNavigatorItemId"],"not":{"required":["layoutDatabaseId"]}}],
                    "dependencies":{"customDataMode":["customData"]}
                }
            }
        },
        "additionalProperties": false,
        "required": ["layoutsData"]
    })";
}

GS::Optional<GS::UniString> SetLayoutSettingsCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{"executionResults":{"$ref":"#/ExecutionResults"}},"additionalProperties":false,"required":["executionResults"]})";
}

bool IsMasterLayoutDatabase (const API_DatabaseUnId& targetUnId)
{
    API_NavigatorItem filterItem = {};
    filterItem.mapId           = API_LayoutMap;
    filterItem.itemType        = API_MasterLayoutNavItem;
    filterItem.db.databaseUnId = targetUnId;

    GS::Array<API_NavigatorItem> results;
    if (ACAPI_Navigator_SearchNavigatorItem (&filterItem, &results) != NoError) {
        return false;
    }

    for (const auto& result : results) {
        if (result.itemType == API_MasterLayoutNavItem) {
            return true;
        }
    }
    return false;
}

GS::ObjectState SetLayoutSettingsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl&) const
{
    GS::Array<GS::ObjectState> items;
    GS::ObjectState errorResponse;
    if (!GetItems (parameters, "layoutsData", items, errorResponse)) {
        return errorResponse;
    }

    GS::Array<GS::ObjectState> executionResults;

    for (const auto& item : items) {
        API_DatabaseInfo dbInfo = {};
        bool isMasterLayout = false;

        const GS::ObjectState* navIdOS = item.Get ("layoutNavigatorItemId");
        const GS::ObjectState* dbIdOS  = item.Get ("layoutDatabaseId");
        if ((navIdOS != nullptr) == (dbIdOS != nullptr) || (item.Contains ("customDataMode") && !item.Contains ("customData"))) {
            executionResults.Push (CreateFailedExecutionResult (APIERR_BADPARS, "Specify exactly one layout identity; customDataMode requires customData.")); continue;
        }
        GS::UniString requestedName, requestedNumber;
        API_LayoutInfo stringLimits = {};
        if ((item.Get ("layoutName", requestedName) && (requestedName.IsEmpty () || requestedName.GetLength () >= API_UniLongNameLen)) ||
            (item.Get ("customLayoutNumber", requestedNumber) && std::strlen (requestedNumber.ToCStr ().Get ()) >= sizeof (stringLimits.customLayoutNumber))) {
            executionResults.Push (CreateFailedExecutionResult (APIERR_BADPARS, "Layout name or number is empty or exceeds native capacity.")); continue;
        }

        if (navIdOS != nullptr) {
            API_Guid navGuid = GetGuidFromObjectState (*navIdOS);
            API_NavigatorItem navItem = {};
            const GSErrCode navErr = ACAPI_Navigator_GetNavigatorItem (&navGuid, &navItem);
            if (navErr != NoError) {
                executionResults.Push (CreateFailedExecutionResult (navErr, "Failed to get navigator item from layoutNavigatorItemId."));
                continue;
            }
            dbInfo = navItem.db;
            if (navItem.itemType != API_MasterLayoutNavItem && navItem.itemType != API_LayoutNavItem) {
                executionResults.Push (CreateFailedExecutionResult (APIERR_BADPARS, "Navigator identity is not a layout or master.")); continue;
            }
            isMasterLayout = (navItem.itemType == API_MasterLayoutNavItem);
        } else if (dbIdOS != nullptr) {
            dbInfo = DatabaseIdResolver::Instance ().GetDatabaseWithId (GetGuidFromObjectState (*dbIdOS));
            const GSErrCode databaseError = ACAPI_Window_GetDatabaseInfo (&dbInfo);
            if (databaseError != NoError) {
                executionResults.Push (CreateFailedExecutionResult (databaseError, "Cannot resolve layout database.")); continue;
            }
            if (dbInfo.typeID != APIWind_MasterLayoutID && dbInfo.typeID != APIWind_LayoutID) {
                executionResults.Push (CreateFailedExecutionResult (APIERR_BADPARS, "Database identity is not a layout or master.")); continue;
            }
            isMasterLayout = dbInfo.typeID == APIWind_MasterLayoutID;
        } else {
            executionResults.Push (CreateFailedExecutionResult (APIERR_BADPARS, "Missing layoutDatabaseId or layoutNavigatorItemId."));
            continue;
        }

        double dummyD = 0.0;
        bool sizeProvided = false;
        sizeProvided |= static_cast<bool> (item.Get ("horizontalSize", dummyD));
        sizeProvided |= static_cast<bool> (item.Get ("verticalSize",   dummyD));
        sizeProvided |= item.Contains ("leftMargin") || item.Contains ("rightMargin") || item.Contains ("topMargin") || item.Contains ("bottomMargin");

        if (sizeProvided && !isMasterLayout) {
            executionResults.Push (CreateFailedExecutionResult (APIERR_NOTSUPPORTED,
                "Paper size and margins can only be changed on master layouts, in millimetres."));
            continue;
        }

        API_LayoutInfo layoutInfo = {};
        GSErrCode err = ACAPI_Navigator_GetLayoutSets (&layoutInfo, &dbInfo.databaseUnId);
        const GS::OnExit layoutDataCleanup ([&] () { delete layoutInfo.customData; });
        if (err != NoError) {
            executionResults.Push (CreateFailedExecutionResult (err, "Failed to read current layout settings."));
            continue;
        }

#ifdef ServerMainVers_2600
        SetUCharProperty (&item, "layoutName", layoutInfo.layoutName);
#else
        SetCharProperty (&item, "layoutName", layoutInfo.layoutName);
#endif
        item.Get ("horizontalSize",          layoutInfo.sizeX);
        item.Get ("verticalSize",            layoutInfo.sizeY);
        item.Get ("leftMargin",              layoutInfo.leftMargin);
        item.Get ("topMargin",               layoutInfo.topMargin);
        item.Get ("rightMargin",             layoutInfo.rightMargin);
        item.Get ("bottomMargin",            layoutInfo.bottomMargin);
        if (sizeProvided) {
            bool valid = true;
            for (double value : {layoutInfo.sizeX, layoutInfo.sizeY, layoutInfo.leftMargin, layoutInfo.rightMargin, layoutInfo.topMargin, layoutInfo.bottomMargin})
                valid &= std::isfinite (value) && value >= 0;
            if (!valid || layoutInfo.sizeX <= 0 || layoutInfo.sizeY <= 0 ||
                layoutInfo.leftMargin + layoutInfo.rightMargin >= layoutInfo.sizeX || layoutInfo.topMargin + layoutInfo.bottomMargin >= layoutInfo.sizeY) {
                executionResults.Push (CreateFailedExecutionResult (APIERR_BADPARS, "Paper size and margins must be finite, positive and leave printable area, in millimetres.")); continue;
            }
        }
        SetCharProperty (&item, "customLayoutNumber", layoutInfo.customLayoutNumber);
        item.Get ("customLayoutNumbering",   layoutInfo.customLayoutNumbering);
        item.Get ("doNotIncludeInNumbering", layoutInfo.doNotIncludeInNumbering);
        bool showMasterBelowRequested = layoutInfo.showMasterBelow;
        const bool showMasterBelowProvided = item.Get ("showMasterBelow", showMasterBelowRequested);
        layoutInfo.showMasterBelow = showMasterBelowRequested;

        GS::Array<GS::ObjectState> customDataItems;
        if (item.Get ("customData", customDataItems)) {
            const auto schemeByGuid = GetLayoutSchemeByGuidString ();

            // Build reverse map: field name → GUID string (for name-based lookup)
            GS::HashTable<GS::UniString, GS::UniString> schemeByName;
            GS::HashSet<GS::UniString> ambiguousNames;
            GS::HashSet<API_Guid> schemeIds;
            for (const auto& sk : schemeByGuid) {
#ifdef ServerMainVers_2800
                const auto name = sk.value; const auto key = sk.key;
#else
                const auto name = *sk.value; const auto key = *sk.key;
#endif
                if (schemeByName.ContainsKey (name)) ambiguousNames.Add (name);
                else schemeByName.Add (name, key);
                schemeIds.Add (APIGuidFromString (key.ToCStr ()));
            }

            GS::UniString mode = "Replace";
            item.Get ("customDataMode", mode);
            if (mode != "Replace" && mode != "Merge") {
                executionResults.Push (CreateFailedExecutionResult (APIERR_BADPARS, "Unknown customDataMode.")); continue;
            }
            if (mode == "Replace") {
                delete layoutInfo.customData;
                layoutInfo.customData = nullptr;
            }
            if (layoutInfo.customData == nullptr) layoutInfo.customData = new GS::HashTable<API_Guid, GS::UniString> ();
            bool fieldsValid = true;
            GS::HashSet<API_Guid> suppliedIds;
            for (const auto& cd : customDataItems) {
                GS::UniString keyStr, value;
                if (!cd.Get ("customSchemeKey", keyStr)) {
                    GS::UniString nameStr;
                    if (cd.Get ("customSchemeName", nameStr) && schemeByName.ContainsKey (nameStr) && !ambiguousNames.Contains (nameStr)) {
                        schemeByName.Get (nameStr, &keyStr);
                    }
                }
                const API_Guid key = APIGuidFromString (keyStr.ToCStr ());
                if (!keyStr.IsEmpty () && schemeIds.Contains (key) && !suppliedIds.Contains (key) && cd.Get ("customSchemeValue", value)) {
                    suppliedIds.Add (key);
                    layoutInfo.customData->Put (key, value);
                } else {
                    fieldsValid = false; break;
                }
            }
            if (!fieldsValid) {
                executionResults.Push (CreateFailedExecutionResult (APIERR_BADPARS, "Custom layout fields contain an unknown, ambiguous or duplicate key/name; no settings were changed."));
                continue;
            }
        }

        err = ACAPI_Navigator_ChangeLayoutSets (&layoutInfo, &dbInfo.databaseUnId);
        delete layoutInfo.customData;
        layoutInfo.customData = nullptr;

        if (err != NoError) {
            executionResults.Push (CreateFailedExecutionResult (err, "Failed to change layout settings."));
        } else if (showMasterBelowProvided) {
            API_LayoutInfo verifyInfo = {};
            const GSErrCode verifyErr = ACAPI_Navigator_GetLayoutSets (&verifyInfo, &dbInfo.databaseUnId);
            const GS::OnExit verifyCleanup ([&] () { delete verifyInfo.customData; });
            if (verifyErr != NoError) {
                executionResults.Push (CreateFailedExecutionResult (verifyErr, "Layout changed but native verification failed; inspect before retrying."));
            } else if (verifyInfo.showMasterBelow != showMasterBelowRequested) {
                executionResults.Push (CreateFailedExecutionResult (APIERR_NOTSUPPORTED,
                    "showMasterBelow cannot be changed for this layout type via the API."));
            } else {
                executionResults.Push (CreateSuccessfulExecutionResult ());
            }
        } else {
            executionResults.Push (CreateSuccessfulExecutionResult ());
        }
    }

    return CreateExecutionResultsResponse (executionResults);
}

GetLayoutCustomSchemeCommand::GetLayoutCustomSchemeCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String GetLayoutCustomSchemeCommand::GetName () const
{
    return "GetLayoutCustomScheme";
}

GS::Optional<GS::UniString> GetLayoutCustomSchemeCommand::GetInputParametersSchema () const
{
    return GS::NoValue;
}

GS::Optional<GS::UniString> GetLayoutCustomSchemeCommand::GetRawResponseSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "customScheme": {
                "type": "array",
                "items": {
                    "type": "object",
                    "properties": {
                        "customSchemeKey":  { "type": "string" },
                        "customSchemeName": { "type": "string" }
                    },
                    "required": ["customSchemeKey", "customSchemeName"],
                    "additionalProperties": false
                }
            }
        },
        "required": ["customScheme"],
        "additionalProperties": false
    })";
}

GS::ObjectState GetLayoutCustomSchemeCommand::Execute (const GS::ObjectState&, GS::ProcessControl&) const
{
    GS::ObjectState response;
    const auto& schemeList = response.AddList<GS::ObjectState> ("customScheme");

    API_LayoutBook book = {};
    if (ACAPI_Navigator_GetLayoutBook (&book) == NoError) {
        for (const auto& kv : book.customScheme) {
            schemeList (GS::ObjectState (
#ifdef ServerMainVers_2800
                "customSchemeKey",  APIGuidToString (kv.key),
                "customSchemeName", kv.value));
#else
                "customSchemeKey",  APIGuidToString (*kv.key),
                "customSchemeName", *kv.value));
#endif
        }
    }

    return response;
}

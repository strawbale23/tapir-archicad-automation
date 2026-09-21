#include "GuardedExecutionCommands.hpp"
#include "SchemaDefinitions.hpp"
#include "ProjectCommands.hpp"
#include "MigrationHelper.hpp"
#include "AddOnVersion.hpp"
#include "NativeOperationJournal.hpp"
#include "ObjectStateJSONConversion.hpp"
#include "JSON/JDOMParser.hpp"
#include "JSON/SchemaDocument.hpp"
#include "GSProcessControl.hpp"
#include <map>
#include <set>
#include <string>

namespace {
std::map<std::string,NativeCommandFactory> factories;
struct Receipt { GS::UniString request; GS::ObjectState response; };
std::map<std::string,Receipt> receipts;
GS::UniString session;
bool notificationsReady=false;
constexpr size_t MaxReceipts=128;
constexpr USize MaxPayloadCharacters=1048576;
constexpr size_t MaxRetainedCharacters=8*1048576;
size_t retainedCharacters=0;
void Reset () { GS::Guid id; id.Generate (); session=id.ToUniString (); receipts.clear (); retainedCharacters=0; }
GSErrCode OnProject (API_NotifyEventID,Int32) { Reset (); return NoError; }
const std::set<std::string> allowed {
    "CreateWalls","ModifyWalls","CreateSlabs","ModifySlabs","CreateColumns","ModifyColumns","CreateBeams","ModifyBeams",
    "CreateRoofs","ModifyRoofs","CreateMeshes","ModifyMeshes","CreateMorphs","ModifyMorphs","CreateObjects","ModifyObjects",
    "CreateDoors","ModifyDoors","CreateWindows","ModifyWindows","CreateZones","ModifyZones","CreatePolygonalWalls","ModifyPolygonalWallGeometry",
    "TransformElements","StretchElementAtHotspot","MoveSlabVertices","OffsetSlabEdge","SetSlabEdgeArc","SetSlabEdgeSettings","EditSlabTopology","SetAssemblySegments",
    "CreateTexts","ModifyTexts","CreateLabels","ModifyLabels","SetAnnotationTextStyle","SetAnnotationRunStyles","CreateAssociativeDimensions","CreateWallThicknessDimensions","EditDimensionChain","ModifyDimensionSettings",
    "CreateSections","CreateElevations","ModifySectionSettings","CreateDrawings","PositionDrawings","PositionDrawingTitles","ChangeDrawingLink",
    "CreateLayers","CreateLayerCombinations","CreateLines","CreateFills","CreateBuildingMaterials","CreateSurfaces","CreateComposites","CreateProfiles","CreatePenTables",
    "CreateMasterLayouts","CreateLayout","SetLayoutSettings","CreateViewMapFolder","CreateViewsInViewMap","SetViewSettings","CopyViewSettings",
    "CreateClassificationSystems","CreateClassificationItems","CreatePropertyGroups","CreatePropertyDefinitions","UpdatePropertyDefinitions","SetElementRenovationStatus","SetPropertyValuesOfElements","SetGDLParametersOfElements","DeleteElements"
};
GS::ObjectState MakeReceipt (const GS::UniString& operation,const GS::UniString& command,const GS::UniString& state,bool replayed=false,bool journal=false,const GS::UniString& originalSession=GS::UniString ()) {
    return GS::ObjectState ("sessionId",originalSession.IsEmpty () ? session : originalSession,"operationId",operation,"commandName",command,"status",state,"replayed",replayed,
        "durability",journal ? "FileJournal" : "CurrentAddOnProjectSessionOnly","modelStateVerified",false);
}
GSErrCode Context (const GS::ObjectState& p,GS::ProcessControl& control,GS::UniString& message) {
    message="Automation session, project path or database changed. Inspect the current context before issuing a new operation.";
    GS::UniString expected; p.Get ("expectedSessionId",expected);
    if (!notificationsReady || expected!=session) return APIERR_BADPARS;
    auto project=GetProjectInfoCommand ().Execute ({},control);
    if (const auto* error=project.Get ("error")) { Int32 code=APIERR_GENERAL; error->Get ("code",code); return code; }
    GS::UniString path,expectedPath; project.Get ("projectPath",path); p.Get ("expectedProjectPath",expectedPath);
    bool untitled=true; project.Get ("isUntitled",untitled);
    if (untitled || path.IsEmpty () || expectedPath!=path) return APIERR_BADPARS;
    API_DatabaseInfo db = {}; const GSErrCode err=ACAPI_Database_GetCurrentDatabase (&db);
    if (err!=NoError) return err;
    return GetGuidFromArrayItem ("expectedDatabaseId",p)==DatabaseIdResolver::Instance ().GetIdOfDatabase (db) ? NoError : APIERR_BADDATABASE;
}
}

void RegisterGuardedNativeCommand (const GS::String& name,NativeCommandFactory factory) {
    const std::string key (name.ToCStr ());
    if (allowed.count (key)) factories.emplace (key,std::move (factory));
}
GSErrCode InitializeAutomationSession () {
    Reset ();
    const GSErrCode err=ACAPI_ProjectOperation_CatchProjectEvent (APINotify_New | APINotify_NewAndReset | APINotify_Open | APINotify_Close,OnProject);
    notificationsReady=err==NoError;
    return err;
}
GS::Optional<GS::UniString> GetAutomationSessionCommand::GetInputParametersSchema () const {
    return R"({"type":"object","properties":{},"additionalProperties":false})";
}
GS::Optional<GS::UniString> GetAutomationSessionCommand::GetRawResponseSchema () const {
    return R"({"type":"object","properties":{"sessionId":{"type":"string"},"addOnVersion":{"type":"string"},"project":{"type":"object"},"databaseId":{"$ref":"#/DatabaseId"},"guardedExecutionAvailable":{"type":"boolean"},"guardedCommands":{"type":"array","items":{"type":"string"}},"receiptCapacity":{"type":"integer"},"receiptCount":{"type":"integer"},"receiptDurability":{"const":"CurrentAddOnProjectSessionOnly"},"fileJournalAvailable":{"type":"boolean"},"fileJournalDescription":{"type":"string"}},"required":["sessionId","addOnVersion","project","databaseId","guardedExecutionAvailable","guardedCommands","receiptCapacity","receiptCount","receiptDurability"],"additionalProperties":false})";
}
GS::ObjectState GetAutomationSessionCommand::Execute (const GS::ObjectState&,GS::ProcessControl& control) const {
    API_DatabaseInfo db = {}; const GSErrCode err=ACAPI_Database_GetCurrentDatabase (&db);
    if (err!=NoError) return CreateErrorResponse (err,"Cannot identify automation database.");
    auto project=GetProjectInfoCommand ().Execute ({},control);
    if (project.Contains ("error")) return project;
    GS::ObjectState result ("sessionId",session,"addOnVersion",ADDON_VERSION,"project",project,
        "databaseId",CreateGuidObjectState (DatabaseIdResolver::Instance ().GetIdOfDatabase (db)),"guardedExecutionAvailable",notificationsReady,
        "receiptCapacity",static_cast<Int32> (MaxReceipts),"receiptCount",static_cast<Int32> (receipts.size ()),"receiptDurability","CurrentAddOnProjectSessionOnly");
    result.Add ("fileJournalAvailable",NativeOperationJournal::Available ());
    result.Add ("fileJournalDescription","Optional caller-selected directory; receipts survive restarts, never save the project, and never prove that recorded changes survived Undo or an unsaved close. Keep receipts until work is reconciled.");
    const auto& add=result.AddList<GS::UniString> ("guardedCommands");
    for (const auto& entry:factories) add (GS::UniString (entry.first.c_str ()));
    return result;
}
GS::Optional<GS::UniString> ExecuteGuardedCommandCommand::GetInputParametersSchema () const {
    return R"({"type":"object","properties":{"expectedSessionId":{"type":"string","minLength":1},"expectedProjectPath":{"type":"string","minLength":1},"expectedDatabaseId":{"$ref":"#/DatabaseId"},"operationId":{"type":"string","minLength":1,"maxLength":128,"pattern":"^[A-Za-z0-9._:-]+$"},"commandName":{"type":"string","minLength":1},"parameters":{"type":"object"},"journalDirectory":{"type":"string","minLength":1,"description":"Optional existing absolute directory for persistent receipts. Requires a UUID operationId. A started receipt is exclusively created and flushed before native execution, and a separate completed receipt afterward. Reusing an ID found on disk never executes again. Does not save the Archicad project. Files contain request and result data; the client manages retention."}},"required":["expectedSessionId","expectedProjectPath","expectedDatabaseId","operationId","commandName","parameters"],"additionalProperties":false})";
}
GS::Optional<GS::UniString> ExecuteGuardedCommandCommand::GetRawResponseSchema () const {
    return R"({"type":"object","properties":{"sessionId":{"type":"string"},"operationId":{"type":"string"},"commandName":{"type":"string"},"status":{"enum":["started","returned","unknownOutcome","resultTooLarge"]},"replayed":{"type":"boolean"},"durability":{"enum":["CurrentAddOnProjectSessionOnly","FileJournal"]},"modelStateVerified":{"const":false},"result":{"type":"object"},"receiptWriteError":{"$ref":"#/Error"},"completionReceiptPersisted":{"type":"boolean"}},"required":["sessionId","operationId","commandName","status","replayed","durability","modelStateVerified"],"additionalProperties":false})";
}
GS::ObjectState ExecuteGuardedCommandCommand::Execute (const GS::ObjectState& p,GS::ProcessControl& control) const {
    GS::UniString message;
    const GSErrCode contextError=Context (p,control,message);
    if (contextError!=NoError) return CreateErrorResponse (contextError,message);
    GS::UniString operation,command,request;
    p.Get ("operationId",operation); p.Get ("commandName",command);
    const auto* args=p.Get ("parameters");
    if (args==nullptr || operation.IsEmpty () || operation.GetLength ()>128) return CreateErrorResponse (APIERR_BADPARS,"Invalid guarded operation.");
    if (JSON::CreateFromObjectState (p,request)!=NoError || request.GetLength ()>MaxPayloadCharacters) return CreateErrorResponse (APIERR_BADPARS,"Guarded request exceeds one million characters or cannot be serialized.");
    GS::UniString journalDirectory;
    const bool journal=p.Get ("journalDirectory",journalDirectory);
    const std::string key (operation.ToCStr (CC_UTF8).Get ());
    auto found=receipts.find (key);
    if (found!=receipts.end ()) {
        if (found->second.request!=request) return CreateErrorResponse (APIERR_BADPARS,"Operation ID already belongs to a different serialized request; no execution.");
        GS::UniString status; found->second.response.Get ("status",status);
        auto replay=MakeReceipt (operation,command,status,true,journal);
        if (journal) {
            bool persisted=false; found->second.response.Get ("completionReceiptPersisted",persisted);
            replay.Add ("completionReceiptPersisted",persisted);
            if (const auto* failure=found->second.response.Get ("receiptWriteError")) replay.Add ("receiptWriteError",*failure);
        }
        if (const auto* output=found->second.response.Get ("result")) replay.Add ("result",*output);
        return replay;
    }
    if (journal) {
        NativeOperationJournal::Record stored; bool exists=false;
        const auto err=NativeOperationJournal::Read (journalDirectory,operation,stored,exists,message);
        if (err!=NoError) return CreateErrorResponse (err,message);
        if (exists) {
            if (stored.request!=request) return CreateErrorResponse (APIERR_BADPARS,"A disk receipt already exists for this operation ID with a different request or session. Use GetOperationReceipt to reconcile it; no execution.");
            GS::UniString status; stored.receipt.Get ("status",status);
            if (status=="started") status="unknownOutcome";
            auto replay=MakeReceipt (operation,command,status,true,true);
            if (const auto* output=stored.receipt.Get ("result")) replay.Add ("result",*output);
            bool persisted=false; stored.receipt.Get ("completionReceiptPersisted",persisted);
            replay.Add ("completionReceiptPersisted",persisted);
            return replay;
        }
    }
    const auto factory=factories.find (std::string (command.ToCStr (CC_UTF8).Get ()));
    if (factory==factories.end ()) return CreateErrorResponse (APIERR_BADPARS,"Command is not registered for guarded native execution. Inspect GetAutomationSession.");
    if (receipts.size ()>=MaxReceipts || retainedCharacters+2*MaxPayloadCharacters>MaxRetainedCharacters)
        return CreateErrorResponse (APIERR_BADPARS,"Session receipt capacity reached. Receipts are never evicted to permit a repeated write.");
    if (control.TestBreak ()) return CreateErrorResponse (APIERR_CANCEL,"Cancelled before native execution.");
    auto native=factory->second ();
    const auto input=native->GetInputParametersSchema ();
    if (!input.HasValue ()) return CreateErrorResponse (APIERR_BADPARS,"Command has no discoverable input schema.");
    GS::UniString common=GetCommonSchemaDefinitions (),argsJson;
    std::string schema (common.ToCStr (CC_UTF8).Get ()); const auto last=schema.rfind ('}');
    if (last==std::string::npos || JSON::CreateFromObjectState (*args,argsJson)!=NoError) return CreateErrorResponse (APIERR_BADPARS,"Cannot prepare input validation.");
    // Keep command-local definitions at the document root, where #/definitions
    // references resolve. Nesting the input under allOf would break those refs.
    const std::string inputJson (input.Get ().ToCStr (CC_UTF8).Get ());
    const auto first=inputJson.find ('{');
    if (first==std::string::npos) return CreateErrorResponse (APIERR_BADPARS,"Invalid native input contract.");
    schema.erase (last); schema+=","; schema+=inputJson.substr (first+1);
    try {
        JSON::SchemaDocument contract (GS::UniString (schema.c_str (),CC_UTF8));
        JSON::JDOMStringParser parser; parser.Parse (argsJson,contract);
    } catch (...) { return CreateErrorResponse (APIERR_BADPARS,"Parameters do not validate against the installed native command schema."); }
    Receipt receipt; receipt.request=request; receipt.response=MakeReceipt (operation,command,"started",false,journal);
    if (journal) {
        receipt.response.Add ("completionReceiptPersisted",false);
        NativeOperationJournal::Record initial; initial.request=request; initial.receipt=receipt.response;
        const auto err=NativeOperationJournal::Write (journalDirectory,operation,initial,false,message);
        if (err!=NoError) return CreateErrorResponse (err,message+" Native execution was not started.");
    }
    auto inserted=receipts.emplace (key,std::move (receipt)).first;
    retainedCharacters+=request.GetLength ();
    const GS::UniString startingSession=session;
    try {
        const GS::ObjectState output=native->Execute (*args,control);
        // The allowlist excludes project switching. Still protect against a
        // synchronous project callback invalidating the receipt map.
        if (session!=startingSession) return CreateErrorResponse (APIERR_GENERAL,"Project session changed during execution. Outcome requires inspection.");
        GS::UniString serialized; const GSErrCode conversion=JSON::CreateFromObjectState (output,serialized);
        const bool fits=conversion==NoError && serialized.GetLength ()<=MaxPayloadCharacters;
        inserted->second.response=MakeReceipt (operation,command,fits?"returned":"resultTooLarge",false,journal);
        if (fits) { inserted->second.response.Add ("result",output); retainedCharacters+=serialized.GetLength (); }
    } catch (...) {
        if (session!=startingSession) return CreateErrorResponse (APIERR_GENERAL,"Session changed during failed execution. Outcome unknown.");
        inserted->second.response=MakeReceipt (operation,command,"unknownOutcome",false,journal);
    }
    if (journal) {
        NativeOperationJournal::Record completed; completed.request=request; completed.receipt=inserted->second.response;
        completed.receipt.Add ("completionReceiptPersisted",true);
        const auto err=NativeOperationJournal::Write (journalDirectory,operation,completed,true,message);
        inserted->second.response.Add ("completionReceiptPersisted",err==NoError);
        if (err!=NoError) inserted->second.response.Add ("receiptWriteError",*CreateErrorResponse (err,message).Get ("error"));
    }
    return inserted->second.response;
}
GS::Optional<GS::UniString> GetOperationReceiptCommand::GetInputParametersSchema () const {
    return R"({"type":"object","properties":{"expectedSessionId":{"type":"string","minLength":1},"operationId":{"type":"string","minLength":1,"maxLength":128},"journalDirectory":{"type":"string","minLength":1,"description":"Read the stored receipt after a restart. Does not execute or save anything. Requires expectedProjectPath matching the currently open saved project and a UUID operationId."},"expectedProjectPath":{"type":"string","minLength":1}},"required":["expectedSessionId","operationId"],"additionalProperties":false,"allOf":[{"if":{"required":["journalDirectory"]},"then":{"required":["expectedProjectPath"]}}]})";
}
GS::Optional<GS::UniString> GetOperationReceiptCommand::GetRawResponseSchema () const { return ExecuteGuardedCommandCommand ().GetRawResponseSchema (); }
GS::ObjectState GetOperationReceiptCommand::Execute (const GS::ObjectState& p,GS::ProcessControl& control) const {
    GS::UniString expected,operation; p.Get ("expectedSessionId",expected); p.Get ("operationId",operation);
    GS::UniString directory;
    if (p.Get ("journalDirectory",directory)) {
        GS::UniString requestedPath;
        if (!p.Get ("expectedProjectPath",requestedPath) || requestedPath.IsEmpty ()) return CreateErrorResponse (APIERR_BADPARS,"Reading a disk receipt requires the exact current saved project path.");
        auto project=GetProjectInfoCommand ().Execute ({},control);
        if (project.Contains ("error")) return project;
        GS::UniString currentPath; project.Get ("projectPath",currentPath);
        bool untitled=true; project.Get ("isUntitled",untitled);
        if (untitled || currentPath!=requestedPath) return CreateErrorResponse (APIERR_BADPARS,"The requested receipt does not target the currently open saved project.");
        NativeOperationJournal::Record record; bool found=false; GS::UniString message;
        const auto err=NativeOperationJournal::Read (directory,operation,record,found,message);
        if (err!=NoError) return CreateErrorResponse (err,message);
        if (!found) return CreateErrorResponse (APIERR_BADPARS,"No file receipt found. This does not prove that an operation was never applied; receipts may have been removed or stored elsewhere.");
        GS::ObjectState request;
        GS::UniString storedPath,storedSession,storedOperation,command,status;
        if (JSON::ConvertToObjectState (record.request,request)!=NoError || !request.Get ("expectedProjectPath",storedPath) ||
            !record.receipt.Get ("sessionId",storedSession) || !record.receipt.Get ("operationId",storedOperation) ||
            !record.receipt.Get ("commandName",command) || !record.receipt.Get ("status",status) || storedPath!=requestedPath || storedSession!=expected || storedOperation!=operation)
            return CreateErrorResponse (APIERR_BADPARS,"Stored receipt identity/project/session differs. No execution is permitted from this record.");
        if (status=="started") status="unknownOutcome";
        auto result=MakeReceipt (operation,command,status,true,true,storedSession);
        if (const auto* output=record.receipt.Get ("result")) result.Add ("result",*output);
        bool persisted=false; record.receipt.Get ("completionReceiptPersisted",persisted); result.Add ("completionReceiptPersisted",persisted);
        return result;
    }
    if (expected!=session) return CreateErrorResponse (APIERR_BADPARS,"Session changed; old receipts are unavailable. Do not assume the earlier operation failed.");
    const auto found=receipts.find (std::string (operation.ToCStr (CC_UTF8).Get ()));
    if (found==receipts.end ()) return CreateErrorResponse (APIERR_BADPARS,"No receipt in this session. This does not prove an operation in another session was not applied.");
    return found->second.response;
}

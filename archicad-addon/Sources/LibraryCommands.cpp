#include "LibraryCommands.hpp"
#include "ObjectState.hpp"
#include "MigrationHelper.hpp"
#include "Folder.hpp"
#include "File.hpp"
#include "FileSystem.hpp"
#include "GSUnID.hpp"
#include "Base64Converter.hpp"
#include <cstring>

AddFilesToEmbeddedLibraryCommand::AddFilesToEmbeddedLibraryCommand () :
    CommandBase (CommonSchema::Used)
{}

GS::String AddFilesToEmbeddedLibraryCommand::GetName () const
{
    return "AddFilesToEmbeddedLibrary";
}

GS::Optional<GS::UniString> AddFilesToEmbeddedLibraryCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "files": {
                "$ref": "#/LibraryFileAdditions"
            }
        },
        "additionalProperties": false,
        "required": [
            "files"
        ]
    })";
}

GS::Optional<GS::UniString> AddFilesToEmbeddedLibraryCommand::GetRawResponseSchema () const
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

GS::ObjectState AddFilesToEmbeddedLibraryCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
	auto folderId = API_SpecFolderID::API_EmbeddedProjectLibraryFolderID;

	IO::Location embeddedLibraryFolder;

	if (ACAPI_ProjectSettings_GetSpecFolder (&folderId, &embeddedLibraryFolder) != NoError || IO::Folder (embeddedLibraryFolder).GetStatus () != NoError) {
        return CreateErrorResponse (APIERR_GENERAL, "Failed to get embedded library folder.");
    }

    GS::ObjectState response;
    const auto& executionResults = response.AddList<GS::ObjectState> ("executionResults");

    GS::Array<GS::ObjectState> files;
    parameters.Get ("files", files);

    for (const GS::ObjectState& file : files) {
        GS::UniString inputPath;
        GS::UniString outputPath;
        if (!file.Get ("inputPath", inputPath) || !file.Get ("outputPath", outputPath) || inputPath.IsEmpty () || outputPath.IsEmpty ()) {
            executionResults (CreateFailedExecutionResult (APIERR_BADPARS, "Missing inputPath or outputPath parameters."));
            continue;
        }

        IO::File inputFile (IO::Location (inputPath), IO::File::OnNotFound::Fail);
        if (inputFile.GetStatus () != NoError) {
            executionResults (CreateFailedExecutionResult (APIERR_BADPARS, "Failed to read file on the given inputPath."));
            continue;
        }

		IO::Location outputFileLoc = embeddedLibraryFolder;
		outputFileLoc.AppendToLocal (IO::RelativeLocation (outputPath));
        IO::Location outputFolder = outputFileLoc;
        if (outputFolder.DeleteLastLocalName () != NoError ||
            IO::fileSystem.CreateFolderTree (outputFolder) != NoError ||
            IO::fileSystem.Copy (inputFile.GetLocation (), outputFileLoc) != NoError) {
            executionResults (CreateFailedExecutionResult (APIERR_BADPARS, "The given outputPath is not a valid relative path."));
            continue;
        }

        API_LibPart libPart = {};

        libPart.typeID = APILib_PictID;

        GS::UniString typeStr;
        if (file.Get ("type", typeStr)) {
            if (typeStr == "Window") {
                libPart.typeID = APILib_WindowID;
            } else if (typeStr == "Door") {
                libPart.typeID = APILib_DoorID;
            } else if (typeStr == "Object") {
                libPart.typeID = APILib_ObjectID;
            } else if (typeStr == "Lamp") {
                libPart.typeID = APILib_LampID;
            } else if (typeStr == "Room") {
                libPart.typeID = APILib_RoomID;
            } else if (typeStr == "Property") {
                libPart.typeID = APILib_PropertyID;
            } else if (typeStr == "PlanSign") {
                libPart.typeID = APILib_PlanSignID;
            } else if (typeStr == "Label") {
                libPart.typeID = APILib_LabelID;
            } else if (typeStr == "Macro") {
                libPart.typeID = APILib_MacroID;
            } else if (typeStr == "Pict" || typeStr == "Picture") {
                libPart.typeID = APILib_PictID;
            } else if (typeStr == "ListScheme") {
                libPart.typeID = APILib_ListSchemeID;
            } else if (typeStr == "Skylight") {
                libPart.typeID = APILib_SkylightID;
            } else if (typeStr == "OpeningSymbol") {
                libPart.typeID = APILib_OpeningSymbolID;
            } else {
                executionResults (CreateFailedExecutionResult (APIERR_BADPARS, "Unknown library part type."));
                continue;
            }
        }

        libPart.location = &outputFileLoc;

        GSErrCode err = ACAPI_LibraryPart_Register (&libPart);
        if (err != NoError) {
            executionResults (CreateFailedExecutionResult (err, "Failed to add the file to the library."));
            continue;
        }

        executionResults (CreateSuccessfulExecutionResult ());
    }

    return response;
}

GS::Optional<GS::UniString> GetLibrariesCommand::GetRawResponseSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "libraries": {
                "type": "array",
                "description": "A list of project libraries.",
                "items": {
                    "type": "object",
                    "description": "Library",
                    "properties": {
                        "name": {
                            "type": "string",
                            "description": "Library name."
                        },
                        "path": {
                            "type": "string",
                            "description": "A filesystem path to library location."
                        },
                        "type": {
                            "type": "string",
                            "description": "Library type."
                        },
                        "available": {
                            "type": "boolean",
                            "description": "Is library not missing."
                        },
                        "readOnly": {
                            "type": "boolean",
                            "description": "Is library not writable."
                        },
                        "twServerUrl": {
                            "type": "string",
                            "description": "URL address of the TeamWork server hosting the library."
                        },
                        "urlWebLibrary": {
                            "type": "string",
                            "description": "URL of the downloaded Internet library."
                        }
                    },
                    "additionalProperties": false,
                    "required": [
                        "name",
                        "type",
                        "path"
                    ]
                }
            }
        },
        "additionalProperties": false,
        "required": [
            "libraries"
        ]
    })";
}

// ---------------------------------------------------------------------------
// SetLibraries / AddLibraries
// ---------------------------------------------------------------------------

static const char* LibraryPathsSchema = R"({
        "type": "object",
        "properties": {
            "libraries": {
                "type": "array",
                "description": "Local library folders or container files, by absolute path.",
                "items": {
                    "type": "object",
                    "properties": {
                        "path": {
                            "type": "string"
                        }
                    },
                    "additionalProperties": false,
                    "required": [
                        "path"
                    ]
                }
            }
        },
        "additionalProperties": false,
        "required": [
            "libraries"
        ]
    })";

static GS::Array<API_LibraryInfo> LocalLibrariesFromParameters (const GS::ObjectState& parameters)
{
    GS::Array<GS::ObjectState> items;
    parameters.Get ("libraries", items);
    GS::Array<API_LibraryInfo> out;
    for (const GS::ObjectState& item : items) {
        GS::UniString path;
        if (!item.Get ("path", path) || path.IsEmpty ()) {
            continue;
        }
        API_LibraryInfo info = {};
        info.location = IO::Location (path);
        info.libraryType = API_LibraryTypeID::API_LocalLibrary;
        IO::Name name;
        if (info.location.GetLastLocalName (&name) == NoError) {
            info.name = name.ToString ();
        }
        out.Push (info);
    }
    return out;
}

static bool SameLocation (const IO::Location& a, const IO::Location& b)
{
    // IO::Location's own equality (InputOutput/Location.hpp, present in every
    // kit): it knows the platform's rules for separators and case, where a
    // display-text comparison guessed.
    return a == b;
}

static GS::ObjectState ApplyLibraries (const GS::Array<API_LibraryInfo>& libs)
{
    const GSErrCode err = ACAPI_LibraryManagement_SetLibraries (&libs);
    if (err != NoError) {
        return CreateFailedExecutionResult (err, "Failed to set the libraries.");
    }
    return CreateSuccessfulExecutionResult ();
}

SetLibrariesCommand::SetLibrariesCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String SetLibrariesCommand::GetName () const
{
    return "SetLibraries";
}

GS::Optional<GS::UniString> SetLibrariesCommand::GetInputParametersSchema () const
{
    return GS::UniString (LibraryPathsSchema);
}

GS::Optional<GS::UniString> SetLibrariesCommand::GetRawResponseSchema () const
{
    return R"({
        "$ref": "#/ExecutionResult"
    })";
}

GS::ObjectState SetLibrariesCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    // The local libraries become exactly the given ones; built-in, embedded, server
    // and web libraries stay as they are.
    GS::Array<API_LibraryInfo> current;
    if (ACAPI_LibraryManagement_GetLibraries (&current) != NoError) {
        return CreateFailedExecutionResult (APIERR_COMMANDFAILED, "Failed to read the libraries.");
    }
    GS::Array<API_LibraryInfo> libs;
    for (const API_LibraryInfo& lib : current) {
        if (lib.libraryType != API_LibraryTypeID::API_LocalLibrary) {
            libs.Push (lib);
        }
    }
    for (const API_LibraryInfo& lib : LocalLibrariesFromParameters (parameters)) {
        libs.Push (lib);
    }
    return ApplyLibraries (libs);
}

AddLibrariesCommand::AddLibrariesCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String AddLibrariesCommand::GetName () const
{
    return "AddLibraries";
}

GS::Optional<GS::UniString> AddLibrariesCommand::GetInputParametersSchema () const
{
    return GS::UniString (LibraryPathsSchema);
}

GS::Optional<GS::UniString> AddLibrariesCommand::GetRawResponseSchema () const
{
    return R"({
        "$ref": "#/ExecutionResult"
    })";
}

GS::ObjectState AddLibrariesCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<API_LibraryInfo> libs;
    if (ACAPI_LibraryManagement_GetLibraries (&libs) != NoError) {
        return CreateFailedExecutionResult (APIERR_COMMANDFAILED, "Failed to read the libraries.");
    }
    USize added = 0;
    for (const API_LibraryInfo& lib : LocalLibrariesFromParameters (parameters)) {
        bool present = false;
        for (const API_LibraryInfo& existing : libs) {
            if (SameLocation (existing.location, lib.location)) {
                present = true;
                break;
            }
        }
        if (!present) {
            libs.Push (lib);
            ++added;
        }
    }
    if (added == 0) {
        // Every folder is already loaded: SetLibraries would only force a reload.
        return CreateSuccessfulExecutionResult ();
    }
    return ApplyLibraries (libs);
}

GetLibrariesCommand::GetLibrariesCommand () :
    CommandBase (CommonSchema::NotUsed)
{
}

GS::String GetLibrariesCommand::GetName () const
{
    return "GetLibraries";
}

GS::ObjectState GetLibrariesCommand::Execute (const GS::ObjectState& /*parameters*/, GS::ProcessControl& /*processControl*/) const
{
    GS::Array<API_LibraryInfo> libs;

    GSErrCode err = ACAPI_LibraryManagement_GetLibraries (&libs);
    if (err != NoError) {
        return CreateErrorResponse (err, "Failed to retrive libraries.");
    }

    GS::ObjectState response;
    const auto& listAdder = response.AddList<GS::ObjectState> ("libraries");

    for (const API_LibraryInfo& lib : libs) {
        GS::ObjectState libraryData;
        GS::UniString   type;
        GS::UniString   twServerUrl;
        GS::UniString   urlWebLibrary;
        switch (lib.libraryType) {
            case API_LibraryTypeID::API_Undefined:
                type = "Undefined";
                break;
            case API_LibraryTypeID::API_LocalLibrary:
                type = "LocalLibrary";
                break;
            case API_LibraryTypeID::API_UrlLibrary:
                type = "UrlLibrary";
                urlWebLibrary = lib.twServerUrl;
                break;
            case API_LibraryTypeID::API_BuiltInLibrary:
                type = "BuiltInLibrary";
                break;
            case API_LibraryTypeID::API_EmbeddedLibrary:
                type = "EmbeddedLibrary";
                break;
            case API_LibraryTypeID::API_OtherObject:
                type = "OtherObject";
                break;
            case API_LibraryTypeID::API_UrlOtherObject:
                type = "UrlOtherObject";
                urlWebLibrary = lib.twServerUrl;
                break;
            case API_LibraryTypeID::API_ServerLibrary:
                type = "ServerLibrary";
                twServerUrl = lib.twServerUrl;
                break;
        }

        libraryData.Add ("name", lib.name);
        libraryData.Add ("path", lib.location.ToDisplayText ());
        libraryData.Add ("type", type);
        libraryData.Add ("available", lib.available);
        libraryData.Add ("readOnly", lib.readOnly);
        libraryData.Add ("twServerUrl", twServerUrl);
        libraryData.Add ("urlWebLibrary", urlWebLibrary);
        listAdder (libraryData);
    }

    return response;
}

ReloadLibrariesCommand::ReloadLibrariesCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String ReloadLibrariesCommand::GetName () const
{
    return "ReloadLibraries";
}

GS::Optional<GS::UniString> ReloadLibrariesCommand::GetRawResponseSchema () const
{
    return R"({
        "$ref": "#/ExecutionResult"
    })";
}

GS::ObjectState ReloadLibrariesCommand::Execute (const GS::ObjectState& /*parameters*/, GS::ProcessControl& /*processControl*/) const
{
    GSErrCode err = ACAPI_ProjectOperation_ReloadLibraries ();
    if (err != NoError) {
        return CreateFailedExecutionResult (err, "Failed to reload libraries.");
    }

    return CreateSuccessfulExecutionResult ();
}

GetAvailableLibraryPartsCommand::GetAvailableLibraryPartsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String GetAvailableLibraryPartsCommand::GetName () const
{
    return "GetAvailableLibraryParts";
}

GS::Optional<GS::UniString> GetAvailableLibraryPartsCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "filterByTypeId": {
                "$ref": "#/LibraryPartType",
                "description": "Optional. Filter by libpart type (matches the value returned by LibPartTypeIdToString)."
            }
        },
        "additionalProperties": false
    })";
}

GS::Optional<GS::UniString> GetAvailableLibraryPartsCommand::GetRawResponseSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "libraryParts": {
                "type": "array",
                "items": {
                    "type": "object",
                    "properties": {
                        "guid": { "type": "string" },
                        "index": { "type": "integer" },
                        "documentName": { "type": "string" },
                        "fileName": { "type": "string" },
                        "typeId": { "$ref": "#/LibraryPartType" }
                    }
                }
            },
            "skippedCount": {
                "type": "integer",
                "description": "Library parts that ACAPI_LibraryPart_Get failed to read. Non-zero means the inventory is partial."
            },
            "skippedSample": {
                "type": "array",
                "description": "First five failed indices with their ACAPI error code, for diagnostic.",
                "items": {
                    "type": "object",
                    "properties": {
                        "index": { "type": "integer" },
                        "code": { "type": "integer" }
                    }
                }
            }
        },
        "additionalProperties": false,
        "required": ["libraryParts", "skippedCount"]
    })";
}

static GS::UniString LibPartTypeIdToString (Int32 typeID)
{
    switch (typeID) {
        case APILib_SpecID:          return "Spec";
        case APILib_WindowID:        return "Window";
        case APILib_DoorID:          return "Door";
        case APILib_ObjectID:        return "Object";
        case APILib_LampID:          return "Lamp";
        case APILib_RoomID:          return "Room";
        case APILib_PropertyID:      return "Property";
        case APILib_PlanSignID:      return "PlanSign";
        case APILib_LabelID:         return "Label";
        case APILib_MacroID:         return "Macro";
        case APILib_PictID:          return "Picture";
        case APILib_ListSchemeID:    return "ListScheme";
        case APILib_SkylightID:      return "Skylight";
        case APILib_OpeningSymbolID: return "OpeningSymbol";
        // API_ZombieLibID (ACAPI's uninitialized / General-Object
        // sentinel) and any future ACAPI subtype fall through here.
        // We return the schema-valid catch-all "Unknown" rather than a
        // dynamic "TypeN" value so callers can validate the response
        // against the LibraryPartType enum without surprises.
        default:                     return "Unknown";
    }
}

GS::ObjectState GetAvailableLibraryPartsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::UniString filterTypeId;
    parameters.Get ("filterByTypeId", filterTypeId);
    // `LibPartTypeIdToString` emits "Picture" for APILib_PictID, but the
    // shared LibraryPartType enum keeps both "Pict" (legacy
    // AddFilesToEmbeddedLibrary input) and "Picture" (this output).
    // Normalise so callers can filter by either spelling.
    if (filterTypeId == "Pict") {
        filterTypeId = "Picture";
    }

    Int32 partCount = 0;
    GSErrCode err = ACAPI_LibraryPart_GetNum (&partCount);
    if (err != NoError) {
        return CreateErrorResponse (err, "Failed to enumerate library parts.");
    }

    GS::ObjectState response;
    const auto& libpartAdder = response.AddList<GS::ObjectState> ("libraryParts");
    const auto& skippedAdder = response.AddList<GS::ObjectState> ("skippedSample");

    Int32 skippedCount = 0;
    constexpr Int32 maxSkippedSample = 5;

    for (Int32 i = 1; i <= partCount; ++i) {
        API_LibPart libPart = {};
        libPart.index = i;
        err = ACAPI_LibraryPart_Get (&libPart);
        if (err != NoError) {
            // libPart.location is not allocated on failure.
            if (skippedCount < maxSkippedSample) {
                GS::ObjectState skipEntry;
                skipEntry.Add ("index", i);
                skipEntry.Add ("code", static_cast<Int32> (err));
                skippedAdder (skipEntry);
            }
            ++skippedCount;
            continue;
        }

        const GS::UniString typeId = LibPartTypeIdToString (libPart.typeID);
        const bool matchesFilter = filterTypeId.IsEmpty () || typeId == filterTypeId;

        if (matchesFilter) {
            GS::ObjectState entry;
            entry.Add ("guid", GS::UnID (libPart.ownUnID).GetMainGuid ().ToUniString ());
            entry.Add ("index", static_cast<Int32> (libPart.index));
            entry.Add ("documentName", GS::UniString (libPart.docu_UName));
            entry.Add ("fileName", GS::UniString (libPart.file_UName));
            entry.Add ("typeId", typeId);
            libpartAdder (entry);
        }

        // ACAPI_LibraryPart_Get allocates libPart.location; free it on
        // every successful Get to avoid leaking ~one IO::Location per
        // libpart (thousands per call).
        delete libPart.location;
        libPart.location = nullptr;
    }

    response.Add ("skippedCount", skippedCount);

    return response;
}

GS::Optional<GS::UniString> GetLibraryPartPreviewCommand::GetInputParametersSchema () const
{
    return R"({"type":"object","properties":{
        "libraryPart":{"type":"object","properties":{"index":{"type":"integer","minimum":1},"guid":{"type":"string","format":"uuid"}},"required":["index","guid"],"additionalProperties":false},
        "maximumBytes":{"type":"integer","minimum":1,"maximum":4194304,"description":"Maximum encoded image source bytes before base64, default 1048576."}
    },"required":["libraryPart"],"additionalProperties":false})";
}

GS::Optional<GS::UniString> GetLibraryPartPreviewCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{
        "available":{"type":"boolean"},"source":{"const":"embeddedLibraryPreview"},"reason":{"type":"string"},
        "libraryPart":{"type":"object"},"mimeType":{"type":"string"},"byteCount":{"type":"integer"},"imageBase64":{"type":"string"}
    },"required":["available","source","libraryPart"],"additionalProperties":false})";
}

GS::ObjectState GetLibraryPartPreviewCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl&) const
{
    const auto* reference = parameters.Get ("libraryPart");
    API_LibPart part = {};
    const GS::OnExit disposePart ([&] () { delete part.location; });
    Int32 maximumBytes=1048576; parameters.Get ("maximumBytes", maximumBytes);
    if (reference == nullptr || !reference->Get ("index", part.index) || part.index < 1 || maximumBytes < 1 || maximumBytes > 4194304)
        return CreateErrorResponse (APIERR_BADPARS, "Supply a loaded library index/GUID and a 1 to 4194304 byte limit.");
    const API_Guid expected = GetGuidFromObjectState (*reference);
    GSErrCode err = ACAPI_LibraryPart_Get (&part);
    if (err != NoError) return CreateErrorResponse (err, "Cannot load requested library identity.");
    if (part.missingDef || expected == APINULLGuid || GSGuid2APIGuid (GS::UnID (part.ownUnID).GetMainGuid ()) != expected)
        return CreateErrorResponse (APIERR_BADPARS, "Library identity changed or is missing; refresh the target project's library search.");
    GS::ObjectState result ("source", "embeddedLibraryPreview", "libraryPart", *reference);
    API_LibPartSection section = {}; section.sectType = API_SectInfoGIF;
    GSHandle data = nullptr;
    const GS::OnExit disposeData ([&] () { BMKillHandle (&data); });
    err = ACAPI_LibraryPart_GetSection (part.index, &section, &data, nullptr);
    if (err == APIERR_NOLIBSECT) {
        result.Add ("available", false); result.Add ("reason", "No embedded MIME preview section."); return result;
    }
    if (err != NoError) return CreateErrorResponse (err, "Cannot read the native library preview section.");
    const GSSize size = data == nullptr ? 0 : BMhGetSize (data);
    if (size < 2 || *data == nullptr) return CreateErrorResponse (APIERR_BADPARS, "Native preview section is empty or malformed.");
    // SDK LibPart_Test writes a NUL-terminated MIME string followed by the image bytes.
    const char* bytes = *data;
    const char* separator = static_cast<const char*> (std::memchr (bytes, 0, static_cast<size_t> (size < 128 ? size : 128)));
    if (separator == nullptr) return CreateErrorResponse (APIERR_BADPARS, "Preview MIME header is missing or oversized.");
    const GSSize headerSize = static_cast<GSSize> (separator-bytes+1);
    const GSSize imageSize = size-headerSize;
    const GS::UniString mime (bytes);
    if (imageSize <= 0 || imageSize > maximumBytes) {
        result.Add ("available", false); result.Add ("reason", "Image is empty or exceeds the requested byte limit."); return result;
    }
    if (mime != "image/png" && mime != "image/jpeg" && mime != "image/gif") {
        result.Add ("available", false); result.Add ("reason", "Embedded MIME format is not PNG, JPEG or GIF."); return result;
    }
    auto encoded = Base64Converter::Encode (bytes+headerSize, static_cast<USize> (imageSize));
    encoded.DeleteAll (GS::UniChar ('\n'));
    result.Add ("available", true); result.Add ("mimeType", mime); result.Add ("byteCount", imageSize); result.Add ("imageBase64", encoded);
    return result;
}

GS::Optional<GS::UniString> SearchLibraryPartsCommand::GetInputParametersSchema () const
{
    return R"({"type":"object","properties":{
        "query":{"type":"string","maxLength":256,"description":"Case-sensitive substring of document or file name."},
        "typeId":{"$ref":"#/LibraryPartType"},"placeableOnly":{"type":"boolean"},
        "offset":{"type":"integer","minimum":0},"limit":{"type":"integer","minimum":1,"maximum":100},
        "scanLimit":{"type":"integer","minimum":1,"maximum":1000}
    },"additionalProperties":false})";
}

GS::Optional<GS::UniString> SearchLibraryPartsCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{
        "libraryParts":{"type":"array","items":{"type":"object","properties":{
            "guid":{"type":"string"},"index":{"type":"integer"},"documentName":{"type":"string"},
            "fileName":{"type":"string"},"typeId":{"type":"string"},"ownUnID":{"type":"string"},
            "parentUnID":{"type":"string"},"isPlaceable":{"type":"boolean"},
            "isTemplate":{"type":"boolean"},"missingDefinition":{"type":"boolean"}
        },"required":["guid","index","documentName","fileName","typeId","ownUnID","parentUnID","isPlaceable","isTemplate","missingDefinition"],"additionalProperties":false}},
        "errors":{"type":"array","items":{"type":"object"}},"candidateCount":{"type":"integer"},
        "scannedCount":{"type":"integer"},"nextOffset":{"type":"integer"},"hasMore":{"type":"boolean"},
        "pagination":{"type":"string"}
    },"required":["libraryParts","errors","candidateCount","scannedCount","nextOffset","hasMore","pagination"],"additionalProperties":false})";
}

GS::ObjectState SearchLibraryPartsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl&) const
{
    GS::UniString query, type;
    parameters.Get ("query", query);
    parameters.Get ("typeId", type);
    if (type == "Pict") type = "Picture";
    bool placeableOnly = false;
    parameters.Get ("placeableOnly", placeableOnly);
    Int32 offset = 0, limit = 50, scanLimit = 500, total = 0;
    parameters.Get ("offset", offset);
    parameters.Get ("limit", limit);
    parameters.Get ("scanLimit", scanLimit);
    if (offset < 0 || limit < 1 || limit > 100 || scanLimit < 1 || scanLimit > 1000)
        return CreateErrorResponse (APIERR_BADPARS, "Invalid pagination limits.");
    const GSErrCode err = ACAPI_LibraryPart_GetNum (&total);
    if (err != NoError) return CreateErrorResponse (err, "Cannot enumerate target-project library parts.");
    GS::ObjectState response ("pagination", "LibraryIndexOffsetRestartAfterReload");
    const auto& add = response.AddList<GS::ObjectState> ("libraryParts");
    const auto& errors = response.AddList<GS::ObjectState> ("errors");
    Int32 cursor = offset, scanned = 0, returned = 0;
    while (cursor < total && scanned < scanLimit && returned < limit) {
        API_LibPart part = {};
        part.index = ++cursor;
        ++scanned;
        const GSErrCode partError = ACAPI_LibraryPart_Get (&part);
        const GS::OnExit dispose ([&] () { delete part.location; });
        if (partError != NoError) {
            GS::ObjectState failure = CreateErrorResponse (partError, "Cannot read library part.");
            failure.Add ("index", cursor);
            errors (failure);
            continue;
        }
        const GS::UniString partType = LibPartTypeIdToString (part.typeID);
        const GS::UniString name (part.docu_UName), fileName (part.file_UName);
        if (!type.IsEmpty () && type != partType) continue;
        if (placeableOnly && (!part.isPlaceable || part.isTemplate || part.missingDef)) continue;
        if (!query.IsEmpty () && name.FindFirst (query) == MaxUIndex && fileName.FindFirst (query) == MaxUIndex) continue;
        add (GS::ObjectState (
            "guid", GS::UnID (part.ownUnID).GetMainGuid ().ToUniString (), "index", cursor,
            "documentName", name, "fileName", fileName, "typeId", partType,
            "ownUnID", GS::UniString (part.ownUnID), "parentUnID", GS::UniString (part.parentUnID),
            "isPlaceable", part.isPlaceable, "isTemplate", part.isTemplate, "missingDefinition", part.missingDef));
        ++returned;
    }
    response.Add ("candidateCount", total);
    response.Add ("scannedCount", scanned);
    response.Add ("nextOffset", cursor);
    response.Add ("hasMore", cursor < total);
    return response;
}

GS::Optional<GS::UniString> CheckLibraryPartAncestryCommand::GetInputParametersSchema () const
{
    return R"({"type":"object","definitions":{"part":{"type":"object","properties":{"index":{"type":"integer","minimum":1},"guid":{"type":"string","format":"uuid"}},"required":["index","guid"],"additionalProperties":false}},"properties":{
        "candidate":{"$ref":"#/definitions/part"},"ancestor":{"$ref":"#/definitions/part"}
    },"required":["candidate","ancestor"],"additionalProperties":false})";
}

GS::Optional<GS::UniString> CheckLibraryPartAncestryCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{
        "relationship":{"type":"string","enum":["samePart","descendant","notDescendant"]},
        "isSubtype":{"type":"boolean"},"placementCompatibility":{"const":"notEvaluated"},
        "candidateUnID":{"type":"string"},"ancestorUnID":{"type":"string"}
    },"required":["relationship","isSubtype","placementCompatibility","candidateUnID","ancestorUnID"],"additionalProperties":false})";
}

GS::ObjectState CheckLibraryPartAncestryCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl&) const
{
#ifndef ServerMainVers_2700
    return CreateErrorResponse (APIERR_NOTSUPPORTED, "Native ancestry inspection requires Archicad 27 or newer in this command.");
#else
    API_LibPart candidate = {}, ancestor = {};
    const GS::OnExit dispose ([&] () { delete candidate.location; delete ancestor.location; });
    const auto load = [&] (const char* key, API_LibPart& part) -> GSErrCode {
        const auto* ref = parameters.Get (key);
        if (ref == nullptr || !ref->Get ("index", part.index) || part.index < 1) return APIERR_BADPARS;
        const API_Guid guid = GetGuidFromObjectState (*ref);
        if (guid == APINULLGuid) return APIERR_BADPARS;
        const GSErrCode err = ACAPI_LibraryPart_Get (&part);
        if (err != NoError) return err;
        if (part.missingDef || GSGuid2APIGuid (GS::UnID (part.ownUnID).GetMainGuid ()) != guid) return APIERR_BADPARS;
        return NoError;
    };
    GSErrCode err = load ("candidate", candidate);
    if (err == NoError) err = load ("ancestor", ancestor);
    if (err != NoError) return CreateErrorResponse (err, "Cannot resolve both loaded parts with the supplied index and main GUID. Refresh library discovery after reload.");
    const bool same = GS::UniString (candidate.ownUnID) == GS::UniString (ancestor.ownUnID);
    err = same ? APIERR_NOTSUBTYPEOF : ACAPI_LibraryPart_CheckLibPartSubtypeOf (candidate.ownUnID, ancestor.ownUnID);
    if (err != NoError && err != APIERR_NOTSUBTYPEOF) return CreateErrorResponse (err, "Native library ancestry lookup failed.");
    return GS::ObjectState ("relationship", same ? "samePart" : err == NoError ? "descendant" : "notDescendant",
        "isSubtype", err == NoError, "placementCompatibility", "notEvaluated",
        "candidateUnID", GS::UniString (candidate.ownUnID), "ancestorUnID", GS::UniString (ancestor.ownUnID));
#endif
}

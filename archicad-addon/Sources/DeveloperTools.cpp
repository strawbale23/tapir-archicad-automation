#include "DeveloperTools.hpp"

#include "SchemaDefinitions.hpp"
#include "AddOnVersion.hpp"

#include "File.hpp"

static std::vector<CommandGroup> gCommandGroups;

GS::Optional<GS::UniString> GetCommandContractsCommand::GetInputParametersSchema () const
{
    return R"({"type":"object","properties":{
        "commandName":{"type":"string","description":"Optional exact native command name."},
        "offset":{"type":"integer","minimum":0},
        "limit":{"type":"integer","minimum":1,"maximum":100},
        "includeSchemas":{"type":"boolean"}
    },"additionalProperties":false})";
}

GS::Optional<GS::UniString> GetCommandContractsCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{
        "contractVersion":{"type":"integer"},"addOnVersion":{"type":"string"},
        "commands":{"type":"array","items":{"type":"object","properties":{
            "name":{"type":"string"},"group":{"type":"string"},
            "description":{"type":"string"},"introducedVersion":{"type":"string"},
            "inputSchemaJson":{"type":"string"},"outputSchemaJson":{"type":"string"}
        },"required":["name","group","description","introducedVersion"],"additionalProperties":false}},
        "commonSchemaJson":{"type":"string"},"total":{"type":"integer"},
        "nextOffset":{"type":"integer"},"hasMore":{"type":"boolean"}
    },"required":["contractVersion","addOnVersion","commands","total","nextOffset","hasMore"],"additionalProperties":false})";
}

GS::ObjectState GetCommandContractsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl&) const
{
    GS::UniString name;
    parameters.Get ("commandName", name);
    Int32 offset = 0, limit = 25;
    bool includeSchemas = false;
    parameters.Get ("offset", offset);
    parameters.Get ("limit", limit);
    parameters.Get ("includeSchemas", includeSchemas);
    if (offset < 0 || limit < 1 || limit > 100)
        return CreateErrorResponse (APIERR_BADPARS, "offset must be nonnegative and limit between 1 and 100.");
    GS::ObjectState result ("contractVersion", 1, "addOnVersion", ADDON_VERSION);
    const auto& add = result.AddList<GS::ObjectState> ("commands");
    Int32 total = 0, returned = 0;
    for (const CommandGroup& group : gCommandGroups) {
        for (const CommandInfo& command : group.commands) {
            if (!name.IsEmpty () && command.name != name) continue;
            const Int32 index = total++;
            if (index < offset || returned >= limit) continue;
            GS::ObjectState entry ("name", command.name, "group", group.name,
                "description", command.description, "introducedVersion", command.version);
            if (includeSchemas) {
                entry.Add ("inputSchemaJson", command.inputScheme.HasValue () ? command.inputScheme.Get () : GS::UniString ("null"));
                entry.Add ("outputSchemaJson", command.outputScheme.HasValue () ? command.outputScheme.Get () : GS::UniString ("null"));
            }
            add (entry);
            ++returned;
        }
    }
    if (!name.IsEmpty () && total == 0)
        return CreateErrorResponse (APIERR_BADPARS, "No registered native command with the requested name.");
    result.Add ("total", total);
    result.Add ("nextOffset", offset + returned);
    result.Add ("hasMore", offset + returned < total);
    if (includeSchemas) result.Add ("commonSchemaJson", GetCommonSchemaDefinitions ());
    return result;
}

static bool WriteStringToFile (const IO::Location& location, const GS::UniString& content)
{
    IO::File file (location, IO::File::OnNotFound::Create);
    if (file.Open (IO::File::OpenMode::WriteEmptyMode) != NoError) {
        return false;
    }

    std::string contentString (content.ToCStr (CC_UTF8).Get ());
    if (file.WriteBin ((char*) contentString.c_str (), (GS::USize) contentString.size ()) != NoError) {
        return false;
    }

    file.Close ();
    return true;
}

static bool GenerateDocumentation (const IO::Location& folder, const std::vector<CommandGroup>& commandGroups)
{
    static const GS::UniString NullString ("null");
    IO::Location commonSchemaLocation = folder;
    commonSchemaLocation.AppendToLocal (IO::Name ("common_schema_definitions.js"));
    GS::UniString commonSchemaContent = "var gSchemaDefinitions = " + GetCommonSchemaDefinitions () + ";";
    if (!WriteStringToFile (commonSchemaLocation, commonSchemaContent)) {
        return false;
    }

    IO::Location commandDefinitionLocation = folder;
    commandDefinitionLocation.AppendToLocal (IO::Name ("command_definitions.js"));
    GS::UniString commandDefinitionContent = "var gCommands = [";
    for (size_t groupIndex = 0; groupIndex < commandGroups.size (); groupIndex++) {
        const CommandGroup& group = commandGroups[groupIndex];
        GS::UniString groupCommandsContent;
        for (size_t commandIndex = 0; commandIndex < group.commands.size (); commandIndex++) {
            const CommandInfo& command = group.commands[commandIndex];
            groupCommandsContent += GS::UniString::Printf (R"({
                "name": "%T",
                "version": "%T",
                "description": "%T",
                "inputScheme": %T,
                "outputScheme": %T
            })",
                command.name.ToPrintf (),
                command.version.ToPrintf (),
                command.description.ToPrintf (),
                command.inputScheme.HasValue () ? command.inputScheme.Get ().ToPrintf () : NullString.ToPrintf (),
                command.outputScheme.HasValue () ? command.outputScheme.Get ().ToPrintf () : NullString.ToPrintf ()
            );
            if (commandIndex < group.commands.size () - 1) {
                groupCommandsContent += ",";
            }
        }
        commandDefinitionContent += GS::UniString::Printf (R"({
            "name": "%T",
            "commands": [%T]
        })",
            group.name.ToPrintf (),
            groupCommandsContent.ToPrintf ()
        );
        if (groupIndex < commandGroups.size () - 1) {
            commandDefinitionContent += ",";
        }
    }
    commandDefinitionContent += "];";
    if (!WriteStringToFile (commandDefinitionLocation, commandDefinitionContent)) {
        return false;
    }

    return true;
}

CommandInfo::CommandInfo (const GS::UniString& name, const GS::UniString& description, const GS::UniString& version, const GS::Optional<GS::UniString>& inputScheme, const GS::Optional<GS::UniString>& outputScheme) :
    name (name),
    description (description),
    version (version),
    inputScheme (inputScheme),
    outputScheme (outputScheme)
{
}

CommandGroup::CommandGroup (const GS::UniString& name) :
    name (name),
    commands ()
{
}

void AddCommandGroup (const CommandGroup& commandGroup)
{
    gCommandGroups.push_back (commandGroup);
}

GenerateDocumentationCommand::GenerateDocumentationCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String GenerateDocumentationCommand::GetName () const
{
    return "GenerateDocumentation";
}

GS::Optional<GS::UniString> GenerateDocumentationCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "destinationFolder": {
                "type": "string",
                "description": "Destination folder for the generated documentation files.",
                "minLength": 1
            }
        },
        "additionalProperties": false,
        "required": [
            "destinationFolder"
        ]
    })";
}

GS::Optional<GS::UniString> GenerateDocumentationCommand::GetRawResponseSchema () const
{
    return R"({
        "$ref": "#/ExecutionResult"
    })";
}

GS::ObjectState GenerateDocumentationCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    GS::UniString destinationFolder;
    parameters.Get ("destinationFolder", destinationFolder);

    IO::Location destinationFolderLoc (destinationFolder);
    if (!GenerateDocumentation (destinationFolderLoc, gCommandGroups)) {
        return CreateFailedExecutionResult (APIERR_GENERAL, "Failed to generate documentation.");
    }
    return CreateSuccessfulExecutionResult ();
}

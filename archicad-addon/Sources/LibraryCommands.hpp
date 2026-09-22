#pragma once

#include "CommandBase.hpp"

class CheckLibraryPartAncestryCommand : public CommandBase
{
public:
    CheckLibraryPartAncestryCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "CheckLibraryPartAncestry"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};

class SearchLibraryPartsCommand : public CommandBase
{
public:
    SearchLibraryPartsCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "SearchLibraryParts"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};

class GetLibraryPartPreviewCommand : public CommandBase
{
public:
    GetLibraryPartPreviewCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "GetLibraryPartPreview"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};

class AddFilesToEmbeddedLibraryCommand : public CommandBase
{
public:
    AddFilesToEmbeddedLibraryCommand ();
    virtual GS::String GetName () const override;
    virtual GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    virtual GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    virtual GS::ObjectState Execute (const GS::ObjectState& parameters, GS::ProcessControl& processControl) const override;
};

class SetLibrariesCommand : public CommandBase
{
public:
    SetLibrariesCommand ();
    virtual GS::String GetName () const override;
    virtual GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    virtual GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    virtual GS::ObjectState Execute (const GS::ObjectState& parameters, GS::ProcessControl& processControl) const override;
};

class AddLibrariesCommand : public CommandBase
{
public:
    AddLibrariesCommand ();
    virtual GS::String GetName () const override;
    virtual GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    virtual GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    virtual GS::ObjectState Execute (const GS::ObjectState& parameters, GS::ProcessControl& processControl) const override;
};

class GetLibrariesCommand : public CommandBase
{
public:
    GetLibrariesCommand ();
    virtual GS::String GetName () const override;
    virtual GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    virtual GS::ObjectState Execute (const GS::ObjectState& parameters, GS::ProcessControl& processControl) const override;
};

class ReloadLibrariesCommand : public CommandBase
{
public:
    ReloadLibrariesCommand ();
    virtual GS::String GetName () const override;
    virtual GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    virtual GS::ObjectState Execute (const GS::ObjectState& parameters, GS::ProcessControl& processControl) const override;
};
class GetAvailableLibraryPartsCommand : public CommandBase
{
public:
    GetAvailableLibraryPartsCommand ();
    virtual GS::String GetName () const override;
    virtual GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    virtual GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    virtual GS::ObjectState Execute (const GS::ObjectState& parameters, GS::ProcessControl& processControl) const override;
};

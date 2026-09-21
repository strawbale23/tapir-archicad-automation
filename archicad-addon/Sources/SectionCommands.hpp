#pragma once
#include "CommandBase.hpp"
class ModifySectionSettingsCommand : public CommandBase {
public:
    ModifySectionSettingsCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "ModifySectionSettings"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};
class GetSectionSettingsCommand : public CommandBase {
public:
    GetSectionSettingsCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "GetSectionSettings"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};

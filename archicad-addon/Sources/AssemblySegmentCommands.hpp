#pragma once
#include "CommandBase.hpp"

// Updated 19 September 2026, 12:05 CEST. Native beam/column segment editing.
class GetAssemblySegmentsCommand : public CommandBase {
public:
    GetAssemblySegmentsCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "GetAssemblySegments"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};

class SetAssemblySegmentsCommand : public CommandBase {
public:
    SetAssemblySegmentsCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "SetAssemblySegments"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};

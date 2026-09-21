#pragma once
#include "CommandBase.hpp"

class GetElementHotspotsCommand : public CommandBase {
public:
    GetElementHotspotsCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "GetElementHotspots"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};

class StretchElementAtHotspotCommand : public CommandBase {
public:
    StretchElementAtHotspotCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "StretchElementAtHotspot"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};

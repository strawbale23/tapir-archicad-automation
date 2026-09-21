#pragma once
#include "CommandBase.hpp"

// Updated 19 September 2026, 12:12 CEST. Shared by dimension creation and revision.
GSErrCode ResolveNativeDimensionHotspot (const GS::ObjectState&, API_Base&, GS::UniString&);

class GetDimensionAnchorsCommand : public CommandBase {
public:
    GetDimensionAnchorsCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "GetDimensionAnchors"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};

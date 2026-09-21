#pragma once
#include "CommandBase.hpp"

// Distances follow the native wall reference line, not a finished room face.
GS::Optional<GS::UniString> ResolveWallOpeningPlacement (
    const GS::ObjectState& data, const API_Guid& wallGuid, double width, double& centerOffset);

class GetWallReferenceGeometryCommand : public CommandBase {
public:
    GetWallReferenceGeometryCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override;
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState& parameters, GS::ProcessControl&) const override;
};


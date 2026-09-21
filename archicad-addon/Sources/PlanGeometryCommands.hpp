#pragma once
#include "CommandBase.hpp"
// Updated 19 September 2026, 12:40 CEST.
class GetPlanPrimitivesCommand : public CommandBase {
public:
    GetPlanPrimitivesCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "GetPlanPrimitives"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};
class GetRoofGeometryCommand : public CommandBase {
public:
    GetRoofGeometryCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "GetRoofGeometry"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};

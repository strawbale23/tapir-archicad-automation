#pragma once
#include "CommandBase.hpp"

class GetNativeComponentQuantitiesCommand : public CommandBase
{
public:
    GetNativeComponentQuantitiesCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "GetNativeComponentQuantities"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};

class GetNativeQuantitiesCommand : public CommandBase
{
public:
    GetNativeQuantitiesCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "GetNativeQuantities"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};

class SetElementRenovationStatusCommand : public CommandBase
{
public:
    SetElementRenovationStatusCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "SetElementRenovationStatus"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};

class GetNativeQuantityDefinitionsCommand : public CommandBase
{
public:
    GetNativeQuantityDefinitionsCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "GetNativeQuantityDefinitions"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};

class GetRenovationFiltersCommand : public CommandBase
{
public:
    GetRenovationFiltersCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "GetRenovationFilters"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};

class SetActiveRenovationFilterCommand : public CommandBase
{
public:
    SetActiveRenovationFilterCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "SetActiveRenovationFilter"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};

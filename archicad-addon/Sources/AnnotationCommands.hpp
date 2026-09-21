#pragma once
#include "CommandBase.hpp"

class ModifyTextsCommand : public CommandBase
{
public:
    ModifyTextsCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "ModifyTexts"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};
class ModifyLabelsCommand : public CommandBase
{
public:
    ModifyLabelsCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "ModifyLabels"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};

class GetAnnotationDetailsCommand : public CommandBase
{
public:
    GetAnnotationDetailsCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "GetAnnotationDetails"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};

class ModifyDimensionSettingsCommand : public CommandBase
{
public:
    ModifyDimensionSettingsCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "ModifyDimensionSettings"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};

class EditDimensionChainCommand : public CommandBase {
public:
    EditDimensionChainCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "EditDimensionChain"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};

class SetAnnotationTextStyleCommand : public CommandBase {
public:
    SetAnnotationTextStyleCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "SetAnnotationTextStyle"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};

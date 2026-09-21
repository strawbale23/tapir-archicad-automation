#pragma once
#include "CommandBase.hpp"

// Updated 19 September 2026, 12:17 CEST.
class GetAnnotationFormattingCommand : public CommandBase {
public:
    GetAnnotationFormattingCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "GetAnnotationFormatting"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};
class SetAnnotationRunStylesCommand : public CommandBase {
public:
    SetAnnotationRunStylesCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "SetAnnotationRunStyles"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};

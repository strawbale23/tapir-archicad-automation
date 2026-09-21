#pragma once
#include "CommandBase.hpp"
// Updated 19 September 2026, 19:38 CEST.
class PositionDrawingTitlesCommand : public CommandBase {
public:
    PositionDrawingTitlesCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "PositionDrawingTitles"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};
class GetDrawingFramesCommand : public CommandBase {
public:
    GetDrawingFramesCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "GetDrawingFrames"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};
class PositionDrawingsCommand : public CommandBase {
public:
    PositionDrawingsCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "PositionDrawings"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};

#pragma once
#include "CommandBase.hpp"
// Updated 19 September 2026, 16:00 CEST.
class SetSlabEdgeSettingsCommand : public CommandBase
{
public:
    SetSlabEdgeSettingsCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "SetSlabEdgeSettings"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};
// Updated 19 September 2026, 15:22 CEST.
class SetSlabEdgeArcCommand : public CommandBase
{
public:
    SetSlabEdgeArcCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "SetSlabEdgeArc"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};
// Updated 19 September 2026, 13:59 CEST.
class OffsetSlabEdgeCommand : public CommandBase
{
public:
    OffsetSlabEdgeCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "OffsetSlabEdge"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};
class GetSlabTopologyCommand : public CommandBase
{
public:
    GetSlabTopologyCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "GetSlabTopology"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};
class MoveSlabVerticesCommand : public CommandBase
{
public:
    MoveSlabVerticesCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "MoveSlabVertices"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};

class EditSlabTopologyCommand : public CommandBase
{
public:
    EditSlabTopologyCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "EditSlabTopology"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};

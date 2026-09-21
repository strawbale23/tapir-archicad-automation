#pragma once
#include "ElementCreationCommands.hpp"
class CreatePolygonalWallsCommand : public CreateElementsCommandBase {
public:
    CreatePolygonalWallsCommand () : CreateElementsCommandBase ("CreatePolygonalWalls", API_WallID, "wallsData") {}
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::ObjectState> SetTypeSpecificParameters (API_Element&, API_ElementMemo&, const Stories&, const GS::ObjectState&) const override;
};

class GetPolygonalWallGeometryCommand : public CommandBase {
public:
    GetPolygonalWallGeometryCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "GetPolygonalWallGeometry"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};

class ModifyPolygonalWallGeometryCommand : public CommandBase {
public:
    ModifyPolygonalWallGeometryCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "ModifyPolygonalWallGeometry"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};

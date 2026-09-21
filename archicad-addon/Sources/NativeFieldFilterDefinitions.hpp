#pragma once

// Filterable native fields for GetElementContext's nativeFieldFilters: struct fields that
// live directly on API_Element (no memo/segment read needed), so filtering stays a single
// cheap ACAPI_Element_Get per candidate. Deliberately narrower than every numeric field
// Archicad exposes - covers the fields most commonly needed to select elements by a
// dimension (height, thickness, level, etc.), not a full reflection of the SDK. Extend the
// table as real gaps are found; each entry is one line, no per-type C++ needed.
struct NativeFieldFilterDefinition {
    API_ElemTypeID type;
    const char* name;
    double (*read) (const API_Element&);
};

inline const auto& NativeFieldFilterDefinitions ()
{
    static const NativeFieldFilterDefinition definitions[] = {
        {API_WallID, "height", [] (const API_Element& e) { return e.wall.height; }},
        {API_WallID, "thickness", [] (const API_Element& e) { return e.wall.thickness; }},
        {API_WallID, "bottomOffset", [] (const API_Element& e) { return e.wall.bottomOffset; }},
        {API_WallID, "topOffset", [] (const API_Element& e) { return e.wall.topOffset; }},
        {API_WallID, "offset", [] (const API_Element& e) { return e.wall.offset; }},
        {API_WallID, "arcAngle", [] (const API_Element& e) { return e.wall.angle; }},

        {API_SlabID, "thickness", [] (const API_Element& e) { return e.slab.thickness; }},
        {API_SlabID, "level", [] (const API_Element& e) { return e.slab.level; }},

        {API_ColumnID, "height", [] (const API_Element& e) { return e.column.height; }},
        {API_ColumnID, "bottomOffset", [] (const API_Element& e) { return e.column.bottomOffset; }},
        {API_ColumnID, "topOffset", [] (const API_Element& e) { return e.column.topOffset; }},

        {API_BeamID, "level", [] (const API_Element& e) { return e.beam.level; }},
        {API_BeamID, "offset", [] (const API_Element& e) { return e.beam.offset; }},

        {API_WindowID, "width", [] (const API_Element& e) { return e.window.openingBase.width; }},
        {API_WindowID, "height", [] (const API_Element& e) { return e.window.openingBase.height; }},
        {API_DoorID, "width", [] (const API_Element& e) { return e.door.openingBase.width; }},
        {API_DoorID, "height", [] (const API_Element& e) { return e.door.openingBase.height; }},
        {API_SkylightID, "width", [] (const API_Element& e) { return e.skylight.openingBase.width; }},
        {API_SkylightID, "height", [] (const API_Element& e) { return e.skylight.openingBase.height; }},

        {API_ZoneID, "stampAngle", [] (const API_Element& e) { return e.zone.stampAngle; }},
    };
    return definitions;
}

// Returns nullptr if elementType has no filterable field by that name.
inline double (*FindNativeFieldFilterReader (API_ElemTypeID type, const GS::UniString& name)) (const API_Element&)
{
    for (const auto& definition : NativeFieldFilterDefinitions ()) {
        if (definition.type == type && name == definition.name) return definition.read;
    }
    return nullptr;
}

// Comma-separated list of field names available for elementType, for error messages.
inline GS::UniString ListNativeFieldFilterNames (API_ElemTypeID type)
{
    GS::UniString names;
    for (const auto& definition : NativeFieldFilterDefinitions ()) {
        if (definition.type != type) continue;
        if (!names.IsEmpty ()) names += ", ";
        names += definition.name;
    }
    return names;
}

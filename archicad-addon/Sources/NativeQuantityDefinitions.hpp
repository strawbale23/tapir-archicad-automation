#pragma once

// Native quantity projections share a catalogue with runtime discovery.
// Units follow AC29 API_ElementQuantity; no client-derived quantities are mixed in.
struct NativeQuantityDefinition {
    API_ElemTypeID type;
    const char* name;
    const char* nativeField;
    const char* unit;
    double (*read) (const API_ElementQuantity&);
};

inline const auto& NativeQuantityDefinitions ()
{
    static const NativeQuantityDefinition definitions[] = {
        {API_WallID, "wall.volume", "wall.volume", "m3", [] (const API_ElementQuantity& q) { return q.wall.volume; }},
        {API_WallID, "wall.referenceSideSurface", "wall.surface1", "m2", [] (const API_ElementQuantity& q) { return q.wall.surface1; }},
        {API_WallID, "wall.oppositeSideSurface", "wall.surface2", "m2", [] (const API_ElementQuantity& q) { return q.wall.surface2; }},
        {API_SlabID, "slab.volume", "slab.volume", "m3", [] (const API_ElementQuantity& q) { return q.slab.volume; }},
        {API_SlabID, "slab.topSurface", "slab.topSurface", "m2", [] (const API_ElementQuantity& q) { return q.slab.topSurface; }},
        {API_SlabID, "slab.bottomSurface", "slab.bottomSurface", "m2", [] (const API_ElementQuantity& q) { return q.slab.bottomSurface; }},
        {API_SlabID, "slab.edgeSurface", "slab.edgeSurface", "m2", [] (const API_ElementQuantity& q) { return q.slab.edgeSurface; }},
        {API_SlabID, "slab.perimeter", "slab.perimeter", "m", [] (const API_ElementQuantity& q) { return q.slab.perimeter; }},
        {API_BeamID, "beam.volume", "beam.volume", "m3", [] (const API_ElementQuantity& q) { return q.beam.volume; }},
        {API_BeamID, "beam.topSurface", "beam.topSurface", "m2", [] (const API_ElementQuantity& q) { return q.beam.topSurface; }},
        {API_BeamID, "beam.bottomSurface", "beam.bottomSurface", "m2", [] (const API_ElementQuantity& q) { return q.beam.bottomSurface; }},
        {API_ColumnID, "column.coreVolume", "column.coreVolume", "m3", [] (const API_ElementQuantity& q) { return q.column.coreVolume; }},
        {API_ColumnID, "column.veneerVolume", "column.veneVolume", "m3", [] (const API_ElementQuantity& q) { return q.column.veneVolume; }},
        {API_ColumnID, "column.coreSurface", "column.coreSurface", "m2", [] (const API_ElementQuantity& q) { return q.column.coreSurface; }},
        {API_ColumnID, "column.veneerSurface", "column.veneSurface", "m2", [] (const API_ElementQuantity& q) { return q.column.veneSurface; }},
        {API_MeshID, "mesh.volume", "mesh.volume", "m3", [] (const API_ElementQuantity& q) { return q.mesh.volume; }},
        {API_MeshID, "mesh.topSurface", "mesh.topSurface", "m2", [] (const API_ElementQuantity& q) { return q.mesh.topSurface; }},
        {API_MeshID, "mesh.projectedArea", "mesh.projectedArea", "m2", [] (const API_ElementQuantity& q) { return q.mesh.projectedArea; }},
        {API_MorphID, "morph.volume", "morph.volume", "m3", [] (const API_ElementQuantity& q) { return q.morph.volume; }},
        {API_MorphID, "morph.surface", "morph.surface", "m2", [] (const API_ElementQuantity& q) { return q.morph.surface; }},
        {API_ObjectID, "object.volume", "symb.volume", "m3", [] (const API_ElementQuantity& q) { return q.symb.volume; }},
        {API_LampID, "object.volume", "symb.volume", "m3", [] (const API_ElementQuantity& q) { return q.symb.volume; }},
        {API_ObjectID, "object.surface", "symb.surface", "m2", [] (const API_ElementQuantity& q) { return q.symb.surface; }},
        {API_LampID, "object.surface", "symb.surface", "m2", [] (const API_ElementQuantity& q) { return q.symb.surface; }},
        {API_DoorID, "opening.volume", "door.volume", "m3", [] (const API_ElementQuantity& q) { return q.door.volume; }},
        {API_WindowID, "opening.volume", "door.volume", "m3", [] (const API_ElementQuantity& q) { return q.door.volume; }},
        {API_DoorID, "opening.surface", "door.surface", "m2", [] (const API_ElementQuantity& q) { return q.door.surface; }},
        {API_WindowID, "opening.surface", "door.surface", "m2", [] (const API_ElementQuantity& q) { return q.door.surface; }},
        {API_ZoneID, "zone.area", "zone.area", "m2", [] (const API_ElementQuantity& q) { return q.zone.area; }},
        {API_RoofID, "roof.volume", "roof.volume", "m3", [] (const API_ElementQuantity& q) { return q.roof.volume; }},
        {API_RoofID, "roof.topSurface", "roof.topSurface", "m2", [] (const API_ElementQuantity& q) { return q.roof.topSurface; }},
        {API_RoofID, "roof.bottomSurface", "roof.bottomSurface", "m2", [] (const API_ElementQuantity& q) { return q.roof.bottomSurface; }},
        {API_RoofID, "roof.edgeSurface", "roof.edgeSurface", "m2", [] (const API_ElementQuantity& q) { return q.roof.edgeSurface; }},
        {API_RoofID, "roof.perimeter", "roof.perimeter", "m", [] (const API_ElementQuantity& q) { return q.roof.perimeter; }},
        {API_RoofID, "roof.contourArea", "roof.contourArea", "m2", [] (const API_ElementQuantity& q) { return q.roof.contourArea; }},
        {API_RoofID, "roof.holesSurface", "roof.holesSurf", "m2", [] (const API_ElementQuantity& q) { return q.roof.holesSurf; }},
        {API_RoofID, "roof.holesPerimeter", "roof.holesPrm", "m", [] (const API_ElementQuantity& q) { return q.roof.holesPrm; }},
        {API_RoofID, "roof.grossVolume", "roof.grossVolume", "m3", [] (const API_ElementQuantity& q) { return q.roof.grossVolume; }},
    };
    return definitions;
}

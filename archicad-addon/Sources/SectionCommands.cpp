#include "SectionCommands.hpp"
#include "MigrationHelper.hpp"
#include <cmath>
#include <string>
#include <utility>
#include "GSUnID.hpp"
#include "GSProcessControl.hpp"

namespace {
GSErrCode SectionLine (const GS::ObjectState& p,const char* name,API_Coord*& output,UInt32& count,bool allowEmpty) {
    GS::Array<GS::ObjectState> points;
    if (!p.Get (name,points) || points.GetSize ()>1000 || (points.GetSize ()<2 && !(allowEmpty && points.IsEmpty ()))) return APIERR_BADPARS;
    count=points.GetSize ();
    if (count==0) return NoError;
    output=reinterpret_cast<API_Coord*> (BMAllocatePtr (static_cast<GSSize> (count*sizeof(API_Coord)),ALLOCATE_CLEAR,0));
    if (output==nullptr) return APIERR_MEMFULL;
    for (UInt32 i=0;i<count;++i) {
        output[i]=Get2DCoordinateFromObjectState (points[i]);
        if (!std::isfinite (output[i].x) || !std::isfinite (output[i].y)) return APIERR_BADPARS;
    }
    return NoError;
}
#ifdef ServerMainVers_2700
GSErrCode PrepareSectionMarker (const GS::ObjectState& spec,const API_Element& section,API_SubElement& marker,bool storyMarker=false) {
    const auto* selection=spec.Get("libraryPart"); GS::UniString policy;
    if (selection==nullptr || !spec.Get("parameterPolicy",policy) || policy!="UseSelectedPartDefaults") return APIERR_BADPARS;
    API_LibPart part={},parent={};
    const GS::OnExit dispose([&] { delete part.location; delete parent.location; });
    const API_Guid identity=GetGuidFromObjectState(*selection);
    if (!selection->Get("index",part.index) || part.index<1 || identity==APINULLGuid) return APIERR_BADPARS;
    auto err=ACAPI_LibraryPart_Get(&part);
    if (err!=NoError) return err;
    if (part.missingDef || !part.isPlaceable || part.isTemplate || GSGuid2APIGuid(GS::UnID(part.ownUnID).GetMainGuid())!=identity) return APIERR_BADPARS;
    GS::UniString revision;
    if (selection->Get("ownUnID",revision) && revision!=GS::UniString(part.ownUnID)) return APIERR_BADPARS;
    marker.subType=storyMarker ? APISubElement_SHMarker : APISubElement_MainMarker;
    const auto& segment=section.cutPlane.segment;
    API_Guid current=storyMarker ? segment.shSymbolId : segment.midMarkerId!=APINULLGuid ? segment.midMarkerId : segment.begMarkerId!=APINULLGuid ? segment.begMarkerId : segment.endMarkerId;
    if (current!=APINULLGuid) {
        marker.subElem.header.guid=current;
        err=ACAPI_Element_Get(&marker.subElem);
        if (err!=NoError) return err;
        if (GetElemTypeId(marker.subElem.header)!=API_ObjectID) return APIERR_BADID;
    } else {
        API_Element defaults={}; defaults.header.type=section.header.type;
        err=ACAPI_Element_GetDefaultsExt(&defaults,nullptr,1,&marker);
        if (err!=NoError) return err;
    }
    if (storyMarker) {
        // No fixed library GUID: constrain replacement to the loaded current/default
        // story marker's declared parent subtype. This intentionally rejects unrelated families.
        parent.index=marker.subElem.object.libInd;
        if (parent.index<1) return APIERR_BADINDEX;
        err=ACAPI_LibraryPart_Get(&parent);
        if (err==NoError && parent.missingDef) return APIERR_BADID;
    } else {
        err=ACAPI_LibraryPart_GetMarkerParent(section.header.type,parent);
    }
    if (err!=NoError || parent.parentUnID[0]==0) return err==NoError ? APIERR_GENERAL : err;
    err=ACAPI_LibraryPart_CheckLibPartSubtypeOf(part.ownUnID,parent.parentUnID);
    if (err!=NoError) return err;
    ACAPI_DisposeAddParHdl(&marker.memo.params);
    double a=0,b=0; Int32 count=0;
    err=ACAPI_LibraryPart_GetParams(part.index,&a,&b,&count,&marker.memo.params);
    if (err!=NoError) return err;
    marker.subElem.object.libInd=part.index;
    ACAPI_ELEMENT_MASK_CLEAR(marker.mask);
    ACAPI_ELEMENT_MASK_SET(marker.mask,API_ObjectType,libInd);
    return NoError;
}
#endif
GS::UniString SectionStamp (const API_Element& e) { return GS::UniString (std::to_string (e.header.modiStamp).c_str ()); }
GSErrCode ApplyPresentation (const GS::ObjectState& p, API_SectionSegment& s, API_Element& mask, bool& changed) {
    GS::UniString reference;
    if (p.Get ("referenceId",reference)) {
        if (reference.GetLength () >= API_UniLongNameLen) return APIERR_BADPARS;
        GS::ucscpy (s.cutPlIdStr,reference.ToUStr ().Get ());
        ACAPI_ELEMENT_MASK_SET (mask,API_CutPlaneType,segment.cutPlIdStr); changed=true;
    }
#define SECTION_BOOL(key,field) if (p.Get (key,s.field)) { ACAPI_ELEMENT_MASK_SET (mask,API_CutPlaneType,segment.field); changed=true; }
    SECTION_BOOL ("beginLine",begLine)
    SECTION_BOOL ("middleLine",middleLine)
    SECTION_BOOL ("endLine",endLine)
    SECTION_BOOL ("continuousLine",continuous)
    SECTION_BOOL ("transparency",transparency)
    SECTION_BOOL ("useUncutSurfaceFill",modelUseUncutSurfFill)
    SECTION_BOOL ("storyUseSymbolPens",shUseSymbolPens)
    SECTION_BOOL ("storyLine",shLineOn)
    SECTION_BOOL ("storyLeftMarker",shLeftMarkerOn)
    SECTION_BOOL ("storyRightMarker",shRightMarkerOn)
#undef SECTION_BOOL
#define SECTION_PEN(key,field,minimum) if (p.Contains (key)) { Int32 value=0; if (!p.Get (key,value) || value<minimum || value>255) return APIERR_BADPARS; s.field=static_cast<short> (value); ACAPI_ELEMENT_MASK_SET (mask,API_CutPlaneType,segment.field); changed=true; }
    SECTION_PEN ("cutLinePen",sectPen,1)
    SECTION_PEN ("cutFillPen",sectFillPen,1)
    SECTION_PEN ("cutFillBackgroundPen",sectFillBGPen,-1)
    SECTION_PEN ("uncutSurfaceBackgroundPen",modelUncutSurfBGPen,-1)
    SECTION_PEN ("boundaryPen",boundaryPen,1)
    SECTION_PEN ("storyLinePen",shLinePen,1)
    SECTION_PEN ("storyMarkerPen",shMarkerPen,1)
#undef SECTION_PEN
#define SECTION_SIZE(key,field) if (p.Contains (key)) { double value=0; if (!p.Get (key,value) || !std::isfinite (value) || value<=0) return APIERR_BADPARS; s.field=value; ACAPI_ELEMENT_MASK_SET (mask,API_CutPlaneType,segment.field); changed=true; }
    SECTION_SIZE ("textSizeMillimetres",textSize)
    SECTION_SIZE ("storyMarkerTextSizeMillimetres",shMarkerTextSize)
    SECTION_SIZE ("storyMarkerSizeMillimetres",shMarkerSize)
#undef SECTION_SIZE
#define SECTION_LINE_ATTRIBUTE(key,field) if (p.Contains (key)) { API_Attribute a={}; a.header.typeID=API_LinetypeID; a.header.guid=GetGuidFromArrayItem (key,p); if (a.header.guid==APINULLGuid) return APIERR_BADPARS; const GSErrCode err=ACAPI_Attribute_Get (&a); if (err!=NoError) return err; s.field=a.header.index; ACAPI_ELEMENT_MASK_SET (mask,API_CutPlaneType,segment.field); changed=true; }
    SECTION_LINE_ATTRIBUTE ("lineTypeId",ltypeInd)
    SECTION_LINE_ATTRIBUTE ("storyLineTypeId",shLineType)
    SECTION_LINE_ATTRIBUTE ("boundaryLineTypeId",boundaryLineType)
#undef SECTION_LINE_ATTRIBUTE
    // 2026-09-19 20:16 CEST: native story-marker typography and boundary controls.
    GS::UniString fontName;
    if (p.Get("storyMarkerFontName",fontName)) {
#ifdef ServerMainVers_2700
        API_FontType font={}; font.head.uniStringNamePtr=&fontName;
        const auto err=ACAPI_Font_GetFont(font);
        if (err!=NoError || font.head.index<1 || font.head.index>32767) return err!=NoError ? err : APIERR_BADINDEX;
        s.shMarkerFont=static_cast<short>(font.head.index);
        ACAPI_ELEMENT_MASK_SET(mask,API_CutPlaneType,segment.shMarkerFont); changed=true;
#else
        return APIERR_NOTSUPPORTED;
#endif
    }
    for (const auto& flag : {std::pair<const char*,unsigned short>("storyMarkerBold",APIFace_Bold),{"storyMarkerItalic",APIFace_Italic},{"storyMarkerUnderline",APIFace_Underline}}) {
        bool enabled=false;
        if (p.Get(flag.first,enabled)) {
            s.shMarkerFaceBits=enabled ? s.shMarkerFaceBits|flag.second : s.shMarkerFaceBits&~flag.second;
            ACAPI_ELEMENT_MASK_SET(mask,API_CutPlaneType,segment.shMarkerFaceBits); changed=true;
        }
    }
    GS::UniString choice;
    if (p.Get("boundaryDisplay",choice)) {
        if (choice=="UncutContours") s.boundaryDisplay=APIBound_UncutContours;
        else if (choice=="NoContours") s.boundaryDisplay=APIBound_NoContours;
        else if (choice=="OverrideContours") s.boundaryDisplay=APIBound_OverrideContours;
        else return APIERR_BADPARS;
        ACAPI_ELEMENT_MASK_SET(mask,API_CutPlaneType,segment.boundaryDisplay); changed=true;
    }
    if (p.Get ("storyLineAppearance",choice)) {
        if (choice=="None") s.shAppearance=APICutPl_SHANone;
        else if (choice=="ScreenOnly") s.shAppearance=APICutPl_SHADisplayOnly;
        else if (choice=="ScreenAndPrint") s.shAppearance=APICutPl_SHAAll;
        else return APIERR_BADPARS;
        ACAPI_ELEMENT_MASK_SET (mask,API_CutPlaneType,segment.shAppearance); changed=true;
    }
    if (p.Get ("uncutSurfaceFillMode",choice)) {
        if (choice=="UniformPen") s.modelUncutSurfFillType=APICutPl_PenColor;
        else if (choice=="SurfaceShaded") s.modelUncutSurfFillType=APICutPl_MaterialColorShaded;
        else if (choice=="SurfaceUnshaded") s.modelUncutSurfFillType=APICutPl_MaterialColorNonShaded;
        else return APIERR_BADPARS;
        ACAPI_ELEMENT_MASK_SET (mask,API_CutPlaneType,segment.modelUncutSurfFillType); changed=true;
    }
    for (const auto& flag : {std::pair<const char*,short> ("vectorHatching",APICutPl_VectorHatch),{"vectorShadows",APICutPl_VectorShadow},{"sunFrom3D",APICutPl_SunFrom3D}}) {
        bool enabled=false;
        if (p.Get (flag.first,enabled)) {
            s.effectBits=enabled ? s.effectBits|flag.second : s.effectBits&~flag.second;
            ACAPI_ELEMENT_MASK_SET (mask,API_CutPlaneType,segment.effectBits); changed=true;
        }
    }
    return NoError;
}
bool PresentationMatches(const GS::ObjectState& request,const API_SectionSegment& wanted,const API_SectionSegment& actual) {
#define MATCH_FIELD(key,field) if (request.Contains(key) && wanted.field!=actual.field) return false;
    MATCH_FIELD("beginLine",begLine) MATCH_FIELD("middleLine",middleLine)
    MATCH_FIELD("endLine",endLine) MATCH_FIELD("continuousLine",continuous)
    MATCH_FIELD("transparency",transparency) MATCH_FIELD("useUncutSurfaceFill",modelUseUncutSurfFill)
    MATCH_FIELD("storyLine",shLineOn) MATCH_FIELD("storyLeftMarker",shLeftMarkerOn)
    MATCH_FIELD("storyRightMarker",shRightMarkerOn) MATCH_FIELD("storyUseSymbolPens",shUseSymbolPens)
    MATCH_FIELD("cutLinePen",sectPen) MATCH_FIELD("cutFillPen",sectFillPen)
    MATCH_FIELD("cutFillBackgroundPen",sectFillBGPen) MATCH_FIELD("uncutSurfaceBackgroundPen",modelUncutSurfBGPen)
    MATCH_FIELD("storyLinePen",shLinePen) MATCH_FIELD("storyMarkerPen",shMarkerPen)
    MATCH_FIELD("boundaryPen",boundaryPen) MATCH_FIELD("lineTypeId",ltypeInd)
    MATCH_FIELD("storyLineTypeId",shLineType) MATCH_FIELD("boundaryLineTypeId",boundaryLineType)
    MATCH_FIELD("storyLineAppearance",shAppearance) MATCH_FIELD("uncutSurfaceFillMode",modelUncutSurfFillType)
    MATCH_FIELD("boundaryDisplay",boundaryDisplay) MATCH_FIELD("storyMarkerFontName",shMarkerFont)
#undef MATCH_FIELD
#define MATCH_SIZE(key,field) if (request.Contains(key) && (!std::isfinite(actual.field) || std::abs(wanted.field-actual.field)>1e-8)) return false;
    MATCH_SIZE("textSizeMillimetres",textSize) MATCH_SIZE("storyMarkerTextSizeMillimetres",shMarkerTextSize)
    MATCH_SIZE("storyMarkerSizeMillimetres",shMarkerSize)
#undef MATCH_SIZE
    if (request.Contains("referenceId") && GS::UniString(wanted.cutPlIdStr)!=GS::UniString(actual.cutPlIdStr)) return false;
    for (const auto& flag:{std::pair<const char*,unsigned short>("storyMarkerBold",APIFace_Bold),{"storyMarkerItalic",APIFace_Italic},{"storyMarkerUnderline",APIFace_Underline}})
        if (request.Contains(flag.first) && (wanted.shMarkerFaceBits&flag.second)!=(actual.shMarkerFaceBits&flag.second)) return false;
    for (const auto& flag:{std::pair<const char*,short>("vectorHatching",APICutPl_VectorHatch),{"vectorShadows",APICutPl_VectorShadow},{"sunFrom3D",APICutPl_SunFrom3D}})
        if (request.Contains(flag.first) && (wanted.effectBits&flag.second)!=(actual.effectBits&flag.second)) return false;
    return true;
}
}

GS::Optional<GS::UniString> ModifySectionSettingsCommand::GetInputParametersSchema () const
{
    return GS::UniString(R"({
    "type": "object",
    "properties": {
        "sectionsWithSettings": {
            "type": "array",
            "minItems": 1,
            "maxItems": 100,
            "items": {
                "type": "object",
                "properties": {
                    "elementId": {
                        "$ref": "#/ElementId"
                    },
                    "name": {
                        "type": "string",
                        "maxLength": 255
                    },
                    "expectedModificationStamp": {
                        "type": "string",
                        "pattern": "^[0-9]+$"
                    },
                    "horizontalRange": {
                        "enum": [
                            "Infinite",
                            "Limited",
                            "ZeroDepth"
                        ],
                        "description": "Retain existing limit geometry; Limited requires an existing depth line or a supplied geometry. Geometry alone retains its legacy Limited default."
                    },
                    "markerPosition": {
                        "enum": [
                            "Middle",
                            "Ends"
                        ]
                    },
                    "segmentGeometry": {
                        "type": "object",
                        "properties": {
                            "mainCoordinates": {
                                "type": "array",
                                "minItems": 2,
                                "maxItems": 1000,
                                "items": {
                                    "$ref": "#/Coordinate2D"
                                }
                            },
                            "depthCoordinates": {
                                "type": "array",
                                "maxItems": 1000,
                                "items": {
                                    "$ref": "#/Coordinate2D"
                                }
                            },
                            "distantCoordinates": {
                                "type": "array",
                                "maxItems": 1000,
                                "items": {
                                    "$ref": "#/Coordinate2D"
                                }
                            },
                            "markedDistantArea": {
                                "type": "boolean"
                            }
                        },
                        "required": [
                            "mainCoordinates",
                            "depthCoordinates",
                            "distantCoordinates",
                            "markedDistantArea"
                        ],
                        "additionalProperties": false,
                        "description": "Complete native main/depth/distant coordinate sequences, in project XY metres. Supports broken lines; no automatic offsets are inferred. Depth/distant lines are empty or have at least two coordinates. Requires expectedModificationStamp. Native geometry constraints are enforced by Archicad."
                    },
                    "presentation": {
                        "type": "object",
                        "minProperties": 1,
                        "properties": {
                            "referenceId": {
                                "type": "string",
                                "maxLength": 255
                            },
                            "lineTypeId": {
                                "$ref": "#/AttributeId"
                            },
                            "storyLineTypeId": {
                                "$ref": "#/AttributeId"
                            },
                            "beginLine": {
                                "type": "boolean"
                            },
                            "middleLine": {
                                "type": "boolean"
                            },
                            "endLine": {
                                "type": "boolean"
                            },
                            "continuousLine": {
                                "type": "boolean"
                            },
                            "transparency": {
                                "type": "boolean"
                            },
                            "vectorHatching": {
                                "type": "boolean"
                            },
                            "vectorShadows": {
                                "type": "boolean"
                            },
                            "sunFrom3D": {
                                "type": "boolean"
                            },
                            "useUncutSurfaceFill": {
                                "type": "boolean"
                            },
                            "uncutSurfaceFillMode": {
                                "enum": [
                                    "UniformPen",
                                    "SurfaceShaded",
                                    "SurfaceUnshaded"
                                ]
                            },
                            "cutLinePen": {
                                "type": "integer",
                                "minimum": 1,
                                "maximum": 255
                            },
                            "cutFillPen": {
                                "type": "integer",
                                "minimum": 1,
                                "maximum": 255
                            },
                            "cutFillBackgroundPen": {
                                "type": "integer",
                                "minimum": -1,
                                "maximum": 255
                            },
                            "uncutSurfaceBackgroundPen": {
                                "type": "integer",
                                "minimum": -1,
                                "maximum": 255
                            },
                            "textSizeMillimetres": {
                                "type": "number",
                                "exclusiveMinimum": 0
                            },
                            "storyMarkerTextSizeMillimetres": {
                                "type": "number",
                                "exclusiveMinimum": 0
                            },
                            "storyMarkerSizeMillimetres": {
                                "type": "number",
                                "exclusiveMinimum": 0
                            },
                            "storyLineAppearance": {
                                "enum": [
                                    "None",
                                    "ScreenOnly",
                                    "ScreenAndPrint"
                                ]
                            },
                            "storyLine": {
                                "type": "boolean"
                            },
                            "storyLeftMarker": {
                                "type": "boolean"
                            },
                            "storyRightMarker": {
                                "type": "boolean"
                            },
                            "storyLinePen": {
                                "type": "integer",
                                "minimum": 1,
                                "maximum": 255
                            },
                            "storyUseSymbolPens": {"type":"boolean"},
                            "storyMarkerFontName": {"type":"string","minLength":1,"maxLength":255,"description":"Loaded font name. Archicad 27 or newer."},
                            "storyMarkerBold": {"type":"boolean"},
                            "storyMarkerItalic": {"type":"boolean"},
                            "storyMarkerUnderline": {"type":"boolean"},
                            "boundaryDisplay": {"enum":["UncutContours","NoContours","OverrideContours"]},
                            "boundaryPen": {"type":"integer","minimum":1,"maximum":255},
                            "boundaryLineTypeId": {"$ref":"#/AttributeId"},
                            "storyMarkerPen": {
                                "type": "integer",
                                "minimum": 1,
                                "maximum": 255
                            }
                        },
                        "additionalProperties": false,
                        "description": "Native section segment presentation. Symbolic markers may override text/pen settings through their library parameters."
                    },
                    "geometry": {
                        "type": "object",
                        "properties": {
                            "startCoordinate": {
                                "$ref": "#/Coordinate2D"
                            },
                            "endCoordinate": {
                                "$ref": "#/Coordinate2D"
                            },
                            "depth": {
                                "type": "number",
                                "exclusiveMinimum": 0
                            }
                        },
                        "required": [
                            "startCoordinate",
                            "endCoordinate",
                            "depth"
                        ],
                        "additionalProperties": false,
                        "description": "Metres in project XY. Looks to the left of start-to-end. Sets limited depth; existing broken or distant lines are rejected."
                    },
                    "verticalRange": {
                        "type": "object",
                        "properties": {
                            "mode": {
                                "enum": [
                                    "Infinite",
                                    "Limited"
                                ]
                            },
                            "minimum": {
                                "type": "number"
                            },
                            "maximum": {
                                "type": "number"
                            }
                        },
                        "required": [
                            "mode"
                        ],
                        "additionalProperties": false,
                        "oneOf": [
                            {
                                "properties": {
                                    "mode": {
                                        "const": "Infinite"
                                    }
                                },
                                "maxProperties": 1
                            },
                            {
                                "properties": {
                                    "mode": {
                                        "const": "Limited"
                                    }
                                },
                                "required": [
                                    "minimum",
                                    "maximum"
                                ]
                            }
                        ],
                        "description": "Limited values are metres above project zero, not relative to a story."
                    },
                    "linePen": {
                        "type": "integer",
                        "minimum": 1,
                        "maximum": 255
                    },
                    "textPen": {
                        "type": "integer",
                        "minimum": 1,
                        "maximum": 255
                    },
                    "beginMarker": {
                        "type": "boolean"
                    },
                    "endMarker": {
                        "type": "boolean"
                    },
                    "useElementPens": {
                        "type": "boolean"
                    },
)") + R"(                    "mainMarker": {
                        "type": "object",
                        "description": "Archicad 27+. Replace the section/elevation main marker library part through its parent element. Uses the selected loaded part's default GDL parameters; existing custom marker parameters are deliberately reset. Marker visibility and section geometry are retained unless explicitly edited. Requires the observed modification stamp.",
                        "properties": {
                            "libraryPart": {
                                "type": "object",
                                "properties": {
                                    "index": {
                                        "type": "integer",
                                        "minimum": 1
                                    },
                                    "guid": {
                                        "type": "string",
                                        "format": "uuid"
                                    },
                                    "ownUnID": {
                                        "type": "string",
                                        "minLength": 1
                                    }
                                },
                                "required": [
                                    "index",
                                    "guid"
                                ],
                                "additionalProperties": false
                            },
                            "parameterPolicy": {
                                "const": "UseSelectedPartDefaults"
                            }
                        },
                        "required": [
                            "libraryPart",
                            "parameterPolicy"
                        ],
                        "additionalProperties": false
                    },
                    "storyMarker": {
                        "type": "object",
                        "description": "Archicad 27+. Replace the story-level marker through its section/elevation parent. The selected part must derive from the current marker's declared parent subtype (or the project default marker when absent); unrelated families are rejected. Uses the selected loaded part's default GDL parameters; existing custom marker parameters are deliberately reset. Marker visibility and section geometry are retained unless explicitly edited. Requires the observed modification stamp.",
                        "properties": {
                            "libraryPart": {
                                "type": "object",
                                "properties": {
                                    "index": {
                                        "type": "integer",
                                        "minimum": 1
                                    },
                                    "guid": {
                                        "type": "string",
                                        "format": "uuid"
                                    },
                                    "ownUnID": {
                                        "type": "string",
                                        "minLength": 1
                                    }
                                },
                                "required": [
                                    "index",
                                    "guid"
                                ],
                                "additionalProperties": false
                            },
                            "parameterPolicy": {
                                "const": "UseSelectedPartDefaults"
                            }
                        },
                        "required": [
                            "libraryPart",
                            "parameterPolicy"
                        ],
                        "additionalProperties": false
                    }
                },
                "required": [
                    "elementId"
                ],
                "minProperties": 2,
                "additionalProperties": false,
                "not": {
                    "required": [
                        "geometry",
                        "segmentGeometry"
                    ]
                },
                "allOf": [
                    {
                        "if": {
                            "required": [
                                "segmentGeometry"
                            ]
                        },
                        "then": {
                            "required": [
                                "expectedModificationStamp"
                            ]
                        }
                    },
                    {
                        "if": {
                            "anyOf": [{"required":["mainMarker"]},{"required":["storyMarker"]}]
                        },
                        "then": {
                            "required": [
                                "expectedModificationStamp"
                            ]
                        }
                    }
                ]
            }
        }
    },
    "required": [
        "sectionsWithSettings"
    ],
    "additionalProperties": false
})";
}
GS::Optional<GS::UniString> ModifySectionSettingsCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{"executionResults":{"$ref":"#/ExecutionResults"}},"required":["executionResults"],"additionalProperties":false})";
}
static GSErrCode LoadSection (const API_Guid& guid, API_Element& element)
{
    element = {}; element.header.guid = guid;
    const GSErrCode err = ACAPI_Element_Get (&element);
    if (err != NoError) return err;
    const auto type = GetElemTypeId (element.header);
    return type == API_CutPlaneID || type == API_ElevationID ? NoError : APIERR_BADID;
}
GS::ObjectState ModifySectionSettingsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& processControl) const
{
    GS::Array<GS::ObjectState> items;
    parameters.Get ("sectionsWithSettings", items);
    if (items.IsEmpty () || items.GetSize () > 100) return CreateErrorResponse (APIERR_BADPARS, "Supply 1 to 100 section/elevation edits.");
    GS::ObjectState response;
    const auto& add = response.AddList<GS::ObjectState> ("executionResults");
    const GSErrCode transaction = ACAPI_CallUndoableCommand ("Modify Section Settings", [&] () -> GSErrCode {
        for (const auto& item : items) {
            if (processControl.TestBreak()) return APIERR_CANCEL;
            API_Element element = {}, mask;
            GSErrCode err = LoadSection (GetGuidFromArrayItem ("elementId", item), element);
            if (err != NoError) { add (CreateFailedExecutionResult (err, "Expected a native section or elevation.")); continue; }
            GS::UniString expected;
            if (item.Get ("expectedModificationStamp",expected) && expected!=SectionStamp (element)) { add (CreateFailedExecutionResult (APIERR_BADPARS,"Section changed since inspection.")); continue; }
            if (!ACAPI_Element_Filter (element.header.guid,APIFilt_IsEditable | APIFilt_InMyWorkspace | APIFilt_HasAccessRight)) { add (CreateFailedExecutionResult (APIERR_NOTEDITABLE,"Section is not editable.")); continue; }
            API_ElementMemo memo = {};
            API_SubElement markerChanges[2]={}; UInt32 markerCount=0; bool replaceMarker=false,replaceStoryMarker=false;
            auto& mainMarker=markerChanges[0];
            const GS::OnExit dispose ([&] () { ACAPI_DisposeElemMemoHdls (&memo); for (auto& marker:markerChanges) ACAPI_DisposeElemMemoHdls(&marker.memo); });
            ACAPI_ELEMENT_MASK_CLEAR (mask);
            auto& segment = element.cutPlane.segment; // API_ElevationType aliases API_CutPlaneType.
            bool changed = false;
            UInt64 memoMask = 0;
            if (const auto* markerSpec=item.Get("mainMarker")) {
                if (!item.Contains("expectedModificationStamp")) { add(CreateFailedExecutionResult(APIERR_BADPARS,"Marker replacement requires the observed section revision.")); continue; }
#ifdef ServerMainVers_2700
                err=PrepareSectionMarker(*markerSpec,element,mainMarker);
#else
                (void)markerSpec;
                err=APIERR_NOTSUPPORTED;
#endif
                if (err!=NoError) { add(CreateFailedExecutionResult(err,"Cannot select a compatible loaded section marker or read its default parameters. No section changes applied.")); continue; }
                replaceMarker=true; ++markerCount; changed=true;
            }
            if (const auto* markerSpec=item.Get("storyMarker")) {
                if (!item.Contains("expectedModificationStamp")) { add(CreateFailedExecutionResult(APIERR_BADPARS,"Story marker replacement requires the observed section revision.")); continue; }
#ifdef ServerMainVers_2700
                err=PrepareSectionMarker(*markerSpec,element,markerChanges[markerCount],true);
#else
                (void)markerSpec; err=APIERR_NOTSUPPORTED;
#endif
                if (err!=NoError) { add(CreateFailedExecutionResult(err,"Cannot select a loaded story marker compatible with the current/default marker family. No section changes applied.")); continue; }
                replaceStoryMarker=true; ++markerCount; changed=true;
            }
            if (const auto* geometry=item.Get ("segmentGeometry")) {
                if (item.Contains ("geometry") || !item.Contains ("expectedModificationStamp")) { add (CreateFailedExecutionResult (APIERR_BADPARS,"Complete section geometry requires the observed revision and cannot be combined with straight geometry.")); continue; }
                err=SectionLine (*geometry,"mainCoordinates",memo.sectionSegmentMainCoords,segment.nMainCoord,false);
                if (err==NoError) err=SectionLine (*geometry,"depthCoordinates",memo.sectionSegmentDepthCoords,segment.nDepthCoord,true);
                if (err==NoError) err=SectionLine (*geometry,"distantCoordinates",memo.sectionSegmentDistCoords,segment.nDistCoord,true);
                if (err==NoError && !geometry->Get ("markedDistantArea",segment.markedDistArea)) err=APIERR_BADPARS;
                if (err!=NoError || (segment.markedDistArea && segment.nDistCoord<2)) { add (CreateFailedExecutionResult (err==NoError ? APIERR_BADPARS : err,"Invalid section line coordinates or distant-area limit.")); continue; }
                ACAPI_ELEMENT_MASK_SET (mask,API_CutPlaneType,segment.nMainCoord);
                ACAPI_ELEMENT_MASK_SET (mask,API_CutPlaneType,segment.nDepthCoord);
                ACAPI_ELEMENT_MASK_SET (mask,API_CutPlaneType,segment.nDistCoord);
                ACAPI_ELEMENT_MASK_SET (mask,API_CutPlaneType,segment.markedDistArea);
                memoMask=APIMemoMask_SectionMainCoords | APIMemoMask_SectionDepthCoords | APIMemoMask_SectionDistCoords;
                changed=true;
            }
            if (const auto* presentation=item.Get ("presentation")) {
                err=ApplyPresentation (*presentation,segment,mask,changed);
                if (err!=NoError) { add (CreateFailedExecutionResult (err,"Invalid section presentation or attribute reference; no changes to this section.")); continue; }
            }
            GS::UniString name;
            if (item.Get ("name", name)) {
                if (name.GetLength () >= API_UniLongNameLen) { add (CreateFailedExecutionResult (APIERR_BADPARS, "Section name exceeds the native UTF-16 capacity.")); continue; }
                GS::ucscpy (segment.cutPlName, name.ToUStr ().Get ());
                ACAPI_ELEMENT_MASK_SET (mask, API_CutPlaneType, segment.cutPlName); changed = true;
            }
            if (const auto* geometry = item.Get ("geometry")) {
                if (segment.nMainCoord != 2 || segment.nDistCoord != 0 || segment.nDepthCoord > 2) { add (CreateFailedExecutionResult (APIERR_BADPARS, "Geometry replacement supports straight sections without distant lines.")); continue; }
                const auto* start = geometry->Get ("startCoordinate"); const auto* end = geometry->Get ("endCoordinate");
                double depth = 0; geometry->Get ("depth", depth);
                if (start == nullptr || end == nullptr) { add (CreateFailedExecutionResult (APIERR_BADPARS, "Start and end are required.")); continue; }
                const API_Coord a = Get2DCoordinateFromObjectState (*start), b = Get2DCoordinateFromObjectState (*end);
                const double dx = b.x-a.x, dy = b.y-a.y, length = std::hypot (dx,dy);
                if (!std::isfinite (length) || length < 1e-6 || !std::isfinite (depth) || depth <= 0) { add (CreateFailedExecutionResult (APIERR_BADPARS, "Finite distinct endpoints and positive depth are required.")); continue; }
                memo.sectionSegmentMainCoords = reinterpret_cast<API_Coord*> (BMpAll (2*sizeof(API_Coord)));
                memo.sectionSegmentDepthCoords = reinterpret_cast<API_Coord*> (BMpAll (2*sizeof(API_Coord)));
                if (memo.sectionSegmentMainCoords == nullptr || memo.sectionSegmentDepthCoords == nullptr) { add (CreateFailedExecutionResult (APIERR_MEMFULL, "Cannot allocate section geometry.")); continue; }
                memo.sectionSegmentMainCoords[0]=a; memo.sectionSegmentMainCoords[1]=b;
                memo.sectionSegmentDepthCoords[0]={a.x-dy/length*depth,a.y+dx/length*depth};
                memo.sectionSegmentDepthCoords[1]={b.x-dy/length*depth,b.y+dx/length*depth};
                segment.nDepthCoord=2; segment.horizRange=APIHorRange_Limited;
                ACAPI_ELEMENT_MASK_SET (mask, API_CutPlaneType, segment.nDepthCoord);
                ACAPI_ELEMENT_MASK_SET (mask, API_CutPlaneType, segment.horizRange);
                memoMask = APIMemoMask_SectionMainCoords | APIMemoMask_SectionDepthCoords; changed = true;
            }
            if (const auto* range = item.Get ("verticalRange")) {
                GS::UniString mode; range->Get ("mode", mode);
                if (mode == "Infinite") segment.vertRange = APIVerRange_Infinite;
                else if (mode == "Limited") {
                    double minimum=0, maximum=0;
                    if (!range->Get ("minimum",minimum) || !range->Get ("maximum",maximum) || !std::isfinite(minimum) || !std::isfinite(maximum) || maximum<=minimum) { add (CreateFailedExecutionResult (APIERR_BADPARS, "Vertical maximum must exceed minimum.")); continue; }
                    segment.vertRange=APIVerRange_Limited; segment.vertMin=minimum; segment.vertMax=maximum; segment.relativeToStory=false;
                    ACAPI_ELEMENT_MASK_SET (mask, API_CutPlaneType, segment.vertMin); ACAPI_ELEMENT_MASK_SET (mask, API_CutPlaneType, segment.vertMax);
                    ACAPI_ELEMENT_MASK_SET (mask, API_CutPlaneType, segment.relativeToStory);
                } else { add (CreateFailedExecutionResult (APIERR_BADPARS, "Invalid vertical range mode.")); continue; }
                ACAPI_ELEMENT_MASK_SET (mask, API_CutPlaneType, segment.vertRange); changed = true;
            }
            GS::UniString horizontal,marker;
            if (item.Get ("horizontalRange",horizontal)) {
                if (horizontal=="Infinite") segment.horizRange=APIHorRange_Infinite;
                else if (horizontal=="ZeroDepth") segment.horizRange=APIHorRange_ZeroDepth;
                else if (horizontal=="Limited" && segment.nDepthCoord>=2) segment.horizRange=APIHorRange_Limited;
                else { add (CreateFailedExecutionResult (APIERR_BADPARS,"Limited range requires existing or supplied depth geometry.")); continue; }
                ACAPI_ELEMENT_MASK_SET (mask,API_CutPlaneType,segment.horizRange); changed=true;
            }
            if (item.Get ("markerPosition",marker)) {
                if (marker=="Middle") element.cutPlane.markerShow=APICutPl_ShowMiddleMarker;
                else if (marker=="Ends") element.cutPlane.markerShow=APICutPl_ShowWingMarkers;
                else { add (CreateFailedExecutionResult (APIERR_BADPARS,"Unknown marker position.")); continue; }
                ACAPI_ELEMENT_MASK_SET (mask,API_CutPlaneType,markerShow); changed=true;
            }
            if (item.Get ("linePen",segment.linePen)) { ACAPI_ELEMENT_MASK_SET (mask, API_CutPlaneType, segment.linePen); changed=true; }
            if (item.Get ("textPen",segment.textPen)) { ACAPI_ELEMENT_MASK_SET (mask, API_CutPlaneType, segment.textPen); changed=true; }
            if (item.Get ("beginMarker",segment.begMark)) { ACAPI_ELEMENT_MASK_SET (mask, API_CutPlaneType, segment.begMark); changed=true; }
            if (item.Get ("endMarker",segment.endMark)) { ACAPI_ELEMENT_MASK_SET (mask, API_CutPlaneType, segment.endMark); changed=true; }
            if (item.Get ("useElementPens",segment.useElemPens)) { ACAPI_ELEMENT_MASK_SET (mask, API_CutPlaneType, segment.useElemPens); changed=true; }
            if (!changed) { add (CreateFailedExecutionResult (APIERR_BADPARS, "No section settings supplied.")); continue; }
            if (item.Contains ("segmentGeometry") && segment.horizRange==APIHorRange_Limited && segment.nDepthCoord<2) { add (CreateFailedExecutionResult (APIERR_BADPARS,"Limited section range requires a depth line; supply it or explicitly choose another horizontal range.")); continue; }
            // Snapshot requested values before the SDK can update its in/out structures.
            const API_SectionSegment requestedSegment=segment;
            const Int32 requestedMainPart=replaceMarker ? mainMarker.subElem.object.libInd : 0;
            const Int32 requestedStoryPart=replaceStoryMarker ? markerChanges[markerCount-1].subElem.object.libInd : 0;
            err = ACAPI_Element_ChangeExt (&element,&mask,memoMask != 0 ? &memo : nullptr,memoMask,markerCount,markerCount!=0 ? markerChanges : nullptr,true,0);
            if (err==NoError && replaceMarker) {
                API_Element observed={}; err=LoadSection(element.header.guid,observed);
                bool found=false;
                if (err==NoError) for (const auto& id:{observed.cutPlane.segment.begMarkerId,observed.cutPlane.segment.midMarkerId,observed.cutPlane.segment.endMarkerId}) {
                    if (id==APINULLGuid) continue;
                    found=true; API_Element marker={}; marker.header.guid=id;
                    err=ACAPI_Element_Get(&marker);
                    if (err!=NoError || GetElemTypeId(marker.header)!=API_ObjectID || marker.object.libInd!=requestedMainPart) { if (err==NoError) err=APIERR_GENERAL; break; }
                }
                if (err!=NoError || !found) { add(CreateFailedExecutionResult(err==NoError ? APIERR_GENERAL : err,"Section edit was accepted, but marker-part readback was not confirmed. Inspect before retrying.")); continue; }
            }
            if (err==NoError && replaceStoryMarker) {
                API_Element observed={}; err=LoadSection(element.header.guid,observed);
                API_Element marker={};
                if (err==NoError) {
                    marker.header.guid=observed.cutPlane.segment.shSymbolId;
                    err=marker.header.guid==APINULLGuid ? APIERR_BADID : ACAPI_Element_Get(&marker);
                }
                if (err==NoError && (GetElemTypeId(marker.header)!=API_ObjectID || marker.object.libInd!=requestedStoryPart)) err=APIERR_GENERAL;
                if (err!=NoError) { add(CreateFailedExecutionResult(err,"Section edit was accepted, but story-marker replacement was not confirmed. Inspect before retrying.")); continue; }
            }
            if (err==NoError) {
                if (const auto* presentation=item.Get("presentation")) {
                    API_Element observed={}; err=LoadSection(element.header.guid,observed);
                    if (err==NoError && !PresentationMatches(*presentation,requestedSegment,observed.cutPlane.segment)) err=APIERR_GENERAL;
                    if (err!=NoError) { add(CreateFailedExecutionResult(err,"Section edit was accepted, but requested presentation settings were not confirmed. The section may have changed; inspect before retrying.")); continue; }
                }
            }
            add (err == NoError ? CreateSuccessfulExecutionResult () : CreateFailedExecutionResult (err,"Native section edit failed."));
        }
        return NoError;
    });
    if (transaction != NoError) return CreateErrorResponse (transaction,"Section transaction failed; committed state is not confirmed.");
    return response;
}

GS::Optional<GS::UniString> GetSectionSettingsCommand::GetInputParametersSchema () const
{
    return R"({"type":"object","properties":{"elementId":{"$ref":"#/ElementId"}},"required":["elementId"],"additionalProperties":false})";
}
GS::Optional<GS::UniString> GetSectionSettingsCommand::GetRawResponseSchema () const
{
    return R"({"type":"object","properties":{
        "elementId":{"$ref":"#/ElementId"},"elementType":{"type":"string"},"name":{"type":"string"},"units":{"const":"metres"},"modificationStamp":{"type":"string"},"presentation":{"type":"object"},"markers":{"type":"array","items":{"type":"object"}},"markerPosition":{"type":"string"},
        "mainCoordinates":{"type":"array","items":{"$ref":"#/Coordinate2D"}},"depthCoordinates":{"type":"array","items":{"$ref":"#/Coordinate2D"}},"distantCoordinates":{"type":"array","items":{"$ref":"#/Coordinate2D"}},"markedDistantArea":{"type":"boolean"},
        "horizontalRange":{"type":"string"},"verticalRange":{"type":"string"},"verticalMinimum":{"type":"number"},"verticalMaximum":{"type":"number"},"relativeToStory":{"type":"boolean"},"verticalRangeStoryIndex":{"type":"integer"},
        "linePen":{"type":"integer"},"textPen":{"type":"integer"},"beginMarker":{"type":"boolean"},"endMarker":{"type":"boolean"},"useElementPens":{"type":"boolean"}
    },"required":["elementId","elementType","name","units","mainCoordinates","depthCoordinates"],"additionalProperties":false})";
}
GS::ObjectState GetSectionSettingsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl&) const
{
    API_Element element = {}; API_ElementMemo memo = {};
    GSErrCode err = LoadSection (GetGuidFromArrayItem ("elementId",parameters),element);
    if (err != NoError) return CreateErrorResponse (err,"Expected a native section or elevation.");
    const auto& segment = element.cutPlane.segment;
    if (segment.nMainCoord > 1000 || segment.nDepthCoord > 1000 || segment.nDistCoord>1000) return CreateErrorResponse (APIERR_BADPARS,"Section exceeds coordinate response limit.");
    const GS::OnExit dispose ([&] () { ACAPI_DisposeElemMemoHdls (&memo); });
    err = ACAPI_Element_GetMemo (element.header.guid,&memo,APIMemoMask_SectionMainCoords | APIMemoMask_SectionDepthCoords | APIMemoMask_SectionDistCoords);
    if (err != NoError) return CreateErrorResponse (err,"Cannot read section coordinates.");
    const auto validCoordinates=[] (const API_Coord* coordinates,UInt32 count) {
        if (count==0) return true;
        if (coordinates==nullptr || BMGetPtrSize (reinterpret_cast<GSPtr> (const_cast<API_Coord*> (coordinates))) < static_cast<GSSize> (count*sizeof(API_Coord))) return false;
        for (UInt32 i=0;i<count;++i) if (!std::isfinite (coordinates[i].x) || !std::isfinite (coordinates[i].y)) return false;
        return true;
    };
    if (!validCoordinates (memo.sectionSegmentMainCoords,segment.nMainCoord) || !validCoordinates (memo.sectionSegmentDepthCoords,segment.nDepthCoord) || !validCoordinates (memo.sectionSegmentDistCoords,segment.nDistCoord)) return CreateErrorResponse (APIERR_GENERAL,"Section coordinate memo is incomplete or invalid.");
    GS::ObjectState result = CreateElementIdObjectState (element.header.guid);
    result.Add ("elementType",GetElementTypeNonLocalizedName(GetElemTypeId(element.header))); result.Add("name",GS::UniString(segment.cutPlName)); result.Add("units","metres");
    const auto& main = result.AddList<GS::ObjectState>("mainCoordinates");
    for (UInt32 i=0;i<segment.nMainCoord;++i) main(Create2DCoordinateObjectState(memo.sectionSegmentMainCoords[i]));
    const auto& depth = result.AddList<GS::ObjectState>("depthCoordinates");
    for (UInt32 i=0;i<segment.nDepthCoord;++i) depth(Create2DCoordinateObjectState(memo.sectionSegmentDepthCoords[i]));
    const auto& distant=result.AddList<GS::ObjectState> ("distantCoordinates");
    for (UInt32 i=0;i<segment.nDistCoord;++i) distant (Create2DCoordinateObjectState (memo.sectionSegmentDistCoords[i]));
    result.Add ("markedDistantArea",segment.markedDistArea);
    result.Add("horizontalRange",segment.horizRange==APIHorRange_Limited ? "Limited" : segment.horizRange==APIHorRange_ZeroDepth ? "ZeroDepth" : "Infinite");
    result.Add("verticalRange",segment.vertRange==APIVerRange_Limited ? "Limited" : segment.vertRange==APIVerRange_Infinite ? "Infinite" : "FitToZone");
    result.Add("verticalMinimum",segment.vertMin); result.Add("verticalMaximum",segment.vertMax); result.Add("relativeToStory",segment.relativeToStory); result.Add("verticalRangeStoryIndex",segment.verticalRangeStoryBaseNumber);
    result.Add("linePen",segment.linePen); result.Add("textPen",segment.textPen); result.Add("beginMarker",segment.begMark); result.Add("endMarker",segment.endMark); result.Add("useElementPens",segment.useElemPens);
    result.Add ("modificationStamp",SectionStamp (element));
    result.Add ("markerPosition",element.cutPlane.markerShow==APICutPl_ShowMiddleMarker ? "Middle" : element.cutPlane.markerShow==APICutPl_ShowWingMarkers ? "Ends" : "Unknown");
    GS::ObjectState presentation;
#define SECTION_READ(key,field) presentation.Add (key,segment.field);
    SECTION_READ ("beginLine",begLine)
    SECTION_READ ("middleLine",middleLine)
    SECTION_READ ("endLine",endLine)
    SECTION_READ ("continuousLine",continuous)
    SECTION_READ ("transparency",transparency)
    SECTION_READ ("useUncutSurfaceFill",modelUseUncutSurfFill)
    SECTION_READ ("storyUseSymbolPens",shUseSymbolPens)
    SECTION_READ ("boundaryPen",boundaryPen)
    SECTION_READ ("storyLine",shLineOn)
    SECTION_READ ("storyLeftMarker",shLeftMarkerOn)
    SECTION_READ ("storyRightMarker",shRightMarkerOn)
    SECTION_READ ("cutLinePen",sectPen)
    SECTION_READ ("cutFillPen",sectFillPen)
    SECTION_READ ("cutFillBackgroundPen",sectFillBGPen)
    SECTION_READ ("uncutSurfaceBackgroundPen",modelUncutSurfBGPen)
    SECTION_READ ("storyLinePen",shLinePen)
    SECTION_READ ("storyMarkerPen",shMarkerPen)
    SECTION_READ ("textSizeMillimetres",textSize)
    SECTION_READ ("storyMarkerTextSizeMillimetres",shMarkerTextSize)
    SECTION_READ ("storyMarkerSizeMillimetres",shMarkerSize)
#undef SECTION_READ
    presentation.Add("storyMarkerBold",(segment.shMarkerFaceBits&APIFace_Bold)!=0);
    presentation.Add("storyMarkerItalic",(segment.shMarkerFaceBits&APIFace_Italic)!=0);
    presentation.Add("storyMarkerUnderline",(segment.shMarkerFaceBits&APIFace_Underline)!=0);
    presentation.Add("storyMarkerFontIndex",segment.shMarkerFont);
#ifdef ServerMainVers_2700
    API_FontType storyFont={}; storyFont.head.index=segment.shMarkerFont;
    GS::UniString storyFontName; storyFont.head.uniStringNamePtr=&storyFontName;
    const auto fontError=ACAPI_Font_GetFont(storyFont);
    if (fontError==NoError) presentation.Add("storyMarkerFontName",storyFontName);
    else presentation.Add("storyMarkerFontError",*CreateErrorResponse(fontError,"Cannot resolve story marker font.").Get("error"));
#endif
    presentation.Add("boundaryDisplay",segment.boundaryDisplay==APIBound_UncutContours ? "UncutContours" : segment.boundaryDisplay==APIBound_NoContours ? "NoContours" : segment.boundaryDisplay==APIBound_OverrideContours ? "OverrideContours" : "Unknown");
    presentation.Add("boundaryLineTypeId",CreateGuidObjectState(GetAttributeGuidFromIndex(API_LinetypeID,segment.boundaryLineType)));
    presentation.Add ("referenceId",GS::UniString (segment.cutPlIdStr));
    presentation.Add ("lineTypeId",CreateGuidObjectState (GetAttributeGuidFromIndex (API_LinetypeID,segment.ltypeInd)));
    presentation.Add ("storyLineTypeId",CreateGuidObjectState (GetAttributeGuidFromIndex (API_LinetypeID,segment.shLineType)));
    presentation.Add ("vectorHatching",(segment.effectBits&APICutPl_VectorHatch)!=0);
    presentation.Add ("vectorShadows",(segment.effectBits&APICutPl_VectorShadow)!=0);
    presentation.Add ("sunFrom3D",(segment.effectBits&APICutPl_SunFrom3D)!=0);
    presentation.Add ("storyLineAppearance",segment.shAppearance==APICutPl_SHANone ? "None" : segment.shAppearance==APICutPl_SHADisplayOnly ? "ScreenOnly" : segment.shAppearance==APICutPl_SHAAll ? "ScreenAndPrint" : "Unknown");
    presentation.Add ("uncutSurfaceFillMode",segment.modelUncutSurfFillType==APICutPl_PenColor ? "UniformPen" : segment.modelUncutSurfFillType==APICutPl_MaterialColorShaded ? "SurfaceShaded" : segment.modelUncutSurfFillType==APICutPl_MaterialColorNonShaded ? "SurfaceUnshaded" : "Unknown");
    const auto& markers=result.AddList<GS::ObjectState>("markers");
    for (const auto& entry:{std::pair<const char*,API_Guid>("Begin",segment.begMarkerId),{"Middle",segment.midMarkerId},{"End",segment.endMarkerId},{"StoryLevel",segment.shSymbolId}}) {
        GS::ObjectState row("role",entry.first,"present",entry.second!=APINULLGuid);
        if (entry.second!=APINULLGuid) {
            row.Add("elementId",CreateGuidObjectState(entry.second));
            API_Element marker={}; marker.header.guid=entry.second;
            auto markerError=ACAPI_Element_Get(&marker);
            if (markerError==NoError && GetElemTypeId(marker.header)!=API_ObjectID) markerError=APIERR_BADID;
            if (markerError==NoError) {
                row.Add("libraryPartIndex",marker.object.libInd);
                API_LibPart part={}; part.index=marker.object.libInd;
                const GS::OnExit disposePart([&] { delete part.location; });
                markerError=ACAPI_LibraryPart_Get(&part);
                if (markerError==NoError) { row.Add("ownUnID",GS::UniString(part.ownUnID)); row.Add("libraryPartName",GS::UniString(part.docu_UName)); }
            }
            if (markerError!=NoError) row.Add("error",*CreateErrorResponse(markerError,"Cannot read section marker part.").Get("error"));
        }
        markers(row);
    }
    result.Add ("presentation",presentation);
    return result;
}

#include "TextRunCommands.hpp"
#include "MigrationHelper.hpp"
#include <cmath>
#include <string>
#include <set>
#include <utility>
#include <vector>
#include <algorithm>
#include "GSProcessControl.hpp"

namespace {
GS::UniString Stamp (const API_Element& e) { return GS::UniString (std::to_string (e.header.modiStamp).c_str ()); }
GSErrCode Read (const GS::ObjectState& p, API_Element& e, API_ElementMemo& memo) {
    e.header.guid = GetGuidFromArrayItem ("elementId",p);
    GSErrCode err = ACAPI_Element_Get (&e);
    if (err != NoError) return err;
    const auto type = GetElemTypeId (e.header);
    if (type != API_TextID && (type != API_LabelID || e.label.labelClass != APILblClass_Text)) return APIERR_BADID;
    return ACAPI_Element_GetMemo (e.header.guid,&memo,APIMemoMask_Paragraph | APIMemoMask_TextContent);
}
GSSize Paragraphs (const API_ElementMemo& m) {
    return m.paragraphs == nullptr ? 0 : BMhGetSize (reinterpret_cast<GSHandle> (m.paragraphs)) / sizeof (API_ParagraphType);
}
GSSize Runs (const API_ParagraphType& p) {
    return p.run == nullptr ? 0 : BMGetPtrSize (reinterpret_cast<GSPtr> (p.run)) / sizeof (API_RunType);
}
GS::ObjectState Run (const API_RunType& r, Int32 index) {
    return GS::ObjectState ("runIndex",index,"from",r.from,"length",r.range,"fontIndex",r.font,"sizeMillimetres",r.size,"pen",r.pen,
        "bold",(r.faceBits & APIFace_Bold) != 0,"italic",(r.faceBits & APIFace_Italic) != 0,"underline",(r.faceBits & APIFace_Underline) != 0,
        "strikeOut",(r.effectBits & APIEffect_StrikeOut) != 0,"superscript",(r.effectBits & APIEffect_SuperScript) != 0,
        "subscript",(r.effectBits & APIEffect_SubScript) != 0,"protected",(r.effectBits & APIEffect_Protected) != 0);
}
unsigned short Flag (const GS::ObjectState& p,const char* name,unsigned short value,unsigned short bit) {
    bool enabled;
    return p.Get (name,enabled) ? (enabled ? value | bit : value & ~bit) : value;
}
GS::UniString Content (const API_ElementMemo& memo) {
#ifdef ServerMainVers_2800
    return memo.textContent!=nullptr ? *memo.textContent : GS::UniString ();
#else
    return memo.textContent!=nullptr ? GS::UniString (reinterpret_cast<GS::uchar_t*> (*memo.textContent)) : GS::UniString ();
#endif
}
bool CharacterBoundary (const GS::UniString& text,Int32 offset) {
    if (offset<0 || static_cast<USize> (offset)>text.GetLength ()) return false;
    if (offset==0 || static_cast<USize> (offset)==text.GetLength ()) return true;
    const auto unicode=text.ToUStr ();
    const auto* chars=unicode.Get ();
    return !(chars[offset-1]>=0xD800 && chars[offset-1]<=0xDBFF && chars[offset]>=0xDC00 && chars[offset]<=0xDFFF);
}
GSErrCode ApplyRunStyle (const GS::ObjectState& style,API_RunType& run) {
    double size=run.size; Int32 pen=run.pen; style.Get ("sizeMillimetres",size); style.Get ("pen",pen);
    if (!std::isfinite (size) || size<=0 || pen<1 || pen>255) return APIERR_BADPARS;
    run.size=size; run.pen=static_cast<short> (pen);
    GS::UniString fontName;
    if (style.Get ("fontName",fontName)) {
#ifdef ServerMainVers_2700
        API_FontType font={}; font.head.uniStringNamePtr=&fontName;
        const GSErrCode err=ACAPI_Font_GetFont (font);
        if (err!=NoError || font.head.index<1 || font.head.index>32767) return err==NoError ? APIERR_BADINDEX : err;
        run.font=static_cast<short> (font.head.index);
#else
        return APIERR_NOTSUPPORTED;
#endif
    }
    run.faceBits=Flag (style,"bold",run.faceBits,APIFace_Bold); run.faceBits=Flag (style,"italic",run.faceBits,APIFace_Italic); run.faceBits=Flag (style,"underline",run.faceBits,APIFace_Underline);
    run.effectBits=Flag (style,"strikeOut",run.effectBits,APIEffect_StrikeOut); run.effectBits=Flag (style,"superscript",run.effectBits,APIEffect_SuperScript); run.effectBits=Flag (style,"subscript",run.effectBits,APIEffect_SubScript);
    return (run.effectBits&APIEffect_SuperScript) && (run.effectBits&APIEffect_SubScript) ? APIERR_BADPARS : NoError;
}
}
GS::Optional<GS::UniString> GetAnnotationFormattingCommand::GetInputParametersSchema () const {
    return R"({"type":"object","properties":{"elementId":{"$ref":"#/ElementId"},"offset":{"type":"integer","minimum":0},"limit":{"type":"integer","minimum":1,"maximum":100}},"required":["elementId"],"additionalProperties":false})";
}
GS::Optional<GS::UniString> GetAnnotationFormattingCommand::GetRawResponseSchema () const {
    return R"({"type":"object","properties":{"elementId":{"$ref":"#/ElementId"},"modificationStamp":{"type":"string"},"rangeConvention":{"const":"NativeTextCharacterOffsets"},"runs":{"type":"array","items":{"type":"object"}},"nextOffset":{"type":"integer"},"hasMore":{"type":"boolean"},"totalRuns":{"type":"integer"}},"required":["elementId","modificationStamp","rangeConvention","runs","nextOffset","hasMore","totalRuns"],"additionalProperties":false})";
}
GS::ObjectState GetAnnotationFormattingCommand::Execute (const GS::ObjectState& p, GS::ProcessControl&) const {
    Int32 offset=0,limit=50; p.Get ("offset",offset); p.Get ("limit",limit);
    if (offset < 0 || limit < 1 || limit > 100) return CreateErrorResponse (APIERR_BADPARS,"Invalid formatting page.");
    API_Element e = {}; API_ElementMemo memo = {};
    const GS::OnExit dispose ([&] { ACAPI_DisposeElemMemoHdls (&memo); });
    const GSErrCode err = Read (p,e,memo);
    if (err != NoError) return CreateErrorResponse (err,"Cannot read text/text-label formatting.");
    GS::ObjectState result = CreateElementIdObjectState (e.header.guid);
    result.Add ("modificationStamp",Stamp (e)); result.Add ("rangeConvention","NativeTextCharacterOffsets");
    const auto& add = result.AddList<GS::ObjectState> ("runs");
    Int32 total=0,returned=0;
    for (GSSize i=0; i<Paragraphs (memo); ++i) {
        const auto& paragraph = (*memo.paragraphs)[i];
        for (GSSize j=0; j<Runs (paragraph); ++j) {
            const Int32 index = total++;
            if (index < offset || returned >= limit) continue;
            auto row = Run (paragraph.run[j],static_cast<Int32> (j));
            row.Add ("paragraphIndex",static_cast<Int32> (i)); row.Add ("paragraphFrom",paragraph.from); row.Add ("paragraphLength",paragraph.range);
            row.Add ("alignment",paragraph.just == APIJust_Center ? "Center" : paragraph.just == APIJust_Right ? "Right" : paragraph.just == APIJust_Full ? "Justified" : "Left");
            row.Add ("firstIndentMillimetres",paragraph.firstIndent); row.Add ("indentMillimetres",paragraph.indent); row.Add ("rightIndentMillimetres",paragraph.rightIndent); row.Add ("lineSpacingFactor",paragraph.spacing);
            add (row); ++returned;
        }
    }
    result.Add ("nextOffset",offset+returned); result.Add ("hasMore",offset+returned < total); result.Add ("totalRuns",total);
    return result;
}
GS::Optional<GS::UniString> SetAnnotationRunStylesCommand::GetInputParametersSchema () const {
    return R"({"type":"object","properties":{
        "elementId":{"$ref":"#/ElementId"},"expectedModificationStamp":{"type":"string","pattern":"^[0-9]+$"},
        "runs":{"type":"array","minItems":1,"maxItems":100,"description":"Edits runs observed in GetAnnotationFormatting. Optional range splits a run to style only that interval. All indices refer to the pre-edit state. Disjoint ranges in the same run are allowed; overlapping edits and protected autotext are rejected. Content, paragraph settings and unselected styles are retained.","items":{"type":"object","properties":{
            "paragraphIndex":{"type":"integer","minimum":0},"runIndex":{"type":"integer","minimum":0},
            "range":{"type":"object","properties":{"from":{"type":"integer","minimum":0},"length":{"type":"integer","minimum":1}},"required":["from","length"],"additionalProperties":false,"description":"Absolute native text character offsets within the selected existing run, as exposed by GetAnnotationFormatting. UTF-16 surrogate pairs may not be split. Omit to style the whole run."},
            "style":{"type":"object","minProperties":1,"properties":{"fontName":{"type":"string","minLength":1,"maxLength":255},"sizeMillimetres":{"type":"number","exclusiveMinimum":0},"pen":{"type":"integer","minimum":1,"maximum":255},"bold":{"type":"boolean"},"italic":{"type":"boolean"},"underline":{"type":"boolean"},"strikeOut":{"type":"boolean"},"superscript":{"type":"boolean"},"subscript":{"type":"boolean"}},"additionalProperties":false}
        },"required":["paragraphIndex","runIndex","style"],"additionalProperties":false}}
    },"required":["elementId","expectedModificationStamp","runs"],"additionalProperties":false})";
}
GS::Optional<GS::UniString> SetAnnotationRunStylesCommand::GetRawResponseSchema () const {
    return R"({"type":"object","properties":{"elementId":{"$ref":"#/ElementId"},"success":{"type":"boolean"},"status":{"enum":["applied","notConfirmed"]},"verification":{"const":"nativeRunStyleReadBack"},"modificationStamp":{"type":"string"},"error":{"$ref":"#/Error"}},"required":["elementId","success","status","verification"],"additionalProperties":false})";
}
GS::ObjectState SetAnnotationRunStylesCommand::Execute (const GS::ObjectState& p, GS::ProcessControl& control) const {
    API_Element e = {},actual = {}; API_ElementMemo memo = {},actualMemo = {};
    const GS::OnExit dispose ([&] { ACAPI_DisposeElemMemoHdls (&memo); ACAPI_DisposeElemMemoHdls (&actualMemo); });
    GSErrCode err = Read (p,e,memo);
    if (err != NoError) return CreateErrorResponse (err,"Cannot read text formatting.");
    GS::UniString stamp; p.Get ("expectedModificationStamp",stamp);
    if (stamp != Stamp (e)) return CreateErrorResponse (APIERR_BADPARS,"Text changed; inspect its formatting again.");
    if (!ACAPI_Element_Filter (e.header.guid,APIFilt_IsEditable | APIFilt_InMyWorkspace | APIFilt_HasAccessRight)) return CreateErrorResponse (APIERR_NOTEDITABLE,"Text is not editable.");
    GS::Array<GS::ObjectState> edits; p.Get ("runs",edits);
    if (edits.IsEmpty () || edits.GetSize () > 100) return CreateErrorResponse (APIERR_BADPARS,"Supply 1..100 run edits.");
    const GS::UniString originalText=Content (memo);
    if (originalText.GetLength ()>1000000 || Paragraphs (memo)>10000)
        return CreateErrorResponse (APIERR_BADPARS,"Text exceeds supported formatting limits.");
    struct Patch { Int32 paragraph,run,from,length; API_RunType styled; };
    std::vector<Patch> patches;
    for (const auto& edit : edits) {
        Int32 paragraphIndex=-1,runIndex=-1;
        edit.Get ("paragraphIndex",paragraphIndex); edit.Get ("runIndex",runIndex);
        const auto* style=edit.Get ("style");
        if (style==nullptr || paragraphIndex<0 || paragraphIndex>=Paragraphs (memo) || runIndex<0 || runIndex>=Runs ((*memo.paragraphs)[paragraphIndex]))
            return CreateErrorResponse (APIERR_BADPARS,"Invalid paragraph/run target.");
        const auto& run=(*memo.paragraphs)[paragraphIndex].run[runIndex];
        if ((run.effectBits&APIEffect_Protected)!=0) return CreateErrorResponse (APIERR_BADPARS,"Protected autotext runs cannot be edited.");
        if (run.from<0 || run.range<0 || static_cast<long long> (run.from)+run.range>originalText.GetLength ())
            return CreateErrorResponse (APIERR_BADPARS,"Native run offsets do not match the text content.");
        Int32 from=run.from,length=run.range;
        if (const auto* range=edit.Get ("range")) {
            if (!range->Get ("from",from) || !range->Get ("length",length) || length<1)
                return CreateErrorResponse (APIERR_BADPARS,"Range requires a starting offset and positive length.");
        }
        const long long end=static_cast<long long> (from)+length;
        if (from<run.from || end>static_cast<long long> (run.from)+run.range || length<0 ||
            !CharacterBoundary (originalText,from) || !CharacterBoundary (originalText,static_cast<Int32> (end)))
            return CreateErrorResponse (APIERR_BADPARS,"Range must stay inside the observed run and cannot split a Unicode character.");
        Patch patch={paragraphIndex,runIndex,from,length,run};
        err=ApplyRunStyle (*style,patch.styled);
        if (err!=NoError) return CreateErrorResponse (err,"Invalid or unavailable run style; no text changes applied.");
        patches.push_back (patch);
    }
    std::sort (patches.begin (),patches.end (),[] (const Patch& a,const Patch& b) {
        if (a.paragraph!=b.paragraph) return a.paragraph<b.paragraph;
        if (a.run!=b.run) return a.run<b.run;
        return a.from<b.from;
    });
    for (size_t i=1;i<patches.size ();++i) {
        const auto& a=patches[i-1]; const auto& b=patches[i];
        if (a.paragraph==b.paragraph && a.run==b.run && (b.from<static_cast<long long> (a.from)+a.length || a.from==b.from))
            return CreateErrorResponse (APIERR_BADPARS,"Formatting ranges overlap or duplicate a target.");
    }
    size_t patchIndex=0;
    while (patchIndex<patches.size ()) {
        const Int32 paragraphIndex=patches[patchIndex].paragraph;
        auto& paragraph=(*memo.paragraphs)[paragraphIndex];
        const GSSize count=Runs (paragraph);
        if (count>100000) return CreateErrorResponse (APIERR_BADPARS,"Paragraph exceeds the supported run count.");
        std::vector<API_RunType> replacement;
        replacement.reserve (static_cast<size_t> (count)+2*patches.size ());
        for (GSSize index=0;index<count;++index) {
            const API_RunType original=paragraph.run[index];
            if (patchIndex>=patches.size () || patches[patchIndex].paragraph!=paragraphIndex || patches[patchIndex].run!=index) {
                replacement.push_back (original); continue;
            }
            Int32 cursor=original.from;
            while (patchIndex<patches.size () && patches[patchIndex].paragraph==paragraphIndex && patches[patchIndex].run==index) {
                const auto& patch=patches[patchIndex++];
                if (patch.from>cursor) { auto prefix=original; prefix.from=cursor; prefix.range=patch.from-cursor; replacement.push_back (prefix); }
                auto middle=patch.styled; middle.from=patch.from; middle.range=patch.length; replacement.push_back (middle);
                cursor=patch.from+patch.length;
            }
            if (cursor<original.from+original.range) { auto suffix=original; suffix.from=cursor; suffix.range=original.from+original.range-cursor; replacement.push_back (suffix); }
        }
        auto* data=reinterpret_cast<API_RunType*> (BMAllocatePtr (static_cast<GSSize> (replacement.size ()*sizeof(API_RunType)),ALLOCATE_CLEAR,0));
        if (data==nullptr) return CreateErrorResponse (APIERR_MEMFULL,"Cannot allocate split text runs; no model change applied.");
        std::copy (replacement.begin (),replacement.end (),data);
        BMKillPtr (reinterpret_cast<GSPtr*> (&paragraph.run));
        paragraph.run=data;
    }
    if (control.TestBreak ()) return CreateErrorResponse (APIERR_CANCEL,"Cancelled before applying text styles.");
    API_Element mask = {}; ACAPI_ELEMENT_MASK_CLEAR (mask);
    err = ACAPI_CallUndoableCommand ("Set annotation run styles",[&] () -> GSErrCode { return ACAPI_Element_Change (&e,&mask,&memo,APIMemoMask_Paragraph,true); });
    if (err != NoError) return CreateErrorResponse (err,"Native text run edit failed; inspect before retrying.");
    err = Read (p,actual,actualMemo);
    bool matches=err == NoError && Content (actualMemo)==originalText && Paragraphs (memo) == Paragraphs (actualMemo);
    if (matches) for (GSSize i=0; i<Paragraphs (memo); ++i) {
        const auto& a=(*memo.paragraphs)[i]; const auto& b=(*actualMemo.paragraphs)[i];
        if (a.from != b.from || a.range != b.range || Runs (a) != Runs (b)) { matches=false; break; }
        for (GSSize j=0; j<Runs (a); ++j) {
            const auto& x=a.run[j]; const auto& y=b.run[j];
            if (x.from != y.from || x.range != y.range || x.font != y.font || x.pen != y.pen || x.faceBits != y.faceBits || x.effectBits != y.effectBits || !std::isfinite (y.size) || std::abs (x.size-y.size)>1e-8) matches=false;
        }
    }
    GS::ObjectState result=CreateElementIdObjectState (e.header.guid);
    result.Add ("success",matches); result.Add ("status",matches ? "applied" : "notConfirmed"); result.Add ("verification","nativeRunStyleReadBack");
    if (err == NoError) result.Add ("modificationStamp",Stamp (actual));
    else result.Add ("error",GS::ObjectState ("code",err,"message","Edit executed; readback failed."));
    return result;
}

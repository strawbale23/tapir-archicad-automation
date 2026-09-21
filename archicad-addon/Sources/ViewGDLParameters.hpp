#pragma once
// 19 September 2026, 20:20 CEST. Native saved-view GDL parameter access.
#ifdef ServerMainVers_2900
#include <cmath>
#include <cstring>

namespace ViewGDL {
inline GSSize Count(API_GDLModelViewOptions* options) {
    if (options==nullptr) return 0;
    const GSSize bytes=BMGetPtrSize(reinterpret_cast<GSPtr>(options));
    return bytes<0 || bytes%sizeof(API_GDLModelViewOptions)!=0 || bytes/sizeof(API_GDLModelViewOptions)>1000 ? -1 : bytes/sizeof(API_GDLModelViewOptions);
}
inline GSSize ParameterCount(API_AddParType** parameters) {
    if (parameters==nullptr) return 0;
    const GSSize bytes=BMGetHandleSize(reinterpret_cast<GSHandle>(parameters));
    return bytes<0 || bytes%sizeof(API_AddParType)!=0 || bytes/sizeof(API_AddParType)>10000 ? -1 : bytes/sizeof(API_AddParType);
}
inline const char* Type(API_AddParID type) {
    switch(type) {
        case APIParT_Integer:return "Integer";
        case APIParT_Boolean:return "Boolean";
        case APIParT_CString:return "String";
        case APIParT_RealNum:return "RealNumber";
        case APIParT_Length:return "Length";
        case APIParT_Angle:return "Angle";
        default:return "Unsupported";
    }
}
inline bool Supported(const API_AddParType& parameter) {
    return (parameter.typeMod==API_ParSimple || parameter.typeMod==API_ParArray) && std::strcmp(Type(parameter.typeID),"Unsupported")!=0;
}
inline bool Value(const GS::ObjectState& patch,const char* key,API_AddParType& parameter) {
    if (!Supported(parameter) || parameter.typeMod!=API_ParSimple) return false;
    if (parameter.typeID==APIParT_CString) {
        GS::UniString value;
        if (!patch.Get(key,value) || value.GetLength()>=API_UAddParStrLen) return false;
        GS::ucscpy(parameter.value.uStr,value.ToUStr().Get()); return true;
    }
    if (parameter.typeID==APIParT_Boolean) {
        bool value=false; if (!patch.Get(key,value)) return false;
        parameter.value.real=value ? 1 : 0; return true;
    }
    if (parameter.typeID==APIParT_Integer) {
        Int32 value=0; if (!patch.Get(key,value)) return false;
        parameter.value.real=value; return true;
    }
    double value=0; if (!patch.Get(key,value) || !std::isfinite(value)) return false;
    parameter.value.real=value; return true;
}
inline bool Equal(const API_AddParType& a,const API_AddParType& b) {
    if (a.typeID!=b.typeID || a.typeMod!=b.typeMod) return false;
    return a.typeID==APIParT_CString ? GS::UniString(a.value.uStr)==GS::UniString(b.value.uStr) :
        std::isfinite(a.value.real) && std::isfinite(b.value.real) && a.value.real==b.value.real;
}
// Arrays retain their native shape and use flattened row-major input/output.
inline Int32 ArrayCount(const API_AddParType& parameter) {
    if (parameter.typeMod!=API_ParArray || parameter.dim1<1 || parameter.dim2<0 || parameter.dim1>10000 || parameter.dim2>10000) return -1;
    const Int32 width=parameter.dim2==0 ? 1 : parameter.dim2;
    if (parameter.dim1>10000/width) return -1;
    return parameter.dim1*width;
}
inline bool ReadArray(const API_AddParType& parameter,GS::Array<double>& numbers,GS::Array<GS::UniString>& strings) {
    const Int32 count=ArrayCount(parameter);
    if (count<1 || parameter.value.array==nullptr) return false;
    const GSSize bytes=BMGetHandleSize(parameter.value.array);
    if (parameter.typeID!=APIParT_CString) {
        if (bytes<static_cast<GSSize>(count*sizeof(double))) return false;
        const auto* values=reinterpret_cast<const double*>(*parameter.value.array);
        for (Int32 i=0;i<count;++i) {
            if (!std::isfinite(values[i])) return false;
            if (parameter.typeID==APIParT_Integer && (values[i]<-2147483648.0 || values[i]>2147483647.0 || std::trunc(values[i])!=values[i])) return false;
            if (parameter.typeID==APIParT_Boolean && values[i]!=0 && values[i]!=1) return false;
            numbers.Push(values[i]);
        }
        return true;
    }
    if (bytes<0 || bytes>4*1024*1024 || bytes%sizeof(GS::uchar_t)!=0) return false;
    const auto* values=reinterpret_cast<const GS::uchar_t*>(*parameter.value.array);
    const GSSize length=bytes/sizeof(GS::uchar_t);
    GSSize offset=0;
    for (Int32 i=0;i<count;++i) {
        const GSSize start=offset;
        while (offset<length && values[offset]!=0) ++offset;
        if (offset==length || offset-start>=API_UAddParStrLen) return false;
        strings.Push(GS::UniString(values+start)); ++offset;
    }
    return true;
}
inline bool ArrayValue(const GS::ObjectState& patch,const char* key,API_AddParType& parameter,bool apply) {
    Int32 dim1=0,dim2=0;
    if (!patch.Get("dim1",dim1) || !patch.Get("dim2",dim2) || dim1!=parameter.dim1 || dim2!=parameter.dim2) return false;
    GS::Array<double> previous,numbers; GS::Array<GS::UniString> oldStrings,strings;
    if (!ReadArray(parameter,previous,oldStrings)) return false;
    const Int32 count=ArrayCount(parameter);
    if (parameter.typeID==APIParT_CString) {
        if (!patch.Get(key,strings) || strings.GetSize()!=static_cast<USize>(count)) return false;
        GSSize chars=0;
        for (Int32 i=0;i<count;++i) {
            if (strings[i].GetLength()>=API_UAddParStrLen) return false;
            chars+=strings[i].GetLength()+1;
            if (!apply && strings[i]!=oldStrings[i]) return false;
        }
        if (chars>2*1024*1024) return false;
        if (!apply) return true;
        GSHandle replacement=BMAllocateHandle(chars*sizeof(GS::uchar_t),ALLOCATE_CLEAR,0);
        if (replacement==nullptr) return false;
        auto* target=reinterpret_cast<GS::uchar_t*>(*replacement);
        GSSize offset=0;
        for (const auto& value:strings) { GS::ucscpy(target+offset,value.ToUStr().Get()); offset+=value.GetLength()+1; }
        BMKillHandle(&parameter.value.array); parameter.value.array=replacement;
        return true;
    }
    if (parameter.typeID==APIParT_Boolean) {
        GS::Array<bool> values; if (!patch.Get(key,values)) return false;
        for (const auto value:values) numbers.Push(value ? 1 : 0);
    } else if (parameter.typeID==APIParT_Integer) {
        GS::Array<Int32> values; if (!patch.Get(key,values)) return false;
        for (const auto value:values) numbers.Push(value);
    } else if (!patch.Get(key,numbers)) return false;
    if (numbers.GetSize()!=static_cast<USize>(count)) return false;
    for (Int32 i=0;i<count;++i) if (!std::isfinite(numbers[i]) || (!apply && numbers[i]!=previous[i])) return false;
    if (!apply) return true;
    auto* target=reinterpret_cast<double*>(*parameter.value.array);
    for (Int32 i=0;i<count;++i) target[i]=numbers[i];
    return true;
}
inline GS::ObjectState Read(API_GDLModelViewOptions* options) {
    GS::ObjectState result;
    const auto& add=result.AddList<GS::ObjectState>("options");
    const GSSize count=Count(options);
    GSSize totalParameters=0;
    Int32 totalArrayValues=0;
    if (count<0) return CreateErrorResponse(APIERR_BADPARS,"Invalid or excessive native GDL view option array.");
    for (GSSize i=0;i<count;++i) {
        const auto& option=options[i];
        GS::ObjectState row("optionId",CreateGuidObjectState(option.guid),"name",GS::UniString(option.name),"supersetId",CreateGuidObjectState(option.supersetGuid));
        const auto& addParameter=row.AddList<GS::ObjectState>("parameters");
        const GSSize n=ParameterCount(option.params);
        if (n<0 || n>10000-totalParameters) return CreateErrorResponse(APIERR_BADPARS,"Invalid or excessive GDL view parameter array (maximum 10000 parameters across all options).");
        totalParameters+=n;
        for (GSSize j=0;j<n;++j) {
            const auto& parameter=(*option.params)[j];
            const bool supported=Supported(parameter);
            GS::ObjectState detail("name",parameter.name,"description",GS::UniString(parameter.uDescname),"type",Type(parameter.typeID),
                "nativeType",static_cast<Int32>(parameter.typeID),"isArray",parameter.typeMod==API_ParArray,"dim1",parameter.dim1,"dim2",parameter.dim2,
                "writable",supported && (parameter.flags&(API_ParFlg_Disabled|API_ParFlg_SHidden))==0,"nativeFlags",parameter.flags);
            if (supported && parameter.typeMod==API_ParArray) {
                const Int32 values=ArrayCount(parameter);
                if (values<0 || values>100000-totalArrayValues) return CreateErrorResponse(APIERR_BADPARS,"GDL view array values exceed the 100000-value response limit or have invalid dimensions.");
                totalArrayValues+=values;
                GS::Array<double> numbers; GS::Array<GS::UniString> strings;
                if (!ReadArray(parameter,numbers,strings)) detail.Add("valueError","Native array is invalid or exceeds the supported size.");
                else if (parameter.typeID==APIParT_CString) detail.Add("value",strings);
                else if (parameter.typeID==APIParT_Boolean) { const auto& append=detail.AddList<bool>("value"); for (const auto value:numbers) append(value!=0); }
                else if (parameter.typeID==APIParT_Integer) { const auto& append=detail.AddList<Int32>("value"); for (const auto value:numbers) append(static_cast<Int32>(value)); }
                else detail.Add("value",numbers);
                detail.Add("valueConvention","NativeGDLParameterValue; flattened row-major; shape retained");
            } else if (supported) {
                if (parameter.typeID==APIParT_CString) detail.Add("value",GS::UniString(parameter.value.uStr));
                else if (std::isfinite(parameter.value.real)) {
                    if (parameter.typeID==APIParT_Boolean) detail.Add("value",parameter.value.real!=0);
                    else if (parameter.typeID==APIParT_Integer && parameter.value.real>=-2147483648.0 && parameter.value.real<=2147483647.0 && std::trunc(parameter.value.real)==parameter.value.real) detail.Add("value",static_cast<Int32>(parameter.value.real));
                    else detail.Add("value",parameter.value.real);
                }
                detail.Add("valueConvention","NativeGDLParameterValue");
            }
            addParameter(detail);
        }
        add(row);
    }
    return result;
}
inline GSErrCode Patch(const GS::ObjectState& custom,API_GDLModelViewOptions* options,bool confirm=false) {
    if (!custom.Contains("gdlOptions")) return NoError;
    GS::Array<GS::ObjectState> patches;
    if (!custom.Get("gdlOptions",patches) || patches.IsEmpty() || patches.GetSize()>100) return APIERR_BADPARS;
    const GSSize count=Count(options); if (count<0) return APIERR_BADPARS;
    GS::HashSet<API_Guid> seen;
    Int32 totalArrayValues=0;
    for (const auto& patch:patches) {
        const auto guid=GetGuidFromArrayItem("optionId",patch);
        if (guid==APINULLGuid || seen.Contains(guid)) return APIERR_BADPARS;
        seen.Add(guid);
        API_GDLModelViewOptions* option=nullptr;
        for (GSSize i=0;i<count;++i) if (options[i].guid==guid) { if (option!=nullptr) return APIERR_BADPARS; option=&options[i]; }
        if (option==nullptr) return APIERR_BADID;
        GS::Array<GS::ObjectState> parameters;
        if (!patch.Get("parameters",parameters) || parameters.IsEmpty() || parameters.GetSize()>1000) return APIERR_BADPARS;
        const GSSize n=ParameterCount(option->params); if (n<0) return APIERR_BADPARS;
        GS::HashSet<GS::UniString> names;
        for (const auto& change:parameters) {
            GS::UniString name,type;
            if (!change.Get("name",name) || name.IsEmpty() || !change.Get("type",type) || names.Contains(name)) return APIERR_BADPARS;
            names.Add(name);
            API_AddParType* parameter=nullptr;
            for (GSSize j=0;j<n;++j) if (GS::UniString((*option->params)[j].name)==name) { if (parameter!=nullptr) return APIERR_BADPARS; parameter=&(*option->params)[j]; }
            if (parameter==nullptr || !Supported(*parameter) || type!=Type(parameter->typeID)) return APIERR_BADPARS;
            if (!confirm && (parameter->flags&(API_ParFlg_Disabled|API_ParFlg_SHidden))!=0) return APIERR_NOTEDITABLE;
            if (parameter->typeMod==API_ParArray) {
                const Int32 values=ArrayCount(*parameter);
                if (values<0 || values>100000-totalArrayValues) return APIERR_BADPARS;
                totalArrayValues+=values;
                if (confirm) { if (!ArrayValue(change,"value",*parameter,false)) return APIERR_GENERAL; }
                else if (!ArrayValue(change,"expectedValue",*parameter,false) || !ArrayValue(change,"value",*parameter,true)) return APIERR_BADPARS;
                continue;
            }
            if (change.Contains("dim1") || change.Contains("dim2")) return APIERR_BADPARS;
            API_AddParType candidate=*parameter;
            if (confirm) {
                if (!Value(change,"value",candidate) || !Equal(candidate,*parameter)) return APIERR_GENERAL;
            } else {
                if (!Value(change,"expectedValue",candidate) || !Equal(candidate,*parameter) || !Value(change,"value",candidate)) return APIERR_BADPARS;
                // Only replace the scalar union; preserve native metadata and all other parameters.
                parameter->value=candidate.value;
            }
        }
    }
    return NoError;
}
}
#endif

#include "NativeOperationJournal.hpp"
#include "ObjectStateJSONConversion.hpp"
#include <chrono>
#include <string>
#ifdef ServerMainVers_2700
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#include <share.h>
#else
#include <unistd.h>
#include <cerrno>
#endif
#endif

namespace NativeOperationJournal {
bool Available () {
#ifdef ServerMainVers_2700
    return true;
#else
    return false;
#endif
}

#ifdef ServerMainVers_2700
namespace {
constexpr size_t MaximumFileBytes=32*1024*1024;
GSErrCode Paths (const GS::UniString& directory,const GS::UniString& operation,
                 std::filesystem::path& started,std::filesystem::path& completed,GS::UniString& message) {
    message="Journal directory must be an existing absolute directory; journal operationId must be a UUID.";
    const std::string id(operation.ToCStr(CC_UTF8).Get());
    if (id.size()!=36) return APIERR_BADPARS;
    for (size_t i=0;i<id.size();++i) {
        if (i==8 || i==13 || i==18 || i==23) { if (id[i]!='-') return APIERR_BADPARS; }
        else if (!((id[i]>='0' && id[i]<='9') || (id[i]>='a' && id[i]<='f') || (id[i]>='A' && id[i]<='F'))) return APIERR_BADPARS;
    }
    const std::string utf8(directory.ToCStr(CC_UTF8).Get());
#ifdef __cpp_char8_t
    const std::filesystem::path supplied(std::u8string(utf8.begin(),utf8.end()));
#else
    const auto supplied=std::filesystem::u8path(utf8);
#endif
    std::error_code error;
    if (!supplied.is_absolute() || !std::filesystem::is_directory(supplied,error) || error) return APIERR_BADPARS;
    const auto folder=std::filesystem::canonical(supplied,error);
    if (error) return APIERR_GENERAL;
    std::string key=id;
    for (auto& c:key) if (c>='A' && c<='F') c=static_cast<char>(c-'A'+'a');
    started=folder/("tapir-operation-"+key+".started.json");
    completed=folder/("tapir-operation-"+key+".completed.json");
    return NoError;
}
GSErrCode ReadFile (const std::filesystem::path& path,Record& record,bool& found,GS::UniString& message) {
    std::error_code error;
    const auto status=std::filesystem::symlink_status(path,error);
    if (error==std::errc::no_such_file_or_directory || (!error && status.type()==std::filesystem::file_type::not_found)) { found=false; return NoError; }
    found=true;
    message="Journal receipt is unreadable, incomplete or invalid. Do not repeat the modelling operation.";
    if (error || !std::filesystem::is_regular_file(status)) return APIERR_GENERAL;
    const auto size=std::filesystem::file_size(path,error);
    if (error || size==0 || size>MaximumFileBytes) return APIERR_GENERAL;
    std::ifstream stream(path,std::ios::binary);
    std::string bytes(static_cast<size_t>(size),'\0');
    if (!stream.read(&bytes[0],static_cast<std::streamsize>(size)) || stream.peek()!=std::char_traits<char>::eof()) return APIERR_GENERAL;
    if (bytes.find('\0')!=std::string::npos) return APIERR_GENERAL;
    GS::ObjectState envelope;
    if (JSON::ConvertToObjectState(GS::UniString(bytes.c_str(),CC_UTF8),envelope)!=NoError) return APIERR_GENERAL;
    GS::UniString format;
    const auto* receipt=envelope.Get("receipt");
    if (!envelope.Get("format",format) || format!="TapirOperationReceipt-1" || !envelope.Get("request",record.request) || receipt==nullptr) return APIERR_GENERAL;
    GS::UniString state;
    if (!receipt->Get("status",state) || (state!="started" && state!="returned" && state!="unknownOutcome" && state!="resultTooLarge")) return APIERR_GENERAL;
    record.receipt=*receipt;
    return NoError;
}
}
#endif

GSErrCode Read (const GS::UniString& directory,const GS::UniString& operation,Record& record,bool& found,GS::UniString& message) {
    found=false;
#ifdef ServerMainVers_2700
    try {
        std::filesystem::path started,completed;
        auto err=Paths(directory,operation,started,completed,message);
        if (err!=NoError) return err;
        err=ReadFile(completed,record,found,message);
        if (err!=NoError || found) return err;
        return ReadFile(started,record,found,message);
    } catch (...) { message="Cannot read the operation journal. Do not repeat the modelling operation."; return APIERR_GENERAL; }
#else
    (void)directory; (void)operation; (void)record;
    message="File journals require Archicad 27 or newer."; return APIERR_BADPARS;
#endif
}

GSErrCode Write (const GS::UniString& directory,const GS::UniString& operation,const Record& record,bool completed,GS::UniString& message) {
#ifdef ServerMainVers_2700
    try {
        std::filesystem::path started,finished;
        auto err=Paths(directory,operation,started,finished,message);
        if (err!=NoError) return err;
        const auto& path=completed ? finished : started;
        const auto milliseconds=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        GS::ObjectState envelope("format","TapirOperationReceipt-1","recordedAtUnixMilliseconds",GS::UniString(std::to_string(milliseconds).c_str()),"request",record.request,"receipt",record.receipt);
        GS::UniString json;
        if (JSON::CreateFromObjectState(envelope,json)!=NoError) return APIERR_GENERAL;
        const std::string bytes(json.ToCStr(CC_UTF8).Get());
        if (bytes.size()>MaximumFileBytes) { message="Journal record exceeds 32 MiB."; return APIERR_BADPARS; }
        message="Cannot exclusively create and flush the journal receipt. Existing or partial receipts are never overwritten; inspect before retrying.";
        int file=-1;
#ifdef _WIN32
        if (_wsopen_s(&file,path.c_str(),_O_WRONLY|_O_CREAT|_O_EXCL|_O_BINARY,_SH_DENYRW,_S_IREAD|_S_IWRITE)!=0) return APIERR_GENERAL;
        const GS::OnExit closeFile([&] { _close(file); });
#else
        file=::open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
        if (file<0) return APIERR_GENERAL;
        const GS::OnExit closeFile([&] { ::close(file); });
#endif
        // Leave an incomplete file in place on any failure: it prevents an unsafe retry.
        size_t written=0;
        while (written<bytes.size()) {
#ifdef _WIN32
            const int count=_write(file,bytes.data()+written,static_cast<unsigned int>(bytes.size()-written));
#else
            const auto count=::write(file,bytes.data()+written,bytes.size()-written);
            if (count<0 && errno==EINTR) continue;
#endif
            if (count<=0) return APIERR_GENERAL;
            written+=static_cast<size_t>(count);
        }
#ifdef _WIN32
        if (_commit(file)!=0) return APIERR_GENERAL;
#else
        if (::fsync(file)!=0) return APIERR_GENERAL;
        const int folder=::open(path.parent_path().c_str(),O_RDONLY);
        if (folder<0) return APIERR_GENERAL;
        const GS::OnExit closeFolder([&] { ::close(folder); });
        if (::fsync(folder)!=0) return APIERR_GENERAL;
#endif
        return NoError;
    } catch (...) { message="Cannot persist the operation receipt. Inspect the journal and model before retrying."; return APIERR_GENERAL; }
#else
    (void)directory; (void)operation; (void)record; (void)completed;
    message="File journals require Archicad 27 or newer."; return APIERR_BADPARS;
#endif
}
}

#pragma once
#include "CommandBase.hpp"

// 19 September 2026, 15:05 CEST. Optional local receipts; never a project save or model-state guarantee.
namespace NativeOperationJournal {
struct Record {
    GS::UniString request;
    GS::ObjectState receipt;
};
bool Available ();
GSErrCode Read (const GS::UniString& directory, const GS::UniString& operation,
               Record& record, bool& found, GS::UniString& message);
GSErrCode Write (const GS::UniString& directory, const GS::UniString& operation,
                const Record& record, bool completed, GS::UniString& message);
}

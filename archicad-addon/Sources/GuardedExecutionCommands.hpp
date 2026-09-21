#pragma once
#include "CommandBase.hpp"
#include <functional>
#include <memory>

// Updated 19 September 2026, 15:05 CEST. Receipts identify an add-on/project session.
// Optional file journals survive restarts but never guarantee the current model state.
using NativeCommandFactory = std::function<std::unique_ptr<CommandBase> ()>;
void RegisterGuardedNativeCommand (const GS::String&, NativeCommandFactory);
GSErrCode InitializeAutomationSession ();

class GetAutomationSessionCommand : public CommandBase {
public:
    GetAutomationSessionCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "GetAutomationSession"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};
class ExecuteGuardedCommandCommand : public CommandBase {
public:
    ExecuteGuardedCommandCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "ExecuteGuardedCommand"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};
class GetOperationReceiptCommand : public CommandBase {
public:
    GetOperationReceiptCommand () : CommandBase (CommonSchema::Used) {}
    GS::String GetName () const override { return "GetOperationReceipt"; }
    GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    GS::ObjectState Execute (const GS::ObjectState&, GS::ProcessControl&) const override;
};

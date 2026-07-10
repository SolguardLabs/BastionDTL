#pragma once

#include "custody/ledger.hpp"

#include <string>
#include <vector>

namespace bastion {

enum class SettlementDecision {
    Accepted,
    Rejected,
};

std::string decision_name(SettlementDecision decision);

struct SettlementResult {
    SettlementDecision decision = SettlementDecision::Rejected;
    ReceiptId receipt_id;
    AccountId source_account;
    AccountId beneficiary_account;
    AccountId fee_account;
    AccountId reserve_account;
    IdentityId submitter;
    IdentityId issuing_operator;
    IdentityId applied_operator;
    PolicyId applied_policy;
    Amount gross;
    Amount beneficiary_amount;
    Amount operator_fee;
    Amount reserve_amount;
    std::string message;
    std::string receipt_digest;
};

class SettlementEngine {
public:
    explicit SettlementEngine(LedgerState& ledger);

    SettlementResult settle(const SettlementReceipt& receipt, IdentityId submitter);
    SettlementResult dry_run(const SettlementReceipt& receipt, IdentityId submitter) const;

private:
    SettlementResult evaluate(const SettlementReceipt& receipt, IdentityId submitter, bool mutate) const;
    void ensure_submitter_allowed(const SettlementReceipt& receipt, const IdentityId& submitter) const;
    void ensure_receipt_allowed(const SettlementReceipt& receipt, const CustodyAccount& source) const;

    LedgerState& ledger_;
};

void write_settlement_result(JsonWriter& json, const SettlementResult& result);

} // namespace bastion


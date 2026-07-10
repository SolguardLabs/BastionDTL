#include "settlement/settlement.hpp"

#include <sstream>

namespace bastion {

std::string decision_name(SettlementDecision decision) {
    switch (decision) {
    case SettlementDecision::Accepted:
        return "accepted";
    case SettlementDecision::Rejected:
        return "rejected";
    }
    return "rejected";
}

SettlementEngine::SettlementEngine(LedgerState& ledger) : ledger_(ledger) {}

SettlementResult SettlementEngine::settle(const SettlementReceipt& receipt, IdentityId submitter) {
    return evaluate(receipt, std::move(submitter), true);
}

SettlementResult SettlementEngine::dry_run(
    const SettlementReceipt& receipt,
    IdentityId submitter
) const {
    return evaluate(receipt, std::move(submitter), false);
}

SettlementResult SettlementEngine::evaluate(
    const SettlementReceipt& receipt,
    IdentityId submitter,
    bool mutate
) const {
    SettlementResult result;
    result.receipt_id = receipt.id;
    result.source_account = receipt.terms.source_account;
    result.beneficiary_account = receipt.terms.beneficiary_account;
    result.submitter = submitter;
    result.issuing_operator = receipt.issued_by;
    result.gross = receipt.terms.gross_amount;
    result.receipt_digest = receipt.digest();

    try {
        ReceiptFactory factory(ledger_.identities());
        if (!factory.verify(receipt)) {
            fail("receipt signature is invalid");
        }
        ensure_submitter_allowed(receipt, submitter);

        auto& source = ledger_.account(receipt.terms.source_account);
        auto& beneficiary = ledger_.account(receipt.terms.beneficiary_account);
        if (source.asset() != beneficiary.asset() || source.asset() != receipt.terms.asset) {
            fail("receipt asset does not match accounts");
        }
        ensure_receipt_allowed(receipt, source);

        if (ledger_.receipt_book().has(receipt.id)) {
            fail("receipt already settled");
        }
        if (ledger_.receipt_book().nonce_used(receipt.terms.source_account, receipt.terms.nonce)) {
            fail("receipt nonce already used");
        }
        if (!receipt.terms.valid_for.contains(ledger_.epoch())) {
            fail("receipt is outside validity range");
        }
        if (!source.can_accept_receipts()) {
            fail("source account cannot accept receipts");
        }
        if (source.balance().available < receipt.terms.gross_amount) {
            fail("insufficient source balance");
        }

        const auto& applied_grant = source.current_grant();
        const auto& applied_policy = applied_grant.policy;
        if (receipt.terms.lane != applied_policy.settlement_lane) {
            fail("receipt lane is not enabled on account");
        }
        if (receipt.terms.gross_amount > applied_policy.receipt_limit) {
            fail("receipt exceeds active policy limit");
        }

        result.applied_operator = applied_grant.operator_id;
        result.applied_policy = applied_policy.id;
        result.fee_account = applied_policy.fee_account;
        result.reserve_account = applied_policy.reserve_account;
        result.operator_fee = applied_policy.fee_for(receipt.terms.gross_amount);
        result.reserve_amount = applied_policy.reserve_for(receipt.terms.gross_amount);
        result.beneficiary_amount = receipt.terms.gross_amount
                                        .checked_sub(result.operator_fee)
                                        .checked_sub(result.reserve_amount);

        if (!ledger_.has_account(result.fee_account) || !ledger_.has_account(result.reserve_account)) {
            fail("policy settlement accounts are missing");
        }
        if (ledger_.account(result.fee_account).asset() != source.asset()) {
            fail("fee account asset mismatch");
        }
        if (ledger_.account(result.reserve_account).asset() != source.asset()) {
            fail("reserve account asset mismatch");
        }

        if (mutate) {
            const auto actor = submitter;
            ledger_.account(result.source_account).debit(result.gross, ledger_.epoch(), "receipt settlement");
            ledger_.account(result.beneficiary_account)
                .credit(result.beneficiary_amount, ledger_.epoch(), "receipt beneficiary");
            if (result.operator_fee.positive()) {
                ledger_.account(result.fee_account).credit(result.operator_fee, ledger_.epoch(), "operator fee");
            }
            if (result.reserve_amount.positive()) {
                ledger_.account(result.reserve_account)
                    .credit(result.reserve_amount, ledger_.epoch(), "settlement reserve");
            }
            ledger_.remember_settled(receipt);
            ledger_.append(
                JournalKind::ReceiptSettle,
                result.source_account,
                actor,
                result.gross,
                result.receipt_id.str() + ":" + result.applied_policy.str()
            );
        }
        result.decision = SettlementDecision::Accepted;
        result.message = "ok";
    } catch (const DomainError& error) {
        result.decision = SettlementDecision::Rejected;
        result.message = error.what();
        if (mutate) {
            ledger_.reject_receipt(receipt, result.message);
        }
    }

    return result;
}

void SettlementEngine::ensure_submitter_allowed(
    const SettlementReceipt& receipt,
    const IdentityId& submitter
) const {
    const auto& identity = ledger_.identities().get(submitter);
    if (!identity.enabled) {
        fail("submitter is disabled");
    }
    if (submitter == receipt.issued_by) {
        return;
    }
    if (identity.role == IdentityRole::Treasurer || identity.role == IdentityRole::Service) {
        return;
    }
    fail("submitter is not allowed to relay receipt");
}

void SettlementEngine::ensure_receipt_allowed(
    const SettlementReceipt& receipt,
    const CustodyAccount& source
) const {
    const auto& issued_grant = source.grant_for_operator_at(receipt.issued_by, receipt.issued_at);
    if (!issued_grant.policy.allow_legacy_receipts && ledger_.epoch() > receipt.issued_at) {
        fail("receipt is not valid after issue epoch");
    }
    if (issued_grant.policy.digest() != receipt.economics.policy_digest) {
        fail("receipt economic snapshot does not match issue policy");
    }
    if (receipt.economics.digest().empty()) {
        fail("receipt economic snapshot is invalid");
    }
}

void write_settlement_result(JsonWriter& json, const SettlementResult& result) {
    json.begin_object();
    json.field("decision", decision_name(result.decision));
    json.field("receiptId", result.receipt_id.str());
    json.field("sourceAccount", result.source_account.str());
    json.field("beneficiaryAccount", result.beneficiary_account.str());
    json.field("feeAccount", result.fee_account.str());
    json.field("reserveAccount", result.reserve_account.str());
    json.field("submitter", result.submitter.str());
    json.field("issuingOperator", result.issuing_operator.str());
    json.field("appliedOperator", result.applied_operator.str());
    json.field("appliedPolicy", result.applied_policy.str());
    json.field("gross", result.gross);
    json.field("beneficiaryAmount", result.beneficiary_amount);
    json.field("operatorFee", result.operator_fee);
    json.field("reserveAmount", result.reserve_amount);
    json.field("message", result.message);
    json.field("receiptDigest", result.receipt_digest);
    json.end_object();
}

} // namespace bastion


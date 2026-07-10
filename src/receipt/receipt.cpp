#include "receipt/receipt.hpp"

#include <sstream>

namespace bastion {

std::string receipt_status_name(ReceiptStatus status) {
    switch (status) {
    case ReceiptStatus::Draft:
        return "draft";
    case ReceiptStatus::Signed:
        return "signed";
    case ReceiptStatus::Accepted:
        return "accepted";
    case ReceiptStatus::Rejected:
        return "rejected";
    case ReceiptStatus::Settled:
        return "settled";
    }
    return "rejected";
}

void ReceiptTerms::validate() const {
    if (source_account.empty()) {
        fail("source account is required");
    }
    if (beneficiary_account.empty()) {
        fail("beneficiary account is required");
    }
    if (source_account == beneficiary_account) {
        fail("source and beneficiary must differ");
    }
    if (asset.empty()) {
        fail("asset is required");
    }
    if (!gross_amount.positive()) {
        fail("gross amount must be positive");
    }
    if (!valid_for.contains(valid_for.not_before)) {
        fail("invalid validity range");
    }
    if (!is_valid_label(lane)) {
        fail("receipt lane is invalid");
    }
}

std::string ReceiptTerms::digest() const {
    CanonicalBuilder builder;
    builder.field("source", source_account.str());
    builder.field("beneficiary", beneficiary_account.str());
    builder.field("asset", asset.str());
    builder.field("gross", static_cast<std::uint64_t>(gross_amount.units()));
    builder.field("nonce", nonce.value);
    builder.field("not_before", valid_for.not_before.value);
    builder.field("expires", valid_for.expires_at.value);
    builder.field("lane", lane);
    builder.field("memo", memo);
    return compact_digest(builder.str());
}

void ReceiptEconomicSnapshot::validate() const {
    if (policy_id.empty()) {
        fail("receipt policy id is required");
    }
    if (operator_id.empty()) {
        fail("receipt operator id is required");
    }
    if (fee_account.empty()) {
        fail("receipt fee account is required");
    }
    if (reserve_account.empty()) {
        fail("receipt reserve account is required");
    }
    if (policy_digest.empty()) {
        fail("receipt policy digest is required");
    }
    if (operator_fee_bps.value() + reserve_bps.value() > 10000) {
        fail("receipt economic rates exceed full amount");
    }
}

std::string ReceiptEconomicSnapshot::digest() const {
    CanonicalBuilder builder;
    builder.field("policy", policy_id.str());
    builder.field("operator", operator_id.str());
    builder.field("fee_account", fee_account.str());
    builder.field("reserve_account", reserve_account.str());
    builder.field("fee_bps", static_cast<std::uint64_t>(operator_fee_bps.value()));
    builder.field("reserve_bps", static_cast<std::uint64_t>(reserve_bps.value()));
    builder.field("policy_digest", policy_digest);
    builder.field("effective_from", effective_from.value);
    return compact_digest(builder.str());
}

std::string SettlementReceipt::payload() const {
    CanonicalBuilder builder;
    builder.field("domain", "bastiondtl-receipt-v1");
    builder.field("receipt", id.str());
    builder.field("terms", terms.digest());
    builder.field("economics", economics.digest());
    builder.field("issued_by", issued_by.str());
    builder.field("issued_at", issued_at.value);
    return builder.str();
}

std::string SettlementReceipt::digest() const {
    CanonicalBuilder builder;
    builder.field("payload", payload());
    builder.field("signature", signature.value);
    builder.field("signer", signature.signer.str());
    return compact_digest(builder.str());
}

bool SettlementReceipt::signed_by_operator() const {
    return signature.signer == issued_by && issued_by == economics.operator_id;
}

void SettlementReceipt::validate_shape() const {
    if (id.empty()) {
        fail("receipt id is required");
    }
    terms.validate();
    economics.validate();
    if (issued_by.empty()) {
        fail("receipt issuer is required");
    }
    if (!signed_by_operator()) {
        fail("receipt signer does not match operator");
    }
    if (signature.empty()) {
        fail("receipt signature is required");
    }
}

ReceiptFactory::ReceiptFactory(const IdentityRegistry& identities) : identities_(identities) {}

SettlementReceipt ReceiptFactory::build(
    const CustodyAccount& account,
    const OperatorGrant& grant,
    const ReceiptBuildRequest& request,
    Epoch issued_at
) const {
    if (request.source_account != account.id()) {
        fail("receipt source does not match account");
    }
    if (!grant.active_at(issued_at)) {
        fail("operator grant is not active at issue epoch");
    }
    if (request.gross_amount > grant.policy.receipt_limit) {
        fail("receipt exceeds policy limit");
    }
    ReceiptTerms terms;
    terms.source_account = request.source_account;
    terms.beneficiary_account = request.beneficiary_account;
    terms.asset = account.asset();
    terms.gross_amount = request.gross_amount;
    terms.nonce = request.nonce;
    terms.valid_for = TimeRange(request.not_before, request.expires_at);
    terms.lane = grant.policy.settlement_lane;
    terms.memo = request.memo;
    terms.validate();

    ReceiptEconomicSnapshot snapshot;
    snapshot.policy_id = grant.policy.id;
    snapshot.operator_id = grant.operator_id;
    snapshot.fee_account = grant.policy.fee_account;
    snapshot.reserve_account = grant.policy.reserve_account;
    snapshot.operator_fee_bps = grant.policy.operator_fee_bps;
    snapshot.reserve_bps = grant.policy.reserve_bps;
    snapshot.policy_digest = grant.policy.digest();
    snapshot.effective_from = grant.effective_from;
    snapshot.validate();

    CanonicalBuilder id_builder;
    id_builder.field("account", account.id().str());
    id_builder.field("beneficiary", request.beneficiary_account.str());
    id_builder.field("amount", static_cast<std::uint64_t>(request.gross_amount.units()));
    id_builder.field("nonce", request.nonce.value);
    id_builder.field("issued", issued_at.value);
    id_builder.field("operator", grant.operator_id.str());

    SettlementReceipt receipt;
    receipt.id = ReceiptId(deterministic_id("rcpt", id_builder.str()));
    receipt.terms = terms;
    receipt.economics = snapshot;
    receipt.issued_by = grant.operator_id;
    receipt.issued_at = issued_at;
    receipt.status = ReceiptStatus::Draft;
    receipt.signature = identities_.sign(grant.operator_id, receipt.payload());
    receipt.status = ReceiptStatus::Signed;
    receipt.validate_shape();
    return receipt;
}

SettlementReceipt ReceiptFactory::resign(
    const SettlementReceipt& receipt,
    const IdentityId& signer
) const {
    SettlementReceipt copy = receipt;
    copy.issued_by = signer;
    copy.economics.operator_id = signer;
    copy.signature = identities_.sign(signer, copy.payload());
    copy.status = ReceiptStatus::Signed;
    return copy;
}

bool ReceiptFactory::verify(const SettlementReceipt& receipt) const {
    try {
        receipt.validate_shape();
    } catch (const DomainError&) {
        return false;
    }
    return identities_.verify(receipt.signature, receipt.payload());
}

bool ReceiptBook::has(const ReceiptId& id) const {
    return receipts_.find(id) != receipts_.end();
}

bool ReceiptBook::nonce_used(const AccountId& account, Nonce nonce) const {
    return account_nonces_.find(account_nonce_key(account, nonce)) != account_nonces_.end();
}

void ReceiptBook::remember(const SettlementReceipt& receipt) {
    receipts_.insert(receipt.id);
    account_nonces_.insert(account_nonce_key(receipt.terms.source_account, receipt.terms.nonce));
    settled_.push_back(receipt.id);
}

void ReceiptBook::reject(const SettlementReceipt& receipt, std::string reason) {
    std::ostringstream out;
    out << receipt.id.str() << ":" << reason;
    rejections_.push_back(out.str());
}

std::size_t ReceiptBook::settled_count() const {
    return settled_.size();
}

std::vector<ReceiptId> ReceiptBook::settled_ids() const {
    return settled_;
}

std::vector<std::string> ReceiptBook::rejection_reasons() const {
    return rejections_;
}

std::string account_nonce_key(const AccountId& account, Nonce nonce) {
    return account.str() + ":" + nonce.str();
}

} // namespace bastion


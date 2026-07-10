#pragma once

#include "custody/account.hpp"
#include "security/identity.hpp"

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace bastion {

enum class ReceiptStatus {
    Draft,
    Signed,
    Accepted,
    Rejected,
    Settled,
};

std::string receipt_status_name(ReceiptStatus status);

struct ReceiptTerms {
    AccountId source_account;
    AccountId beneficiary_account;
    AssetId asset;
    Amount gross_amount;
    Nonce nonce;
    TimeRange valid_for;
    std::string lane;
    std::string memo;

    void validate() const;
    std::string digest() const;
};

struct ReceiptEconomicSnapshot {
    PolicyId policy_id;
    IdentityId operator_id;
    AccountId fee_account;
    AccountId reserve_account;
    BasisPoints operator_fee_bps;
    BasisPoints reserve_bps;
    std::string policy_digest;
    Epoch effective_from;

    void validate() const;
    std::string digest() const;
};

struct SettlementReceipt {
    ReceiptId id;
    ReceiptTerms terms;
    ReceiptEconomicSnapshot economics;
    IdentityId issued_by;
    Epoch issued_at;
    Signature signature;
    ReceiptStatus status = ReceiptStatus::Draft;

    std::string payload() const;
    std::string digest() const;
    bool signed_by_operator() const;
    void validate_shape() const;
};

struct ReceiptBuildRequest {
    AccountId source_account;
    AccountId beneficiary_account;
    Amount gross_amount;
    Nonce nonce;
    Epoch not_before;
    Epoch expires_at;
    std::string memo;
};

class ReceiptFactory {
public:
    explicit ReceiptFactory(const IdentityRegistry& identities);

    SettlementReceipt build(
        const CustodyAccount& account,
        const OperatorGrant& grant,
        const ReceiptBuildRequest& request,
        Epoch issued_at
    ) const;

    SettlementReceipt resign(const SettlementReceipt& receipt, const IdentityId& signer) const;
    bool verify(const SettlementReceipt& receipt) const;

private:
    const IdentityRegistry& identities_;
};

class ReceiptBook {
public:
    bool has(const ReceiptId& id) const;
    bool nonce_used(const AccountId& account, Nonce nonce) const;
    void remember(const SettlementReceipt& receipt);
    void reject(const SettlementReceipt& receipt, std::string reason);
    std::size_t settled_count() const;
    std::vector<ReceiptId> settled_ids() const;
    std::vector<std::string> rejection_reasons() const;

private:
    std::set<ReceiptId> receipts_;
    std::set<std::string> account_nonces_;
    std::vector<ReceiptId> settled_;
    std::vector<std::string> rejections_;
};

std::string account_nonce_key(const AccountId& account, Nonce nonce);

} // namespace bastion


#pragma once

#include "common/json.hpp"
#include "receipt/receipt.hpp"

#include <map>
#include <string>
#include <vector>

namespace bastion {

struct ManifestLine {
    ReceiptId receipt_id;
    AccountId source_account;
    AccountId beneficiary_account;
    IdentityId operator_id;
    PolicyId policy_id;
    Amount gross_amount;
    Nonce nonce;
    Epoch issued_at;
    Epoch expires_at;
    std::string lane;
    std::string receipt_digest;
};

struct ManifestAccountSummary {
    AccountId account;
    Amount gross_amount;
    std::uint64_t receipts = 0;
    std::uint64_t operators = 0;
};

struct ManifestOperatorSummary {
    IdentityId operator_id;
    Amount gross_amount;
    std::uint64_t receipts = 0;
    Epoch first_issue;
    Epoch last_issue;
};

struct ReceiptManifest {
    std::string manifest_id;
    std::vector<ManifestLine> lines;
    std::vector<ManifestAccountSummary> accounts;
    std::vector<ManifestOperatorSummary> operators;
    Amount gross_amount;
    Epoch min_issue;
    Epoch max_expiry;
    bool duplicate_nonce = false;

    bool empty() const;
    bool ok() const;
    std::string digest() const;
};

class ReceiptManifestBuilder {
public:
    explicit ReceiptManifestBuilder(std::string manifest_id);

    void add(const SettlementReceipt& receipt);
    ReceiptManifest build() const;

private:
    std::vector<ManifestLine> lines_;
    std::string manifest_id_;
};

ManifestLine manifest_line_from_receipt(const SettlementReceipt& receipt);
std::vector<ManifestAccountSummary> summarize_manifest_accounts(const std::vector<ManifestLine>& lines);
std::vector<ManifestOperatorSummary> summarize_manifest_operators(const std::vector<ManifestLine>& lines);
bool manifest_has_duplicate_nonce(const std::vector<ManifestLine>& lines);
std::string manifest_nonce_key(const ManifestLine& line);

void write_receipt_manifest(JsonWriter& json, const ReceiptManifest& manifest);
void write_manifest_line(JsonWriter& json, const ManifestLine& line);
void write_manifest_account_summary(JsonWriter& json, const ManifestAccountSummary& summary);
void write_manifest_operator_summary(JsonWriter& json, const ManifestOperatorSummary& summary);

} // namespace bastion

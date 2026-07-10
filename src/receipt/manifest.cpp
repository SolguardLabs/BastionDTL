#include "receipt/manifest.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace bastion {

bool ReceiptManifest::empty() const {
    return lines.empty();
}

bool ReceiptManifest::ok() const {
    return !empty() && !duplicate_nonce;
}

std::string ReceiptManifest::digest() const {
    CanonicalBuilder builder;
    builder.field("manifest", manifest_id);
    builder.field("ok", ok());
    builder.field("gross", static_cast<std::uint64_t>(gross_amount.units()));
    builder.field("min_issue", min_issue.value);
    builder.field("max_expiry", max_expiry.value);
    builder.open("lines");
    for (const auto& line : lines) {
        builder.field("receipt", line.receipt_id.str());
        builder.field("source", line.source_account.str());
        builder.field("beneficiary", line.beneficiary_account.str());
        builder.field("operator", line.operator_id.str());
        builder.field("policy", line.policy_id.str());
        builder.field("gross", static_cast<std::uint64_t>(line.gross_amount.units()));
        builder.field("nonce", line.nonce.value);
        builder.field("issued", line.issued_at.value);
        builder.field("expires", line.expires_at.value);
        builder.field("lane", line.lane);
        builder.field("digest", line.receipt_digest);
    }
    builder.close();
    return stable_hash_hex(builder.str());
}

ReceiptManifestBuilder::ReceiptManifestBuilder(std::string manifest_id)
    : manifest_id_(normalize_label(manifest_id)) {
    if (!is_valid_label(manifest_id_)) {
        fail("manifest id is invalid");
    }
}

void ReceiptManifestBuilder::add(const SettlementReceipt& receipt) {
    lines_.push_back(manifest_line_from_receipt(receipt));
}

ReceiptManifest ReceiptManifestBuilder::build() const {
    ReceiptManifest manifest;
    manifest.manifest_id = manifest_id_;
    manifest.lines = lines_;
    manifest.accounts = summarize_manifest_accounts(lines_);
    manifest.operators = summarize_manifest_operators(lines_);
    manifest.duplicate_nonce = manifest_has_duplicate_nonce(lines_);
    if (lines_.empty()) {
        manifest.min_issue = Epoch(0);
        manifest.max_expiry = Epoch(0);
        return manifest;
    }

    manifest.min_issue = lines_.front().issued_at;
    manifest.max_expiry = lines_.front().expires_at;
    for (const auto& line : lines_) {
        manifest.gross_amount = manifest.gross_amount.checked_add(line.gross_amount);
        if (line.issued_at < manifest.min_issue) {
            manifest.min_issue = line.issued_at;
        }
        if (line.expires_at > manifest.max_expiry) {
            manifest.max_expiry = line.expires_at;
        }
    }
    return manifest;
}

ManifestLine manifest_line_from_receipt(const SettlementReceipt& receipt) {
    ManifestLine line;
    line.receipt_id = receipt.id;
    line.source_account = receipt.terms.source_account;
    line.beneficiary_account = receipt.terms.beneficiary_account;
    line.operator_id = receipt.issued_by;
    line.policy_id = receipt.economics.policy_id;
    line.gross_amount = receipt.terms.gross_amount;
    line.nonce = receipt.terms.nonce;
    line.issued_at = receipt.issued_at;
    line.expires_at = receipt.terms.valid_for.expires_at;
    line.lane = receipt.terms.lane;
    line.receipt_digest = receipt.digest();
    return line;
}

std::vector<ManifestAccountSummary> summarize_manifest_accounts(
    const std::vector<ManifestLine>& lines
) {
    struct Accumulator {
        Amount gross;
        std::uint64_t receipts = 0;
        std::set<IdentityId> operators;
    };
    std::map<AccountId, Accumulator> by_account;
    for (const auto& line : lines) {
        auto& acc = by_account[line.source_account];
        acc.gross = acc.gross.checked_add(line.gross_amount);
        acc.receipts += 1;
        acc.operators.insert(line.operator_id);
    }
    std::vector<ManifestAccountSummary> out;
    out.reserve(by_account.size());
    for (const auto& [account, acc] : by_account) {
        out.push_back(ManifestAccountSummary{
            account,
            acc.gross,
            acc.receipts,
            static_cast<std::uint64_t>(acc.operators.size()),
        });
    }
    return out;
}

std::vector<ManifestOperatorSummary> summarize_manifest_operators(
    const std::vector<ManifestLine>& lines
) {
    struct Accumulator {
        Amount gross;
        std::uint64_t receipts = 0;
        Epoch first;
        Epoch last;
        bool initialized = false;
    };
    std::map<IdentityId, Accumulator> by_operator;
    for (const auto& line : lines) {
        auto& acc = by_operator[line.operator_id];
        acc.gross = acc.gross.checked_add(line.gross_amount);
        acc.receipts += 1;
        if (!acc.initialized) {
            acc.first = line.issued_at;
            acc.last = line.issued_at;
            acc.initialized = true;
        } else {
            if (line.issued_at < acc.first) {
                acc.first = line.issued_at;
            }
            if (line.issued_at > acc.last) {
                acc.last = line.issued_at;
            }
        }
    }
    std::vector<ManifestOperatorSummary> out;
    out.reserve(by_operator.size());
    for (const auto& [operator_id, acc] : by_operator) {
        out.push_back(ManifestOperatorSummary{
            operator_id,
            acc.gross,
            acc.receipts,
            acc.first,
            acc.last,
        });
    }
    return out;
}

bool manifest_has_duplicate_nonce(const std::vector<ManifestLine>& lines) {
    std::set<std::string> seen;
    for (const auto& line : lines) {
        const auto key = manifest_nonce_key(line);
        if (seen.find(key) != seen.end()) {
            return true;
        }
        seen.insert(key);
    }
    return false;
}

std::string manifest_nonce_key(const ManifestLine& line) {
    return line.source_account.str() + ":" + line.nonce.str();
}

void write_receipt_manifest(JsonWriter& json, const ReceiptManifest& manifest) {
    json.begin_object();
    json.field("manifestId", manifest.manifest_id);
    json.field("ok", manifest.ok());
    json.field("digest", manifest.digest());
    json.field("grossAmount", manifest.gross_amount);
    json.field("minIssue", manifest.min_issue.value);
    json.field("maxExpiry", manifest.max_expiry.value);
    json.field("duplicateNonce", manifest.duplicate_nonce);
    json.key("accounts");
    json.begin_array();
    for (const auto& account : manifest.accounts) {
        write_manifest_account_summary(json, account);
    }
    json.end_array();
    json.key("operators");
    json.begin_array();
    for (const auto& operator_summary : manifest.operators) {
        write_manifest_operator_summary(json, operator_summary);
    }
    json.end_array();
    json.key("lines");
    json.begin_array();
    for (const auto& line : manifest.lines) {
        write_manifest_line(json, line);
    }
    json.end_array();
    json.end_object();
}

void write_manifest_line(JsonWriter& json, const ManifestLine& line) {
    json.begin_object();
    json.field("receiptId", line.receipt_id.str());
    json.field("sourceAccount", line.source_account.str());
    json.field("beneficiaryAccount", line.beneficiary_account.str());
    json.field("operator", line.operator_id.str());
    json.field("policy", line.policy_id.str());
    json.field("grossAmount", line.gross_amount);
    json.field("nonce", line.nonce.value);
    json.field("issuedAt", line.issued_at.value);
    json.field("expiresAt", line.expires_at.value);
    json.field("lane", line.lane);
    json.field("receiptDigest", line.receipt_digest);
    json.end_object();
}

void write_manifest_account_summary(JsonWriter& json, const ManifestAccountSummary& summary) {
    json.begin_object();
    json.field("account", summary.account.str());
    json.field("grossAmount", summary.gross_amount);
    json.field("receipts", summary.receipts);
    json.field("operators", summary.operators);
    json.end_object();
}

void write_manifest_operator_summary(JsonWriter& json, const ManifestOperatorSummary& summary) {
    json.begin_object();
    json.field("operator", summary.operator_id.str());
    json.field("grossAmount", summary.gross_amount);
    json.field("receipts", summary.receipts);
    json.field("firstIssue", summary.first_issue.value);
    json.field("lastIssue", summary.last_issue.value);
    json.end_object();
}

} // namespace bastion


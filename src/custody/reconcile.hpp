#pragma once

#include "custody/ledger.hpp"

#include <map>
#include <string>
#include <vector>

namespace bastion {

struct AccountReconciliationLine {
    AccountId account;
    AssetId asset;
    AccountStatus status = AccountStatus::Open;
    Amount available;
    Amount reserved;
    Amount locked;
    Amount total;
    std::string current_operator;
    std::uint64_t active_grants = 0;
    std::uint64_t retired_grants = 0;
    bool closeable = false;
};

struct PolicyReconciliationLine {
    AccountId account;
    PolicyId policy;
    IdentityId operator_id;
    AccountId fee_account;
    AccountId reserve_account;
    BasisPoints fee_bps;
    BasisPoints reserve_bps;
    Epoch effective_from;
    std::string digest;
    bool current = false;
};

struct AssetReconciliationLine {
    AssetId asset;
    Amount available;
    Amount reserved;
    Amount locked;
    Amount total;
    std::uint64_t accounts = 0;
};

struct ReconciliationFinding {
    std::string code;
    std::string severity;
    AccountId account;
    std::string message;
};

struct ReconciliationReport {
    std::string network_id;
    Epoch epoch;
    std::vector<AccountReconciliationLine> accounts;
    std::vector<PolicyReconciliationLine> policies;
    std::vector<AssetReconciliationLine> assets;
    std::vector<ReconciliationFinding> findings;
    std::string digest;

    bool ok() const;
    std::uint64_t current_policy_count() const;
    std::uint64_t retired_policy_count() const;
};

class LedgerReconciler {
public:
    explicit LedgerReconciler(const LedgerState& ledger);

    ReconciliationReport run() const;

private:
    AccountReconciliationLine account_line(const CustodyAccount& account) const;
    std::vector<PolicyReconciliationLine> policy_lines(const CustodyAccount& account) const;
    std::vector<AssetReconciliationLine> asset_lines(
        const std::vector<AccountReconciliationLine>& accounts
    ) const;
    std::vector<ReconciliationFinding> findings(
        const std::vector<AccountReconciliationLine>& accounts,
        const std::vector<PolicyReconciliationLine>& policies
    ) const;
    std::string digest(
        const std::vector<AccountReconciliationLine>& accounts,
        const std::vector<PolicyReconciliationLine>& policies,
        const std::vector<AssetReconciliationLine>& assets
    ) const;

    const LedgerState& ledger_;
};

void write_reconciliation_report(JsonWriter& json, const ReconciliationReport& report);
void write_account_reconciliation_line(JsonWriter& json, const AccountReconciliationLine& line);
void write_policy_reconciliation_line(JsonWriter& json, const PolicyReconciliationLine& line);
void write_asset_reconciliation_line(JsonWriter& json, const AssetReconciliationLine& line);
void write_reconciliation_finding(JsonWriter& json, const ReconciliationFinding& finding);

} // namespace bastion


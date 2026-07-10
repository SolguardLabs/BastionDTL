#include "custody/reconcile.hpp"

#include <algorithm>
#include <map>

namespace bastion {

bool ReconciliationReport::ok() const {
    return findings.empty();
}

std::uint64_t ReconciliationReport::current_policy_count() const {
    return static_cast<std::uint64_t>(std::count_if(
        policies.begin(),
        policies.end(),
        [](const PolicyReconciliationLine& line) { return line.current; }
    ));
}

std::uint64_t ReconciliationReport::retired_policy_count() const {
    return static_cast<std::uint64_t>(std::count_if(
        policies.begin(),
        policies.end(),
        [](const PolicyReconciliationLine& line) { return !line.current; }
    ));
}

LedgerReconciler::LedgerReconciler(const LedgerState& ledger) : ledger_(ledger) {}

ReconciliationReport LedgerReconciler::run() const {
    ReconciliationReport report;
    report.network_id = ledger_.network_id();
    report.epoch = ledger_.epoch();

    for (const auto& id : ledger_.account_ids()) {
        const auto& account = ledger_.account(id);
        report.accounts.push_back(account_line(account));
        auto account_policies = policy_lines(account);
        report.policies.insert(report.policies.end(), account_policies.begin(), account_policies.end());
    }
    report.assets = asset_lines(report.accounts);
    report.findings = findings(report.accounts, report.policies);
    report.digest = digest(report.accounts, report.policies, report.assets);
    return report;
}

AccountReconciliationLine LedgerReconciler::account_line(const CustodyAccount& account) const {
    AccountReconciliationLine line;
    line.account = account.id();
    line.asset = account.asset();
    line.status = account.status();
    line.available = account.balance().available;
    line.reserved = account.balance().reserved;
    line.locked = account.balance().locked;
    line.total = account.balance().total();
    line.closeable = account.balance().empty() && account.status() != AccountStatus::Closed;
    for (const auto& grant : account.grants()) {
        if (grant.retired()) {
            line.retired_grants += 1;
        } else {
            line.active_grants += 1;
            line.current_operator = grant.operator_id.str();
        }
    }
    return line;
}

std::vector<PolicyReconciliationLine> LedgerReconciler::policy_lines(
    const CustodyAccount& account
) const {
    std::vector<PolicyReconciliationLine> out;
    for (const auto& grant : account.grants()) {
        PolicyReconciliationLine line;
        line.account = account.id();
        line.policy = grant.policy.id;
        line.operator_id = grant.operator_id;
        line.fee_account = grant.policy.fee_account;
        line.reserve_account = grant.policy.reserve_account;
        line.fee_bps = grant.policy.operator_fee_bps;
        line.reserve_bps = grant.policy.reserve_bps;
        line.effective_from = grant.effective_from;
        line.digest = grant.policy.digest();
        line.current = !grant.retired();
        out.push_back(line);
    }
    return out;
}

std::vector<AssetReconciliationLine> LedgerReconciler::asset_lines(
    const std::vector<AccountReconciliationLine>& accounts
) const {
    std::map<AssetId, AssetReconciliationLine> by_asset;
    for (const auto& account : accounts) {
        auto& line = by_asset[account.asset];
        line.asset = account.asset;
        line.available = line.available.checked_add(account.available);
        line.reserved = line.reserved.checked_add(account.reserved);
        line.locked = line.locked.checked_add(account.locked);
        line.total = line.total.checked_add(account.total);
        line.accounts += 1;
    }
    std::vector<AssetReconciliationLine> out;
    out.reserve(by_asset.size());
    for (const auto& [_, line] : by_asset) {
        out.push_back(line);
    }
    return out;
}

std::vector<ReconciliationFinding> LedgerReconciler::findings(
    const std::vector<AccountReconciliationLine>& accounts,
    const std::vector<PolicyReconciliationLine>& policies
) const {
    std::vector<ReconciliationFinding> out;
    std::map<AccountId, std::uint64_t> current_policies;
    for (const auto& policy : policies) {
        if (policy.current) {
            current_policies[policy.account] += 1;
        }
        if (!ledger_.has_account(policy.fee_account)) {
            out.push_back(ReconciliationFinding{
                "missing-fee-account",
                "high",
                policy.account,
                "fee account is not present in ledger",
            });
        }
        if (!ledger_.has_account(policy.reserve_account)) {
            out.push_back(ReconciliationFinding{
                "missing-reserve-account",
                "high",
                policy.account,
                "reserve account is not present in ledger",
            });
        }
    }
    for (const auto& account : accounts) {
        const bool custody_account = account.account.str().rfind("custody:", 0) == 0;
        if (account.status == AccountStatus::Closed && !account.total.is_zero()) {
            out.push_back(ReconciliationFinding{
                "closed-account-balance",
                "critical",
                account.account,
                "closed account retains balance",
            });
        }
        if (custody_account && account.status != AccountStatus::Closed && current_policies[account.account] > 1) {
            out.push_back(ReconciliationFinding{
                "multiple-current-policies",
                "medium",
                account.account,
                "account has more than one current operator policy",
            });
        }
        if (custody_account && account.status == AccountStatus::Open && current_policies[account.account] == 0) {
            out.push_back(ReconciliationFinding{
                "missing-current-policy",
                "medium",
                account.account,
                "open account has no current operator policy",
            });
        }
    }
    return out;
}

std::string LedgerReconciler::digest(
    const std::vector<AccountReconciliationLine>& accounts,
    const std::vector<PolicyReconciliationLine>& policies,
    const std::vector<AssetReconciliationLine>& assets
) const {
    CanonicalBuilder builder;
    builder.field("network", ledger_.network_id());
    builder.field("epoch", ledger_.epoch().value);
    builder.open("accounts");
    for (const auto& account : accounts) {
        builder.field("account", account.account.str());
        builder.field("status", status_name(account.status));
        builder.field("total", static_cast<std::uint64_t>(account.total.units()));
        builder.field("operator", account.current_operator);
    }
    builder.close();
    builder.open("policies");
    for (const auto& policy : policies) {
        builder.field("account", policy.account.str());
        builder.field("policy", policy.policy.str());
        builder.field("operator", policy.operator_id.str());
        builder.field("digest", policy.digest);
        builder.field("current", policy.current);
    }
    builder.close();
    builder.open("assets");
    for (const auto& asset : assets) {
        builder.field("asset", asset.asset.str());
        builder.field("total", static_cast<std::uint64_t>(asset.total.units()));
        builder.field("accounts", asset.accounts);
    }
    builder.close();
    return stable_hash_hex(builder.str());
}

void write_reconciliation_report(JsonWriter& json, const ReconciliationReport& report) {
    json.begin_object();
    json.field("networkId", report.network_id);
    json.field("epoch", report.epoch.value);
    json.field("ok", report.ok());
    json.field("digest", report.digest);
    json.field("currentPolicies", report.current_policy_count());
    json.field("retiredPolicies", report.retired_policy_count());
    json.key("accounts");
    json.begin_array();
    for (const auto& line : report.accounts) {
        write_account_reconciliation_line(json, line);
    }
    json.end_array();
    json.key("policies");
    json.begin_array();
    for (const auto& line : report.policies) {
        write_policy_reconciliation_line(json, line);
    }
    json.end_array();
    json.key("assets");
    json.begin_array();
    for (const auto& line : report.assets) {
        write_asset_reconciliation_line(json, line);
    }
    json.end_array();
    json.key("findings");
    json.begin_array();
    for (const auto& finding : report.findings) {
        write_reconciliation_finding(json, finding);
    }
    json.end_array();
    json.end_object();
}

void write_account_reconciliation_line(JsonWriter& json, const AccountReconciliationLine& line) {
    json.begin_object();
    json.field("account", line.account.str());
    json.field("asset", line.asset.str());
    json.field("status", status_name(line.status));
    json.field("available", line.available);
    json.field("reserved", line.reserved);
    json.field("locked", line.locked);
    json.field("total", line.total);
    json.field("currentOperator", line.current_operator);
    json.field("activeGrants", line.active_grants);
    json.field("retiredGrants", line.retired_grants);
    json.field("closeable", line.closeable);
    json.end_object();
}

void write_policy_reconciliation_line(JsonWriter& json, const PolicyReconciliationLine& line) {
    json.begin_object();
    json.field("account", line.account.str());
    json.field("policy", line.policy.str());
    json.field("operator", line.operator_id.str());
    json.field("feeAccount", line.fee_account.str());
    json.field("reserveAccount", line.reserve_account.str());
    json.field("feeBps", static_cast<std::uint64_t>(line.fee_bps.value()));
    json.field("reserveBps", static_cast<std::uint64_t>(line.reserve_bps.value()));
    json.field("effectiveFrom", line.effective_from.value);
    json.field("digest", line.digest);
    json.field("current", line.current);
    json.end_object();
}

void write_asset_reconciliation_line(JsonWriter& json, const AssetReconciliationLine& line) {
    json.begin_object();
    json.field("asset", line.asset.str());
    json.field("available", line.available);
    json.field("reserved", line.reserved);
    json.field("locked", line.locked);
    json.field("total", line.total);
    json.field("accounts", line.accounts);
    json.end_object();
}

void write_reconciliation_finding(JsonWriter& json, const ReconciliationFinding& finding) {
    json.begin_object();
    json.field("code", finding.code);
    json.field("severity", finding.severity);
    json.field("account", finding.account.str());
    json.field("message", finding.message);
    json.end_object();
}

} // namespace bastion

#pragma once

#include "settlement/settlement.hpp"

#include <functional>
#include <string>
#include <vector>

namespace bastion {

struct ScenarioCheck {
    std::string name;
    bool ok = false;
    std::string detail;
};

struct ScenarioReport {
    std::string scenario;
    bool ok = true;
    LedgerState ledger;
    std::vector<SettlementResult> settlements;
    std::vector<WithdrawalResult> withdrawals;
    std::vector<ScenarioCheck> checks;
    std::vector<std::string> notes;

    ScenarioReport(std::string name, LedgerState ledger_state);

    void add_check(std::string name, bool ok, std::string detail);
    void add_note(std::string note);
    void write_json(JsonWriter& json) const;
};

std::vector<std::string> scenario_names();
ScenarioReport run_scenario(std::string_view name);

ScenarioReport run_receipts_scenario();
ScenarioReport run_permissions_scenario();
ScenarioReport run_rotation_scenario();
ScenarioReport run_withdrawals_scenario();
ScenarioReport run_closure_scenario();
ScenarioReport run_snapshot_scenario();

SettlementReceipt issue_current_receipt(
    LedgerState& ledger,
    AccountId source,
    AccountId beneficiary,
    Amount amount,
    Nonce nonce,
    std::string memo
);

void write_withdrawal_result(JsonWriter& json, const WithdrawalResult& result);
void write_scenario_check(JsonWriter& json, const ScenarioCheck& check);

} // namespace bastion


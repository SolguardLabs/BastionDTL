#include "runtime/scenarios.hpp"

#include "custody/reconcile.hpp"
#include "receipt/manifest.hpp"
#include "settlement/batch.hpp"
#include "runtime/version.hpp"

#include <algorithm>
#include <sstream>

namespace bastion {

namespace {

Amount balance_of(const LedgerState& ledger, std::string_view account_id) {
    return ledger.account(AccountId(account_id)).balance().available;
}

bool account_status_is(const LedgerState& ledger, std::string_view account_id, AccountStatus status) {
    return ledger.account(AccountId(account_id)).status() == status;
}

void configure_rotation_to_south(LedgerState& ledger, std::uint32_t fee_bps, std::uint32_t reserve_bps) {
    auto policy = default_policy(
        "policy:atlas:south",
        AccountId("fees:south"),
        AccountId("reserve:atlas"),
        "standard",
        fee_bps,
        reserve_bps,
        100'000
    );
    policy.withdrawal_mode = WithdrawalMode::OwnerOrTreasurer;
    ledger.rotate_operator(
        AccountId("custody:atlas"),
        IdentityId("owner:atlas"),
        make_grant(IdentityId("operator:south"), policy, ledger.epoch(), "south rotation")
    );
}

} // namespace

ScenarioReport::ScenarioReport(std::string name, LedgerState ledger_state)
    : scenario(std::move(name)), ledger(std::move(ledger_state)) {}

void ScenarioReport::add_check(std::string name, bool check_ok, std::string detail) {
    checks.push_back(ScenarioCheck{std::move(name), check_ok, std::move(detail)});
    ok = ok && check_ok;
}

void ScenarioReport::add_note(std::string note) {
    notes.push_back(std::move(note));
}

void ScenarioReport::write_json(JsonWriter& json) const {
    json.begin_object();
    json.field("ok", ok);
    json.field("scenario", scenario);
    json.field("networkId", ledger.network_id());
    json.field("epoch", ledger.epoch().value);
    json.field("stateDigest", ledger.state_digest());
    json.field("totalSupply", ledger.total_supply(ledger.native_asset()));
    json.key("build");
    write_build_info(json, build_info());
    json.key("settlements");
    json.begin_array();
    for (const auto& settlement : settlements) {
        write_settlement_result(json, settlement);
    }
    json.end_array();
    json.key("withdrawals");
    json.begin_array();
    for (const auto& withdrawal : withdrawals) {
        write_withdrawal_result(json, withdrawal);
    }
    json.end_array();
    json.key("checks");
    json.begin_array();
    for (const auto& check : checks) {
        write_scenario_check(json, check);
    }
    json.end_array();
    json.key("notes");
    json.begin_array();
    for (const auto& note : notes) {
        json.string(note);
    }
    json.end_array();
    json.key("reconciliation");
    LedgerReconciler reconciler(ledger);
    write_reconciliation_report(json, reconciler.run());
    json.key("liquidity");
    if (liquidity.has_value()) {
        write_liquidity_report(json, *liquidity);
    } else {
        json.null();
    }
    json.key("governance");
    if (governance.has_value()) {
        write_change_control_report(json, *governance);
    } else {
        json.null();
    }
    json.key("ledger");
    ledger.write_json(json);
    json.end_object();
}

std::vector<std::string> scenario_names() {
    return {
        "receipts",
        "permissions",
        "rotation",
        "withdrawals",
        "closure",
        "snapshot",
        "liquidity",
        "governance",
    };
}

ScenarioReport run_scenario(std::string_view name) {
    const auto normalized = normalize_label(name);
    if (normalized == "receipts" || normalized == "receipt") {
        return run_receipts_scenario();
    }
    if (normalized == "permissions" || normalized == "perms") {
        return run_permissions_scenario();
    }
    if (normalized == "rotation" || normalized == "operators") {
        return run_rotation_scenario();
    }
    if (normalized == "withdrawals" || normalized == "withdraw") {
        return run_withdrawals_scenario();
    }
    if (normalized == "closure" || normalized == "close") {
        return run_closure_scenario();
    }
    if (normalized == "snapshot" || normalized == "state") {
        return run_snapshot_scenario();
    }
    if (normalized == "liquidity" || normalized == "risk") {
        return run_liquidity_scenario();
    }
    if (normalized == "governance" || normalized == "changes") {
        return run_governance_scenario();
    }
    fail("unknown scenario");
}

ScenarioReport run_receipts_scenario() {
    auto ledger = make_default_ledger();
    ScenarioReport report("receipts", std::move(ledger));
    SettlementEngine engine(report.ledger);

    auto receipt = issue_current_receipt(
        report.ledger,
        AccountId("custody:atlas"),
        AccountId("beneficiary:merchant"),
        Amount::from_units(25'000),
        Nonce(1001),
        "merchant cycle"
    );
    auto result = engine.settle(receipt, IdentityId("operator:north"));
    report.settlements.push_back(result);
    report.add_check("receipt accepted", result.decision == SettlementDecision::Accepted, result.message);
    report.add_check(
        "beneficiary credited",
        balance_of(report.ledger, "beneficiary:merchant") == result.beneficiary_amount,
        balance_of(report.ledger, "beneficiary:merchant").str()
    );
    report.add_check(
        "fee credited",
        balance_of(report.ledger, "fees:north") == result.operator_fee,
        balance_of(report.ledger, "fees:north").str()
    );
    report.add_check(
        "reserve credited",
        balance_of(report.ledger, "reserve:atlas") == result.reserve_amount,
        balance_of(report.ledger, "reserve:atlas").str()
    );
    report.add_check(
        "supply conserved",
        report.ledger.total_supply(report.ledger.native_asset()) == Amount::from_units(1'500'000),
        report.ledger.total_supply(report.ledger.native_asset()).str()
    );
    return report;
}

ScenarioReport run_permissions_scenario() {
    auto ledger = make_default_ledger();
    ScenarioReport report("permissions", std::move(ledger));
    SettlementEngine engine(report.ledger);
    ReceiptFactory factory(report.ledger.identities());

    auto valid = issue_current_receipt(
        report.ledger,
        AccountId("custody:atlas"),
        AccountId("beneficiary:merchant"),
        Amount::from_units(12'000),
        Nonce(2001),
        "operator permission"
    );
    auto invalid = factory.resign(valid, IdentityId("operator:west"));
    auto rejected = engine.settle(invalid, IdentityId("operator:west"));
    report.settlements.push_back(rejected);
    report.add_check("unauthorized operator rejected", rejected.decision == SettlementDecision::Rejected, rejected.message);

    auto operator_withdraw = report.ledger.withdraw(
        AccountId("custody:atlas"),
        IdentityId("operator:north"),
        Amount::from_units(1'000),
        "operator withdrawal"
    );
    report.withdrawals.push_back(operator_withdraw);
    report.add_check("operator withdrawal rejected", !operator_withdraw.ok, operator_withdraw.message);

    auto treasurer_withdraw = report.ledger.withdraw(
        AccountId("custody:atlas"),
        IdentityId("treasury:core"),
        Amount::from_units(1'000),
        "treasury sweep"
    );
    report.withdrawals.push_back(treasurer_withdraw);
    report.add_check("treasurer withdrawal accepted", treasurer_withdraw.ok, treasurer_withdraw.message);

    report.ledger.freeze_account(AccountId("custody:forge"), IdentityId("owner:forge"), "review window");
    auto frozen_withdraw = report.ledger.withdraw(
        AccountId("custody:forge"),
        IdentityId("owner:forge"),
        Amount::from_units(1'000),
        "frozen withdrawal"
    );
    report.withdrawals.push_back(frozen_withdraw);
    report.add_check("frozen account blocks withdrawal", !frozen_withdraw.ok, frozen_withdraw.message);
    return report;
}

ScenarioReport run_rotation_scenario() {
    auto ledger = make_default_ledger();
    ScenarioReport report("rotation", std::move(ledger));
    SettlementEngine engine(report.ledger);

    auto pre_rotation = issue_current_receipt(
        report.ledger,
        AccountId("custody:atlas"),
        AccountId("beneficiary:merchant"),
        Amount::from_units(10'000),
        Nonce(3001),
        "handoff receivable"
    );
    ReceiptManifestBuilder handoff_manifest("atlas-handoff");
    handoff_manifest.add(pre_rotation);
    report.add_note("handoff:" + handoff_manifest.build().digest().substr(0, 16));

    report.ledger.advance("prepare rotation");
    configure_rotation_to_south(report.ledger, 40, 25);

    auto old_result = engine.settle(pre_rotation, IdentityId("treasury:core"));
    report.settlements.push_back(old_result);
    report.add_check("pre-rotation receipt accepted", old_result.decision == SettlementDecision::Accepted, old_result.message);
    report.add_check(
        "current operator recorded",
        old_result.applied_operator == IdentityId("operator:south"),
        old_result.applied_operator.str()
    );

    auto post_rotation = issue_current_receipt(
        report.ledger,
        AccountId("custody:atlas"),
        AccountId("beneficiary:market"),
        Amount::from_units(15'000),
        Nonce(3002),
        "new desk receivable"
    );
    auto new_result = engine.settle(post_rotation, IdentityId("operator:south"));
    report.settlements.push_back(new_result);
    report.add_check("post-rotation receipt accepted", new_result.decision == SettlementDecision::Accepted, new_result.message);
    report.add_check(
        "two receipts settled",
        report.ledger.receipt_book().settled_count() == 2,
        std::to_string(report.ledger.receipt_book().settled_count())
    );
    return report;
}

ScenarioReport run_withdrawals_scenario() {
    auto ledger = make_default_ledger();
    ScenarioReport report("withdrawals", std::move(ledger));

    auto owner_result = report.ledger.withdraw(
        AccountId("custody:forge"),
        IdentityId("owner:forge"),
        Amount::from_units(25'000),
        "owner withdrawal"
    );
    report.withdrawals.push_back(owner_result);
    report.add_check("owner withdrawal accepted", owner_result.ok, owner_result.message);

    auto other_owner = report.ledger.withdraw(
        AccountId("custody:forge"),
        IdentityId("owner:atlas"),
        Amount::from_units(1'000),
        "cross owner withdrawal"
    );
    report.withdrawals.push_back(other_owner);
    report.add_check("cross-owner withdrawal rejected", !other_owner.ok, other_owner.message);

    auto remaining = balance_of(report.ledger, "custody:forge");
    report.add_check("remaining balance updated", remaining == Amount::from_units(475'000), remaining.str());
    return report;
}

ScenarioReport run_closure_scenario() {
    auto ledger = make_default_ledger();
    ScenarioReport report("closure", std::move(ledger));

    report.ledger.create_account(AccountId("custody:empty"), AssetId("usdc"), IdentityId("owner:atlas"));
    auto policy = default_policy(
        "policy:empty:north",
        AccountId("fees:north"),
        AccountId("reserve:atlas"),
        "standard",
        30,
        10,
        5'000
    );
    report.ledger.install_initial_operator(
        AccountId("custody:empty"),
        IdentityId("owner:atlas"),
        make_grant(IdentityId("operator:north"), policy, report.ledger.epoch(), "temporary")
    );
    report.ledger.mark_closing(AccountId("custody:empty"), IdentityId("owner:atlas"));
    report.ledger.close_account(AccountId("custody:empty"), IdentityId("owner:atlas"));
    report.add_check(
        "empty account closed",
        account_status_is(report.ledger, "custody:empty", AccountStatus::Closed),
        status_name(report.ledger.account(AccountId("custody:empty")).status())
    );

    try {
        report.ledger.close_account(AccountId("custody:atlas"), IdentityId("owner:atlas"));
        report.add_check("funded account closure rejected", false, "unexpected close");
    } catch (const DomainError& error) {
        report.add_check("funded account closure rejected", true, error.what());
    }
    return report;
}

ScenarioReport run_snapshot_scenario() {
    auto ledger = make_default_ledger();
    ScenarioReport report("snapshot", std::move(ledger));
    SettlementEngine engine(report.ledger);
    BatchSettlementProcessor batcher(engine);

    auto merchant = issue_current_receipt(
        report.ledger,
        AccountId("custody:atlas"),
        AccountId("beneficiary:merchant"),
        Amount::from_units(7'500),
        Nonce(6001),
        "snapshot merchant"
    );
    report.settlements.push_back(engine.settle(merchant, IdentityId("operator:north")));

    auto escrow = issue_current_receipt(
        report.ledger,
        AccountId("custody:forge"),
        AccountId("beneficiary:escrow"),
        Amount::from_units(6'000),
        Nonce(6002),
        "snapshot escrow"
    );
    BatchSettlementRequest batch;
    batch.batch_id = "snapshot-batch";
    batch.submitter = IdentityId("treasury:core");
    batch.receipts = {merchant, escrow};
    batch.continue_on_error = true;
    ReceiptManifestBuilder manifest("snapshot-manifest");
    manifest.add(merchant);
    manifest.add(escrow);
    report.add_note("manifest:" + manifest.build().digest().substr(0, 16));
    auto batch_report = batcher.run(batch);
    report.settlements.insert(report.settlements.end(), batch_report.results.begin(), batch_report.results.end());
    report.add_note("batch:" + batch_report.digest().substr(0, 16));

    report.add_check(
        "digest present",
        report.ledger.state_digest().size() == 32,
        report.ledger.state_digest()
    );
    report.add_check(
        "receipts settled",
        report.ledger.receipt_book().settled_count() == 2,
        std::to_string(report.ledger.receipt_book().settled_count())
    );
    report.add_check(
        "supply conserved",
        report.ledger.total_supply(report.ledger.native_asset()) == Amount::from_units(1'500'000),
        report.ledger.total_supply(report.ledger.native_asset()).str()
    );
    return report;
}

ScenarioReport run_liquidity_scenario() {
    auto ledger = make_default_ledger();
    ScenarioReport report("liquidity", std::move(ledger));

    const std::vector<LiquidityPosition> positions = {
        LiquidityPosition{
            AccountId("custody:atlas"),
            AssetId("usdc"),
            Amount::from_units(400'000),
            Amount::from_units(100'000),
            Amount::from_units(50'000),
            Amount::from_units(80'000),
            Amount::from_units(600'000),
            BasisPoints(8'000),
            BasisPoints(2'000),
            BasisPoints(5'000),
        },
        LiquidityPosition{
            AccountId("custody:forge"),
            AssetId("usdc"),
            Amount::from_units(500'000),
            Amount::from_units(80'000),
            Amount::from_units(20'000),
            Amount::from_units(100'000),
            Amount::from_units(300'000),
            BasisPoints(7'000),
            BasisPoints(1'000),
            BasisPoints(5'000),
        },
    };
    const LiquidityLimits limits{
        BasisPoints(7'000),
        BasisPoints(6'000),
        11'000,
        Amount::zero(),
    };
    LiquidityRiskEngine engine;
    report.liquidity = engine.evaluate(positions, limits);
    report.add_check(
        "stressed requirement calculated",
        report.liquidity->total_stressed_outflow == Amount::from_units(1'050'000),
        report.liquidity->total_stressed_outflow.str()
    );
    report.add_check(
        "portfolio coverage calculated",
        report.liquidity->coverage_bps == 10'704,
        std::to_string(report.liquidity->coverage_bps)
    );
    report.add_check(
        "shortfall surfaced",
        report.liquidity->total_shortfall == Amount::from_units(206'000),
        report.liquidity->total_shortfall.str()
    );
    report.add_check(
        "limits enforce escalation",
        !report.liquidity->within_limits,
        report.liquidity->digest
    );
    return report;
}

ScenarioReport run_governance_scenario() {
    auto ledger = make_default_ledger();
    ScenarioReport report("governance", std::move(ledger));
    auto identities = default_identities();
    ChangeControl control(identities, 65, 60);
    control.configure_reviewer(IdentityId("owner:atlas"), 40);
    control.configure_reviewer(IdentityId("owner:forge"), 35);
    control.configure_reviewer(IdentityId("auditor:watch"), 25);

    ChangeRequest request;
    request.id = "change-risk-42";
    request.target = "custody-atlas";
    request.action = "rotate-policy";
    request.parameter_digest = compact_digest("policy:atlas:epoch-3");
    request.proposer = IdentityId("treasury:core");
    request.proposed_at = Epoch(1);
    request.ready_at = Epoch(3);
    request.expires_at = Epoch(8);
    request.nonce = 42;

    control.schedule(request, identities.sign(request.proposer, request.payload()));
    const auto atlas = IdentityId("owner:atlas");
    const auto auditor = IdentityId("auditor:watch");
    control.approve(
        request.id,
        identities.sign(atlas, control.approval_payload(request, atlas))
    );
    control.approve(
        request.id,
        identities.sign(auditor, control.approval_payload(request, auditor))
    );
    report.add_check(
        "delay window enforced",
        control.state(request.id, Epoch(2)) == ChangeState::Waiting,
        change_state_name(control.state(request.id, Epoch(2)))
    );
    control.execute(request.id, Epoch(3));
    report.governance = control.report(Epoch(3));
    report.add_check(
        "change executed after quorum",
        report.governance->changes.front().state == "executed",
        report.governance->changes.front().state
    );
    report.add_check(
        "approval weight retained",
        report.governance->changes.front().approval_weight == 65,
        std::to_string(report.governance->changes.front().approval_weight)
    );
    report.add_check(
        "governance digest present",
        report.governance->digest.size() == 20,
        report.governance->digest
    );
    return report;
}

SettlementReceipt issue_current_receipt(
    LedgerState& ledger,
    AccountId source,
    AccountId beneficiary,
    Amount amount,
    Nonce nonce,
    std::string memo
) {
    const auto& account = ledger.account(source);
    const auto& grant = account.current_grant();
    ReceiptFactory factory(ledger.identities());
    ReceiptBuildRequest request;
    request.source_account = std::move(source);
    request.beneficiary_account = std::move(beneficiary);
    request.gross_amount = amount;
    request.nonce = nonce;
    request.not_before = ledger.epoch();
    request.expires_at = Epoch(ledger.epoch().value + 8);
    request.memo = std::move(memo);
    return factory.build(account, grant, request, ledger.epoch());
}

void write_withdrawal_result(JsonWriter& json, const WithdrawalResult& result) {
    json.begin_object();
    json.field("ok", result.ok);
    json.field("account", result.account.str());
    json.field("actor", result.actor.str());
    json.field("amount", result.amount);
    json.field("message", result.message);
    json.end_object();
}

void write_scenario_check(JsonWriter& json, const ScenarioCheck& check) {
    json.begin_object();
    json.field("name", check.name);
    json.field("ok", check.ok);
    json.field("detail", check.detail);
    json.end_object();
}

} // namespace bastion

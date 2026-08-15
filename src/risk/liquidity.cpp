#include "risk/liquidity.hpp"

#include <algorithm>
#include <limits>
#include <numeric>

namespace bastion {

namespace {

constexpr std::uint64_t kBps = 10000;

std::uint64_t mul_div_floor(std::uint64_t left, std::uint64_t right, std::uint64_t denominator) {
    if (denominator == 0) {
        fail("liquidity denominator cannot be zero");
    }
    const auto first_gcd = std::gcd(left, denominator);
    left /= first_gcd;
    denominator /= first_gcd;
    const auto second_gcd = std::gcd(right, denominator);
    right /= second_gcd;
    denominator /= second_gcd;
    if (right != 0 && left > std::numeric_limits<std::uint64_t>::max() / right) {
        fail("liquidity multiplication overflow");
    }
    return (left * right) / denominator;
}

Amount apply_bps(Amount amount, std::uint32_t bps) {
    const auto value = mul_div_floor(
        static_cast<std::uint64_t>(amount.units()),
        static_cast<std::uint64_t>(bps),
        kBps
    );
    if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        fail("liquidity result exceeds amount range");
    }
    return Amount::from_units(static_cast<std::int64_t>(value));
}

} // namespace

void LiquidityPosition::validate() const {
    if (account.empty() || asset.empty()) {
        fail("liquidity position identifiers are required");
    }
    if (available.is_zero() && reserved.is_zero() && expected_inflow.is_zero()) {
        fail("liquidity position has no funding source");
    }
}

std::string LiquidityPosition::digest() const {
    CanonicalBuilder builder;
    builder.field("account", account.str());
    builder.field("asset", asset.str());
    builder.field("available", available.units());
    builder.field("reserved", reserved.units());
    builder.field("locked", locked.units());
    builder.field("expected_inflow", expected_inflow.units());
    builder.field("committed_outflow", committed_outflow.units());
    builder.field("inflow_recovery_bps", static_cast<std::uint64_t>(inflow_recovery_bps.value()));
    builder.field("outflow_stress_bps", static_cast<std::uint64_t>(outflow_stress_bps.value()));
    builder.field("reserve_release_bps", static_cast<std::uint64_t>(reserve_release_bps.value()));
    return compact_digest(builder.str());
}

void LiquidityLimits::validate() const {
    if (max_single_requirement_share_bps.value() == 0) {
        fail("maximum requirement share is required");
    }
    if (max_requirement_hhi_bps.value() == 0) {
        fail("maximum requirement hhi is required");
    }
    if (min_coverage_bps < 10000 || min_coverage_bps > 50000) {
        fail("minimum liquidity coverage is outside range");
    }
}

LiquidityReport LiquidityRiskEngine::evaluate(
    const std::vector<LiquidityPosition>& positions,
    const LiquidityLimits& limits
) const {
    if (positions.empty()) {
        fail("liquidity portfolio cannot be empty");
    }
    limits.validate();

    LiquidityReport report;
    std::string previous_account;
    for (const auto& position : positions) {
        position.validate();
        if (!previous_account.empty() && position.account.str() <= previous_account) {
            fail("liquidity positions must use canonical account order");
        }
        previous_account = position.account.str();

        auto risk = evaluate_account(position);
        report.total_immediately_available =
            report.total_immediately_available.checked_add(risk.immediately_available);
        report.total_releasable_reserve =
            report.total_releasable_reserve.checked_add(risk.releasable_reserve);
        report.total_recovered_inflow = report.total_recovered_inflow.checked_add(risk.recovered_inflow);
        report.total_stressed_outflow = report.total_stressed_outflow.checked_add(risk.stressed_outflow);
        report.total_surplus = report.total_surplus.checked_add(risk.surplus);
        report.total_shortfall = report.total_shortfall.checked_add(risk.shortfall);
        report.accounts.push_back(risk);
    }

    if (report.total_stressed_outflow.is_zero()) {
        report.coverage_bps = std::numeric_limits<std::uint32_t>::max();
    } else {
        const auto usable = report.total_immediately_available
                                .checked_add(report.total_releasable_reserve)
                                .checked_add(report.total_recovered_inflow);
        report.coverage_bps = ratio_bps(usable, report.total_stressed_outflow);
    }

    for (auto& risk : report.accounts) {
        risk.requirement_share_bps = report.total_stressed_outflow.is_zero()
            ? 0
            : ratio_bps(risk.stressed_outflow, report.total_stressed_outflow);
        risk.hhi_contribution_bps = hhi_contribution(risk.requirement_share_bps);
        report.requirement_hhi_bps += risk.hhi_contribution_bps;
        report.largest_requirement_share_bps =
            std::max(report.largest_requirement_share_bps, risk.requirement_share_bps);
    }

    report.within_limits =
        report.total_shortfall <= limits.max_total_shortfall
        && report.coverage_bps >= limits.min_coverage_bps
        && report.largest_requirement_share_bps <= limits.max_single_requirement_share_bps.value()
        && report.requirement_hhi_bps <= limits.max_requirement_hhi_bps.value();

    CanonicalBuilder builder;
    builder.field("domain", "bastion-liquidity-risk-v1");
    for (const auto& position : positions) {
        builder.field("position", position.digest());
    }
    builder.field("available", report.total_immediately_available.units());
    builder.field("reserve", report.total_releasable_reserve.units());
    builder.field("inflow", report.total_recovered_inflow.units());
    builder.field("outflow", report.total_stressed_outflow.units());
    builder.field("shortfall", report.total_shortfall.units());
    builder.field("coverage_bps", static_cast<std::uint64_t>(report.coverage_bps));
    builder.field("hhi_bps", static_cast<std::uint64_t>(report.requirement_hhi_bps));
    builder.field("within_limits", report.within_limits);
    report.digest = compact_digest(builder.str());
    return report;
}

AccountLiquidityRisk LiquidityRiskEngine::evaluate_account(
    const LiquidityPosition& position
) const {
    AccountLiquidityRisk risk;
    risk.account = position.account;
    risk.asset = position.asset;
    risk.immediately_available = position.available;
    risk.releasable_reserve = apply_bps(position.reserved, position.reserve_release_bps.value());
    risk.recovered_inflow = apply_bps(position.expected_inflow, position.inflow_recovery_bps.value());
    risk.stressed_outflow = position.committed_outflow.checked_add(
        apply_bps(position.committed_outflow, position.outflow_stress_bps.value())
    );
    const auto usable = risk.immediately_available
                            .checked_add(risk.releasable_reserve)
                            .checked_add(risk.recovered_inflow);
    risk.surplus = usable.saturating_sub(risk.stressed_outflow);
    risk.shortfall = risk.stressed_outflow.saturating_sub(usable);
    return risk;
}

std::uint32_t ratio_bps(Amount numerator, Amount denominator) {
    if (denominator.is_zero()) {
        fail("liquidity ratio denominator cannot be zero");
    }
    const auto value = mul_div_floor(
        static_cast<std::uint64_t>(numerator.units()),
        kBps,
        static_cast<std::uint64_t>(denominator.units())
    );
    return value > std::numeric_limits<std::uint32_t>::max()
        ? std::numeric_limits<std::uint32_t>::max()
        : static_cast<std::uint32_t>(value);
}

std::uint32_t hhi_contribution(std::uint32_t share_bps) {
    return static_cast<std::uint32_t>(mul_div_floor(share_bps, share_bps, kBps));
}

void write_liquidity_report(JsonWriter& json, const LiquidityReport& report) {
    json.begin_object();
    json.field("totalImmediatelyAvailable", report.total_immediately_available);
    json.field("totalReleasableReserve", report.total_releasable_reserve);
    json.field("totalRecoveredInflow", report.total_recovered_inflow);
    json.field("totalStressedOutflow", report.total_stressed_outflow);
    json.field("totalSurplus", report.total_surplus);
    json.field("totalShortfall", report.total_shortfall);
    json.field("coverageBps", static_cast<std::uint64_t>(report.coverage_bps));
    json.field("requirementHhiBps", static_cast<std::uint64_t>(report.requirement_hhi_bps));
    json.field(
        "largestRequirementShareBps",
        static_cast<std::uint64_t>(report.largest_requirement_share_bps)
    );
    json.field("withinLimits", report.within_limits);
    json.field("digest", report.digest);
    json.key("accounts");
    json.begin_array();
    for (const auto& account : report.accounts) {
        json.begin_object();
        json.field("account", account.account.str());
        json.field("asset", account.asset.str());
        json.field("immediatelyAvailable", account.immediately_available);
        json.field("releasableReserve", account.releasable_reserve);
        json.field("recoveredInflow", account.recovered_inflow);
        json.field("stressedOutflow", account.stressed_outflow);
        json.field("surplus", account.surplus);
        json.field("shortfall", account.shortfall);
        json.field("requirementShareBps", static_cast<std::uint64_t>(account.requirement_share_bps));
        json.field("hhiContributionBps", static_cast<std::uint64_t>(account.hhi_contribution_bps));
        json.end_object();
    }
    json.end_array();
    json.end_object();
}

} // namespace bastion

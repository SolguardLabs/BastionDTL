#pragma once

#include "common/amount.hpp"
#include "common/hash.hpp"
#include "common/json.hpp"
#include "common/types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace bastion {

struct LiquidityPosition {
    AccountId account;
    AssetId asset;
    Amount available;
    Amount reserved;
    Amount locked;
    Amount expected_inflow;
    Amount committed_outflow;
    BasisPoints inflow_recovery_bps;
    BasisPoints outflow_stress_bps;
    BasisPoints reserve_release_bps;

    void validate() const;
    std::string digest() const;
};

struct LiquidityLimits {
    BasisPoints max_single_requirement_share_bps;
    BasisPoints max_requirement_hhi_bps;
    std::uint32_t min_coverage_bps = 10000;
    Amount max_total_shortfall;

    void validate() const;
};

struct AccountLiquidityRisk {
    AccountId account;
    AssetId asset;
    Amount immediately_available;
    Amount releasable_reserve;
    Amount recovered_inflow;
    Amount stressed_outflow;
    Amount surplus;
    Amount shortfall;
    std::uint32_t requirement_share_bps = 0;
    std::uint32_t hhi_contribution_bps = 0;
};

struct LiquidityReport {
    std::vector<AccountLiquidityRisk> accounts;
    Amount total_immediately_available;
    Amount total_releasable_reserve;
    Amount total_recovered_inflow;
    Amount total_stressed_outflow;
    Amount total_surplus;
    Amount total_shortfall;
    std::uint32_t coverage_bps = 0;
    std::uint32_t requirement_hhi_bps = 0;
    std::uint32_t largest_requirement_share_bps = 0;
    bool within_limits = false;
    std::string digest;
};

class LiquidityRiskEngine {
public:
    LiquidityReport evaluate(
        const std::vector<LiquidityPosition>& positions,
        const LiquidityLimits& limits
    ) const;

private:
    AccountLiquidityRisk evaluate_account(const LiquidityPosition& position) const;
};

std::uint32_t ratio_bps(Amount numerator, Amount denominator);
std::uint32_t hhi_contribution(std::uint32_t share_bps);
void write_liquidity_report(JsonWriter& json, const LiquidityReport& report);

} // namespace bastion

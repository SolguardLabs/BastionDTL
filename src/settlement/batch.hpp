#pragma once

#include "settlement/settlement.hpp"

#include <string>
#include <vector>

namespace bastion {

struct BatchSettlementRequest {
    std::string batch_id;
    IdentityId submitter;
    std::vector<SettlementReceipt> receipts;
    bool continue_on_error = true;

    void validate() const;
};

struct BatchSettlementReport {
    std::string batch_id;
    IdentityId submitter;
    std::vector<SettlementResult> results;
    Amount gross;
    Amount beneficiary_amount;
    Amount operator_fees;
    Amount reserves;
    std::uint64_t accepted = 0;
    std::uint64_t rejected = 0;

    bool ok() const;
    std::string digest() const;
};

class BatchSettlementProcessor {
public:
    explicit BatchSettlementProcessor(SettlementEngine& engine);

    BatchSettlementReport run(const BatchSettlementRequest& request);

private:
    SettlementEngine& engine_;
};

void write_batch_settlement_report(JsonWriter& json, const BatchSettlementReport& report);

} // namespace bastion


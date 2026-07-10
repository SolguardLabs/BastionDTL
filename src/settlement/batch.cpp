#include "settlement/batch.hpp"

namespace bastion {

void BatchSettlementRequest::validate() const {
    if (!is_valid_label(batch_id)) {
        fail("batch id is invalid");
    }
    if (submitter.empty()) {
        fail("batch submitter is required");
    }
    if (receipts.empty()) {
        fail("batch has no receipts");
    }
}

bool BatchSettlementReport::ok() const {
    return rejected == 0;
}

std::string BatchSettlementReport::digest() const {
    CanonicalBuilder builder;
    builder.field("batch", batch_id);
    builder.field("submitter", submitter.str());
    builder.field("accepted", accepted);
    builder.field("rejected", rejected);
    builder.field("gross", static_cast<std::uint64_t>(gross.units()));
    builder.field("beneficiary", static_cast<std::uint64_t>(beneficiary_amount.units()));
    builder.field("fees", static_cast<std::uint64_t>(operator_fees.units()));
    builder.field("reserves", static_cast<std::uint64_t>(reserves.units()));
    builder.open("results");
    for (const auto& result : results) {
        builder.field("receipt", result.receipt_id.str());
        builder.field("decision", decision_name(result.decision));
        builder.field("digest", result.receipt_digest);
    }
    builder.close();
    return stable_hash_hex(builder.str());
}

BatchSettlementProcessor::BatchSettlementProcessor(SettlementEngine& engine) : engine_(engine) {}

BatchSettlementReport BatchSettlementProcessor::run(const BatchSettlementRequest& request) {
    request.validate();
    BatchSettlementReport report;
    report.batch_id = request.batch_id;
    report.submitter = request.submitter;

    for (const auto& receipt : request.receipts) {
        auto result = engine_.settle(receipt, request.submitter);
        if (result.decision == SettlementDecision::Accepted) {
            report.accepted += 1;
            report.gross = report.gross.checked_add(result.gross);
            report.beneficiary_amount = report.beneficiary_amount.checked_add(result.beneficiary_amount);
            report.operator_fees = report.operator_fees.checked_add(result.operator_fee);
            report.reserves = report.reserves.checked_add(result.reserve_amount);
        } else {
            report.rejected += 1;
        }
        report.results.push_back(result);
        if (result.decision == SettlementDecision::Rejected && !request.continue_on_error) {
            break;
        }
    }
    return report;
}

void write_batch_settlement_report(JsonWriter& json, const BatchSettlementReport& report) {
    json.begin_object();
    json.field("batchId", report.batch_id);
    json.field("submitter", report.submitter.str());
    json.field("ok", report.ok());
    json.field("digest", report.digest());
    json.field("accepted", report.accepted);
    json.field("rejected", report.rejected);
    json.field("gross", report.gross);
    json.field("beneficiaryAmount", report.beneficiary_amount);
    json.field("operatorFees", report.operator_fees);
    json.field("reserves", report.reserves);
    json.key("results");
    json.begin_array();
    for (const auto& result : report.results) {
        write_settlement_result(json, result);
    }
    json.end_array();
    json.end_object();
}

} // namespace bastion


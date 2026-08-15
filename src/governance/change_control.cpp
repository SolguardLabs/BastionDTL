#include "governance/change_control.hpp"

#include <algorithm>
#include <limits>

namespace bastion {

namespace {

bool contains_reviewer(const std::vector<ChangeVote>& votes, const IdentityId& reviewer) {
    return std::any_of(votes.begin(), votes.end(), [&](const ChangeVote& vote) {
        return vote.reviewer == reviewer;
    });
}

std::uint32_t checked_weight_add(std::uint32_t left, std::uint32_t right) {
    if (right > std::numeric_limits<std::uint32_t>::max() - left) {
        fail("change control weight overflow");
    }
    return left + right;
}

} // namespace

std::string change_state_name(ChangeState state) {
    switch (state) {
    case ChangeState::Unknown:
        return "unknown";
    case ChangeState::Waiting:
        return "waiting";
    case ChangeState::Ready:
        return "ready";
    case ChangeState::Executed:
        return "executed";
    case ChangeState::Cancelled:
        return "cancelled";
    case ChangeState::Expired:
        return "expired";
    }
    return "unknown";
}

void ChangeRequest::validate() const {
    if (!is_valid_label(id) || !is_valid_label(target) || !is_valid_label(action)) {
        fail("change request identifiers are invalid");
    }
    if (parameter_digest.empty() || proposer.empty()) {
        fail("change request authorization data is required");
    }
    if (!predecessor.empty() && !is_valid_label(predecessor)) {
        fail("change predecessor is invalid");
    }
    if (nonce == 0) {
        fail("change nonce is required");
    }
    if (ready_at <= proposed_at || expires_at <= ready_at) {
        fail("change request window is invalid");
    }
}

std::string ChangeRequest::payload() const {
    CanonicalBuilder builder;
    builder.field("domain", "bastion-change-request-v1");
    builder.field("id", id);
    builder.field("target", target);
    builder.field("action", action);
    builder.field("parameters", parameter_digest);
    builder.field("predecessor", predecessor);
    builder.field("proposer", proposer.str());
    builder.field("proposed_at", proposed_at.value);
    builder.field("ready_at", ready_at.value);
    builder.field("expires_at", expires_at.value);
    builder.field("nonce", nonce);
    return builder.str();
}

std::string ChangeRequest::digest() const {
    return compact_digest(payload());
}

std::uint32_t ChangeRecord::approval_weight() const {
    std::uint32_t total = 0;
    for (const auto& vote : approvals) {
        total = checked_weight_add(total, vote.weight);
    }
    return total;
}

std::uint32_t ChangeRecord::cancellation_weight() const {
    std::uint32_t total = 0;
    for (const auto& vote : cancellations) {
        total = checked_weight_add(total, vote.weight);
    }
    return total;
}

ChangeControl::ChangeControl(
    const IdentityRegistry& identities,
    std::uint32_t approval_quorum,
    std::uint32_t cancellation_quorum
)
    : identities_(identities),
      approval_quorum_(approval_quorum),
      cancellation_quorum_(cancellation_quorum) {
    if (approval_quorum == 0 || cancellation_quorum == 0) {
        fail("change control quorum is required");
    }
}

void ChangeControl::configure_reviewer(const IdentityId& reviewer, std::uint32_t weight) {
    const auto& identity = identities_.get(reviewer);
    if (!identity.enabled) {
        fail("change reviewer is disabled");
    }
    if (identity.role != IdentityRole::Owner && identity.role != IdentityRole::Treasurer
        && identity.role != IdentityRole::Auditor) {
        fail("change reviewer role is not allowed");
    }
    if (weight == 0 || weight > 100) {
        fail("reviewer weight is outside range");
    }
    reviewer_weights_[reviewer] = weight;
}

void ChangeControl::schedule(
    const ChangeRequest& request,
    const Signature& proposer_signature
) {
    request.validate();
    if (records_.contains(request.id)) {
        fail("change request already exists");
    }
    const auto& proposer = identities_.get(request.proposer);
    if (proposer.role != IdentityRole::Owner && proposer.role != IdentityRole::Treasurer) {
        fail("change proposer role is not allowed");
    }
    if (proposer_signature.signer != request.proposer
        || !identities_.verify(proposer_signature, request.payload())) {
        fail("change proposer signature is invalid");
    }
    records_.emplace(
        request.id,
        ChangeRecord{request, proposer_signature, {}, {}, false, false}
    );
}

void ChangeControl::approve(const std::string& change_id, const Signature& signature) {
    auto found = records_.find(change_id);
    if (found == records_.end()) {
        fail("change request is unknown");
    }
    auto& record = found->second;
    if (record.executed || record.cancelled) {
        fail("change request is finalized");
    }
    validate_reviewer(signature.signer);
    if (contains_reviewer(record.approvals, signature.signer)) {
        fail("reviewer approval already recorded");
    }
    if (!identities_.verify(signature, approval_payload(record.request, signature.signer))) {
        fail("change approval signature is invalid");
    }
    record.approvals.push_back(ChangeVote{signature.signer, reviewer_weight(signature.signer), signature});
}

void ChangeControl::vote_cancel(const std::string& change_id, const Signature& signature) {
    auto found = records_.find(change_id);
    if (found == records_.end()) {
        fail("change request is unknown");
    }
    auto& record = found->second;
    if (record.executed || record.cancelled) {
        fail("change request is finalized");
    }
    validate_reviewer(signature.signer);
    if (contains_reviewer(record.cancellations, signature.signer)) {
        fail("reviewer cancellation already recorded");
    }
    if (!identities_.verify(signature, cancellation_payload(record.request, signature.signer))) {
        fail("change cancellation signature is invalid");
    }
    record.cancellations.push_back(ChangeVote{signature.signer, reviewer_weight(signature.signer), signature});
    if (record.cancellation_weight() >= cancellation_quorum_) {
        record.cancelled = true;
    }
}

void ChangeControl::execute(const std::string& change_id, Epoch epoch) {
    auto found = records_.find(change_id);
    if (found == records_.end()) {
        fail("change request is unknown");
    }
    auto& record = found->second;
    const auto current_state = state(change_id, epoch);
    if (current_state != ChangeState::Ready) {
        fail("change request is not ready");
    }
    if (record.approval_weight() < approval_quorum_) {
        fail("change request approval quorum is missing");
    }
    if (!predecessor_executed(record.request)) {
        fail("change request predecessor is missing");
    }
    record.executed = true;
}

ChangeState ChangeControl::state(const std::string& change_id, Epoch epoch) const {
    auto found = records_.find(change_id);
    if (found == records_.end()) {
        return ChangeState::Unknown;
    }
    const auto& record = found->second;
    if (record.executed) {
        return ChangeState::Executed;
    }
    if (record.cancelled) {
        return ChangeState::Cancelled;
    }
    if (epoch > record.request.expires_at) {
        return ChangeState::Expired;
    }
    if (epoch >= record.request.ready_at && record.approval_weight() >= approval_quorum_
        && predecessor_executed(record.request)) {
        return ChangeState::Ready;
    }
    return ChangeState::Waiting;
}

const ChangeRecord& ChangeControl::get(const std::string& change_id) const {
    auto found = records_.find(change_id);
    if (found == records_.end()) {
        fail("change request is unknown");
    }
    return found->second;
}

std::string ChangeControl::approval_payload(
    const ChangeRequest& request,
    const IdentityId& reviewer
) const {
    CanonicalBuilder builder;
    builder.field("domain", "bastion-change-approval-v1");
    builder.field("change", request.digest());
    builder.field("reviewer", reviewer.str());
    return builder.str();
}

std::string ChangeControl::cancellation_payload(
    const ChangeRequest& request,
    const IdentityId& reviewer
) const {
    CanonicalBuilder builder;
    builder.field("domain", "bastion-change-cancellation-v1");
    builder.field("change", request.digest());
    builder.field("reviewer", reviewer.str());
    return builder.str();
}

ChangeControlReport ChangeControl::report(Epoch epoch) const {
    ChangeControlReport report;
    report.approval_quorum = approval_quorum_;
    report.cancellation_quorum = cancellation_quorum_;
    CanonicalBuilder digest_builder;
    digest_builder.field("domain", "bastion-change-control-report-v1");
    digest_builder.field("epoch", epoch.value);
    for (const auto& [id, record] : records_) {
        ChangeSnapshot snapshot;
        snapshot.id = id;
        snapshot.target = record.request.target;
        snapshot.action = record.request.action;
        snapshot.state = change_state_name(state(id, epoch));
        snapshot.predecessor = record.request.predecessor;
        snapshot.nonce = record.request.nonce;
        snapshot.ready_at = record.request.ready_at.value;
        snapshot.expires_at = record.request.expires_at.value;
        snapshot.approval_weight = record.approval_weight();
        snapshot.cancellation_weight = record.cancellation_weight();
        snapshot.digest = record.request.digest();
        report.changes.push_back(snapshot);
        digest_builder.field("change", snapshot.digest + ":" + snapshot.state);
    }
    report.digest = compact_digest(digest_builder.str());
    return report;
}

std::uint32_t ChangeControl::reviewer_weight(const IdentityId& reviewer) const {
    auto found = reviewer_weights_.find(reviewer);
    if (found == reviewer_weights_.end()) {
        fail("change reviewer is not configured");
    }
    return found->second;
}

bool ChangeControl::predecessor_executed(const ChangeRequest& request) const {
    if (request.predecessor.empty()) {
        return true;
    }
    auto found = records_.find(request.predecessor);
    return found != records_.end() && found->second.executed;
}

void ChangeControl::validate_reviewer(const IdentityId& reviewer) const {
    const auto& identity = identities_.get(reviewer);
    if (!identity.enabled) {
        fail("change reviewer is disabled");
    }
    if (identity.role != IdentityRole::Owner && identity.role != IdentityRole::Treasurer
        && identity.role != IdentityRole::Auditor) {
        fail("change reviewer role is not allowed");
    }
    if (!reviewer_weights_.contains(reviewer)) {
        fail("change reviewer is not configured");
    }
}

void write_change_control_report(JsonWriter& json, const ChangeControlReport& report) {
    json.begin_object();
    json.field("approvalQuorum", static_cast<std::uint64_t>(report.approval_quorum));
    json.field("cancellationQuorum", static_cast<std::uint64_t>(report.cancellation_quorum));
    json.field("digest", report.digest);
    json.key("changes");
    json.begin_array();
    for (const auto& change : report.changes) {
        json.begin_object();
        json.field("id", change.id);
        json.field("target", change.target);
        json.field("action", change.action);
        json.field("state", change.state);
        json.field("predecessor", change.predecessor);
        json.field("nonce", change.nonce);
        json.field("readyAt", change.ready_at);
        json.field("expiresAt", change.expires_at);
        json.field("approvalWeight", static_cast<std::uint64_t>(change.approval_weight));
        json.field("cancellationWeight", static_cast<std::uint64_t>(change.cancellation_weight));
        json.field("changeDigest", change.digest);
        json.end_object();
    }
    json.end_array();
    json.end_object();
}

} // namespace bastion

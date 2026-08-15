#pragma once

#include "common/hash.hpp"
#include "common/json.hpp"
#include "common/types.hpp"
#include "security/identity.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace bastion {

enum class ChangeState {
    Unknown,
    Waiting,
    Ready,
    Executed,
    Cancelled,
    Expired,
};

std::string change_state_name(ChangeState state);

struct ChangeRequest {
    std::string id;
    std::string target;
    std::string action;
    std::string parameter_digest;
    std::string predecessor;
    IdentityId proposer;
    Epoch proposed_at;
    Epoch ready_at;
    Epoch expires_at;
    std::uint64_t nonce = 0;

    void validate() const;
    std::string payload() const;
    std::string digest() const;
};

struct ChangeVote {
    IdentityId reviewer;
    std::uint32_t weight = 0;
    Signature signature;
};

struct ChangeRecord {
    ChangeRequest request;
    Signature proposer_signature;
    std::vector<ChangeVote> approvals;
    std::vector<ChangeVote> cancellations;
    bool executed = false;
    bool cancelled = false;

    std::uint32_t approval_weight() const;
    std::uint32_t cancellation_weight() const;
};

struct ChangeSnapshot {
    std::string id;
    std::string target;
    std::string action;
    std::string state;
    std::string predecessor;
    std::uint64_t nonce = 0;
    std::uint64_t ready_at = 0;
    std::uint64_t expires_at = 0;
    std::uint32_t approval_weight = 0;
    std::uint32_t cancellation_weight = 0;
    std::string digest;
};

struct ChangeControlReport {
    std::uint32_t approval_quorum = 0;
    std::uint32_t cancellation_quorum = 0;
    std::vector<ChangeSnapshot> changes;
    std::string digest;
};

class ChangeControl {
public:
    ChangeControl(
        const IdentityRegistry& identities,
        std::uint32_t approval_quorum,
        std::uint32_t cancellation_quorum
    );

    void configure_reviewer(const IdentityId& reviewer, std::uint32_t weight);
    void schedule(const ChangeRequest& request, const Signature& proposer_signature);
    void approve(const std::string& change_id, const Signature& signature);
    void vote_cancel(const std::string& change_id, const Signature& signature);
    void execute(const std::string& change_id, Epoch epoch);

    ChangeState state(const std::string& change_id, Epoch epoch) const;
    const ChangeRecord& get(const std::string& change_id) const;
    std::string approval_payload(const ChangeRequest& request, const IdentityId& reviewer) const;
    std::string cancellation_payload(const ChangeRequest& request, const IdentityId& reviewer) const;
    ChangeControlReport report(Epoch epoch) const;

private:
    const IdentityRegistry& identities_;
    std::uint32_t approval_quorum_;
    std::uint32_t cancellation_quorum_;
    std::map<IdentityId, std::uint32_t> reviewer_weights_;
    std::map<std::string, ChangeRecord> records_;

    std::uint32_t reviewer_weight(const IdentityId& reviewer) const;
    bool predecessor_executed(const ChangeRequest& request) const;
    void validate_reviewer(const IdentityId& reviewer) const;
};

void write_change_control_report(JsonWriter& json, const ChangeControlReport& report);

} // namespace bastion

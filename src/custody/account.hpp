#pragma once

#include "common/amount.hpp"
#include "common/hash.hpp"
#include "security/identity.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace bastion {

enum class AccountStatus {
    Open,
    Frozen,
    Closing,
    Closed,
};

enum class WithdrawalMode {
    OwnerOnly,
    OwnerOrTreasurer,
    OwnerOrCurrentOperator,
    Disabled,
};

std::string status_name(AccountStatus status);
std::string withdrawal_mode_name(WithdrawalMode mode);

struct EconomicPolicy {
    PolicyId id;
    BasisPoints operator_fee_bps;
    BasisPoints reserve_bps;
    Amount receipt_limit;
    Amount daily_limit;
    AccountId fee_account;
    AccountId reserve_account;
    std::string settlement_lane;
    WithdrawalMode withdrawal_mode = WithdrawalMode::OwnerOnly;
    bool allow_legacy_receipts = true;
    bool require_reserve_release = false;

    void validate() const;
    Amount fee_for(Amount amount) const;
    Amount reserve_for(Amount amount) const;
    std::string digest() const;
};

struct OperatorGrant {
    IdentityId operator_id;
    EconomicPolicy policy;
    Epoch effective_from;
    std::optional<Epoch> retired_at;
    std::string memo;

    bool active_at(Epoch epoch) const;
    bool retired() const;
    std::string digest() const;
};

struct AccountEvent {
    Epoch epoch;
    std::string kind;
    IdentityId actor;
    Amount amount;
    std::string details;
};

struct AccountSnapshot {
    AccountId id;
    AssetId asset;
    IdentityId owner;
    AccountStatus status = AccountStatus::Open;
    Balance balance;
    std::string current_operator;
    std::string current_policy;
    std::uint64_t grant_count = 0;
};

class CustodyAccount {
public:
    CustodyAccount() = default;
    CustodyAccount(AccountId id, AssetId asset, IdentityId owner, Epoch opened_at);

    const AccountId& id() const;
    const AssetId& asset() const;
    const IdentityId& owner() const;
    AccountStatus status() const;
    Epoch opened_at() const;
    std::optional<Epoch> closed_at() const;
    const Balance& balance() const;
    const std::vector<OperatorGrant>& grants() const;
    const std::vector<AccountEvent>& events() const;

    bool is_open() const;
    bool is_closed() const;
    bool can_accept_receipts() const;
    bool has_operator(const IdentityId& operator_id) const;
    bool operator_authorized_at(const IdentityId& operator_id, Epoch epoch) const;

    const OperatorGrant& current_grant() const;
    const OperatorGrant& current_grant_at(Epoch epoch) const;
    const OperatorGrant& grant_for_operator_at(const IdentityId& operator_id, Epoch epoch) const;
    std::optional<OperatorGrant> find_current_grant() const;
    std::optional<OperatorGrant> find_grant_at(Epoch epoch) const;

    void add_initial_operator(OperatorGrant grant);
    void rotate_operator(IdentityId actor, OperatorGrant grant, Epoch epoch);
    void freeze(IdentityId actor, Epoch epoch, std::string reason);
    void unfreeze(IdentityId actor, Epoch epoch, std::string reason);
    void mark_closing(IdentityId actor, Epoch epoch);
    void close(IdentityId actor, Epoch epoch);

    void credit(Amount amount, Epoch epoch, std::string reason);
    void debit(Amount amount, Epoch epoch, std::string reason);
    void reserve(Amount amount, Epoch epoch, std::string reason);
    void release_reserve(Amount amount, Epoch epoch, std::string reason);
    void lock(Amount amount, Epoch epoch, std::string reason);
    void unlock(Amount amount, Epoch epoch, std::string reason);

    bool withdrawal_allowed(const Identity& actor) const;
    AccountSnapshot snapshot() const;
    std::string digest() const;

private:
    void ensure_mutable() const;
    void ensure_can_debit(Amount amount) const;
    void record(Epoch epoch, std::string kind, IdentityId actor, Amount amount, std::string details);
    void retire_current_grant(Epoch epoch);

    AccountId id_;
    AssetId asset_;
    IdentityId owner_;
    AccountStatus status_ = AccountStatus::Open;
    Epoch opened_at_;
    std::optional<Epoch> closed_at_;
    Balance balance_;
    std::vector<OperatorGrant> grants_;
    std::vector<AccountEvent> events_;
};

EconomicPolicy default_policy(
    std::string_view policy_id,
    AccountId fee_account,
    AccountId reserve_account,
    std::string_view lane,
    std::uint32_t fee_bps,
    std::uint32_t reserve_bps,
    std::int64_t receipt_limit
);

OperatorGrant make_grant(
    IdentityId operator_id,
    EconomicPolicy policy,
    Epoch effective_from,
    std::string memo
);

} // namespace bastion


#include "custody/account.hpp"

#include <algorithm>
#include <sstream>

namespace bastion {

std::string status_name(AccountStatus status) {
    switch (status) {
    case AccountStatus::Open:
        return "open";
    case AccountStatus::Frozen:
        return "frozen";
    case AccountStatus::Closing:
        return "closing";
    case AccountStatus::Closed:
        return "closed";
    }
    return "closed";
}

std::string withdrawal_mode_name(WithdrawalMode mode) {
    switch (mode) {
    case WithdrawalMode::OwnerOnly:
        return "owner-only";
    case WithdrawalMode::OwnerOrTreasurer:
        return "owner-or-treasurer";
    case WithdrawalMode::OwnerOrCurrentOperator:
        return "owner-or-current-operator";
    case WithdrawalMode::Disabled:
        return "disabled";
    }
    return "disabled";
}

void EconomicPolicy::validate() const {
    if (id.empty()) {
        fail("policy id is required");
    }
    if (fee_account.empty()) {
        fail("fee account is required");
    }
    if (reserve_account.empty()) {
        fail("reserve account is required");
    }
    if (!is_valid_label(settlement_lane)) {
        fail("settlement lane is invalid");
    }
    if (receipt_limit.is_zero()) {
        fail("receipt limit must be positive");
    }
    if (daily_limit.is_zero()) {
        fail("daily limit must be positive");
    }
    if (operator_fee_bps.value() + reserve_bps.value() > 10000) {
        fail("policy fees exceed full amount");
    }
}

Amount EconomicPolicy::fee_for(Amount amount) const {
    return operator_fee_bps.apply(amount);
}

Amount EconomicPolicy::reserve_for(Amount amount) const {
    return reserve_bps.apply(amount);
}

std::string EconomicPolicy::digest() const {
    CanonicalBuilder builder;
    builder.field("id", id.str());
    builder.field("fee_bps", static_cast<std::uint64_t>(operator_fee_bps.value()));
    builder.field("reserve_bps", static_cast<std::uint64_t>(reserve_bps.value()));
    builder.field("receipt_limit", static_cast<std::uint64_t>(receipt_limit.units()));
    builder.field("daily_limit", static_cast<std::uint64_t>(daily_limit.units()));
    builder.field("fee_account", fee_account.str());
    builder.field("reserve_account", reserve_account.str());
    builder.field("lane", settlement_lane);
    builder.field("withdrawal", withdrawal_mode_name(withdrawal_mode));
    builder.field("legacy", allow_legacy_receipts);
    builder.field("reserve_release", require_reserve_release);
    return compact_digest(builder.str());
}

bool OperatorGrant::active_at(Epoch epoch) const {
    if (epoch < effective_from) {
        return false;
    }
    if (retired_at.has_value() && epoch >= *retired_at) {
        return false;
    }
    return true;
}

bool OperatorGrant::retired() const {
    return retired_at.has_value();
}

std::string OperatorGrant::digest() const {
    CanonicalBuilder builder;
    builder.field("operator", operator_id.str());
    builder.field("policy", policy.digest());
    builder.field("from", effective_from.value);
    builder.field("retired", retired_at.has_value() ? retired_at->value : 0);
    builder.field("memo", memo);
    return compact_digest(builder.str());
}

CustodyAccount::CustodyAccount(AccountId id, AssetId asset, IdentityId owner, Epoch opened_at)
    : id_(std::move(id)), asset_(std::move(asset)), owner_(std::move(owner)), opened_at_(opened_at) {
    record(opened_at_, "open", owner_, Amount::zero(), "account opened");
}

const AccountId& CustodyAccount::id() const {
    return id_;
}

const AssetId& CustodyAccount::asset() const {
    return asset_;
}

const IdentityId& CustodyAccount::owner() const {
    return owner_;
}

AccountStatus CustodyAccount::status() const {
    return status_;
}

Epoch CustodyAccount::opened_at() const {
    return opened_at_;
}

std::optional<Epoch> CustodyAccount::closed_at() const {
    return closed_at_;
}

const Balance& CustodyAccount::balance() const {
    return balance_;
}

const std::vector<OperatorGrant>& CustodyAccount::grants() const {
    return grants_;
}

const std::vector<AccountEvent>& CustodyAccount::events() const {
    return events_;
}

bool CustodyAccount::is_open() const {
    return status_ == AccountStatus::Open;
}

bool CustodyAccount::is_closed() const {
    return status_ == AccountStatus::Closed;
}

bool CustodyAccount::can_accept_receipts() const {
    return status_ == AccountStatus::Open || status_ == AccountStatus::Closing;
}

bool CustodyAccount::has_operator(const IdentityId& operator_id) const {
    return std::any_of(grants_.begin(), grants_.end(), [&](const OperatorGrant& grant) {
        return grant.operator_id == operator_id;
    });
}

bool CustodyAccount::operator_authorized_at(const IdentityId& operator_id, Epoch epoch) const {
    return std::any_of(grants_.begin(), grants_.end(), [&](const OperatorGrant& grant) {
        return grant.operator_id == operator_id && grant.active_at(epoch);
    });
}

const OperatorGrant& CustodyAccount::current_grant() const {
    const auto found = find_current_grant();
    if (!found.has_value()) {
        fail("account has no active operator");
    }
    for (const auto& grant : grants_) {
        if (grant.digest() == found->digest()) {
            return grant;
        }
    }
    fail("account has no active operator");
}

const OperatorGrant& CustodyAccount::current_grant_at(Epoch epoch) const {
    for (auto iter = grants_.rbegin(); iter != grants_.rend(); ++iter) {
        if (iter->active_at(epoch)) {
            return *iter;
        }
    }
    fail("account has no operator at epoch");
}

const OperatorGrant& CustodyAccount::grant_for_operator_at(
    const IdentityId& operator_id,
    Epoch epoch
) const {
    for (auto iter = grants_.rbegin(); iter != grants_.rend(); ++iter) {
        if (iter->operator_id == operator_id && iter->active_at(epoch)) {
            return *iter;
        }
    }
    fail("operator is not authorized at epoch");
}

std::optional<OperatorGrant> CustodyAccount::find_current_grant() const {
    for (auto iter = grants_.rbegin(); iter != grants_.rend(); ++iter) {
        if (!iter->retired()) {
            return *iter;
        }
    }
    return std::nullopt;
}

std::optional<OperatorGrant> CustodyAccount::find_grant_at(Epoch epoch) const {
    for (auto iter = grants_.rbegin(); iter != grants_.rend(); ++iter) {
        if (iter->active_at(epoch)) {
            return *iter;
        }
    }
    return std::nullopt;
}

void CustodyAccount::add_initial_operator(OperatorGrant grant) {
    if (!grants_.empty()) {
        fail("initial operator already configured");
    }
    if (grant.effective_from < opened_at_) {
        fail("operator cannot predate account");
    }
    grant.policy.validate();
    grants_.push_back(grant);
    record(grant.effective_from, "operator-add", grant.operator_id, Amount::zero(), grant.memo);
}

void CustodyAccount::rotate_operator(IdentityId actor, OperatorGrant grant, Epoch epoch) {
    ensure_mutable();
    if (status_ == AccountStatus::Frozen) {
        fail("frozen account cannot rotate operator");
    }
    if (grant.effective_from.value != epoch.value) {
        fail("grant effective epoch must match rotation epoch");
    }
    grant.policy.validate();
    retire_current_grant(epoch);
    grants_.push_back(grant);
    record(epoch, "operator-rotate", actor, Amount::zero(), grant.operator_id.str());
}

void CustodyAccount::freeze(IdentityId actor, Epoch epoch, std::string reason) {
    ensure_mutable();
    if (status_ == AccountStatus::Frozen) {
        return;
    }
    if (status_ != AccountStatus::Open && status_ != AccountStatus::Closing) {
        fail("account cannot be frozen");
    }
    status_ = AccountStatus::Frozen;
    record(epoch, "freeze", actor, Amount::zero(), std::move(reason));
}

void CustodyAccount::unfreeze(IdentityId actor, Epoch epoch, std::string reason) {
    ensure_mutable();
    if (status_ != AccountStatus::Frozen) {
        fail("account is not frozen");
    }
    status_ = AccountStatus::Open;
    record(epoch, "unfreeze", actor, Amount::zero(), std::move(reason));
}

void CustodyAccount::mark_closing(IdentityId actor, Epoch epoch) {
    ensure_mutable();
    if (status_ != AccountStatus::Open) {
        fail("only open account can enter closing");
    }
    status_ = AccountStatus::Closing;
    record(epoch, "closing", actor, Amount::zero(), "account marked for closure");
}

void CustodyAccount::close(IdentityId actor, Epoch epoch) {
    ensure_mutable();
    if (status_ != AccountStatus::Closing && status_ != AccountStatus::Open) {
        fail("account is not closeable");
    }
    if (!balance_.empty()) {
        fail("account balance must be empty before closure");
    }
    status_ = AccountStatus::Closed;
    closed_at_ = epoch;
    record(epoch, "close", actor, Amount::zero(), "account closed");
}

void CustodyAccount::credit(Amount amount, Epoch epoch, std::string reason) {
    ensure_mutable();
    balance_.available = balance_.available.checked_add(amount);
    record(epoch, "credit", owner_, amount, std::move(reason));
}

void CustodyAccount::debit(Amount amount, Epoch epoch, std::string reason) {
    ensure_mutable();
    ensure_can_debit(amount);
    balance_.available = balance_.available.checked_sub(amount);
    record(epoch, "debit", owner_, amount, std::move(reason));
}

void CustodyAccount::reserve(Amount amount, Epoch epoch, std::string reason) {
    ensure_mutable();
    ensure_can_debit(amount);
    balance_.available = balance_.available.checked_sub(amount);
    balance_.reserved = balance_.reserved.checked_add(amount);
    record(epoch, "reserve", owner_, amount, std::move(reason));
}

void CustodyAccount::release_reserve(Amount amount, Epoch epoch, std::string reason) {
    ensure_mutable();
    if (balance_.reserved < amount) {
        fail("insufficient reserved amount");
    }
    balance_.reserved = balance_.reserved.checked_sub(amount);
    balance_.available = balance_.available.checked_add(amount);
    record(epoch, "release-reserve", owner_, amount, std::move(reason));
}

void CustodyAccount::lock(Amount amount, Epoch epoch, std::string reason) {
    ensure_mutable();
    ensure_can_debit(amount);
    balance_.available = balance_.available.checked_sub(amount);
    balance_.locked = balance_.locked.checked_add(amount);
    record(epoch, "lock", owner_, amount, std::move(reason));
}

void CustodyAccount::unlock(Amount amount, Epoch epoch, std::string reason) {
    ensure_mutable();
    if (balance_.locked < amount) {
        fail("insufficient locked amount");
    }
    balance_.locked = balance_.locked.checked_sub(amount);
    balance_.available = balance_.available.checked_add(amount);
    record(epoch, "unlock", owner_, amount, std::move(reason));
}

bool CustodyAccount::withdrawal_allowed(const Identity& actor) const {
    if (status_ != AccountStatus::Open && status_ != AccountStatus::Closing) {
        return false;
    }
    const auto grant = find_current_grant();
    const auto mode = grant.has_value() ? grant->policy.withdrawal_mode : WithdrawalMode::OwnerOnly;
    if (mode == WithdrawalMode::Disabled) {
        return false;
    }
    if (actor.id == owner_) {
        return actor.enabled;
    }
    if (mode == WithdrawalMode::OwnerOrTreasurer && actor.role == IdentityRole::Treasurer) {
        return actor.enabled;
    }
    if (mode == WithdrawalMode::OwnerOrCurrentOperator && grant.has_value()) {
        return actor.enabled && actor.id == grant->operator_id;
    }
    return false;
}

AccountSnapshot CustodyAccount::snapshot() const {
    AccountSnapshot out;
    out.id = id_;
    out.asset = asset_;
    out.owner = owner_;
    out.status = status_;
    out.balance = balance_;
    out.grant_count = static_cast<std::uint64_t>(grants_.size());
    if (auto grant = find_current_grant(); grant.has_value()) {
        out.current_operator = grant->operator_id.str();
        out.current_policy = grant->policy.id.str();
    }
    return out;
}

std::string CustodyAccount::digest() const {
    CanonicalBuilder builder;
    builder.field("id", id_.str());
    builder.field("asset", asset_.str());
    builder.field("owner", owner_.str());
    builder.field("status", status_name(status_));
    builder.field("available", static_cast<std::uint64_t>(balance_.available.units()));
    builder.field("reserved", static_cast<std::uint64_t>(balance_.reserved.units()));
    builder.field("locked", static_cast<std::uint64_t>(balance_.locked.units()));
    builder.field("opened", opened_at_.value);
    builder.field("closed", closed_at_.has_value() ? closed_at_->value : 0);
    builder.open("grants");
    for (const auto& grant : grants_) {
        builder.field("grant", grant.digest());
    }
    builder.close();
    return compact_digest(builder.str());
}

void CustodyAccount::ensure_mutable() const {
    if (status_ == AccountStatus::Closed) {
        fail("account is closed");
    }
}

void CustodyAccount::ensure_can_debit(Amount amount) const {
    if (amount.is_zero()) {
        fail("amount must be positive");
    }
    if (status_ != AccountStatus::Open && status_ != AccountStatus::Closing) {
        fail("account cannot be debited");
    }
    if (balance_.available < amount) {
        fail("insufficient available balance");
    }
}

void CustodyAccount::record(
    Epoch epoch,
    std::string kind,
    IdentityId actor,
    Amount amount,
    std::string details
) {
    events_.push_back(AccountEvent{epoch, std::move(kind), std::move(actor), amount, std::move(details)});
}

void CustodyAccount::retire_current_grant(Epoch epoch) {
    for (auto& grant : grants_) {
        if (!grant.retired()) {
            grant.retired_at = epoch;
        }
    }
}

EconomicPolicy default_policy(
    std::string_view policy_id,
    AccountId fee_account,
    AccountId reserve_account,
    std::string_view lane,
    std::uint32_t fee_bps,
    std::uint32_t reserve_bps,
    std::int64_t receipt_limit
) {
    EconomicPolicy policy;
    policy.id = PolicyId(policy_id);
    policy.operator_fee_bps = BasisPoints(fee_bps);
    policy.reserve_bps = BasisPoints(reserve_bps);
    policy.receipt_limit = Amount::from_units(receipt_limit);
    policy.daily_limit = Amount::from_units(receipt_limit * 10);
    policy.fee_account = std::move(fee_account);
    policy.reserve_account = std::move(reserve_account);
    policy.settlement_lane = normalize_label(lane);
    policy.withdrawal_mode = WithdrawalMode::OwnerOnly;
    policy.allow_legacy_receipts = true;
    policy.require_reserve_release = false;
    policy.validate();
    return policy;
}

OperatorGrant make_grant(
    IdentityId operator_id,
    EconomicPolicy policy,
    Epoch effective_from,
    std::string memo
) {
    OperatorGrant grant;
    grant.operator_id = std::move(operator_id);
    grant.policy = std::move(policy);
    grant.effective_from = effective_from;
    grant.memo = std::move(memo);
    grant.policy.validate();
    return grant;
}

} // namespace bastion


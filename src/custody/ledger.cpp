#include "custody/ledger.hpp"

#include <algorithm>
#include <sstream>

namespace bastion {

std::string journal_kind_name(JournalKind kind) {
    switch (kind) {
    case JournalKind::AccountCreate:
        return "account-create";
    case JournalKind::OperatorInstall:
        return "operator-install";
    case JournalKind::OperatorRotate:
        return "operator-rotate";
    case JournalKind::Deposit:
        return "deposit";
    case JournalKind::Withdrawal:
        return "withdrawal";
    case JournalKind::Transfer:
        return "transfer";
    case JournalKind::Reserve:
        return "reserve";
    case JournalKind::ReceiptSettle:
        return "receipt-settle";
    case JournalKind::AccountFreeze:
        return "account-freeze";
    case JournalKind::AccountClose:
        return "account-close";
    }
    return "transfer";
}

LedgerState::LedgerState(std::string network_id, AssetId native_asset, IdentityRegistry identities)
    : network_id_(normalize_label(network_id)),
      native_asset_(std::move(native_asset)),
      identities_(std::move(identities)),
      epoch_(Epoch(1)) {
    if (!is_valid_label(network_id_)) {
        fail("network id is invalid");
    }
}

const std::string& LedgerState::network_id() const {
    return network_id_;
}

const AssetId& LedgerState::native_asset() const {
    return native_asset_;
}

Epoch LedgerState::epoch() const {
    return epoch_;
}

Epoch LedgerState::advance(std::string reason) {
    epoch_ = epoch_.next();
    append(JournalKind::Transfer, AccountId("system:clock"), IdentityId("auditor:watch"), Amount::zero(), reason);
    return epoch_;
}

void LedgerState::set_epoch(Epoch epoch) {
    if (epoch.value == 0) {
        fail("ledger epoch cannot be zero");
    }
    epoch_ = epoch;
}

const IdentityRegistry& LedgerState::identities() const {
    return identities_;
}

IdentityRegistry& LedgerState::identities() {
    return identities_;
}

const ReceiptBook& LedgerState::receipt_book() const {
    return receipt_book_;
}

ReceiptBook& LedgerState::receipt_book() {
    return receipt_book_;
}

bool LedgerState::has_account(const AccountId& id) const {
    return accounts_.find(id) != accounts_.end();
}

CustodyAccount& LedgerState::account(const AccountId& id) {
    auto found = accounts_.find(id);
    if (found == accounts_.end()) {
        fail("account is not registered");
    }
    return found->second;
}

const CustodyAccount& LedgerState::account(const AccountId& id) const {
    auto found = accounts_.find(id);
    if (found == accounts_.end()) {
        fail("account is not registered");
    }
    return found->second;
}

std::vector<AccountId> LedgerState::account_ids() const {
    std::vector<AccountId> out;
    out.reserve(accounts_.size());
    for (const auto& [id, _] : accounts_) {
        out.push_back(id);
    }
    return out;
}

void LedgerState::create_account(AccountId id, AssetId asset, IdentityId owner) {
    if (!identities_.contains(owner)) {
        fail("account owner is not registered");
    }
    const auto& owner_identity = identities_.get(owner);
    if (!owner_identity.enabled) {
        fail("account owner is disabled");
    }
    if (accounts_.find(id) != accounts_.end()) {
        fail("account already exists");
    }
    CustodyAccount account(id, asset, owner, epoch_);
    append(JournalKind::AccountCreate, account.id(), owner, Amount::zero(), "created");
    accounts_.emplace(account.id(), std::move(account));
}

void LedgerState::install_initial_operator(AccountId account_id, IdentityId actor, OperatorGrant grant) {
    require_manager(account_id, actor);
    const auto& identity = identities_.get(grant.operator_id);
    if (!identity.can_sign_receipts()) {
        fail("operator identity cannot sign receipts");
    }
    if (!has_account(grant.policy.fee_account) || !has_account(grant.policy.reserve_account)) {
        fail("policy accounts must exist");
    }
    require_account_asset_match(account_id, grant.policy.fee_account);
    require_account_asset_match(account_id, grant.policy.reserve_account);
    auto& account_ref = account(account_id);
    account_ref.add_initial_operator(grant);
    append(JournalKind::OperatorInstall, account_id, actor, Amount::zero(), "initial operator");
}

void LedgerState::rotate_operator(AccountId account_id, IdentityId actor, OperatorGrant grant) {
    require_manager(account_id, actor);
    const auto& identity = identities_.get(grant.operator_id);
    if (!identity.can_sign_receipts()) {
        fail("operator identity cannot sign receipts");
    }
    if (!has_account(grant.policy.fee_account) || !has_account(grant.policy.reserve_account)) {
        fail("policy accounts must exist");
    }
    require_account_asset_match(account_id, grant.policy.fee_account);
    require_account_asset_match(account_id, grant.policy.reserve_account);
    auto& account_ref = account(account_id);
    account_ref.rotate_operator(actor, grant, epoch_);
    append(JournalKind::OperatorRotate, account_id, actor, Amount::zero(), grant.operator_id.str());
}

void LedgerState::freeze_account(AccountId account_id, IdentityId actor, std::string reason) {
    require_manager(account_id, actor);
    account(account_id).freeze(actor, epoch_, reason);
    append(JournalKind::AccountFreeze, account_id, actor, Amount::zero(), reason);
}

void LedgerState::unfreeze_account(AccountId account_id, IdentityId actor, std::string reason) {
    require_manager(account_id, actor);
    account(account_id).unfreeze(actor, epoch_, reason);
    append(JournalKind::AccountFreeze, account_id, actor, Amount::zero(), reason);
}

void LedgerState::mark_closing(AccountId account_id, IdentityId actor) {
    require_manager(account_id, actor);
    account(account_id).mark_closing(actor, epoch_);
    append(JournalKind::AccountClose, account_id, actor, Amount::zero(), "closing");
}

void LedgerState::close_account(AccountId account_id, IdentityId actor) {
    require_manager(account_id, actor);
    account(account_id).close(actor, epoch_);
    append(JournalKind::AccountClose, account_id, actor, Amount::zero(), "closed");
}

void LedgerState::deposit(AccountId account_id, IdentityId actor, Amount amount, std::string reason) {
    if (!identities_.enabled(actor)) {
        fail("deposit actor is not enabled");
    }
    account(account_id).credit(amount, epoch_, reason);
    append(JournalKind::Deposit, account_id, actor, amount, reason);
}

WithdrawalResult LedgerState::withdraw(
    AccountId account_id,
    IdentityId actor,
    Amount amount,
    std::string reason
) {
    WithdrawalResult out;
    out.account = account_id;
    out.actor = actor;
    out.amount = amount;
    try {
        const auto& identity = identities_.get(actor);
        auto& account_ref = account(account_id);
        if (!account_ref.withdrawal_allowed(identity)) {
            fail("actor is not allowed to withdraw");
        }
        account_ref.debit(amount, epoch_, reason);
        append(JournalKind::Withdrawal, account_id, actor, amount, reason);
        out.ok = true;
        out.message = "ok";
    } catch (const DomainError& error) {
        out.ok = false;
        out.message = error.what();
    }
    return out;
}

void LedgerState::transfer(AccountId from, AccountId to, Amount amount, IdentityId actor, std::string reason) {
    if (!identities_.enabled(actor)) {
        fail("transfer actor is not enabled");
    }
    require_account_asset_match(from, to);
    account(from).debit(amount, epoch_, reason);
    account(to).credit(amount, epoch_, reason);
    append(JournalKind::Transfer, from, actor, amount, "to=" + to.str() + ":" + reason);
}

void LedgerState::reserve(AccountId account_id, Amount amount, IdentityId actor, std::string reason) {
    if (!identities_.enabled(actor)) {
        fail("reserve actor is not enabled");
    }
    account(account_id).reserve(amount, epoch_, reason);
    append(JournalKind::Reserve, account_id, actor, amount, reason);
}

void LedgerState::release_reserve(AccountId account_id, Amount amount, IdentityId actor, std::string reason) {
    if (!identities_.enabled(actor)) {
        fail("reserve actor is not enabled");
    }
    account(account_id).release_reserve(amount, epoch_, reason);
    append(JournalKind::Reserve, account_id, actor, amount, "release:" + reason);
}

Amount LedgerState::total_supply(const AssetId& asset) const {
    Amount total = Amount::zero();
    for (const auto& [_, account_ref] : accounts_) {
        if (account_ref.asset() == asset) {
            total = total.checked_add(account_ref.balance().total());
        }
    }
    return total;
}

Amount LedgerState::available_supply(const AssetId& asset) const {
    Amount total = Amount::zero();
    for (const auto& [_, account_ref] : accounts_) {
        if (account_ref.asset() == asset) {
            total = total.checked_add(account_ref.balance().available);
        }
    }
    return total;
}

std::string LedgerState::state_digest() const {
    CanonicalBuilder builder;
    builder.field("network", network_id_);
    builder.field("asset", native_asset_.str());
    builder.field("epoch", epoch_.value);
    builder.open("accounts");
    for (const auto& [id, account_ref] : accounts_) {
        builder.field(id.str(), account_ref.digest());
    }
    builder.close();
    builder.open("receipts");
    for (const auto& receipt_id : receipt_book_.settled_ids()) {
        builder.field("settled", receipt_id.str());
    }
    builder.close();
    return stable_hash_hex(builder.str());
}

LedgerSnapshot LedgerState::snapshot() const {
    LedgerSnapshot out;
    out.epoch = epoch_;
    out.network_id = network_id_;
    out.accounts.reserve(accounts_.size());
    for (const auto& [_, account_ref] : accounts_) {
        out.accounts.push_back(account_ref.snapshot());
    }
    out.journal = journal_;
    out.settled_receipts = static_cast<std::uint64_t>(receipt_book_.settled_count());
    out.digest = state_digest();
    return out;
}

void LedgerState::write_json(JsonWriter& json) const {
    const auto snap = snapshot();
    json.begin_object();
    json.field("networkId", snap.network_id);
    json.field("epoch", snap.epoch.value);
    json.field("nativeAsset", native_asset_.str());
    json.field("stateDigest", snap.digest);
    json.field("settledReceipts", snap.settled_receipts);
    json.field("totalSupply", total_supply(native_asset_));
    json.key("accounts");
    json.begin_array();
    for (const auto& account_ref : snap.accounts) {
        write_account_snapshot(json, account_ref);
    }
    json.end_array();
    json.key("journal");
    json.begin_array();
    for (const auto& event : snap.journal) {
        write_ledger_event(json, event);
    }
    json.end_array();
    json.end_object();
}

void LedgerState::remember_settled(const SettlementReceipt& receipt) {
    receipt_book_.remember(receipt);
}

void LedgerState::reject_receipt(const SettlementReceipt& receipt, std::string reason) {
    receipt_book_.reject(receipt, reason);
}

void LedgerState::append(
    JournalKind kind,
    AccountId account_id,
    IdentityId actor,
    Amount amount,
    std::string detail
) {
    journal_.push_back(LedgerEvent{epoch_, kind, std::move(account_id), std::move(actor), amount, std::move(detail)});
}

void LedgerState::require_manager(const AccountId& account_id, const IdentityId& actor) const {
    const auto& identity = identities_.get(actor);
    const auto& account_ref = account(account_id);
    if (!identity.enabled) {
        fail("manager identity is disabled");
    }
    if (identity.role == IdentityRole::Treasurer) {
        return;
    }
    if (identity.role == IdentityRole::Owner && identity.id == account_ref.owner()) {
        return;
    }
    fail("actor cannot manage account");
}

void LedgerState::require_account_asset_match(const AccountId& left, const AccountId& right) const {
    const auto& left_account = account(left);
    const auto& right_account = account(right);
    if (left_account.asset() != right_account.asset()) {
        fail("account assets differ");
    }
}

LedgerState make_default_ledger() {
    auto identities = default_identities();
    LedgerState ledger("bastion-mainnet-sim", AssetId("usdc"), identities);

    ledger.create_account(AccountId("custody:atlas"), AssetId("usdc"), IdentityId("owner:atlas"));
    ledger.create_account(AccountId("custody:forge"), AssetId("usdc"), IdentityId("owner:forge"));
    ledger.create_account(AccountId("fees:north"), AssetId("usdc"), IdentityId("treasury:core"));
    ledger.create_account(AccountId("fees:south"), AssetId("usdc"), IdentityId("treasury:core"));
    ledger.create_account(AccountId("fees:west"), AssetId("usdc"), IdentityId("treasury:core"));
    ledger.create_account(AccountId("reserve:atlas"), AssetId("usdc"), IdentityId("treasury:core"));
    ledger.create_account(AccountId("reserve:forge"), AssetId("usdc"), IdentityId("treasury:core"));
    ledger.create_account(AccountId("beneficiary:merchant"), AssetId("usdc"), IdentityId("beneficiary:merchant"));
    ledger.create_account(AccountId("beneficiary:market"), AssetId("usdc"), IdentityId("beneficiary:market"));
    ledger.create_account(AccountId("beneficiary:escrow"), AssetId("usdc"), IdentityId("beneficiary:escrow"));

    ledger.deposit(AccountId("custody:atlas"), IdentityId("treasury:core"), Amount::from_units(1'000'000), "seed");
    ledger.deposit(AccountId("custody:forge"), IdentityId("treasury:core"), Amount::from_units(500'000), "seed");

    auto atlas_policy = default_policy(
        "policy:atlas:north",
        AccountId("fees:north"),
        AccountId("reserve:atlas"),
        "standard",
        40,
        25,
        100'000
    );
    atlas_policy.withdrawal_mode = WithdrawalMode::OwnerOrTreasurer;
    ledger.install_initial_operator(
        AccountId("custody:atlas"),
        IdentityId("owner:atlas"),
        make_grant(IdentityId("operator:north"), atlas_policy, ledger.epoch(), "north primary")
    );

    auto forge_policy = default_policy(
        "policy:forge:south",
        AccountId("fees:south"),
        AccountId("reserve:forge"),
        "express",
        55,
        20,
        80'000
    );
    forge_policy.withdrawal_mode = WithdrawalMode::OwnerOnly;
    ledger.install_initial_operator(
        AccountId("custody:forge"),
        IdentityId("owner:forge"),
        make_grant(IdentityId("operator:south"), forge_policy, ledger.epoch(), "south primary")
    );

    return ledger;
}

void write_account_snapshot(JsonWriter& json, const AccountSnapshot& account) {
    json.begin_object();
    json.field("id", account.id.str());
    json.field("asset", account.asset.str());
    json.field("owner", account.owner.str());
    json.field("status", status_name(account.status));
    json.field("available", account.balance.available);
    json.field("reserved", account.balance.reserved);
    json.field("locked", account.balance.locked);
    json.field("total", account.balance.total());
    json.field("currentOperator", account.current_operator);
    json.field("currentPolicy", account.current_policy);
    json.field("grantCount", account.grant_count);
    json.end_object();
}

void write_ledger_event(JsonWriter& json, const LedgerEvent& event) {
    json.begin_object();
    json.field("epoch", event.epoch.value);
    json.field("kind", journal_kind_name(event.kind));
    json.field("account", event.account.str());
    json.field("actor", event.actor.str());
    json.field("amount", event.amount);
    json.field("detail", event.detail);
    json.end_object();
}

} // namespace bastion


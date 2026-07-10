#pragma once

#include "common/json.hpp"
#include "custody/account.hpp"
#include "receipt/receipt.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace bastion {

enum class JournalKind {
    AccountCreate,
    OperatorInstall,
    OperatorRotate,
    Deposit,
    Withdrawal,
    Transfer,
    Reserve,
    ReceiptSettle,
    AccountFreeze,
    AccountClose,
};

std::string journal_kind_name(JournalKind kind);

struct LedgerEvent {
    Epoch epoch;
    JournalKind kind = JournalKind::Transfer;
    AccountId account;
    IdentityId actor;
    Amount amount;
    std::string detail;
};

struct WithdrawalResult {
    bool ok = false;
    AccountId account;
    IdentityId actor;
    Amount amount;
    std::string message;
};

struct LedgerSnapshot {
    Epoch epoch;
    std::string network_id;
    std::vector<AccountSnapshot> accounts;
    std::vector<LedgerEvent> journal;
    std::uint64_t settled_receipts = 0;
    std::string digest;
};

class LedgerState {
public:
    LedgerState(std::string network_id, AssetId native_asset, IdentityRegistry identities);

    const std::string& network_id() const;
    const AssetId& native_asset() const;
    Epoch epoch() const;
    Epoch advance(std::string reason);
    void set_epoch(Epoch epoch);

    const IdentityRegistry& identities() const;
    IdentityRegistry& identities();
    const ReceiptBook& receipt_book() const;
    ReceiptBook& receipt_book();

    bool has_account(const AccountId& id) const;
    CustodyAccount& account(const AccountId& id);
    const CustodyAccount& account(const AccountId& id) const;
    std::vector<AccountId> account_ids() const;

    void create_account(AccountId id, AssetId asset, IdentityId owner);
    void install_initial_operator(AccountId account_id, IdentityId actor, OperatorGrant grant);
    void rotate_operator(AccountId account_id, IdentityId actor, OperatorGrant grant);
    void freeze_account(AccountId account_id, IdentityId actor, std::string reason);
    void unfreeze_account(AccountId account_id, IdentityId actor, std::string reason);
    void mark_closing(AccountId account_id, IdentityId actor);
    void close_account(AccountId account_id, IdentityId actor);

    void deposit(AccountId account_id, IdentityId actor, Amount amount, std::string reason);
    WithdrawalResult withdraw(AccountId account_id, IdentityId actor, Amount amount, std::string reason);
    void transfer(AccountId from, AccountId to, Amount amount, IdentityId actor, std::string reason);
    void reserve(AccountId account_id, Amount amount, IdentityId actor, std::string reason);
    void release_reserve(AccountId account_id, Amount amount, IdentityId actor, std::string reason);

    Amount total_supply(const AssetId& asset) const;
    Amount available_supply(const AssetId& asset) const;
    std::string state_digest() const;
    LedgerSnapshot snapshot() const;
    void write_json(JsonWriter& json) const;

    void remember_settled(const SettlementReceipt& receipt);
    void reject_receipt(const SettlementReceipt& receipt, std::string reason);
    void append(JournalKind kind, AccountId account, IdentityId actor, Amount amount, std::string detail);

private:
    void require_manager(const AccountId& account_id, const IdentityId& actor) const;
    void require_account_asset_match(const AccountId& left, const AccountId& right) const;

    std::string network_id_;
    AssetId native_asset_;
    IdentityRegistry identities_;
    ReceiptBook receipt_book_;
    Epoch epoch_;
    std::map<AccountId, CustodyAccount> accounts_;
    std::vector<LedgerEvent> journal_;
};

LedgerState make_default_ledger();
void write_account_snapshot(JsonWriter& json, const AccountSnapshot& account);
void write_ledger_event(JsonWriter& json, const LedgerEvent& event);

} // namespace bastion


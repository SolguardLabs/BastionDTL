#pragma once

#include <cstdint>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace bastion {

class DomainError final : public std::runtime_error {
public:
    explicit DomainError(const std::string& message);
};

[[noreturn]] void fail(std::string_view message);

bool is_valid_label(std::string_view value);
std::string normalize_label(std::string_view value);
std::string join_path(const std::vector<std::string>& parts, std::string_view separator);
std::string bool_text(bool value);
std::string quote_for_log(std::string_view value);

struct AccountId {
    std::string value;

    AccountId() = default;
    explicit AccountId(std::string_view raw);

    bool empty() const;
    std::string str() const;

    friend bool operator==(const AccountId& left, const AccountId& right) = default;
    friend bool operator<(const AccountId& left, const AccountId& right);
};

struct AssetId {
    std::string value;

    AssetId() = default;
    explicit AssetId(std::string_view raw);

    bool empty() const;
    std::string str() const;

    friend bool operator==(const AssetId& left, const AssetId& right) = default;
    friend bool operator<(const AssetId& left, const AssetId& right);
};

struct IdentityId {
    std::string value;

    IdentityId() = default;
    explicit IdentityId(std::string_view raw);

    bool empty() const;
    std::string str() const;

    friend bool operator==(const IdentityId& left, const IdentityId& right) = default;
    friend bool operator<(const IdentityId& left, const IdentityId& right);
};

struct ReceiptId {
    std::string value;

    ReceiptId() = default;
    explicit ReceiptId(std::string_view raw);

    bool empty() const;
    std::string str() const;

    friend bool operator==(const ReceiptId& left, const ReceiptId& right) = default;
    friend bool operator<(const ReceiptId& left, const ReceiptId& right);
};

struct PolicyId {
    std::string value;

    PolicyId() = default;
    explicit PolicyId(std::string_view raw);

    bool empty() const;
    std::string str() const;

    friend bool operator==(const PolicyId& left, const PolicyId& right) = default;
    friend bool operator<(const PolicyId& left, const PolicyId& right);
};

struct Epoch {
    std::uint64_t value = 0;

    Epoch() = default;
    explicit Epoch(std::uint64_t raw);

    Epoch next() const;
    bool is_zero() const;
    std::string str() const;

    friend bool operator==(Epoch left, Epoch right) = default;
    friend bool operator<(Epoch left, Epoch right);
    friend bool operator<=(Epoch left, Epoch right);
    friend bool operator>(Epoch left, Epoch right);
    friend bool operator>=(Epoch left, Epoch right);
};

struct Nonce {
    std::uint64_t value = 0;

    Nonce() = default;
    explicit Nonce(std::uint64_t raw);

    std::string str() const;

    friend bool operator==(Nonce left, Nonce right) = default;
    friend bool operator<(Nonce left, Nonce right);
};

struct TimeRange {
    Epoch not_before;
    Epoch expires_at;

    TimeRange() = default;
    TimeRange(Epoch not_before, Epoch expires_at);

    bool contains(Epoch epoch) const;
    std::string describe() const;
};

std::ostream& operator<<(std::ostream& out, const AccountId& id);
std::ostream& operator<<(std::ostream& out, const AssetId& id);
std::ostream& operator<<(std::ostream& out, const IdentityId& id);
std::ostream& operator<<(std::ostream& out, const ReceiptId& id);
std::ostream& operator<<(std::ostream& out, const PolicyId& id);
std::ostream& operator<<(std::ostream& out, Epoch epoch);
std::ostream& operator<<(std::ostream& out, Nonce nonce);

template <typename T>
const T& require_value(const std::optional<T>& value, std::string_view message) {
    if (!value.has_value()) {
        fail(message);
    }
    return *value;
}

} // namespace bastion


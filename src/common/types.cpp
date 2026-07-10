#include "common/types.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace bastion {

namespace {

std::string normalize_kind(std::string_view raw, std::string_view kind) {
    auto normalized = normalize_label(raw);
    if (!is_valid_label(normalized)) {
        std::ostringstream out;
        out << "invalid " << kind << ": " << quote_for_log(raw);
        fail(out.str());
    }
    return normalized;
}

} // namespace

DomainError::DomainError(const std::string& message) : std::runtime_error(message) {}

void fail(std::string_view message) {
    throw DomainError(std::string(message));
}

bool is_valid_label(std::string_view value) {
    if (value.empty() || value.size() > 80) {
        return false;
    }
    bool saw_alnum = false;
    for (char ch : value) {
        const auto c = static_cast<unsigned char>(ch);
        if (std::isalnum(c) != 0) {
            saw_alnum = true;
            continue;
        }
        if (ch == '-' || ch == '_' || ch == ':' || ch == '.') {
            continue;
        }
        return false;
    }
    return saw_alnum;
}

std::string normalize_label(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    bool previous_dash = false;
    for (char ch : value) {
        const auto c = static_cast<unsigned char>(ch);
        if (std::isspace(c) != 0) {
            if (!previous_dash && !out.empty()) {
                out.push_back('-');
                previous_dash = true;
            }
            continue;
        }
        auto lowered = static_cast<char>(std::tolower(c));
        if (lowered == '/') {
            lowered = ':';
        }
        out.push_back(lowered);
        previous_dash = lowered == '-';
    }
    while (!out.empty() && out.front() == '-') {
        out.erase(out.begin());
    }
    while (!out.empty() && out.back() == '-') {
        out.pop_back();
    }
    return out;
}

std::string join_path(const std::vector<std::string>& parts, std::string_view separator) {
    std::ostringstream out;
    for (std::size_t index = 0; index < parts.size(); ++index) {
        if (index != 0) {
            out << separator;
        }
        out << parts[index];
    }
    return out.str();
}

std::string bool_text(bool value) {
    return value ? "true" : "false";
}

std::string quote_for_log(std::string_view value) {
    std::ostringstream out;
    out << "'";
    for (char ch : value) {
        if (ch == '\'' || ch == '\\') {
            out << '\\';
        }
        out << ch;
    }
    out << "'";
    return out.str();
}

AccountId::AccountId(std::string_view raw) : value(normalize_kind(raw, "account id")) {}

bool AccountId::empty() const {
    return value.empty();
}

std::string AccountId::str() const {
    return value;
}

bool operator<(const AccountId& left, const AccountId& right) {
    return left.value < right.value;
}

AssetId::AssetId(std::string_view raw) : value(normalize_kind(raw, "asset id")) {}

bool AssetId::empty() const {
    return value.empty();
}

std::string AssetId::str() const {
    return value;
}

bool operator<(const AssetId& left, const AssetId& right) {
    return left.value < right.value;
}

IdentityId::IdentityId(std::string_view raw) : value(normalize_kind(raw, "identity id")) {}

bool IdentityId::empty() const {
    return value.empty();
}

std::string IdentityId::str() const {
    return value;
}

bool operator<(const IdentityId& left, const IdentityId& right) {
    return left.value < right.value;
}

ReceiptId::ReceiptId(std::string_view raw) : value(normalize_kind(raw, "receipt id")) {}

bool ReceiptId::empty() const {
    return value.empty();
}

std::string ReceiptId::str() const {
    return value;
}

bool operator<(const ReceiptId& left, const ReceiptId& right) {
    return left.value < right.value;
}

PolicyId::PolicyId(std::string_view raw) : value(normalize_kind(raw, "policy id")) {}

bool PolicyId::empty() const {
    return value.empty();
}

std::string PolicyId::str() const {
    return value;
}

bool operator<(const PolicyId& left, const PolicyId& right) {
    return left.value < right.value;
}

Epoch::Epoch(std::uint64_t raw) : value(raw) {}

Epoch Epoch::next() const {
    return Epoch(value + 1);
}

bool Epoch::is_zero() const {
    return value == 0;
}

std::string Epoch::str() const {
    return std::to_string(value);
}

bool operator<(Epoch left, Epoch right) {
    return left.value < right.value;
}

bool operator<=(Epoch left, Epoch right) {
    return left.value <= right.value;
}

bool operator>(Epoch left, Epoch right) {
    return left.value > right.value;
}

bool operator>=(Epoch left, Epoch right) {
    return left.value >= right.value;
}

Nonce::Nonce(std::uint64_t raw) : value(raw) {
    if (raw == 0) {
        fail("nonce must be non-zero");
    }
}

std::string Nonce::str() const {
    return std::to_string(value);
}

bool operator<(Nonce left, Nonce right) {
    return left.value < right.value;
}

TimeRange::TimeRange(Epoch start, Epoch end) : not_before(start), expires_at(end) {
    if (expires_at < not_before) {
        fail("time range expires before it starts");
    }
}

bool TimeRange::contains(Epoch epoch) const {
    return epoch >= not_before && epoch <= expires_at;
}

std::string TimeRange::describe() const {
    return not_before.str() + ".." + expires_at.str();
}

std::ostream& operator<<(std::ostream& out, const AccountId& id) {
    return out << id.value;
}

std::ostream& operator<<(std::ostream& out, const AssetId& id) {
    return out << id.value;
}

std::ostream& operator<<(std::ostream& out, const IdentityId& id) {
    return out << id.value;
}

std::ostream& operator<<(std::ostream& out, const ReceiptId& id) {
    return out << id.value;
}

std::ostream& operator<<(std::ostream& out, const PolicyId& id) {
    return out << id.value;
}

std::ostream& operator<<(std::ostream& out, Epoch epoch) {
    return out << epoch.value;
}

std::ostream& operator<<(std::ostream& out, Nonce nonce) {
    return out << nonce.value;
}

} // namespace bastion


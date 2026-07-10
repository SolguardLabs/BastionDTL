#include "common/hash.hpp"

#include "common/types.hpp"

#include <array>
#include <iomanip>
#include <sstream>

namespace bastion {

namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

std::string encode_value(std::string_view value) {
    std::ostringstream out;
    out << value.size() << ":";
    for (char ch : value) {
        if (ch == '\\' || ch == '|' || ch == '{' || ch == '}') {
            out << '\\';
        }
        out << ch;
    }
    return out.str();
}

std::string hex64(std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << value;
    return out.str();
}

} // namespace

CanonicalBuilder& CanonicalBuilder::field(std::string_view key, std::string_view value) {
    if (!is_valid_label(key)) {
        fail("canonical key is invalid");
    }
    parts_.push_back(std::string(key) + "=" + encode_value(value));
    return *this;
}

CanonicalBuilder& CanonicalBuilder::field(std::string_view key, std::uint64_t value) {
    const auto text = std::to_string(value);
    return field(key, std::string_view(text));
}

CanonicalBuilder& CanonicalBuilder::field(std::string_view key, std::int64_t value) {
    const auto text = std::to_string(value);
    return field(key, std::string_view(text));
}

CanonicalBuilder& CanonicalBuilder::field(std::string_view key, bool value) {
    return field(key, std::string_view(value ? "1" : "0"));
}

CanonicalBuilder& CanonicalBuilder::open(std::string_view key) {
    if (!is_valid_label(key)) {
        fail("canonical section key is invalid");
    }
    parts_.push_back(std::string(key) + "={");
    return *this;
}

CanonicalBuilder& CanonicalBuilder::close() {
    parts_.push_back("}");
    return *this;
}

std::string CanonicalBuilder::str() const {
    return join_path(parts_, "|");
}

std::uint64_t stable_hash64(std::string_view input) {
    std::uint64_t hash = kFnvOffset;
    for (unsigned char ch : input) {
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= kFnvPrime;
        hash ^= hash >> 32;
    }
    hash ^= static_cast<std::uint64_t>(input.size());
    hash *= kFnvPrime;
    return hash;
}

std::string stable_hash_hex(std::string_view input) {
    const auto first = stable_hash64(input);
    const auto second = stable_hash64(hex64(first) + std::string(input));
    return hex64(first) + hex64(second);
}

std::string compact_digest(std::string_view input) {
    return stable_hash_hex(input).substr(0, 20);
}

std::string keyed_digest(std::string_view key, std::string_view payload) {
    CanonicalBuilder builder;
    builder.field("key", key);
    builder.field("payload", payload);
    builder.field("domain", "bastiondtl-signature-v1");
    return stable_hash_hex(builder.str());
}

std::string deterministic_id(std::string_view prefix, std::string_view payload) {
    if (!is_valid_label(prefix)) {
        fail("id prefix is invalid");
    }
    return std::string(prefix) + "-" + compact_digest(payload);
}

} // namespace bastion

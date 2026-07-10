#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace bastion {

class CanonicalBuilder {
public:
    CanonicalBuilder& field(std::string_view key, std::string_view value);
    CanonicalBuilder& field(std::string_view key, std::uint64_t value);
    CanonicalBuilder& field(std::string_view key, std::int64_t value);
    CanonicalBuilder& field(std::string_view key, bool value);
    CanonicalBuilder& open(std::string_view key);
    CanonicalBuilder& close();

    std::string str() const;

private:
    std::vector<std::string> parts_;
};

std::uint64_t stable_hash64(std::string_view input);
std::string stable_hash_hex(std::string_view input);
std::string compact_digest(std::string_view input);
std::string keyed_digest(std::string_view key, std::string_view payload);
std::string deterministic_id(std::string_view prefix, std::string_view payload);

} // namespace bastion

